#include "vm.h"
#include "../operacoes.h"
#include "../utf8.h"
#include "../gc.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace cinza::cvm {

// ============================================================================
// ERROS E STACK TRACE — mesmo formato do interpretador (Executor::snapshotTrace)
// ============================================================================

std::vector<std::string> VM::trace(int error_line) const {
    std::vector<std::string> t;
    int line = error_line;
    for (std::size_t i = frames.size(); i-- > 0;) {
        // a inicialização dos const não é uma chamada: fica fora do trace,
        // como no interpretador (lá os const rodam sem frame de chamada)
        if (frames[i].proto->hidden()) continue;
        t.push_back("em " + frames[i].proto->name + " (" +
                    formatPosition({frames[i].proto->decl.file_id, line, 0}) + ")");
        line = frames[i].call_line;
    }
    constexpr std::size_t mostrar = 10;
    if (t.size() > mostrar + 1) {
        const std::size_t omitidas = t.size() - mostrar - 1;
        std::vector<std::string> curto(t.begin(), t.begin() + mostrar);
        curto.push_back("... (" + std::to_string(omitidas) + " chamadas omitidas)");
        curto.push_back(t.back());
        t = std::move(curto);
    }
    return t;
}

// Erro de uma instrução: sem posição, ganha a da instrução que o lançou
void VM::fail(const RuntimeError& err, const Frame& f) const {
    RuntimeError e = err;
    if (e.line == 0) {
        const SourceLocation& loc = f.proto->lines[f.pc - 1];
        e = RuntimeError(err.kind, err.message, loc.line, loc.column, loc.file_id);
    }
    if (e.trace.empty()) e.trace = trace(e.line);
    throw e;
}

// ============================================================================
// EXECUÇÃO
// ============================================================================

void VM::run(const std::vector<std::string>& args) {
    globals.assign(img.num_globals, Value());
    for (const auto& [slot, c] : img.native_globals) globals[slot] = c->value;

    for (std::size_t init : img.inits) execute(init, 0);

    ensure(img.protos[img.main].num_regs + 1);
    if (img.main_has_args) {
        std::vector<Value> elems;
        for (const auto& a : args) elems.emplace_back(a);
        regs[0] = makeList(std::move(elems));
    }
    execute(img.main, 0);

    // CINZA_GC_STATS=1: coleta final e resumo, como no interpretador (testes gc_*)
    if (const char* e = std::getenv("CINZA_GC_STATS"); e && *e) {
        gcCollect();
        std::cout << "[gc] liberados em ciclos: " << gc_stats.liberados
                  << ", contêineres vivos: " << gc_registro.vivos << "\n";
    }
}

Value VM::execute(std::size_t idx, std::size_t base) {
    const std::size_t entrada = frames.size();
    ensure(base + img.protos[idx].num_regs);
    frames.push_back(Frame{&img.protos[idx], 0, base, 0});
    if (!img.protos[idx].hidden()) ++depth;

    for (;;) {
        try {
            return dispatch(entrada);
        } catch (const RuntimeError& err) {
            if (!handle(err, entrada)) throw;   // sem tratador até `entrada`: sobe
            // tratado: o despacho recomeça no tratador
        }
    }
}

