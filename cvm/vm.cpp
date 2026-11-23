#include "vm.h"
#include <cstdint>
#include "../operacoes.h"
#include "../utf8.h"
#include "../gc.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace cinza::cvm {

// ============================================================================
// ERROS E STACK TRACE — formato da spec 7 ("em f (arquivo:linha)")
// ============================================================================

std::vector<std::string> VM::trace(int error_line) const {
    std::vector<std::string> t;
    int line = error_line;
    for (std::size_t i = frames.size(); i-- > 0;) {
        // a inicialização dos const não é uma chamada: fica fora do trace,
        // (os const rodam sem frame de chamada)
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

    // CINZA_GC_STATS=1: coleta final e resumo (testes gc_*)
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
                case Op::LOADK:     R[in.a] = K[static_cast<std::uint32_t>(in.bc())]; break;
                case Op::LOADINT:   R[in.a] = Value(static_cast<std::int64_t>(in.bc())); break;
                case Op::LOADI32:   R[in.a] = Value::i32(in.bc()); break;
                case Op::LOADBOOL:  R[in.a] = Value(in.b != 0); break;
                case Op::LOADVOID:  R[in.a] = Value(); break;
                case Op::LOADNULL:  R[in.a] = Value::nulo(); break;
                case Op::GETGLOBAL: R[in.a] = globals[in.b]; break;
                case Op::SETGLOBAL: globals[in.b] = R[in.a]; break;

                // ── aritmética inteira (overflow e divisão por zero: a regra
                //    compartilhada lança o erro com a mesma mensagem) ───────
#define CVM_CONTA_I64(OPC, BUILTIN, TOK)                                          \
                case Op::OPC: {                                                   \
                    std::int64_t r;                                               \
                    if (BUILTIN(R[in.b].asInt(), R[in.c].asInt(), &r))            \
                        applyBinaryOp(TOK, R[in.b], R[in.c]);                     \
                    R[in.a] = Value(r);                                           \
                    break;                                                        \
                }
                CVM_CONTA_I64(ADD_I64, __builtin_add_overflow, TokenType::OP_PLUS)
                CVM_CONTA_I64(SUB_I64, __builtin_sub_overflow, TokenType::OP_MINUS)
                CVM_CONTA_I64(MUL_I64, __builtin_mul_overflow, TokenType::OP_MULTIPLY)
#undef CVM_CONTA_I64
                // i32: a conta em 64 bits não estoura; o resultado precisa caber em 32
#define CVM_CONTA_I32(OPC, EXPR, TOK)                                             \
                case Op::OPC: {                                                   \
                    const std::int64_t x = R[in.b].asI32(), y = R[in.c].asI32();  \
                    const std::int64_t r = EXPR;                                  \
                    if (r < INT32_MIN || r > INT32_MAX)                           \
                        applyBinaryOp(TOK, R[in.b], R[in.c]);                     \
                    R[in.a] = Value::i32(static_cast<std::int32_t>(r));           \
                    break;                                                        \
                }
                CVM_CONTA_I32(ADD_I32, x + y, TokenType::OP_PLUS)
                CVM_CONTA_I32(SUB_I32, x - y, TokenType::OP_MINUS)
                CVM_CONTA_I32(MUL_I32, x * y, TokenType::OP_MULTIPLY)
#undef CVM_CONTA_I32
                case Op::DIV_I64: case Op::MOD_I64: case Op::DIV_I32: case Op::MOD_I32: {
                    const std::int64_t a = R[in.b].asInt(), b = R[in.c].asInt();
                    const bool div = in.op == Op::DIV_I64 || in.op == Op::DIV_I32;
                    // bordas (divisor 0, menor / -1, x % -1) ficam com a regra compartilhada
                    if (b == 0 || b == -1) {
                        R[in.a] = applyBinaryOp(div ? TokenType::OP_DIVIDE : TokenType::OP_MODULO, R[in.b], R[in.c]);
                        break;
                    }
                    const std::int64_t r = div ? a / b : a % b;
                    if (in.op == Op::DIV_I32 || in.op == Op::MOD_I32) R[in.a] = Value::i32(static_cast<std::int32_t>(r));
                    else                                              R[in.a] = Value(r);
                    break;
                }

                // ── ponto flutuante (seção M): infinito e NaN não existem — divisão
                //    por zero e resultado além da faixa: a regra comum lança ──
#define CVM_CONTA_F(OPC, TIPO, LER, FAZ, EXPR, TOK)                               \
                case Op::OPC: {                                                   \
                    const TIPO x = R[in.b].LER(), y = R[in.c].LER();              \
                    const TIPO r = EXPR;                                          \
                    if (!std::isfinite(r) || (TOK == TokenType::OP_DIVIDE && y == 0)) \
                        applyBinaryOp(TOK, R[in.b], R[in.c]);                     \
                    R[in.a] = FAZ(r);                                             \
                    break;                                                        \
                }
                CVM_CONTA_F(ADD_F64, double, asFloat, Value, x + y, TokenType::OP_PLUS)
                CVM_CONTA_F(SUB_F64, double, asFloat, Value, x - y, TokenType::OP_MINUS)
                CVM_CONTA_F(MUL_F64, double, asFloat, Value, x * y, TokenType::OP_MULTIPLY)
                CVM_CONTA_F(DIV_F64, double, asFloat, Value, y == 0 ? 0.0 : x / y, TokenType::OP_DIVIDE)
                CVM_CONTA_F(ADD_F32, float, asF32, Value::f32, x + y, TokenType::OP_PLUS)
                CVM_CONTA_F(SUB_F32, float, asF32, Value::f32, x - y, TokenType::OP_MINUS)
                CVM_CONTA_F(MUL_F32, float, asF32, Value::f32, x * y, TokenType::OP_MULTIPLY)
                CVM_CONTA_F(DIV_F32, float, asF32, Value::f32, y == 0 ? 0.0f : x / y, TokenType::OP_DIVIDE)
#undef CVM_CONTA_F
                case Op::NEG_I64: case Op::NEG_I32:
                    R[in.a] = negateOp(R[in.b]);   // o menor valor não tem negativo: a regra lança
                    break;
                case Op::NEG_F64: R[in.a] = Value(-R[in.b].asFloat()); break;
                case Op::NEG_F32: R[in.a] = Value::f32(-R[in.b].asF32()); break;
                case Op::CONV: {   // cast<T> e promoções: a regra comum (operacoes.cpp)
                    Value r = convertValue(R[in.b], convTipo(in.c));
                    R[in.a] = std::move(r);
                    break;
                }

                // ── inteiro com constante embutida (erro: a regra compartilhada lança) ──
                case Op::ADDK_I64: case Op::SUBK_I64: case Op::MULK_I64: {
                    const std::int64_t a = R[in.b].asInt();
                    const std::int64_t k = static_cast<std::int16_t>(in.c);
                    std::int64_t r;
                    bool estouro;
                    TokenType op;
                    if (in.op == Op::ADDK_I64)      { estouro = __builtin_add_overflow(a, k, &r); op = TokenType::OP_PLUS; }
                    else if (in.op == Op::SUBK_I64) { estouro = __builtin_sub_overflow(a, k, &r); op = TokenType::OP_MINUS; }
                    else                            { estouro = __builtin_mul_overflow(a, k, &r); op = TokenType::OP_MULTIPLY; }
                    if (estouro) applyBinaryOp(op, R[in.b], Value(k));
                    R[in.a] = Value(r);
                    break;
                }
                case Op::ADDK_I32: case Op::SUBK_I32: case Op::MULK_I32: {
                    const std::int64_t a = R[in.b].asI32();
                    const std::int64_t k = static_cast<std::int16_t>(in.c);
                    const std::int64_t r = in.op == Op::ADDK_I32 ? a + k : in.op == Op::SUBK_I32 ? a - k : a * k;
                    if (r < INT32_MIN || r > INT32_MAX)
                        applyBinaryOp(in.op == Op::ADDK_I32 ? TokenType::OP_PLUS :
                                      in.op == Op::SUBK_I32 ? TokenType::OP_MINUS : TokenType::OP_MULTIPLY,
                                      R[in.b], Value::i32(static_cast<std::int32_t>(k)));
                    R[in.a] = Value::i32(static_cast<std::int32_t>(r));
                    break;
                }
                case Op::DIVK_I64: case Op::MODK_I64: case Op::DIVK_I32: case Op::MODK_I32: {
                    const std::int64_t a = R[in.b].asInt();
                    const std::int64_t k = static_cast<std::int16_t>(in.c);
                    const bool i32 = in.op == Op::DIVK_I32 || in.op == Op::MODK_I32;
                    const bool div = in.op == Op::DIVK_I64 || in.op == Op::DIVK_I32;
                    if (k == 0 || k == -1) {   // bordas: regra compartilhada
                        R[in.a] = applyBinaryOp(div ? TokenType::OP_DIVIDE : TokenType::OP_MODULO, R[in.b],
                                                i32 ? Value::i32(static_cast<std::int32_t>(k)) : Value(k));
                        break;
                    }
                    const std::int64_t r = div ? a / k : a % k;
                    if (i32) R[in.a] = Value::i32(static_cast<std::int32_t>(r));
                    else     R[in.a] = Value(r);
                    break;
                }
                case Op::LTK_I: R[in.a] = Value(R[in.b].asInt() <  static_cast<std::int16_t>(in.c)); break;
                case Op::LEK_I: R[in.a] = Value(R[in.b].asInt() <= static_cast<std::int16_t>(in.c)); break;
                case Op::GTK_I: R[in.a] = Value(R[in.b].asInt() >  static_cast<std::int16_t>(in.c)); break;
                case Op::GEK_I: R[in.a] = Value(R[in.b].asInt() >= static_cast<std::int16_t>(in.c)); break;

                case Op::CONCAT: {
                    const Value& x = R[in.b];
                    const Value& y = R[in.c];
                    std::string s = x.kind() == Value::Kind::STRING ? x.asString() : x.toString();
                    s += y.kind() == Value::Kind::STRING ? y.asString() : y.toString();
                    R[in.a] = Value(std::move(s));
                    break;
                }
                case Op::FMT: {   // {x:formato} do printf/format (regra em operacoes.cpp)
                    const std::int64_t formato = K[in.c].asInt();   // f é o frame
                    std::string s = formatPart(R[in.b], static_cast<int>(formato / 1000),
                                               static_cast<int>(formato % 1000) - 1);
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
                case Op::LT_F: R[in.a] = Value(R[in.b].asFloat() <  R[in.c].asFloat()); break;
                case Op::LE_F: R[in.a] = Value(R[in.b].asFloat() <= R[in.c].asFloat()); break;
                case Op::LT_S: R[in.a] = Value(R[in.b].asString() <  R[in.c].asString()); break;
                case Op::LE_S: R[in.a] = Value(R[in.b].asString() <= R[in.c].asString()); break;
                case Op::EQ:   R[in.a] = Value(R[in.b] == R[in.c]); break;
                case Op::NE:   R[in.a] = Value(R[in.b] != R[in.c]); break;
                case Op::NOT:  R[in.a] = Value(!R[in.b].asBool()); break;

                // ── saltos ───────────────────────────────────────────────
                case Op::JMP:      f->pc += in.bc(); break;
                case Op::JMPIF:    if (R[in.a].asBool())  f->pc += in.bc(); break;
                case Op::JMPIFNOT: if (!R[in.a].asBool()) f->pc += in.bc(); break;
                case Op::JMPNOTNULL: if (!R[in.a].isNull()) f->pc += in.bc(); break;
                case Op::JMPNULL:    if (R[in.a].isNull()) f->pc += in.bc(); break;
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
                    // string é imutável: a cópia da spec 5.5 não é observável; o
                    // FORNEXT corta um caractere por vez (desenho seção 11), com a
                    // posição em R[a+1] contando bytes. O resto: regra compartilhada.
                    if (col.kind() == Value::Kind::STRING) R[in.a] = col;
                    else                                   R[in.a] = makeList(forElements(col));
                    R[in.a + 1] = Value(std::int64_t{0});
                    R[in.a + 2] = Value(static_cast<std::int64_t>(in.c));
                    break;
                }
                case Op::RANGEPREP:
                    if (R[in.a + 2].asInt() == 0)   // mesma mensagem da nativa range
                        throw RuntimeError("ValueError: o passo de 'range' não pode ser 0");
                    R[in.a + 3] = Value(static_cast<std::int64_t>(in.b));
                    break;
                case Op::FORRANGE: {
                    const std::int64_t i = R[in.a].asInt(), fim = R[in.a + 1].asInt(),
                                       passo = R[in.a + 2].asInt();
                    if (passo > 0 ? i >= fim : i <= fim) { f->pc += in.bc(); break; }
                    // seção K: o contador tem a largura dos argumentos (i32 ou i64)
                    const bool i32 = R[in.a + 1].kind() == Value::Kind::I32;
                    Value& destino = R[R[in.a + 3].asInt()];
                    destino = i32 ? Value::i32(static_cast<std::int32_t>(i)) : Value(i);
                    std::int64_t prox;
                    // passou do limite da largura: este foi o último (como a nativa range)
                    if (__builtin_add_overflow(i, passo, &prox) ||
                        (i32 && (prox < INT32_MIN || prox > INT32_MAX))) R[in.a] = R[in.a + 1];
                    else R[in.a] = i32 ? Value::i32(static_cast<std::int32_t>(prox)) : Value(prox);
                    break;
                }
                case Op::FORNEXT: case Op::FORNEXT_W: {
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
                    if (in.op == Op::FORNEXT_W && e.kind() == Value::Kind::I32)
                        destino = Value(e.asInt());
                    else if (in.op == Op::FORNEXT_W && e.kind() == Value::Kind::F32)
                        destino = Value(e.asFloat());
                    else
                        destino = e;
                    R[in.a + 1] = Value(i + 1);
                    break;
                }

                // ── coleções (regras em operacoes.cpp) ──
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
                case Op::APPEND: {
                    auto& elems = R[in.a].asList()->elements;
                    elems.insert(elems.end(), R + in.b, R + in.b + in.c);
                    break;
                }
                case Op::DICTADD: {
                    auto& entries = R[in.a].asDict()->entries;
                    for (std::uint16_t i = 0; i < in.c; ++i)
                        entries[R[in.b + 2 * i]] = R[in.b + 2 * i + 1];
                    break;
                }
                case Op::NEWPAIR:   R[in.a] = makePair(R[in.b], R[in.c]); break;
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
                    // coleta de ciclos na criação de objetos (ponto seguro); a
                    // coleta não move registradores, mas pode liberar objetos
                    gcMaybeCollect();
                    R[in.a] = makeInstance(img.desc_classes[in.b].get());
                    break;
                }
                case Op::NEWSTRUCT: {
                    gcMaybeCollect();
                    auto sv  = novo<StructValue>(img.desc_structs[in.b].get());
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

                // ── type() (regra em operacoes.cpp) ───────────────────────
                case Op::TYPEOF:
                    R[in.a] = Value(runtimeType(R[in.b], K[in.c].asType()));
                    break;

                // ── exceções ─────────────────────────────────────────────
                case Op::NEWERROR: {   // posição: a da criação (a da chamada Tipo(...))
                    const SourceLocation& loc = f->proto->lines[f->pc - 1];
                    auto ev     = novo<ErrorValue>();
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
                        case 2:  R[in.a] = Value::i32(static_cast<std::int32_t>(ev.line)); break;
                        default: R[in.a] = Value::i32(static_cast<std::int32_t>(ev.column)); break;
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
                        const ClassRef& ref = img.classes[R[in.a].asInstance()->classe()->indice];
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
            auto ev     = novo<ErrorValue>();
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
