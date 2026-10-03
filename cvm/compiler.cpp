#include "compiler.h"
#include "../operacoes.h"
#include <limits>
#include <unordered_map>

namespace cinza::cvm {

// ============================================================================
// COMPILADOR AST → BYTECODE (desenho: cvm/DESENHO.md)
//
// Recebe a AST já analisada: tipos resolvidos (resolved_type), slots do B2,
// alvos de chamada e conversões inseridas pelo semântico (CastExpr).
//
// Registradores de uma função: os slots do B2 (parâmetros e locais, 0 ..
// num_slots-1) e, acima deles, temporários alocados em pilha (`top`). Toda
// função de compilação de expressão devolve `top` ao valor de entrada.
// ============================================================================

namespace {

using TK = TypeInfo::Kind;

class Compiler {
public:
    Compiler(const std::vector<const ::cinza::Program*>& programs, const GlobalLayout& layout)
        : programs(programs), layout(layout) {}

    Image run();

private:
    const std::vector<const ::cinza::Program*>& programs;
    const GlobalLayout&                         layout;
    Image                                       img;
    std::unordered_map<const void*, std::size_t>         proto_of;    // FunctionDecl*/Constructor* → protótipo
    std::unordered_map<std::string, std::size_t>         class_of;    // nome completo → img.classes
    std::unordered_map<std::string, std::size_t>         struct_of;   // nome completo → img.structs
    std::unordered_map<const InterfaceDecl*, std::size_t> iface_of;   // → img.interfaces
    std::unordered_map<const NativeFn*, std::size_t>     native_of;

    // laços em compilação: saltos de break a corrigir e destino do continue
    struct Laco {
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
    };
    std::vector<Laco> lacos;

    // try com finally em compilação (desenho, seção 6): ação e valor pendentes
    struct Finally {
        std::size_t              loops;          // lacos.size() quando o try começou
        std::uint16_t            acao, valor;    // registradores
        std::vector<std::size_t> saltos;         // saídas que vão para o finally
        bool usa_return = false, usa_break = false, usa_continue = false;
    };
    std::vector<Finally> finallys;
    bool ret_void = false;   // a função em compilação é void

    void emitReturn(std::uint16_t reg, bool tem_valor, const Token& tok);
    void emitBreak(bool continua, const Token& tok);
    void tryStmt(const TryStmt* t);

    // estado da função em compilação
    Proto*        p      = nullptr;
    std::uint16_t nlocal = 0;   // registradores 0 .. nlocal-1 são variáveis (slots do B2)
    std::uint16_t top    = 0;   // próximo temporário livre
    std::uint16_t off    = 0;   // 1 em métodos, construtor e inicializador: o self ocupa r0

    std::uint16_t slotReg(std::uint32_t slot) const { return static_cast<std::uint16_t>(slot + off); }

    // ── registradores ────────────────────────────────────────────────────
    std::uint16_t alloc(const Token& tok) {
        if (top == std::numeric_limits<std::uint16_t>::max())
            throw Unsupported("função grande demais para a CVM (mais de 65535 registradores)", tok);
        const std::uint16_t r = top++;
        if (top > p->num_regs) p->num_regs = top;
        return r;
    }
    bool isTemp(std::uint16_t r) const { return r >= nlocal; }

    // ── emissão ──────────────────────────────────────────────────────────
    std::size_t emit(Op op, std::uint16_t a, std::uint16_t b, std::uint16_t c, const Token& tok) {
        p->code.push_back(Instr{op, a, b, c});
        p->lines.push_back(tok.loc());
        return p->code.size() - 1;
    }
    std::size_t emitBc(Op op, std::uint16_t a, std::int32_t bc, const Token& tok) {
        Instr in{op, a, 0, 0};
        in.setBc(bc);
        p->code.push_back(in);
        p->lines.push_back(tok.loc());
        return p->code.size() - 1;
    }
    std::size_t here() const { return p->code.size(); }
    // salto em `at` passa a ir para `target` (relativo à instrução seguinte)
    void patch(std::size_t at, std::size_t target) {
        p->code[at].setBc(static_cast<std::int32_t>(target) - static_cast<std::int32_t>(at + 1));
        rotulo = target;
    }
    std::size_t rotulo = SIZE_MAX;   // último alvo de salto: não fundir instruções através dele

    // Salto tomado quando a condição, já no temporário c, é falsa. Se ela acabou
    // de sair de uma comparação int, a comparação vira compara-e-salta: um
    // despacho em vez de dois, sem o bool intermediário (desenho seção 11).
    // Não funde se algum salto chega logo depois da comparação (a && x < y).
    std::size_t jumpIfFalse(std::uint16_t c, const Token& tok) {
        if (isTemp(c) && here() > 0 && rotulo != here() && p->code.back().a == c) {
            Instr& u = p->code.back();
            switch (u.op) {   // salta quando a comparação é falsa
                case Op::LT_I:  u = Instr{Op::JLE_I,  u.c, u.b, 0}; break;   // !(x < y)  ≡ y <= x
                case Op::LE_I:  u = Instr{Op::JLT_I,  u.c, u.b, 0}; break;   // !(x <= y) ≡ y < x
                case Op::LTK_I: u = Instr{Op::JGEK_I, u.b, u.c, 0}; break;
                case Op::LEK_I: u = Instr{Op::JGTK_I, u.b, u.c, 0}; break;
                case Op::GTK_I: u = Instr{Op::JLEK_I, u.b, u.c, 0}; break;
                case Op::GEK_I: u = Instr{Op::JLTK_I, u.b, u.c, 0}; break;
                default: return emitBc(Op::JMPIFNOT, c, 0, tok);
            }
            return emitBc(Op::JMP, 0, 0, tok);
        }
        return emitBc(Op::JMPIFNOT, c, 0, tok);
    }
    std::uint16_t constant(Value v, const Token& tok) {
        if (p->consts.size() >= std::numeric_limits<std::uint16_t>::max())
            throw Unsupported("constantes demais numa função para a CVM", tok);
        p->consts.push_back(std::move(v));
        return static_cast<std::uint16_t>(p->consts.size() - 1);
    }
    std::uint16_t nativeIndex(const NativeFn* fn) {
        auto [it, novo] = native_of.try_emplace(fn, img.natives.size());
        if (novo) img.natives.push_back(fn);
        return static_cast<std::uint16_t>(it->second);
    }
    static std::uint16_t u16(std::size_t v, const Token& tok, const char* oque) {
        if (v > std::numeric_limits<std::uint16_t>::max())
            throw Unsupported(std::string(oque) + " além do limite da CVM (65535)", tok);
        return static_cast<std::uint16_t>(v);
    }

    [[noreturn]] static void unsupported(const std::string& recurso, int etapa, const Token& tok) {
        throw Unsupported(recurso + " ainda não é suportado pela CVM (etapa " +
                          std::to_string(etapa) + "); use --interp", tok);
    }

