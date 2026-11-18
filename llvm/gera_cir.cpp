// AST analisada → CIR. P3: o subconjunto do "olá mundo" — a main sem parâmetros, com
// print(literais...). Tudo o mais é NaoSuportado, com a posição: as etapas P4 a P9
// (REVISAO_V2.md, seção P) vão ampliando.
#include "gera_cir.h"
#include "../ast.h"
#include "../natives.h"

namespace cinza::cir {

namespace {

[[noreturn]] void naoSuportado(const std::string& o_que, const Token& tok) {
    throw NaoSuportado{o_que, tok};
}

const char* nomeDeclaracao(NodeKind k) {
    switch (k) {
        case NodeKind::FunctionDecl:  return "função";
        case NodeKind::ClassDecl:     return "class";
        case NodeKind::StructDecl:    return "struct";
        case NodeKind::EnumDecl:      return "enum";
        case NodeKind::InterfaceDecl: return "interface";
        case NodeKind::ErrorDecl:     return "error";
        case NodeKind::VarDecl:       return "const global";
        default:                      return "declaração";
    }
}

class Gerador {
public:
    explicit Gerador(Modulo& m) : m(m) {}

    void funcaoMain(const FunctionDecl& fn) {
        if (!fn.parameters.empty()) naoSuportado("main(list<string> args)", fn.token);
        Funcao f;
        f.nome = "cz:::main";
        f.ret  = Tipo::Void;
        f.blocos.push_back(Bloco{"b0", {}});
        atual = &f;
        for (const auto& s : static_cast<const BlockStmt&>(*fn.body).statements) instrucao(*s);
        bloco().instr.push_back(Instr{Op::Ret, 0, {}, 0, fn.token});
        m.funcoes.push_back(std::move(f));
    }

private:
    Modulo& m;
    Funcao* atual = nullptr;

    Bloco& bloco() { return atual->blocos.back(); }

    void instrucao(const Stmt& s) {
        if (s.node_kind != NodeKind::ExprStmt) naoSuportado("esta instrução", s.token);
        const Expr& e = *static_cast<const ExprStmt&>(s).expression;
        if (e.node_kind != NodeKind::Call) naoSuportado("esta expressão como instrução", e.token);
        const auto& c = static_cast<const CallExpr&>(e);
        if (!c.native || c.native->name != "print" || c.interpolated)
            naoSuportado("chamada de '" + c.function_name + "'", c.token);
        // print(a, b, ...): cada argumento embrulhado num CzValor, sem posse; o resultado
        // (void) tem posse e é solto
        std::vector<Operando> args;
        for (const auto& a : c.arguments) {
            const std::uint32_t t = atual->novoTemp(Tipo::Valor);
            bloco().instr.push_back(Instr{Op::Embrulha, t, {constante(*a)}, 0, a->token});
            args.push_back(Operando::temp(t));
        }
        const std::uint32_t r = atual->novoTemp(Tipo::Valor);
        bloco().instr.push_back(Instr{Op::ChamaNativa, r, args, m.nativa("", "print"), c.token});
        bloco().instr.push_back(Instr{Op::Solta, 0, {Operando::temp(r)}, 0, c.token});
    }

    Operando constante(const Expr& e) {
        // -literal: o valor negado já aqui (o tipo é o do literal, decidido pelo semântico)
        if (e.node_kind == NodeKind::Unary) {
            const auto& u = static_cast<const UnaryExpr&>(e);
            if (u.op == TokenType::OP_MINUS && u.operand->node_kind == NodeKind::Literal) {
                Operando o = constante(*u.operand);
                if (o.k == Operando::K::I32 || o.k == Operando::K::I64) { o.i = -o.i; return o; }
                if (o.k == Operando::K::F32 || o.k == Operando::K::F64) { o.f = -o.f; return o; }
            }
        }
        if (e.node_kind != NodeKind::Literal) naoSuportado("esta expressão", e.token);
        const auto& lit = static_cast<const LiteralExpr&>(e);
        Operando o;
        if (const auto* s = std::get_if<std::string>(&lit.value)) return Operando::literal(m.literal(*s));
        if (const auto* b = std::get_if<bool>(&lit.value)) { o.k = Operando::K::Bool; o.i = *b; return o; }
        if (const auto* i = std::get_if<std::int64_t>(&lit.value)) {
            o.k = lit.resolved_type && lit.resolved_type->is(TypeInfo::Kind::I64) ? Operando::K::I64
                                                                                  : Operando::K::I32;
            o.i = *i;
            return o;
        }
        const double d = std::get<double>(lit.value);
        o.k = lit.resolved_type && lit.resolved_type->is(TypeInfo::Kind::F32) ? Operando::K::F32
                                                                              : Operando::K::F64;
        o.f = d;
        return o;
    }
};

}  // namespace

Modulo gera(const std::vector<Module>& modulos) {
    Modulo m;
    for (std::size_t i = 0; i + 1 < modulos.size(); ++i)
        if (!modulos[i].native) {
            const auto& imps = modulos.back().program->imports;
            naoSuportado("import de módulo", imps.empty() ? Token() : imps.front().token);
        }
    const Program& prog = *modulos.back().program;
    if (!prog.instancias.empty())
        naoSuportado("funções e structs genéricos", prog.instancias.front()->token);
    Gerador g(m);
    for (const auto& s : prog.statements) {
        const auto* fn = s->node_kind == NodeKind::FunctionDecl ? static_cast<const FunctionDecl*>(s.get())
                                                                : nullptr;
        if (fn && fn->name == "main" && fn->type_params.empty()) g.funcaoMain(*fn);
        else naoSuportado(nomeDeclaracao(s->node_kind), s->token);
    }
    return m;
}

}  // namespace cinza::cir