// Laço de despacho, num frame próprio de C++: as variáveis quentes (f, code,
// R, K) não atravessam o catch, então o compilador as mantém em registradores
Value VM::dispatch(std::size_t entrada) {
    Frame*       f    = &frames.back();
    const Instr* code = f->proto->code.data();
    Value*       R    = regs.data() + f->base;
    const Value* K    = f->proto->consts.data();

    // depois de mudar de frame (ou de a área crescer), recarrega os ponteiros
    auto recarrega = [&] {
        f    = &frames.back();
        code = f->proto->code.data();
        R    = regs.data() + f->base;
        K    = f->proto->consts.data();
    };

    for (;;) {
        {
            const Instr in = code[f->pc++];
            switch (in.op) {
                // ── carga e movimento ────────────────────────────────────
                case Op::MOVE:      R[in.a] = R[in.b]; break;
                case Op::LOADK:     R[in.a] = K[in.b]; break;
                case Op::LOADINT:   R[in.a] = Value(static_cast<std::int64_t>(in.bc())); break;
                case Op::LOADBOOL:  R[in.a] = Value(in.b != 0); break;
                case Op::LOADVOID:  R[in.a] = Value(); break;
                case Op::GETGLOBAL: R[in.a] = globals[in.b]; break;
                case Op::SETGLOBAL: globals[in.b] = R[in.a]; break;

                // ── aritmética de int (overflow e divisão por zero: a regra
                //    compartilhada lança o erro com a mesma mensagem) ───────
                case Op::ADD_I: {
                    std::int64_t r;
                    if (__builtin_add_overflow(R[in.b].asInt(), R[in.c].asInt(), &r))
                        applyBinaryOp(TokenType::OP_PLUS, R[in.b], R[in.c]);
                    R[in.a] = Value(r);
                    break;
                }
                case Op::SUB_I: {
                    std::int64_t r;
                    if (__builtin_sub_overflow(R[in.b].asInt(), R[in.c].asInt(), &r))
                        applyBinaryOp(TokenType::OP_MINUS, R[in.b], R[in.c]);
                    R[in.a] = Value(r);
                    break;
                }
                case Op::MUL_I: {
                    std::int64_t r;
                    if (__builtin_mul_overflow(R[in.b].asInt(), R[in.c].asInt(), &r))
                        applyBinaryOp(TokenType::OP_MULTIPLY, R[in.b], R[in.c]);
                    R[in.a] = Value(r);
                    break;
                }
                case Op::DIV_I: case Op::MOD_I: {
                    const std::int64_t a = R[in.b].asInt(), b = R[in.c].asInt();
                    const TokenType op = in.op == Op::DIV_I ? TokenType::OP_DIVIDE : TokenType::OP_MODULO;
                    // bordas (divisor 0, menor int / -1, x % -1) ficam com a regra compartilhada
                    if (b == 0 || b == -1) { R[in.a] = applyBinaryOp(op, R[in.b], R[in.c]); break; }
                    R[in.a] = Value(in.op == Op::DIV_I ? a / b : a % b);
                    break;
                }

                // ── aritmética de decimal ────────────────────────────────
                case Op::ADD_D: case Op::SUB_D: case Op::MUL_D: case Op::DIV_D: {
                    const double a = R[in.b].asDecimal(), b = R[in.c].asDecimal();
                    double r;
                    TokenType op;
                    switch (in.op) {
                        case Op::ADD_D: r = a + b; op = TokenType::OP_PLUS;     break;
                        case Op::SUB_D: r = a - b; op = TokenType::OP_MINUS;    break;
                        case Op::MUL_D: r = a * b; op = TokenType::OP_MULTIPLY; break;
                        default:        r = b == 0.0 ? 0.0 : a / b; op = TokenType::OP_DIVIDE; break;
                    }
                    if ((in.op == Op::DIV_D && b == 0.0) || !std::isfinite(r))
                        applyBinaryOp(op, R[in.b], R[in.c]);   // lança
                    R[in.a] = Value(r);
                    break;
                }
                case Op::MOD_D:
                    R[in.a] = applyBinaryOp(TokenType::OP_MODULO, R[in.b], R[in.c]);
                    break;
                case Op::NEG_I:
                    R[in.a] = negateOp(R[in.b]);
                    break;
                case Op::NEG_D:
                    R[in.a] = Value(-R[in.b].asDecimal());
                    break;
                // ── int com constante embutida (erro: a regra compartilhada lança) ──
                case Op::ADDK_I: case Op::SUBK_I: case Op::MULK_I: {
                    const std::int64_t a = R[in.b].asInt();
                    const std::int64_t k = static_cast<std::int16_t>(in.c);
                    std::int64_t r;
                    bool estouro;
                    TokenType op;
                    if (in.op == Op::ADDK_I)      { estouro = __builtin_add_overflow(a, k, &r); op = TokenType::OP_PLUS; }
                    else if (in.op == Op::SUBK_I) { estouro = __builtin_sub_overflow(a, k, &r); op = TokenType::OP_MINUS; }
                    else                          { estouro = __builtin_mul_overflow(a, k, &r); op = TokenType::OP_MULTIPLY; }
                    if (estouro) applyBinaryOp(op, R[in.b], Value(k));
                    R[in.a] = Value(r);
                    break;
                }
                case Op::DIVK_I: case Op::MODK_I: {
                    const std::int64_t a = R[in.b].asInt();
                    const std::int64_t k = static_cast<std::int16_t>(in.c);
                    if (k == 0 || k == -1) {   // bordas: regra compartilhada
                        R[in.a] = applyBinaryOp(in.op == Op::DIVK_I ? TokenType::OP_DIVIDE : TokenType::OP_MODULO,
                                                R[in.b], Value(k));
                        break;
                    }
                    R[in.a] = Value(in.op == Op::DIVK_I ? a / k : a % k);
                    break;
                }
                case Op::LTK_I: R[in.a] = Value(R[in.b].asInt() <  static_cast<std::int16_t>(in.c)); break;
                case Op::LEK_I: R[in.a] = Value(R[in.b].asInt() <= static_cast<std::int16_t>(in.c)); break;
                case Op::GTK_I: R[in.a] = Value(R[in.b].asInt() >  static_cast<std::int16_t>(in.c)); break;
                case Op::GEK_I: R[in.a] = Value(R[in.b].asInt() >= static_cast<std::int16_t>(in.c)); break;

                case Op::I2D:
                    if (R[in.b].kind() == Value::Kind::INT)
                        R[in.a] = Value(static_cast<double>(R[in.b].asInt()));
                    else if (in.a != in.b)
                        R[in.a] = R[in.b];
                    break;
                case Op::CONCAT: {
                    const Value& x = R[in.b];
                    const Value& y = R[in.c];
                    std::string s = x.kind() == Value::Kind::STRING ? x.asString() : x.toString();
                    s += y.kind() == Value::Kind::STRING ? y.asString() : y.toString();
                    R[in.a] = Value(std::move(s));
                    break;
                }
                case Op::FMT: {   // {x:formato} do printf/format (regra em operacoes.cpp)
                    const std::int64_t f = K[in.c].asInt();
                    std::string s = formatPart(R[in.b], static_cast<int>(f / 1000), static_cast<int>(f % 1000) - 1);
                    R[in.a] = Value(std::move(s));
                    break;
                }
                case Op::SLICE: {
                    Value r = sliceGet(R[in.b], R[in.b + 1], R[in.b + 2], R[in.b + 3]);
                    R[in.a] = std::move(r);
                    break;
                }

                // ── comparação ───────────────────────────────────────────
                case Op::LT_I: R[in.a] = Value(R[in.b].asInt() <  R[in.c].asInt()); break;
                case Op::LE_I: R[in.a] = Value(R[in.b].asInt() <= R[in.c].asInt()); break;
                case Op::LT_D: R[in.a] = Value(R[in.b].asDecimal() <  R[in.c].asDecimal()); break;
                case Op::LE_D: R[in.a] = Value(R[in.b].asDecimal() <= R[in.c].asDecimal()); break;
                case Op::LT_S: R[in.a] = Value(R[in.b].asString() <  R[in.c].asString()); break;
                case Op::LE_S: R[in.a] = Value(R[in.b].asString() <= R[in.c].asString()); break;
                case Op::EQ:   R[in.a] = Value(R[in.b] == R[in.c]); break;
                case Op::NE:   R[in.a] = Value(R[in.b] != R[in.c]); break;
                case Op::NOT:  R[in.a] = Value(!R[in.b].asBool()); break;

                // ── saltos ───────────────────────────────────────────────
                case Op::JMP:      f->pc += in.bc(); break;
                case Op::JMPIF:    if (R[in.a].asBool())  f->pc += in.bc(); break;
                case Op::JMPIFNOT: if (!R[in.a].asBool()) f->pc += in.bc(); break;
                // compara e salta: se verdadeiro, faz aqui o JMP seguinte; senão o pula
#define CVM_SALTA_SE(cond) f->pc += (cond) ? 1 + code[f->pc].bc() : 1
                case Op::JLT_I:  CVM_SALTA_SE(R[in.a].asInt() <  R[in.b].asInt()); break;
                case Op::JLE_I:  CVM_SALTA_SE(R[in.a].asInt() <= R[in.b].asInt()); break;
                case Op::JLTK_I: CVM_SALTA_SE(R[in.a].asInt() <  static_cast<std::int16_t>(in.b)); break;
                case Op::JLEK_I: CVM_SALTA_SE(R[in.a].asInt() <= static_cast<std::int16_t>(in.b)); break;
                case Op::JGTK_I: CVM_SALTA_SE(R[in.a].asInt() >  static_cast<std::int16_t>(in.b)); break;
                case Op::JGEK_I: CVM_SALTA_SE(R[in.a].asInt() >= static_cast<std::int16_t>(in.b)); break;
#undef CVM_SALTA_SE

                // ── for (spec 5.5: os elementos são copiados no início) ──
                case Op::FORPREP: {
                    const Value& col = R[in.b];
                    std::vector<Value> elems;
                    bool texto = false;
                    switch (col.kind()) {
                        case Value::Kind::LIST:
                            elems = col.asList()->elements;
                            break;
                        case Value::Kind::DICT:
                            elems.reserve(col.asDict()->entries.size());
                            for (const auto& [k, v] : col.asDict()->entries)
                                elems.push_back(makePair(k, v));
                            break;
                        case Value::Kind::STRING:
                            // string é imutável: a cópia da spec 5.5 não é observável;
                            // o FORNEXT corta um caractere por vez (desenho seção 11)
                            texto = true;
                            break;
                        default:
                            throw RuntimeError("'for' esperava list, dict ou string como iterável");
                    }
                    if (texto) R[in.a] = col;   // posição em R[a+1] conta bytes
                    else       R[in.a] = makeList(std::move(elems));
                    R[in.a + 1] = Value(std::int64_t{0});
                    R[in.a + 2] = Value(static_cast<std::int64_t>(in.c));
                    break;
                }
                case Op::RANGEPREP:
                    if (R[in.a + 2].asInt() == 0)   // mesma mensagem da nativa range
                        throw RuntimeError("ValueError: o passo de 'range' não pode ser 0");
                    R[in.a + 3] = Value(static_cast<std::int64_t>(in.b));
                    break;
                case Op::FORRANGE: case Op::FORRANGE_D: {
                    const std::int64_t i = R[in.a].asInt(), fim = R[in.a + 1].asInt(),
                                       passo = R[in.a + 2].asInt();
                    if (passo > 0 ? i >= fim : i <= fim) { f->pc += in.bc(); break; }
                    Value& destino = R[R[in.a + 3].asInt()];
                    if (in.op == Op::FORRANGE_D) destino = Value(static_cast<double>(i));
                    else                         destino = Value(i);
                    std::int64_t prox;
                    // chegou ao limite de int: este foi o último (como a nativa range)
                    if (__builtin_add_overflow(i, passo, &prox)) R[in.a] = R[in.a + 1];
                    else                                         R[in.a] = Value(prox);
                    break;
                }
                case Op::FORNEXT: case Op::FORNEXT_D: {
                    if (R[in.a].kind() == Value::Kind::STRING) {   // um caractere UTF-8 por vez
                        const std::string& s = R[in.a].asString();
                        const auto i = static_cast<std::size_t>(R[in.a + 1].asInt());
                        if (i >= s.size()) { f->pc += in.bc(); break; }
                        const std::size_t len = utf8::bytesDoCaractere(s, i);
                        R[R[in.a + 2].asInt()] = Value(s.substr(i, len));
                        R[in.a + 1] = Value(static_cast<std::int64_t>(i + len));
                        break;
                    }
                    auto& elems = R[in.a].asList()->elements;
                    const std::int64_t i = R[in.a + 1].asInt();
                    if (i >= static_cast<std::int64_t>(elems.size())) { f->pc += in.bc(); break; }
                    Value& destino = R[R[in.a + 2].asInt()];
                    const Value& e = elems[static_cast<std::size_t>(i)];
                    if (in.op == Op::FORNEXT_D && e.kind() == Value::Kind::INT)
                        destino = Value(static_cast<double>(e.asInt()));
                    else
                        destino = e;
                    R[in.a + 1] = Value(i + 1);
                    break;
                }

                // ── coleções (regras em operacoes.cpp, as mesmas do interpretador) ──
                case Op::NEWLIST:
                    R[in.a] = makeList(std::vector<Value>(R + in.b, R + in.b + in.c));
                    break;
                case Op::NEWDICT: {
                    Value d = makeDict();
                    auto& entries = d.asDict()->entries;
                    for (std::uint16_t i = 0; i < in.c; ++i)
                        entries[R[in.b + 2 * i]] = R[in.b + 2 * i + 1];
                    R[in.a] = std::move(d);
                    break;
                }
                case Op::NEWPAIR:    R[in.a] = makePair(R[in.b], R[in.c]); break;
                case Op::GETINDEX:   R[in.a] = indexGet(R[in.b], R[in.c]); break;
                case Op::INDEXPLACE: R[in.a] = *indexPlace(R[in.b], R[in.c]); break;
                case Op::SETINDEX:   *indexPlace(R[in.a], R[in.b]) = R[in.c]; break;
                case Op::CALLBUILTIN: {
                    Value r = callBuiltin(static_cast<Builtin>(in.b), R[in.a],
                                          std::span<const Value>(R + in.a + 1, in.c));
                    R[in.a] = std::move(r);
                    break;
                }
                case Op::GETFIRST:  R[in.a] = Value(R[in.b].asPair()->first);  break;
                case Op::GETSECOND: R[in.a] = Value(R[in.b].asPair()->second); break;

                // ── objetos e struct ─────────────────────────────────────
                case Op::NEWOBJ: {
                    // coleta de ciclos na criação de objetos, como o evalNew; a
                    // coleta não move registradores, mas pode liberar objetos
                    gcMaybeCollect();
                    const ClassDecl* c = img.classes[in.b].decl;
                    R[in.a] = makeInstance(c->class_name, c);
                    break;
                }
                case Op::NEWSTRUCT: {
                    gcMaybeCollect();
                    auto sv  = std::make_shared<StructValue>();
                    sv->decl = img.structs[in.b];
                    sv->fields.assign(R + in.a, R + in.a + in.c);
                    R[in.a] = Value(std::move(sv));
                    break;
                }
                case Op::GETFIELD: {
                    const Value& o = R[in.b];
                    Value v = o.kind() == Value::Kind::STRUCT ? o.asStruct()->fields[in.c]
                                                              : o.asInstance()->fields[in.c];
                    R[in.a] = std::move(v);
                    break;
                }
                case Op::SETFIELD: {
                    Value& o = R[in.a];
                    if (o.kind() == Value::Kind::STRUCT) o.asStruct()->fields[in.b] = R[in.c];
                    else                                 o.asInstance()->fields[in.b] = R[in.c];
                    break;
                }

                // ── op<...> e type() (regras em operacoes.cpp) ────────────
                case Op::TYPEOF:
                    R[in.a] = Value(runtimeType(R[in.b], K[in.c].asType()));
                    break;
                case Op::CAST:
                    R[in.a] = narrowValue(R[in.b], K[in.c + 1].asType(), K[in.c].asType());
                    break;
                case Op::KEEPLOCK:
                    R[in.a] = keepLock(R[in.a], R[in.b]);
                    break;
                case Op::ADD: R[in.a] = applyBinaryOp(TokenType::OP_PLUS,       R[in.b], R[in.c]); break;
                case Op::SUB: R[in.a] = applyBinaryOp(TokenType::OP_MINUS,      R[in.b], R[in.c]); break;
                case Op::MUL: R[in.a] = applyBinaryOp(TokenType::OP_MULTIPLY,   R[in.b], R[in.c]); break;
                case Op::DIV: R[in.a] = applyBinaryOp(TokenType::OP_DIVIDE,     R[in.b], R[in.c]); break;
                case Op::MOD: R[in.a] = applyBinaryOp(TokenType::OP_MODULO,     R[in.b], R[in.c]); break;
                case Op::LT:  R[in.a] = applyBinaryOp(TokenType::OP_LESS,       R[in.b], R[in.c]); break;
                case Op::LE:  R[in.a] = applyBinaryOp(TokenType::OP_LESS_EQUAL, R[in.b], R[in.c]); break;
                case Op::NEG: R[in.a] = negateOp(R[in.b]); break;

                // ── exceções ─────────────────────────────────────────────
                case Op::NEWERROR: {   // posição: a da criação (a da chamada Tipo(...))
                    const SourceLocation& loc = f->proto->lines[f->pc - 1];
                    auto ev     = std::make_shared<ErrorValue>();
                    ev->kind    = K[in.b].asString();
                    ev->message = in.c == NO_REG ? std::string() : R[in.c].asString();
                    ev->line    = loc.line;
                    ev->column  = loc.column;
                    ev->file_id = loc.file_id;
                    R[in.a] = Value(std::move(ev));
                    break;
                }
                case Op::ERRFIELD: {
                    const ErrorValue& ev = *R[in.b].asError();
                    switch (in.c) {
                        case 0:  R[in.a] = Value(displayName(ev.kind)); break;   // sem prefixo de módulo
                        case 1:  R[in.a] = Value(ev.message); break;
                        case 2:  R[in.a] = Value(ev.line); break;
                        default: R[in.a] = Value(ev.column); break;
                    }
                    break;
                }
                case Op::THROW: {   // relançar preserva tipo, posição e stack trace
                    const ErrorValue& ev = *R[in.a].asError();
                    RuntimeError err(ev.kind, ev.message, static_cast<int>(ev.line),
                                     static_cast<int>(ev.column), ev.file_id);
                    err.trace = ev.trace;   // vazio num erro novo: o catch tira o trace de agora
                    throw err;
                }

                // ── chamadas ─────────────────────────────────────────────
                case Op::CALL: case Op::CALLIFACE: {
                    std::size_t idx = in.b;
                    if (in.op == Op::CALLIFACE) {   // pela classe real do objeto
                        const ClassDecl* c = R[in.a].asInstance()->decl;
                        const ClassRef& ref = img.classes[img.class_index.at(c)];
                        const InterfaceDecl* iface = img.interfaces[in.b];
                        idx = ClassRef::none;
                        for (const auto& [i, protos] : ref.itables)
                            if (i == iface) idx = protos[in.c];
                        if (idx == ClassRef::none)
                            throw RuntimeError("Erro interno: a classe não cumpre a interface");
                    }
                    const Proto& alvo = img.protos[idx];
                    if (!alvo.hidden() && depth >= max_call_depth) {
                        // como o CallGuard: posição da declaração chamada
                        RuntimeError err("StackOverflowError",
                                         "profundidade máxima de chamadas (" +
                                         std::to_string(max_call_depth) + ") excedida ao chamar '" +
                                         alvo.name + "'",
                                         alvo.decl.line, alvo.decl.column, alvo.decl.file_id);
                        err.trace = trace(err.line);
                        throw err;
                    }
                    const std::size_t nova_base = f->base + in.a;
                    const int linha = f->proto->lines[f->pc - 1].line;
                    ensure(nova_base + alvo.num_regs);
                    frames.push_back(Frame{&alvo, 0, nova_base, linha});
                    if (!alvo.hidden()) ++depth;
                    recarrega();
                    break;
                }
                case Op::CALLNATIVE: {
                    const NativeFn* fn = img.natives[in.b];
                    Value r = fn->impl(std::span<const Value>(R + in.a, in.c));
                    R[in.a] = std::move(r);
                    break;
                }
                case Op::RET: case Op::RETVOID: {
                    Value resultado = in.op == Op::RET ? std::move(R[in.a]) : Value();
                    // janela morta: solta o que ela segurava (seção 3 do desenho)
                    for (std::size_t i = 0; i < f->proto->num_regs; ++i) R[i] = Value();
                    const std::size_t base_retorno = f->base;
                    if (!f->proto->hidden()) --depth;
                    frames.pop_back();
                    if (frames.size() == entrada) return resultado;
                    regs[base_retorno] = std::move(resultado);   // R[a] de quem chamou
                    recarrega();
                    break;
                }

                default:
                    throw RuntimeError("Erro interno da CVM: instrução desconhecida");
            }
        }
    }
}

