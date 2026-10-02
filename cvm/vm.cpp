#include "vm.h"
#include "../operacoes.h"
#include <cmath>
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
        if (frames[i].proto->name.empty()) continue;
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
}

Value VM::execute(std::size_t idx, std::size_t base) {
    const std::size_t entrada = frames.size();
    ensure(base + img.protos[idx].num_regs);
    frames.push_back(Frame{&img.protos[idx], 0, base, 0});

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

    try {
        for (;;) {
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
                    if (b == 0 || (b == -1 && a == std::numeric_limits<std::int64_t>::min()))
                        applyBinaryOp(op, R[in.b], R[in.c]);   // lança
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

                // ── chamadas ─────────────────────────────────────────────
                case Op::CALL: {
                    const Proto& alvo = img.protos[in.b];
                    if (frames.size() >= max_call_depth) {
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
    } catch (const RuntimeError& err) {
        // sem try/except ainda (etapa 7): o erro sobe para quem chamou a VM
        const Frame topo = frames.back();
        RuntimeError final = err;
        try {
            fail(err, topo);
        } catch (const RuntimeError& e) {
            final = e;
        }
        frames.resize(entrada);
        throw final;
    }
}

} // namespace cinza::cvm
