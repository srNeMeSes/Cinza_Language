#include "compiler.h"
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
    std::unordered_map<const FunctionDecl*, std::size_t> proto_of;
    std::unordered_map<const NativeFn*, std::size_t>     native_of;

    // estado da função em compilação
    Proto*        p      = nullptr;
    std::uint16_t nlocal = 0;   // registradores 0 .. nlocal-1 são variáveis (slots do B2)
    std::uint16_t top    = 0;   // próximo temporário livre

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
    void          call(const CallExpr* e, std::uint16_t dst);

    // ── instruções ───────────────────────────────────────────────────────
    void stmt(const Stmt* s);
    void assign(const AssignStmt* s);

    // ── funções ──────────────────────────────────────────────────────────
    std::size_t newProto(const std::string& name, const Token& decl);
    void        function(const FunctionDecl* fn);
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
        if (id->res.kind == Resolution::Kind::Local) return static_cast<std::uint16_t>(id->res.slot);
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
                    const auto slot = static_cast<std::uint16_t>(id->res.slot);
                    if (slot != dst) emit(Op::MOVE, dst, slot, 0, e->token);
                    break;
                }
                case Resolution::Kind::Global:
                    emit(Op::GETGLOBAL, dst, u16(id->res.slot, e->token, "slots globais"), 0, e->token);
                    break;
                case Resolution::Kind::Field:
                case Resolution::Kind::Self:
                    unsupported("Campo e self", 5, e->token);
                case Resolution::Kind::Type:
                    unsupported("Tipo como valor", 6, e->token);
                case Resolution::Kind::None:
                    throw Unsupported("identificador sem resolução (erro interno)", e->token);
            }
            break;
        }

        case NodeKind::Cast: {
            auto* c = static_cast<const CastExpr*>(e);
            if (hasOp(c->operand->resolved_type) || hasOp(c->resolved_type))
                unsupported("op<...>", 6, e->token);
            if (c->resolved_type->is(TK::Decimal) && c->operand->resolved_type->is(TK::Int)) {
                const std::uint16_t r = exprReg(c->operand.get());
                emit(Op::I2D, dst, r, 0, e->token);
            } else {
                expr(c->operand.get(), dst);
            }
            break;
        }

        case NodeKind::Unary: {
            auto* u = static_cast<const UnaryExpr*>(e);
            if (hasOp(u->operand->resolved_type)) unsupported("op<...>", 6, e->token);
            const std::uint16_t r = exprReg(u->operand.get());
            if (u->op == TokenType::OP_NOT)
                emit(Op::NOT, dst, r, 0, e->token);
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

        case NodeKind::ListLiteral: case NodeKind::DictLiteral: case NodeKind::PairLiteral:
        case NodeKind::IndexAccess:
            unsupported("Coleções", 4, e->token);
        case NodeKind::MemberAccess: case NodeKind::MethodCall: case NodeKind::New:
            unsupported("Campos, métodos e new", 5, e->token);
        case NodeKind::TypeLiteral:
            unsupported("Tipo como valor", 6, e->token);
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
    if (hasOp(lt) || hasOp(rt)) unsupported("op<...>", 6, tok);

    // operandos na ordem do fonte; a instrução final pode trocar os registradores
    const std::uint16_t l = exprReg(e->left.get());
    const std::uint16_t r = exprReg(e->right.get());
    arith(e->op, lt, l, rt, r, dst, tok);
}

// a op b com os dois valores já em registradores
void Compiler::arith(TokenType op, TypeRef lt, std::uint16_t l, TypeRef rt, std::uint16_t r,
                     std::uint16_t dst, const Token& tok) {
    if (op == TokenType::OP_EQUAL)     { emit(Op::EQ, dst, l, r, tok); return; }
    if (op == TokenType::OP_NOT_EQUAL) { emit(Op::NE, dst, l, r, tok); return; }

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
    if (e->type_of) unsupported("type()", 6, tok);

    const bool nativa = e->native != nullptr;
    if (!nativa && !e->target) unsupported("Criar um erro (Tipo(\"...\"))", 7, tok);
    if (e->implicit_method) unsupported("Chamada de método", 5, tok);

    // argumentos em registradores consecutivos a partir de `base` (convenção do Lua)
    const std::uint16_t base = top;
    for (const auto& arg : e->arguments) {
        const std::uint16_t r = alloc(tok);
        expr(arg.get(), r);
    }
    if (e->arguments.empty()) alloc(tok);   // lugar do resultado
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
            const auto slot = static_cast<std::uint16_t>(v->res.slot);
            if (!v->initializer) unsupported("Coleção sem valor inicial", 4, s->token);
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
            if (r->value) emit(Op::RET, exprReg(r->value.get()), 0, 0, s->token);
            else          emit(Op::RETVOID, 0, 0, 0, s->token);
            break;
        }

        case NodeKind::Block:
            for (const auto& x : static_cast<const BlockStmt*>(s)->statements)
                if (x) stmt(x.get());
            break;

        case NodeKind::If: case NodeKind::While: case NodeKind::For:
        case NodeKind::Break: case NodeKind::Continue:
            unsupported("Controle de fluxo", 3, s->token);
        case NodeKind::Try: case NodeKind::Throw:
            unsupported("Exceções", 7, s->token);
        default:
            throw Unsupported("instrução desconhecida (erro interno)", s->token);
    }
    top = save;
}

void Compiler::assign(const AssignStmt* s) {
    const Token& tok = s->token;
    if (s->keep_lock) unsupported("op<...>", 6, tok);
    if (s->target->node_kind != NodeKind::Identifier) unsupported("Atribuição a campo ou índice", 4, tok);
    auto* id = static_cast<const IdentifierExpr*>(s->target.get());
    if (id->res.kind != Resolution::Kind::Local) unsupported("Atribuição a campo", 5, tok);
    const auto slot = static_cast<std::uint16_t>(id->res.slot);

    if (s->op == TokenType::OP_ASSIGN) {
        expr(s->value.get(), slot);
        return;
    }
    // a op= b: o valor primeiro, depois o valor atual do alvo (spec 5.2)
    TypeRef tt = s->target->resolved_type, vt = s->value->resolved_type;
    if (hasOp(tt) || hasOp(vt)) unsupported("op<...>", 6, tok);
    const std::uint16_t v = exprReg(s->value.get());
    arith(compoundBaseOp(s->op), tt, slot, vt, v, slot, tok);
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

void Compiler::function(const FunctionDecl* fn) {
    p      = &img.protos[proto_of.at(fn)];
    nlocal = u16(fn->num_slots, fn->token, "variáveis locais");
    top    = nlocal;
    p->num_params = u16(fn->parameters.size(), fn->token, "parâmetros");
    p->num_regs   = nlocal;
    stmt(fn->body.get());
    emit(Op::RETVOID, 0, 0, 0, fn->token);   // fim do corpo sem return (função void)
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

    // 1. um protótipo por função (chamadas podem ir para funções declaradas depois)
    for (const auto* prog : programs)
        for (const auto& s : prog->statements)
            if (s->node_kind == NodeKind::FunctionDecl) {
                auto* fn = static_cast<const FunctionDecl*>(s.get());
                proto_of[fn] = newProto(fn->trace_name, fn->token);
            }

    // 2. inicialização dos const de cada módulo, na ordem topológica
    for (const auto* prog : programs) moduleInit(prog);

    // 3. corpos
    for (const auto* prog : programs)
        for (const auto& s : prog->statements)
            if (s->node_kind == NodeKind::FunctionDecl)
                function(static_cast<const FunctionDecl*>(s.get()));

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