    // ── expressões ───────────────────────────────────────────────────────
    void          expr(const Expr* e, std::uint16_t dst);
    std::uint16_t exprReg(const Expr* e);   // registrador com o valor (variável local: sem cópia)
    void          binary(const BinaryExpr* e, std::uint16_t dst);
    void          arith(TokenType op, TypeRef lt, std::uint16_t l, TypeRef rt, std::uint16_t r,
                        std::uint16_t dst, const Token& tok);
    // a op k com k literal int pequeno: instrução com a constante embutida
    bool          arithK(TokenType op, TypeRef lt, std::uint16_t l, const Expr* direita,
                         std::uint16_t dst, const Token& tok);
    void          call(const CallExpr* e, std::uint16_t dst);
    void          builtinCall(const MethodCallExpr* e, std::uint16_t dst);
    void          newExpr(const NewExpr* e, std::uint16_t dst);
    void          assignPlace(const AssignStmt* s);

    // ── instruções ───────────────────────────────────────────────────────
    void stmt(const Stmt* s);
    void assign(const AssignStmt* s);

    // ── funções ──────────────────────────────────────────────────────────
    std::size_t newProto(const std::string& name, const Token& decl);
    void        body(Proto* pr, std::uint32_t num_slots, std::size_t nparams, bool metodo,
                     const Stmt* corpo, const Token& tok, bool vazio);
    void        classInit(const ClassDecl* cls, std::size_t idx);
    void        moduleInit(const ::cinza::Program* prog);
};

static bool hasOp(TypeRef t) {
    if (!t) return false;
    if (t->is(TK::Op)) return true;
    for (TypeRef p : t->params) if (hasOp(p)) return true;
    return false;
}

// ============================================================================
// EXPRESSÕES
// ============================================================================

std::uint16_t Compiler::exprReg(const Expr* e) {
    if (e->node_kind == NodeKind::Identifier) {
        auto* id = static_cast<const IdentifierExpr*>(e);
        if (id->res.kind == Resolution::Kind::Local) return slotReg(id->res.slot);
        if (id->res.kind == Resolution::Kind::Self)  return 0;
    }
    const std::uint16_t t = alloc(e->token);
    expr(e, t);
    return t;
}

void Compiler::expr(const Expr* e, std::uint16_t dst) {
    const std::uint16_t save = top;
    switch (e->node_kind) {
        case NodeKind::Literal: {
            auto* lit = static_cast<const LiteralExpr*>(e);
            if (auto* i = std::get_if<std::int64_t>(&lit->value)) {
                if (*i >= std::numeric_limits<std::int32_t>::min() &&
                    *i <= std::numeric_limits<std::int32_t>::max())
                    emitBc(Op::LOADINT, dst, static_cast<std::int32_t>(*i), e->token);
                else
                    emit(Op::LOADK, dst, constant(Value(*i), e->token), 0, e->token);
            } else if (auto* d = std::get_if<double>(&lit->value)) {
                emit(Op::LOADK, dst, constant(Value(*d), e->token), 0, e->token);
            } else if (auto* s = std::get_if<std::string>(&lit->value)) {
                emit(Op::LOADK, dst, constant(Value(*s), e->token), 0, e->token);
            } else {
                emit(Op::LOADBOOL, dst, std::get<bool>(lit->value) ? 1 : 0, 0, e->token);
            }
            break;
        }

        case NodeKind::Identifier: {
            auto* id = static_cast<const IdentifierExpr*>(e);
            switch (id->res.kind) {
                case Resolution::Kind::Local: {
                    const auto slot = slotReg(id->res.slot);
                    if (slot != dst) emit(Op::MOVE, dst, slot, 0, e->token);
                    break;
                }
                case Resolution::Kind::Global:
                    emit(Op::GETGLOBAL, dst, u16(id->res.slot, e->token, "slots globais"), 0, e->token);
                    break;
                case Resolution::Kind::Field:   // campo do objeto atual (self em r0)
                    emit(Op::GETFIELD, dst, 0, u16(id->res.slot, e->token, "campos"), e->token);
                    break;
                case Resolution::Kind::Self:
                    if (dst != 0) emit(Op::MOVE, dst, 0, 0, e->token);
                    break;
                case Resolution::Kind::Type:   // Pessoa, Cor... usado como valor (type(x) == Pessoa)
                    emit(Op::LOADK, dst, constant(Value(id->type_value), e->token), 0, e->token);
                    break;
                case Resolution::Kind::None:
                    throw Unsupported("identificador sem resolução (erro interno)", e->token);
            }
            break;
        }

        case NodeKind::Cast: {
            auto* c = static_cast<const CastExpr*>(e);
            if (c->operand->resolved_type->is(TK::Op) || c->resolved_type->is(TK::Op)) {
                // conversão de/para op (como o evalCast): K[c] é o tipo de destino;
                // o tipo de origem vai numa segunda constante logo depois
                const std::uint16_t r = exprReg(c->operand.get());
                const std::uint16_t k = constant(Value(c->resolved_type), e->token);
                constant(Value(c->operand->resolved_type), e->token);
                emit(Op::CAST, dst, r, k, e->token);
            } else if (c->resolved_type->is(TK::Decimal) && c->operand->resolved_type->is(TK::Int)) {
                const std::uint16_t r = exprReg(c->operand.get());
                emit(Op::I2D, dst, r, 0, e->token);
            } else {
                expr(c->operand.get(), dst);
            }
            break;
        }

        case NodeKind::Unary: {
            auto* u = static_cast<const UnaryExpr*>(e);
            const std::uint16_t r = exprReg(u->operand.get());
            if (u->op == TokenType::OP_NOT)
                emit(Op::NOT, dst, r, 0, e->token);
            else if (hasOp(u->operand->resolved_type))   // tipo travado desconhecido
                emit(Op::NEG, dst, r, 0, e->token);
            else
                emit(u->operand->resolved_type->is(TK::Int) ? Op::NEG_I : Op::NEG_D, dst, r, 0, e->token);
            break;
        }

        case NodeKind::Binary:
            binary(static_cast<const BinaryExpr*>(e), dst);
            break;

        case NodeKind::Call:
            call(static_cast<const CallExpr*>(e), dst);
            break;

        case NodeKind::ListLiteral: {
            auto* l = static_cast<const ListLiteralExpr*>(e);
            const std::uint16_t base = top;
            for (const auto& el : l->elements) expr(el.get(), alloc(e->token));
            emit(Op::NEWLIST, dst, base, u16(l->elements.size(), e->token, "elementos"), e->token);
            break;
        }
        case NodeKind::DictLiteral: {
            auto* d = static_cast<const DictLiteralExpr*>(e);
            const std::uint16_t base = top;
            for (const auto& [k, v] : d->pairs) {   // chave e valor, par a par
                expr(k.get(), alloc(e->token));
                expr(v.get(), alloc(e->token));
            }
            emit(Op::NEWDICT, dst, base, u16(d->pairs.size(), e->token, "entradas"), e->token);
            break;
        }
        case NodeKind::PairLiteral: {
            auto* pr = static_cast<const PairLiteralExpr*>(e);
            const std::uint16_t a = exprReg(pr->first.get());
            const std::uint16_t b = exprReg(pr->second.get());
            emit(Op::NEWPAIR, dst, a, b, e->token);
            break;
        }
        case NodeKind::IndexAccess: {
            auto* ix = static_cast<const IndexAccessExpr*>(e);
            const std::uint16_t o = exprReg(ix->object.get());
            const std::uint16_t k = exprReg(ix->index.get());
            emit(Op::GETINDEX, dst, o, k, e->token);
            break;
        }
        case NodeKind::Slice: {   // s[ini:fim:passo]: 4 registradores seguidos; omitido = void
            auto* sl = static_cast<const SliceExpr*>(e);
            const std::uint16_t b = alloc(e->token);
            alloc(e->token); alloc(e->token); alloc(e->token);
            expr(sl->object.get(), b);   // na ordem do fonte (spec 5.2)
            const Expr* partes[] = {sl->start.get(), sl->end.get(), sl->step.get()};
            for (int i = 0; i < 3; ++i) {
                const auto r = static_cast<std::uint16_t>(b + 1 + i);
                if (partes[i]) expr(partes[i], r);
                else           emit(Op::LOADVOID, r, 0, 0, e->token);
            }
            emit(Op::SLICE, dst, b, 0, e->token);
            break;
        }
        case NodeKind::MemberAccess: {
            auto* m = static_cast<const MemberAccessExpr*>(e);
            TypeRef t = m->object->resolved_type;
            if (m->enum_decl) {   // Cor.Verde: uma constante
                emit(Op::LOADK, dst,
                     constant(Value(EnumValue{m->enum_decl, static_cast<std::uint32_t>(m->field_index)}),
                              e->token), 0, e->token);
                break;
            }
            if (t->is(TK::Error)) {   // e.kind, e.message, e.line, e.column
                const std::uint16_t o = exprReg(m->object.get());
                const std::string& n = m->member_name;
                const std::uint16_t c = n == "kind" ? 0 : n == "message" ? 1 : n == "line" ? 2 : 3;
                emit(Op::ERRFIELD, dst, o, c, e->token);
                break;
            }
            const std::uint16_t o = exprReg(m->object.get());
            if (t->is(TK::Pair))
                emit(m->member_name == "first" ? Op::GETFIRST : Op::GETSECOND, dst, o, 0, e->token);
            else   // objeto de classe ou struct: índice do campo resolvido pelo semântico
                emit(Op::GETFIELD, dst, o, u16(static_cast<std::size_t>(m->field_index), e->token, "campos"),
                     e->token);
            break;
        }
        case NodeKind::MethodCall: {
            auto* mc = static_cast<const MethodCallExpr*>(e);
            TypeRef t = mc->object->resolved_type;
            if (t->is(TK::List) || t->is(TK::Dict) || t->is(TK::String)) { builtinCall(mc, dst); break; }
            // o objeto vira o self (r0) da janela do método; argumentos depois dele
            const std::uint16_t base = alloc(e->token);
            expr(mc->object.get(), base);
            for (const auto& arg : mc->arguments) expr(arg.get(), alloc(e->token));
            if (mc->iface)   // pela interface: o método sai da tabela da classe do objeto
                emit(Op::CALLIFACE, base, static_cast<std::uint16_t>(iface_of.at(mc->iface)),
                     static_cast<std::uint16_t>(mc->iface_method), e->token);
            else
                emit(Op::CALL, base, u16(proto_of.at(mc->target), e->token, "funções"),
                     u16(mc->arguments.size(), e->token, "argumentos"), e->token);
            if (dst != base) emit(Op::MOVE, dst, base, 0, e->token);
            break;
        }
        case NodeKind::New:
            newExpr(static_cast<const NewExpr*>(e), dst);
            break;
        case NodeKind::TypeLiteral:   // int, list<int>... escrito como valor
            emit(Op::LOADK, dst,
                 constant(Value(static_cast<const TypeLiteralExpr*>(e)->value), e->token), 0, e->token);
            break;
        default:
            throw Unsupported("expressão desconhecida (erro interno)", e->token);
    }
    top = save;
}

void Compiler::binary(const BinaryExpr* e, std::uint16_t dst) {
    const Token& tok = e->token;

    // && e ||: curto-circuito. Escrever em dst antes do lado direito só é
    // seguro se dst for temporário (um local pode ser lido pelo lado direito).
    if (e->op == TokenType::OP_AND || e->op == TokenType::OP_OR) {
        const std::uint16_t t = isTemp(dst) ? dst : alloc(tok);
        expr(e->left.get(), t);
        const std::size_t salto = emitBc(e->op == TokenType::OP_AND ? Op::JMPIFNOT : Op::JMPIF, t, 0, tok);
        expr(e->right.get(), t);
        patch(salto, here());
        if (t != dst) emit(Op::MOVE, dst, t, 0, tok);
        return;
    }

    TypeRef lt = e->left->resolved_type, rt = e->right->resolved_type;

    // operandos na ordem do fonte; a instrução final pode trocar os registradores
    const std::uint16_t l = exprReg(e->left.get());
    if (arithK(e->op, lt, l, e->right.get(), dst, tok)) return;   // literal não tem efeito colateral
    const std::uint16_t r = exprReg(e->right.get());
    arith(e->op, lt, l, rt, r, dst, tok);
}

// Literal int que cabe em 16 bits com sinal (constante embutida na instrução)
static bool intPequeno(const Expr* e, std::int16_t& k) {
    if (e->node_kind != NodeKind::Literal || !e->resolved_type || !e->resolved_type->is(TK::Int))
        return false;
    auto* v = std::get_if<std::int64_t>(&static_cast<const LiteralExpr*>(e)->value);
    if (!v || *v < std::numeric_limits<std::int16_t>::min() || *v > std::numeric_limits<std::int16_t>::max())
        return false;
    k = static_cast<std::int16_t>(*v);
    return true;
}

bool Compiler::arithK(TokenType op, TypeRef lt, std::uint16_t l, const Expr* direita,
                      std::uint16_t dst, const Token& tok) {
    std::int16_t k;
    if (!lt->is(TK::Int) || !intPequeno(direita, k)) return false;
    Op o;
    switch (op) {
        case TokenType::OP_PLUS:          o = Op::ADDK_I; break;
        case TokenType::OP_MINUS:         o = Op::SUBK_I; break;
        case TokenType::OP_MULTIPLY:      o = Op::MULK_I; break;
        case TokenType::OP_DIVIDE:        o = Op::DIVK_I; break;
        case TokenType::OP_MODULO:        o = Op::MODK_I; break;
        case TokenType::OP_LESS:          o = Op::LTK_I;  break;
        case TokenType::OP_LESS_EQUAL:    o = Op::LEK_I;  break;
        case TokenType::OP_GREATER:       o = Op::GTK_I;  break;
        case TokenType::OP_GREATER_EQUAL: o = Op::GEK_I;  break;
        default: return false;
    }
    emit(o, dst, l, static_cast<std::uint16_t>(k), tok);
    return true;
}

// a op b com os dois valores já em registradores
void Compiler::arith(TokenType op, TypeRef lt, std::uint16_t l, TypeRef rt, std::uint16_t r,
                     std::uint16_t dst, const Token& tok) {
    if (op == TokenType::OP_EQUAL)     { emit(Op::EQ, dst, l, r, tok); return; }
    if (op == TokenType::OP_NOT_EQUAL) { emit(Op::NE, dst, l, r, tok); return; }

    // op<...> de tipo travado desconhecido: o tipo real só aparece em runtime;
    // o semântico garantiu que toda combinação é válida (spec 3.7)
    if (hasOp(lt) || hasOp(rt)) {
        switch (op) {
            case TokenType::OP_PLUS:          emit(Op::ADD, dst, l, r, tok); break;
            case TokenType::OP_MINUS:         emit(Op::SUB, dst, l, r, tok); break;
            case TokenType::OP_MULTIPLY:      emit(Op::MUL, dst, l, r, tok); break;
            case TokenType::OP_DIVIDE:        emit(Op::DIV, dst, l, r, tok); break;
            case TokenType::OP_MODULO:        emit(Op::MOD, dst, l, r, tok); break;
            case TokenType::OP_LESS:          emit(Op::LT, dst, l, r, tok); break;
            case TokenType::OP_LESS_EQUAL:    emit(Op::LE, dst, l, r, tok); break;
            case TokenType::OP_GREATER:       emit(Op::LT, dst, r, l, tok); break;
            case TokenType::OP_GREATER_EQUAL: emit(Op::LE, dst, r, l, tok); break;
            default: throw Unsupported("operador desconhecido (erro interno)", tok);
        }
        return;
    }

    if (op == TokenType::OP_PLUS && (lt->is(TK::String) || rt->is(TK::String))) {
        emit(Op::CONCAT, dst, l, r, tok);
        return;
    }

    const bool strings = lt->is(TK::String) && rt->is(TK::String);
    const bool inteiros = lt->is(TK::Int) && rt->is(TK::Int);
    // int misturado com decimal: converte o lado int (sem efeito colateral,
    // depois de os dois operandos já estarem avaliados)
    std::uint16_t a = l, b = r;
    if (!inteiros && !strings) {
        if (lt->is(TK::Int)) { a = alloc(tok); emit(Op::I2D, a, l, 0, tok); }
        if (rt->is(TK::Int)) { b = alloc(tok); emit(Op::I2D, b, r, 0, tok); }
    }

    auto pick = [&](Op i, Op d, Op s) { return inteiros ? i : (strings ? s : d); };
    switch (op) {
        case TokenType::OP_PLUS:     emit(inteiros ? Op::ADD_I : Op::ADD_D, dst, a, b, tok); break;
        case TokenType::OP_MINUS:    emit(inteiros ? Op::SUB_I : Op::SUB_D, dst, a, b, tok); break;
        case TokenType::OP_MULTIPLY: emit(inteiros ? Op::MUL_I : Op::MUL_D, dst, a, b, tok); break;
        case TokenType::OP_DIVIDE:   emit(inteiros ? Op::DIV_I : Op::DIV_D, dst, a, b, tok); break;
        case TokenType::OP_MODULO:   emit(inteiros ? Op::MOD_I : Op::MOD_D, dst, a, b, tok); break;
        // > e >= trocam só os registradores; a avaliação já aconteceu na ordem do fonte
        case TokenType::OP_LESS:          emit(pick(Op::LT_I, Op::LT_D, Op::LT_S), dst, a, b, tok); break;
        case TokenType::OP_LESS_EQUAL:    emit(pick(Op::LE_I, Op::LE_D, Op::LE_S), dst, a, b, tok); break;
        case TokenType::OP_GREATER:       emit(pick(Op::LT_I, Op::LT_D, Op::LT_S), dst, b, a, tok); break;
        case TokenType::OP_GREATER_EQUAL: emit(pick(Op::LE_I, Op::LE_D, Op::LE_S), dst, b, a, tok); break;
        default:
            throw Unsupported("operador desconhecido (erro interno)", tok);
    }
}

void Compiler::call(const CallExpr* e, std::uint16_t dst) {
    const Token& tok = e->token;
    if (e->type_of) {   // type(x): o tipo real, com o tipo estático em K[c]
        const std::uint16_t r = exprReg(e->arguments[0].get());
        emit(Op::TYPEOF, dst, r, constant(Value(e->type_of), tok), tok);
        return;
    }

    // printf/format: o texto é montado em t (trecho fixo: LOADK; {expr}: FMT, que
    // aplica o formato e converte para texto; CONCAT junta); printf o passa ao print
    if (e->interpolated) {
        const std::uint16_t t = alloc(tok);
        std::uint16_t u = NO_REG;
        if (e->interp.empty()) emit(Op::LOADK, t, constant(Value(std::string()), tok), 0, tok);
        for (std::size_t i = 0; i < e->interp.size(); ++i) {
            const auto& parte = e->interp[i];
            const std::uint16_t r = i == 0 ? t : (u == NO_REG ? (u = alloc(tok)) : u);
            if (!parte.expr) {
                emit(Op::LOADK, r, constant(Value(parte.text), tok), 0, tok);
            } else {
                expr(parte.expr.get(), r);
                const std::int64_t formato = std::int64_t{parte.width} * 1000 + parte.precision + 1;
                emit(Op::FMT, r, r, constant(Value(formato), tok), tok);
            }
            if (i > 0) emit(Op::CONCAT, t, t, r, tok);
        }
        if (e->native) emit(Op::CALLNATIVE, t, nativeIndex(e->native), 1, tok);
        if (dst != t) emit(Op::MOVE, dst, t, 0, tok);
        return;
    }

    const bool nativa = e->native != nullptr;
    if (!nativa && !e->target) {   // Tipo("mensagem"): cria um erro, na posição da chamada
        const std::uint16_t msg = e->arguments.empty() ? NO_REG : exprReg(e->arguments[0].get());
        emit(Op::NEWERROR, dst, constant(Value(e->function_name), tok), msg, tok);
        return;
    }
    // argumentos em registradores consecutivos a partir de `base` (convenção do Lua)
    const std::uint16_t base = top;
    // m() dentro de um método: o self atual (r0) vai antes dos argumentos
    if (e->implicit_method) emit(Op::MOVE, alloc(tok), 0, 0, tok);
    for (const auto& arg : e->arguments) {
        const std::uint16_t r = alloc(tok);
        expr(arg.get(), r);
    }
    if (top == base) alloc(tok);   // lugar do resultado
    const auto n = u16(e->arguments.size(), tok, "argumentos");

    if (nativa) {
        emit(Op::CALLNATIVE, base, nativeIndex(e->native), n, tok);
        // nativa genérica pode devolver int onde o tipo é decimal (Lists.sum de
        // list<decimal> vazia): I2D converte só se vier int
        if (e->resolved_type && e->resolved_type->is(TK::Decimal))
            emit(Op::I2D, base, base, 0, tok);
    } else {
        emit(Op::CALL, base, u16(proto_of.at(e->target), tok, "funções"), n, tok);
    }
    if (dst != base) emit(Op::MOVE, dst, base, 0, tok);
}

// Método embutido de list, dict ou string: o objeto em R[base], os argumentos
// depois (objeto avaliado antes dos argumentos, spec 5.2)
void Compiler::builtinCall(const MethodCallExpr* e, std::uint16_t dst) {
    TypeRef t = e->object->resolved_type;
    const Value::Kind kind = t->is(TK::List) ? Value::Kind::LIST
                           : t->is(TK::Dict) ? Value::Kind::DICT : Value::Kind::STRING;
    const Builtin b = builtinFor(kind, e->method_name);
    const std::uint16_t base = alloc(e->token);
    expr(e->object.get(), base);
    for (const auto& arg : e->arguments) expr(arg.get(), alloc(e->token));
    emit(Op::CALLBUILTIN, base, static_cast<std::uint16_t>(b),
         u16(e->arguments.size(), e->token, "argumentos"), e->token);
    if (dst != base) emit(Op::MOVE, dst, base, 0, e->token);
}

// new Classe(args) / new Struct(...) — na ordem do interpretador
void Compiler::newExpr(const NewExpr* e, std::uint16_t dst) {
    const Token& tok = e->token;

    if (auto it = struct_of.find(e->class_name); it != struct_of.end()) {
        // struct: os campos em ordem — os argumentos, ou os valores padrão
        // (calculados aqui mesmo: só enxergam os const globais)
        const StructDecl* decl = img.structs[it->second];
        const std::uint16_t base = top;
        if (!e->arguments.empty()) {
            for (const auto& arg : e->arguments) expr(arg.get(), alloc(tok));
        } else {
            for (const auto& f : decl->fields) {
                const std::uint16_t r = alloc(tok);
                if (f.initializer) expr(f.initializer.get(), r);
                else emit(f.type->kind == Type::Kind::DICT ? Op::NEWDICT : Op::NEWLIST, r, 0, 0, f.token);
            }
        }
        if (top == base) alloc(tok);
        emit(Op::NEWSTRUCT, base, static_cast<std::uint16_t>(it->second),
             u16(decl->fields.size(), tok, "campos"), tok);
        if (dst != base) emit(Op::MOVE, dst, base, 0, tok);
        return;
    }

    // classe: argumentos → objeto → campos (inicializador) → construtor
    const std::size_t ci = class_of.at(e->class_name);
    const ClassRef& cls = img.classes[ci];
    const std::uint16_t obj  = alloc(tok);   // o objeto
    const std::uint16_t self = alloc(tok);   // self do construtor; argumentos logo depois
    for (const auto& arg : e->arguments) expr(arg.get(), alloc(tok));
    emit(Op::NEWOBJ, obj, static_cast<std::uint16_t>(ci), 0, tok);
    if (cls.init != ClassRef::none) {
        const std::uint16_t t = alloc(tok);
        emit(Op::MOVE, t, obj, 0, tok);
        emit(Op::CALL, t, u16(cls.init, tok, "funções"), 0, tok);
    }
    if (cls.ctor != ClassRef::none) {
        emit(Op::MOVE, self, obj, 0, tok);
        emit(Op::CALL, self, u16(cls.ctor, tok, "funções"), u16(e->arguments.size(), tok, "argumentos"), tok);
    }
    if (dst != obj) emit(Op::MOVE, dst, obj, 0, tok);
}

// ============================================================================
// INSTRUÇÕES
// ============================================================================

void Compiler::stmt(const Stmt* s) {
    const std::uint16_t save = top;
    switch (s->node_kind) {
        case NodeKind::VarDecl: {
            auto* v = static_cast<const VarDeclStmt*>(s);
            if (v->res.kind != Resolution::Kind::Local)
                throw Unsupported("variável não local fora do nível superior (erro interno)", s->token);
            const auto slot = slotReg(v->res.slot);
            if (!v->initializer) {   // list<T> e dict<K, V> nascem vazios
                emit(v->type->kind == Type::Kind::DICT ? Op::NEWDICT : Op::NEWLIST, slot, 0, 0, s->token);
                break;
            }
            expr(v->initializer.get(), slot);
            break;
        }

        case NodeKind::Assign:
            assign(static_cast<const AssignStmt*>(s));
            break;

        case NodeKind::ExprStmt: {
            const std::uint16_t t = alloc(s->token);
            expr(static_cast<const ExprStmt*>(s)->expression.get(), t);
            break;
        }

        case NodeKind::Return: {
            auto* r = static_cast<const ReturnStmt*>(s);
            if (r->value) emitReturn(exprReg(r->value.get()), true, s->token);
            else          emitReturn(0, false, s->token);
            break;
        }

        case NodeKind::Block:
            for (const auto& x : static_cast<const BlockStmt*>(s)->statements)
                if (x) stmt(x.get());
            break;

        case NodeKind::If: {
            auto* i = static_cast<const IfStmt*>(s);
            const std::uint16_t c = exprReg(i->condition.get());
            const std::size_t para_else = jumpIfFalse(c, s->token);
            top = save;
            stmt(i->then_branch.get());
            if (i->else_branch) {
                const std::size_t para_fim = emitBc(Op::JMP, 0, 0, s->token);
                patch(para_else, here());
                stmt(i->else_branch.get());
                patch(para_fim, here());
            } else {
                patch(para_else, here());
            }
            break;
        }

        case NodeKind::While: {
            auto* w = static_cast<const WhileStmt*>(s);
            const std::size_t inicio = here();
            // while (true): sem teste
            const bool sempre = w->condition->node_kind == NodeKind::Literal &&
                std::holds_alternative<bool>(static_cast<const LiteralExpr*>(w->condition.get())->value) &&
                std::get<bool>(static_cast<const LiteralExpr*>(w->condition.get())->value);
            std::size_t sai = 0;
            if (!sempre) {
                const std::uint16_t c = exprReg(w->condition.get());
                sai = jumpIfFalse(c, s->token);
                top = save;
            }
            lacos.push_back({});
            stmt(w->body.get());
            const std::size_t volta = emitBc(Op::JMP, 0, 0, s->token);
            patch(volta, inicio);
            if (!sempre) patch(sai, here());
            for (std::size_t j : lacos.back().breaks)    patch(j, here());
            for (std::size_t j : lacos.back().continues) patch(j, inicio);
            lacos.pop_back();
            break;
        }

        case NodeKind::For: {
            auto* f = static_cast<const ForStmt*>(s);
            // for (int i in range(...)) com o range embutido: conta direto, sem
            // criar a lista — o resultado é o mesmo, a lista não é observável
            if (f->iterable->node_kind == NodeKind::Call &&
                static_cast<const CallExpr*>(f->iterable.get())->native == findPrelude("range")) {
                auto* rc = static_cast<const CallExpr*>(f->iterable.get());
                const std::uint16_t a = alloc(s->token);
                alloc(s->token); alloc(s->token); alloc(s->token);
                expr(rc->arguments[0].get(), a);   // na ordem dos argumentos do range
                expr(rc->arguments[1].get(), static_cast<std::uint16_t>(a + 1));
                if (rc->arguments.size() == 3) expr(rc->arguments[2].get(), static_cast<std::uint16_t>(a + 2));
                else emitBc(Op::LOADINT, static_cast<std::uint16_t>(a + 2), 1, s->token);
                // o erro de passo 0 sai na posição da chamada, como na nativa
                emit(Op::RANGEPREP, a, slotReg(f->iter_slot), 0, rc->token);
                const bool para_decimal = f->type_iterator->kind == Type::Kind::DECIMAL;
                const std::size_t prox = emitBc(para_decimal ? Op::FORRANGE_D : Op::FORRANGE, a, 0, s->token);
                lacos.push_back({});
                stmt(f->body.get());
                const std::size_t volta = emitBc(Op::JMP, 0, 0, s->token);
                patch(volta, prox);
                patch(prox, here());
                for (std::size_t j : lacos.back().breaks)    patch(j, here());
                for (std::size_t j : lacos.back().continues) patch(j, prox);
                lacos.pop_back();
                break;
            }
            // três registradores consecutivos: cópia, índice, slot do iterador
            const std::uint16_t a = alloc(s->token);
            alloc(s->token);
            alloc(s->token);
            const std::uint16_t col = exprReg(f->iterable.get());
            emit(Op::FORPREP, a, col, slotReg(f->iter_slot), s->token);
            top = static_cast<std::uint16_t>(a + 3);
            // for (decimal x in list<int>): converte cada elemento
            const bool para_decimal = f->type_iterator->kind == Type::Kind::DECIMAL;
            const std::size_t prox = emitBc(para_decimal ? Op::FORNEXT_D : Op::FORNEXT, a, 0, s->token);
            lacos.push_back({});
            stmt(f->body.get());
            const std::size_t volta = emitBc(Op::JMP, 0, 0, s->token);
            patch(volta, prox);
            patch(prox, here());
            for (std::size_t j : lacos.back().breaks)    patch(j, here());
            for (std::size_t j : lacos.back().continues) patch(j, prox);
            lacos.pop_back();
            break;
        }

        case NodeKind::Break:
            emitBreak(false, s->token);
            break;
        case NodeKind::Continue:
            emitBreak(true, s->token);
            break;
        case NodeKind::Throw:
            emit(Op::THROW, exprReg(static_cast<const ThrowStmt*>(s)->value.get()), 0, 0, s->token);
            break;
        case NodeKind::Try:
            tryStmt(static_cast<const TryStmt*>(s));
            break;
        default:
            throw Unsupported("instrução desconhecida (erro interno)", s->token);
    }
    top = save;
}

void Compiler::assign(const AssignStmt* s) {
    const Token& tok = s->token;
    if (s->target->node_kind != NodeKind::Identifier) { assignPlace(s); return; }
    auto* id = static_cast<const IdentifierExpr*>(s->target.get());
    TypeRef tt = s->target->resolved_type, vt = s->value->resolved_type;

    if (id->res.kind == Resolution::Kind::Field) {   // campo do objeto atual
        const auto campo = u16(id->res.slot, tok, "campos");
        std::uint16_t v = exprReg(s->value.get());
        if (s->op != TokenType::OP_ASSIGN || s->keep_lock) {
            const std::uint16_t atual = alloc(tok);
            emit(Op::GETFIELD, atual, 0, campo, tok);   // lido depois do valor (spec 5.2)
            if (s->op != TokenType::OP_ASSIGN) {
                const std::uint16_t r = alloc(tok);
                arith(compoundBaseOp(s->op), tt, atual, vt, v, r, tok);
                v = r;
            }
            if (s->keep_lock) { emit(Op::KEEPLOCK, atual, v, 0, tok); v = atual; }
        }
        emit(Op::SETFIELD, 0, campo, v, tok);
        return;
    }
    if (id->res.kind != Resolution::Kind::Local)
        throw Unsupported("alvo de atribuição inesperado (erro interno)", tok);
    const auto slot = slotReg(id->res.slot);

    if (s->op == TokenType::OP_ASSIGN && !s->keep_lock) {
        expr(s->value.get(), slot);
        return;
    }
    // a op= b: o valor primeiro, depois o valor atual do alvo (spec 5.2)
    if (!s->keep_lock && !hasOp(tt) &&
        arithK(compoundBaseOp(s->op), tt, slot, s->value.get(), slot, tok))
        return;   // x += 1: constante embutida
    std::uint16_t v = exprReg(s->value.get());
    if (!s->keep_lock) {   // a op= b direto no registrador da variável
        arith(compoundBaseOp(s->op), tt, slot, vt, v, slot, tok);
        return;
    }
    if (s->op != TokenType::OP_ASSIGN) {
        const std::uint16_t r = alloc(tok);
        arith(compoundBaseOp(s->op), tt, slot, vt, v, r, tok);
        v = r;
    }
    // op<...> de tipo travado desconhecido: o valor novo mantém o tipo travado
    emit(Op::KEEPLOCK, slot, v, 0, tok);
}

// Gravação num lugar com passos de índice e campo: x[i].c = v, o.lista[0] += v,
// self.p.x = v. Mesma ordem do interpretador (B5, spec 5.2): base e índices da
// esquerda para a direita, depois o valor; só então o caminho até o lugar e a
// gravação. Struct é valor: cada nível que é struct é lido para um
// temporário, alterado e DEVOLVIDO ao nível de cima (desenho, seção 7). Entre
// ler e devolver não roda código do programa.
void Compiler::assignPlace(const AssignStmt* s) {
    const Token& tok = s->token;

    std::vector<const Expr*> passos;   // da base para o alvo (IndexAccess ou MemberAccess)
    const Expr* e = s->target.get();
    while (e->node_kind == NodeKind::IndexAccess || e->node_kind == NodeKind::MemberAccess) {
        passos.insert(passos.begin(), e);
        e = e->node_kind == NodeKind::IndexAccess ? static_cast<const IndexAccessExpr*>(e)->object.get()
                                                  : static_cast<const MemberAccessExpr*>(e)->object.get();
    }

    // base: variável local e self são o próprio registrador (alteração no lugar);
    // campo do objeto atual é lido depois do valor e devolvido se for struct
    std::uint16_t base = 0;
    bool campo_base = false;
    std::uint16_t campo = 0;
    if (e->node_kind == NodeKind::Identifier &&
        static_cast<const IdentifierExpr*>(e)->res.kind == Resolution::Kind::Field) {
        campo_base = true;
        campo = u16(static_cast<const IdentifierExpr*>(e)->res.slot, tok, "campos");
        base = alloc(tok);
    } else {
        base = exprReg(e);
    }

    std::vector<std::uint16_t> chaves(passos.size(), 0);
    for (std::size_t i = 0; i < passos.size(); ++i)
        if (passos[i]->node_kind == NodeKind::IndexAccess)
            chaves[i] = exprReg(static_cast<const IndexAccessExpr*>(passos[i])->index.get());

    TypeRef tt = s->target->resolved_type, vt = s->value->resolved_type;
    std::uint16_t v = exprReg(s->value.get());

    if (campo_base) emit(Op::GETFIELD, base, 0, campo, tok);

    auto ler = [&](std::size_t i, std::uint16_t dst, std::uint16_t de) {
        if (passos[i]->node_kind == NodeKind::IndexAccess)
            emit(Op::INDEXPLACE, dst, de, chaves[i], passos[i]->token);
        else
            emit(Op::GETFIELD, dst, de,
                 static_cast<std::uint16_t>(static_cast<const MemberAccessExpr*>(passos[i])->field_index),
                 passos[i]->token);
    };
    auto gravar = [&](std::size_t i, std::uint16_t em, std::uint16_t valor) {
        if (passos[i]->node_kind == NodeKind::IndexAccess)
            emit(Op::SETINDEX, em, chaves[i], valor, passos[i]->token);
        else
            emit(Op::SETFIELD, em,
                 static_cast<std::uint16_t>(static_cast<const MemberAccessExpr*>(passos[i])->field_index),
                 valor, passos[i]->token);
    };

    std::vector<std::uint16_t> nivel{base};   // nivel[i]: o valor de que parte o passo i
    for (std::size_t i = 0; i + 1 < passos.size(); ++i) {
        const std::uint16_t t = alloc(tok);
        ler(i, t, nivel[i]);
        nivel.push_back(t);
    }
    const std::size_t ult = passos.size() - 1;
    if (s->op != TokenType::OP_ASSIGN || s->keep_lock) {
        // o valor atual é lido depois do lado direito (spec 5.2)
        const std::uint16_t atual = alloc(tok);
        ler(ult, atual, nivel[ult]);
        if (s->op != TokenType::OP_ASSIGN) {
            const std::uint16_t r = alloc(tok);
            arith(compoundBaseOp(s->op), tt, atual, vt, v, r, tok);
            v = r;
        }
        if (s->keep_lock) { emit(Op::KEEPLOCK, atual, v, 0, tok); v = atual; }
    }
    gravar(ult, nivel[ult], v);

    // devolve, de dentro para fora, cada nível que é struct (valor)
    for (std::size_t j = ult; j >= 1; --j)
        if (passos[j - 1]->resolved_type->is(TK::Struct)) gravar(j - 1, nivel[j - 1], nivel[j]);
    if (campo_base && e->resolved_type->is(TK::Struct)) emit(Op::SETFIELD, 0, campo, base, tok);
}

// ============================================================================
// EXCEÇÕES (desenho, seção 6)
// ============================================================================

// return: dentro de um try com finally, grava a ação pendente e vai para o
// finally (o despacho no fim dele faz o return de verdade, no contexto de fora)
void Compiler::emitReturn(std::uint16_t reg, bool tem_valor, const Token& tok) {
    if (!finallys.empty()) {
        Finally& f = finallys.back();
        if (tem_valor) emit(Op::MOVE, f.valor, reg, 0, tok);
        emitBc(Op::LOADINT, f.acao, 1, tok);
        f.saltos.push_back(emitBc(Op::JMP, 0, 0, tok));
        f.usa_return = true;
        return;
    }
    if (tem_valor) emit(Op::RET, reg, 0, 0, tok);
    else           emit(Op::RETVOID, 0, 0, 0, tok);
}

// break/continue: só passa pelo finally se sair do try (o laço é de fora dele)
void Compiler::emitBreak(bool continua, const Token& tok) {
    if (!finallys.empty() && finallys.back().loops == lacos.size()) {
        Finally& f = finallys.back();
        emitBc(Op::LOADINT, f.acao, continua ? 3 : 2, tok);
        f.saltos.push_back(emitBc(Op::JMP, 0, 0, tok));
        (continua ? f.usa_continue : f.usa_break) = true;
        return;
    }
    auto& destino = continua ? lacos.back().continues : lacos.back().breaks;
    destino.push_back(emitBc(Op::JMP, 0, 0, tok));
}

void Compiler::tryStmt(const TryStmt* t) {
    const Token& tok = t->token;
    const bool tem_finally = t->finally_block != nullptr;
    std::vector<Handler> tabela;          // deste try (os de dentro já foram para a tabela)
    std::vector<std::size_t> para_fim;    // saídas sem finally

    std::uint16_t acao = 0, valor = 0;
    if (tem_finally) {
        acao  = alloc(tok);
        valor = alloc(tok);
        finallys.push_back({lacos.size(), acao, valor, {}});
    }
    auto sair = [&] {   // fim normal do try ou de um except
        if (tem_finally) {
            emitBc(Op::LOADINT, acao, 0, tok);
            finallys.back().saltos.push_back(emitBc(Op::JMP, 0, 0, tok));
        } else {
            para_fim.push_back(emitBc(Op::JMP, 0, 0, tok));
        }
    };

    const auto ini = static_cast<std::uint32_t>(here());
    stmt(t->body.get());
    const auto fim = static_cast<std::uint32_t>(here());
    sair();

    std::vector<std::pair<std::uint32_t, std::uint32_t>> blocos_except;
    for (const auto& h : t->handlers) {
        const auto alvo = static_cast<std::uint32_t>(here());
        tabela.push_back({ini, fim, alvo, h.type_name == "Error" ? std::string() : h.type_name,
                          slotReg(h.var_slot)});
        stmt(h.body.get());
        blocos_except.emplace_back(alvo, static_cast<std::uint32_t>(here()));
        sair();
    }

    if (tem_finally) {
        Finally f = std::move(finallys.back());
        finallys.pop_back();   // o finally e o despacho são do contexto de fora

        // pega-tudo: erro do try ou de um except roda o finally e é relançado
        const auto pega = static_cast<std::uint32_t>(here());
        tabela.push_back({ini, fim, pega, "", valor});
        for (const auto& [a, b] : blocos_except) tabela.push_back({a, b, pega, "", valor});
        emitBc(Op::LOADINT, acao, 4, tok);

        const std::size_t inicio_finally = here();
        for (std::size_t j : f.saltos) patch(j, inicio_finally);
        stmt(t->finally_block.get());

        // despacho da ação pendente
        const std::uint16_t cmp = alloc(tok);
        auto caso = [&](int codigo, auto&& acao_pendente) {
            emitBc(Op::LOADINT, cmp, codigo, tok);
            emit(Op::EQ, cmp, acao, cmp, tok);
            const std::size_t pula = emitBc(Op::JMPIFNOT, cmp, 0, tok);
            acao_pendente();
            patch(pula, here());
        };
        if (f.usa_return)   caso(1, [&] { emitReturn(valor, !ret_void, tok); });
        if (f.usa_break)    caso(2, [&] { emitBreak(false, tok); });
        if (f.usa_continue) caso(3, [&] { emitBreak(true, tok); });
        caso(4, [&] { emit(Op::THROW, valor, 0, 0, tok); });
    }
    for (std::size_t j : para_fim) patch(j, here());
    for (auto& h : tabela) p->handlers.push_back(std::move(h));
}

// ============================================================================
// FUNÇÕES E MÓDULOS
// ============================================================================

std::size_t Compiler::newProto(const std::string& name, const Token& decl) {
    Proto pr;
    pr.name = name;
    pr.decl = decl.loc();
    img.protos.push_back(std::move(pr));
    return img.protos.size() - 1;
}

// Corpo de função (metodo = false) ou de método/construtor (self em r0)
void Compiler::body(Proto* pr, std::uint32_t num_slots, std::size_t nparams, bool metodo,
                    const Stmt* corpo, const Token& tok, bool vazio) {
    p      = pr;
    ret_void = vazio;
    off    = metodo ? 1 : 0;
    nlocal = u16(num_slots + off, tok, "variáveis locais");
    top    = nlocal;
    p->num_params = u16(nparams + off, tok, "parâmetros");
    p->num_regs   = nlocal;
    stmt(corpo);
    emit(Op::RETVOID, 0, 0, 0, tok);   // fim do corpo sem return (void)
}

// Inicializador dos campos (protótipo oculto: sem frame no stack trace, como no
// interpretador), com self em r0; os campos na ordem da declaração
void Compiler::classInit(const ClassDecl* cls, std::size_t idx) {
    p      = &img.protos[idx];
    off    = 1;
    nlocal = top = p->num_regs = 1;
    p->num_params = 1;
    for (std::size_t i = 0; i < cls->fields.size(); ++i) {
        const auto& f = cls->fields[i];
        const std::uint16_t t = alloc(f.token);
        if (f.initializer) expr(f.initializer.get(), t);
        else emit(f.type->kind == Type::Kind::DICT ? Op::NEWDICT : Op::NEWLIST, t, 0, 0, f.token);
        emit(Op::SETFIELD, 0, u16(i, f.token, "campos"), t, f.token);
        top = 1;
    }
    emit(Op::RETVOID, 0, 0, 0, cls->token);
}

// const globais do módulo, na ordem em que aparecem
void Compiler::moduleInit(const ::cinza::Program* prog) {
    bool tem = false;
    for (const auto& s : prog->statements)
        if (s->node_kind == NodeKind::VarDecl) tem = true;
    if (!tem) return;

    const std::size_t idx = newProto("", prog->statements.front()->token);   // nome vazio: fora do stack trace
    img.inits.push_back(idx);
    p = &img.protos[idx];
    nlocal = top = p->num_regs = 0;
    off = 0;
    for (const auto& s : prog->statements) {
        if (s->node_kind != NodeKind::VarDecl) continue;
        auto* v = static_cast<const VarDeclStmt*>(s.get());
        const std::uint16_t t = alloc(s->token);
        expr(v->initializer.get(), t);
        emit(Op::SETGLOBAL, t, u16(v->res.slot, s->token, "slots globais"), 0, s->token);
        top = 0;
    }
    emit(Op::RETVOID, 0, 0, 0, prog->statements.front()->token);
}

Image Compiler::run() {
    img.num_globals    = layout.count;
    img.native_globals = layout.natives;

    auto metodos = [](const ClassDecl* c) {
        std::vector<const FunctionDecl*> v;
        for (const auto* lista : {&c->priv_methods, &c->pub_methods})
            for (const auto& m : *lista)
                if (m->node_kind == NodeKind::FunctionDecl) v.push_back(static_cast<const FunctionDecl*>(m.get()));
        return v;
    };

    // 1. um protótipo por função, método, construtor e inicializador de campos
    //    (chamadas podem ir para declarações que vêm depois)
    for (const auto* prog : programs)
        for (const auto& s : prog->statements) {
            if (s->node_kind == NodeKind::FunctionDecl) {
                auto* fn = static_cast<const FunctionDecl*>(s.get());
                proto_of[fn] = newProto(fn->trace_name, fn->token);
            } else if (s->node_kind == NodeKind::ClassDecl) {
                auto* c = static_cast<const ClassDecl*>(s.get());
                ClassRef ref;
                ref.decl = c;
                for (const auto* m : metodos(c)) proto_of[m] = newProto(m->trace_name, m->token);
                if (c->constructor) {
                    ref.ctor = newProto(c->constructor->trace_name, c->constructor->token);
                    proto_of[c->constructor.get()] = ref.ctor;
                }
                if (!c->fields.empty()) ref.init = newProto("", c->token);   // oculto
                img.class_index[c] = img.classes.size();
                class_of[c->class_name] = img.classes.size();
                img.classes.push_back(ref);
            } else if (s->node_kind == NodeKind::StructDecl) {
                auto* st = static_cast<const StructDecl*>(s.get());
                struct_of[st->name] = img.structs.size();
                img.structs.push_back(st);
            }
        }

    // tabelas de interface: para cada interface cumprida, os protótipos na ordem
    // dos métodos dela (montadas pelo semântico em ClassDecl::itables)
    for (ClassRef& ref : img.classes)
        for (const auto& [iface, metodos] : ref.decl->itables) {
            if (!iface_of.count(iface)) {
                iface_of[iface] = img.interfaces.size();
                img.interfaces.push_back(iface);
            }
            std::vector<std::size_t> protos;
            for (const FunctionDecl* m : metodos) protos.push_back(proto_of.at(m));
            ref.itables.emplace_back(iface, std::move(protos));
        }
    // interfaces que nenhuma classe cumpre ainda podem aparecer em chamadas
    for (const auto* prog : programs)
        for (const auto& s : prog->statements)
            if (s->node_kind == NodeKind::InterfaceDecl) {
                auto* in = static_cast<const InterfaceDecl*>(s.get());
                if (!iface_of.count(in)) {
                    iface_of[in] = img.interfaces.size();
                    img.interfaces.push_back(in);
                }
            }

    // 2. inicialização dos const de cada módulo, na ordem topológica
    for (const auto* prog : programs) moduleInit(prog);

    // 3. corpos
    for (const auto* prog : programs)
        for (const auto& s : prog->statements) {
            if (s->node_kind == NodeKind::FunctionDecl) {
                auto* fn = static_cast<const FunctionDecl*>(s.get());
                body(&img.protos[proto_of.at(fn)], fn->num_slots, fn->parameters.size(), false,
                     fn->body.get(), fn->token, fn->return_type->kind == Type::Kind::VOID);
            } else if (s->node_kind == NodeKind::ClassDecl) {
                auto* c = static_cast<const ClassDecl*>(s.get());
                const ClassRef& ref = img.classes[class_of.at(c->class_name)];
                for (const auto* m : metodos(c))
                    body(&img.protos[proto_of.at(m)], m->num_slots, m->parameters.size(), true,
                         m->body.get(), m->token, m->return_type->kind == Type::Kind::VOID);
                if (c->constructor)
                    body(&img.protos[ref.ctor], c->constructor->num_slots,
                         c->constructor->parameters.size(), true, c->constructor->body.get(),
                         c->constructor->token, true);
                if (ref.init != ClassRef::none) classInit(c, ref.init);
            }
        }

    // 4. a main do principal (o semântico garante que existe)
    for (const auto& s : programs.back()->statements)
        if (s->node_kind == NodeKind::FunctionDecl) {
            auto* fn = static_cast<const FunctionDecl*>(s.get());
            if (fn->name == "main") {
                img.main          = proto_of.at(fn);
                img.main_has_args = !fn->parameters.empty();
            }
        }
    return std::move(img);
}

} // namespace

Image compile(const std::vector<const ::cinza::Program*>& programs, const GlobalLayout& layout) {
    return Compiler(programs, layout).run();
}

} // namespace cinza::cvm