// Erro de runtime: completa posição e stack trace no ponto do erro e procura um
// tratador, do frame do erro para fora; cada frame sem tratador é desempilhado
// e tem a janela limpa. Devolve false se não houver tratador acima de
// `entrada` (o erro já completo é lançado por handle).
bool VM::handle(const RuntimeError& err, std::size_t entrada) {
    RuntimeError e = err;
    try {
        fail(err, frames.back());
    } catch (const RuntimeError& completo) {
        e = completo;
    }
    for (;;) {
        Frame& fr = frames.back();
        const auto pc = static_cast<std::uint32_t>(fr.pc - 1);   // a instrução do erro / da chamada
        for (const Handler& h : fr.proto->handlers) {
            if (pc < h.start || pc >= h.end) continue;
            // vazio: pega-tudo do finally (até o exit); "Error": todo erro, menos o exit
            const bool pega = h.kind.empty() || h.kind == e.kind || (h.kind == "Error" && !isExit(e));
            if (!pega) continue;
            auto ev     = std::make_shared<ErrorValue>();
            ev->kind    = e.kind;
            ev->message = e.message;
            ev->line    = e.line;
            ev->column  = e.column;
            ev->trace   = e.trace;
            ev->file_id = e.file_id;
            regs[fr.base + h.reg] = Value(std::move(ev));
            fr.pc = h.target;
            return true;
        }
        for (std::size_t i = 0; i < fr.proto->num_regs; ++i) regs[fr.base + i] = Value();
        if (!fr.proto->hidden()) --depth;
        frames.pop_back();
        if (frames.size() == entrada) throw e;
    }
}

} // namespace cinza::cvm
