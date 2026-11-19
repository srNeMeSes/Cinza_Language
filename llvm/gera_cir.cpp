// AST analisada → CIR. P4: números, bool, funções, if/while/for range, print (e as
// nativas de valores simples), const globais. O resto é NaoSuportado, com a posição: as
// etapas P5 a P9 (REVISAO_V2.md, seção P) vão ampliando.
//
// A ordem de avaliação e as posições dos erros são as do compilador da CVM
// (cvm/compiler.cpp): mesma posição de token em cada operação que pode falhar.
#include "gera_cir.h"
#include "../natives.h"
#include "../semantic.h"
#include <map>
#include <unordered_map>

namespace cinza::cir {

namespace {

using TK = TypeInfo::Kind;

[[noreturn]] void naoSuportado(const std::string& o_que, const Token& tok) {
    throw NaoSuportado{o_que, tok};
}

const char* nomeDeclaracao(NodeKind k) {
    switch (k) {
        case NodeKind::ClassDecl:     return "class";
        case NodeKind::StructDecl:    return "struct";
        case NodeKind::EnumDecl:      return "enum";
        case NodeKind::InterfaceDecl: return "interface";
        case NodeKind::ErrorDecl:     return "error";
        default:                      return "declaração";
    }
}

// O tipo da CIR de um tipo da linguagem (P4: só os escalares)
Tipo tipoDe(TypeRef t, const Token& tok) {
    if (!t) naoSuportado("expressão sem tipo (erro interno)", tok);
    switch (t->kind) {
        case TK::Void: return Tipo::Void;
        case TK::Bool: return Tipo::Bool;
        case TK::I32:  return Tipo::I32;
        case TK::I64:  return Tipo::I64;
        case TK::F32:  return Tipo::F32;
        case TK::F64:  return Tipo::F64;
        case TK::String: naoSuportado("string fora de um literal passado a uma nativa", tok);
        default:       naoSuportado("o tipo '" + t->str() + "'", tok);
    }
}

bool literalVerdadeiro(const Expr& e) {
    if (e.node_kind != NodeKind::Literal) return false;
    const auto* b = std::get_if<bool>(&static_cast<const LiteralExpr&>(e).value);
    return b && *b;
}

// Nome do módulo nativo de uma função da biblioteca ("" = embutida)
std::string moduloDaNativa(const NativeFn* fn, const Token& tok) {
    if (findPrelude(fn->name) == fn) return "";
    for (const NativeModule& m : nativeModules())
        for (const NativeFn& f : m.fns)
            if (&f == fn) return m.name;
    naoSuportado("nativa sem módulo (erro interno)", tok);
}

class Gerador {
public:
    Gerador(Modulo& m, const GlobalLayout& layout) : m(m) {
        for (const auto& [slot, c] : layout.natives) consts_nativos[slot] = c;
    }

    // 1ª passada: a assinatura de cada função (chamadas podem ir para as de depois)
    void declara(const FunctionDecl& fn) {
        Funcao f;
        f.nome       = "cz:::" + fn.name;
        f.nome_trace = fn.trace_name;
        f.decl       = fn.token;
        for (const Parameter& p : fn.parameters) f.params.push_back(tipoDe(TypeChecker::fromAst(p.type.get()), p.token));
        f.ret = tipoDe(TypeChecker::fromAst(fn.return_type.get()), fn.token);
        funcao_de[&fn] = static_cast<std::uint32_t>(m.funcoes.size());
        if (fn.name == "main") {
            if (!fn.parameters.empty()) naoSuportado("main(list<string> args)", fn.token);
            m.main = static_cast<int>(m.funcoes.size());
        }
        m.funcoes.push_back(std::move(f));
    }

    // const do nível superior: um global, escrito pela função de inicialização
    void declaraGlobal(const VarDeclStmt& v) {
        TypeRef t = v.type->kind == Type::Kind::VAR ? v.initializer->resolved_type
                                                    : TypeChecker::fromAst(v.type.get());
        global_de[v.res.slot] = static_cast<std::uint32_t>(m.globais.size());
        m.globais.push_back(tipoDe(t, v.token));
    }

    // 2ª passada: o corpo
    void corpo(const FunctionDecl& fn) {
        comeca(funcao_de.at(&fn));
        for (std::size_t i = 0; i < fn.parameters.size(); ++i) {
            const std::uint32_t s = f->novoSlot(f->params[i]);
            slot_de[{static_cast<std::uint32_t>(i), f->params[i]}] = s;
        }
        instrucao(*fn.body);
        if (!terminado) {
            // o semântico garante o return nas funções com valor
            if (f->ret == Tipo::Void) poe(Op::Ret, fn.body->token);
            else poe(Op::Inalcancavel, fn.body->token);
        }
    }

    // A função oculta que inicializa os const, na ordem em que aparecem
    void inicializacao(const std::vector<const VarDeclStmt*>& consts) {
        Funcao fi;
        fi.nome   = "cz.init";
        fi.oculta = true;
        fi.decl   = consts.front()->token;
        m.init    = static_cast<int>(m.funcoes.size());
        m.funcoes.push_back(std::move(fi));
        comeca(static_cast<std::uint32_t>(m.init));
        for (const VarDeclStmt* v : consts) {
            const std::uint32_t g = global_de.at(v->res.slot);
            const Operando val = valorDe(*v->initializer, m.globais[g]);
            Instr& in = poe(Op::GravaG, v->token);   // depois do valor: poe invalida referências
            in.aux = g;
            in.args = {val};
        }
        poe(Op::Ret, consts.front()->token);
    }

private:
    Modulo& m;
    Funcao* f = nullptr;
    std::uint32_t atual = 0;      // o bloco em que se escreve
    bool terminado = false;       // o bloco atual já terminou (br, ret...)
    std::unordered_map<const FunctionDecl*, std::uint32_t> funcao_de;
    std::unordered_map<std::uint32_t, std::uint32_t>       global_de;        // slot do B2 → global
    std::unordered_map<std::uint32_t, const NativeConst*>  consts_nativos;   // Math.pi...
    std::map<std::pair<std::uint32_t, Tipo>, std::uint32_t> slot_de;         // (slot do B2, tipo) → slot
    struct Laco { std::uint32_t continua, sai; };
    std::vector<Laco> lacos;

    void comeca(std::uint32_t fi) {
        f = &m.funcoes[fi];
        slot_de.clear();
        lacos.clear();
        atual = f->novoBloco("b");
        terminado = false;
    }

    // ── emissão ──────────────────────────────────────────────────────────
    static bool terminador(Op op) {
        return op == Op::Br || op == Op::BrC || op == Op::Ret || op == Op::Inalcancavel;
    }
    Instr& poe(Op op, const Token& pos) {
        // código depois de return/break/continue: um bloco sem predecessores
        if (terminado) { atual = f->novoBloco("morto"); terminado = false; }
        Bloco& b = f->blocos[atual];
        b.instr.emplace_back();
        b.instr.back().op = op;
        b.instr.back().pos = pos;
        terminado = terminador(op);
        return b.instr.back();
    }
    std::uint32_t temp(Op op, Tipo t, std::vector<Operando> args, const Token& pos, std::uint32_t aux = 0) {
        Instr& in = poe(op, pos);
        in.dst  = f->novoTemp(t);
        in.args = std::move(args);
        in.aux  = aux;
        return in.dst;
    }
    void br(std::uint32_t alvo, const Token& pos) {
        if (terminado) return;   // depois de um return: nada a ligar
        poe(Op::Br, pos).alvo = alvo;
    }
    void brc(const Operando& c, std::uint32_t sim, std::uint32_t nao, const Token& pos) {
        Instr& in = poe(Op::BrC, pos);
        in.args  = {c};
        in.alvo  = sim;
        in.senao = nao;
    }
    void entra(std::uint32_t bloco) { atual = bloco; terminado = false; }

    Tipo tipoOperando(const Operando& o) const {
        switch (o.k) {
            case Operando::K::Temp:    return f->temps[o.indice];
            case Operando::K::I32:     return Tipo::I32;
            case Operando::K::I64:     return Tipo::I64;
            case Operando::K::F32:     return Tipo::F32;
            case Operando::K::F64:     return Tipo::F64;
            case Operando::K::Bool:    return Tipo::Bool;
            case Operando::K::Literal: return Tipo::Str;
        }
        return Tipo::Void;
    }

    // O slot de uma variável local (o semântico pode reusar um slot do B2 em escopos
    // diferentes, com outro tipo: a chave leva o tipo)
    std::uint32_t slotVar(std::uint32_t b2, Tipo t) {
        auto [it, novo] = slot_de.try_emplace({b2, t}, 0);
        if (novo) it->second = f->novoSlot(t);
        return it->second;
    }

    // ── expressões ───────────────────────────────────────────────────────

    // O valor de `e`, que precisa ter o tipo `esperado` (o semântico já inseriu as
    // conversões; diferença aqui é erro interno)
    Operando valorDe(const Expr& e, Tipo esperado) {
        Operando o = expr(e);
        if (tipoOperando(o) != esperado)
            naoSuportado(std::string("tipos diferentes na CIR (") + nomeTipo(tipoOperando(o)) + " e " +
                         nomeTipo(esperado) + ", erro interno)", e.token);
        return o;
    }

    Operando expr(const Expr& e) {
        switch (e.node_kind) {
            case NodeKind::Literal:    return literal(static_cast<const LiteralExpr&>(e));
            case NodeKind::Identifier: return identificador(static_cast<const IdentifierExpr&>(e));
            case NodeKind::Binary:     return binaria(static_cast<const BinaryExpr&>(e));
            case NodeKind::Unary: {
                const auto& u = static_cast<const UnaryExpr&>(e);
                const Operando a = expr(*u.operand);
                const Tipo t = tipoOperando(a);
                if (u.op == TokenType::OP_NOT) return Operando::temp(temp(Op::Nao, Tipo::Bool, {a}, e.token));
                return Operando::temp(temp(Op::Neg, t, {a}, e.token));
            }
            case NodeKind::Cast: {
                const auto& c = static_cast<const CastExpr&>(e);
                const Tipo para = tipoDe(c.resolved_type, e.token);
                const Operando a = expr(*c.operand);
                if (tipoOperando(a) == para) return a;   // cast<i64>(n) com n i64
                if (!(ehInteiro(para) || ehFlutuante(para)) ||
                    !(ehInteiro(tipoOperando(a)) || ehFlutuante(tipoOperando(a))))
                    naoSuportado("esta conversão", e.token);
                return Operando::temp(temp(Op::Converte, para, {a}, e.token));
            }
            case NodeKind::Call: return chamada(static_cast<const CallExpr&>(e));
            case NodeKind::ListLiteral: case NodeKind::DictLiteral: case NodeKind::PairLiteral:
                naoSuportado("list, dict e pair", e.token);
            case NodeKind::IndexAccess: case NodeKind::Slice:
                naoSuportado("acesso por índice", e.token);
            case NodeKind::MemberAccess: naoSuportado("acesso a membro", e.token);
            case NodeKind::MethodCall:   naoSuportado("chamada de método", e.token);
            case NodeKind::New:          naoSuportado("new", e.token);
            case NodeKind::TypeLiteral:  naoSuportado("tipo usado como valor", e.token);
            default:                     naoSuportado("esta expressão", e.token);
        }
    }

    Operando literal(const LiteralExpr& lit) {
        if (const auto* b = std::get_if<bool>(&lit.value)) return Operando::boolean(*b);
        if (const auto* i = std::get_if<std::int64_t>(&lit.value)) {
            // como o LOADI32 da CVM: o literal i32 nos 32 bits
            if (tipoDe(lit.resolved_type, lit.token) == Tipo::I32)
                return Operando::i32(static_cast<std::int32_t>(*i));
            return Operando::i64(*i);
        }
        if (const auto* d = std::get_if<double>(&lit.value))
            return tipoDe(lit.resolved_type, lit.token) == Tipo::F32 ? Operando::f32(*d) : Operando::f64(*d);
        naoSuportado("string fora de um literal passado a uma nativa", lit.token);
    }

    Operando identificador(const IdentifierExpr& id) {
        switch (id.res.kind) {
            case Resolution::Kind::Local: {
                const Tipo t = tipoDe(id.resolved_type, id.token);
                auto it = slot_de.find({id.res.slot, t});
                if (it == slot_de.end()) naoSuportado("variável sem slot (erro interno)", id.token);
                Instr& in = poe(Op::Carrega, id.token);
                in.dst = f->novoTemp(t);
                in.aux = it->second;
                return Operando::temp(in.dst);
            }
            case Resolution::Kind::Global: {
                if (auto c = consts_nativos.find(id.res.slot); c != consts_nativos.end()) {
                    const Value& v = c->second->value;   // Math.pi: o valor já pronto
                    switch (v.kind()) {
                        case Value::Kind::I32:  return Operando::i32(v.asI32());
                        case Value::Kind::I64:  return Operando::i64(v.asInt());
                        case Value::Kind::F32:  return Operando::f32(v.asF32());
                        case Value::Kind::F64:  return Operando::f64(v.asFloat());
                        case Value::Kind::BOOL: return Operando::boolean(v.asBool());
                        default: naoSuportado("esta constante da biblioteca", id.token);
                    }
                }
                auto g = global_de.find(id.res.slot);
                if (g == global_de.end()) naoSuportado("const de outro módulo", id.token);
                Instr& in = poe(Op::CarregaG, id.token);
                in.dst = f->novoTemp(m.globais[g->second]);
                in.aux = g->second;
                return Operando::temp(in.dst);
            }
            case Resolution::Kind::Field: case Resolution::Kind::Self:
                naoSuportado("campos e self", id.token);
            case Resolution::Kind::Type:
                naoSuportado("tipo usado como valor", id.token);
            case Resolution::Kind::None:
                break;
        }
        naoSuportado("identificador sem resolução (erro interno)", id.token);
    }

    Operando binaria(const BinaryExpr& e) {
        if (e.op == TokenType::OP_AND || e.op == TokenType::OP_OR) {
            // curto-circuito: o resultado passa por um slot (atravessa blocos)
            const std::uint32_t s = f->novoSlot(Tipo::Bool);
            const Operando a = valorDe(*e.left, Tipo::Bool);
            Instr& g1 = poe(Op::Grava, e.token);
            g1.aux = s;
            g1.args = {a};
            const std::uint32_t direita = f->novoBloco("direita"), fim = f->novoBloco("fim");
            if (e.op == TokenType::OP_AND) brc(a, direita, fim, e.token);
            else                           brc(a, fim, direita, e.token);
            entra(direita);
            const Operando b = valorDe(*e.right, Tipo::Bool);
            Instr& g2 = poe(Op::Grava, e.token);
            g2.aux = s;
            g2.args = {b};
            br(fim, e.token);
            entra(fim);
            Instr& c = poe(Op::Carrega, e.token);
            c.dst = f->novoTemp(Tipo::Bool);
            c.aux = s;
            return Operando::temp(c.dst);
        }
        // operandos na ordem do fonte; o semântico já deu aos dois o mesmo tipo
        const Operando a = expr(*e.left);
        const Tipo t = tipoOperando(a);
        const Operando b = valorDe(*e.right, t);
        switch (e.op) {
            case TokenType::OP_EQUAL: case TokenType::OP_NOT_EQUAL:
                return Operando::temp(temp(Op::Compara, Tipo::Bool, {a, b}, e.token, static_cast<std::uint32_t>(e.op)));
            case TokenType::OP_LESS: case TokenType::OP_LESS_EQUAL:
            case TokenType::OP_GREATER: case TokenType::OP_GREATER_EQUAL:
                if (t == Tipo::Bool) naoSuportado("comparação de bool (erro interno)", e.token);
                return Operando::temp(temp(Op::Compara, Tipo::Bool, {a, b}, e.token, static_cast<std::uint32_t>(e.op)));
            case TokenType::OP_PLUS: case TokenType::OP_MINUS: case TokenType::OP_MULTIPLY:
            case TokenType::OP_DIVIDE: case TokenType::OP_MODULO:
                if (t == Tipo::Bool) naoSuportado("conta com bool (erro interno)", e.token);
                return Operando::temp(temp(Op::Arit, t, {a, b}, e.token, static_cast<std::uint32_t>(e.op)));
            default:
                naoSuportado("este operador", e.token);
        }
    }

    Operando chamada(const CallExpr& c) {
        if (c.type_of) naoSuportado("type()", c.token);
        if (c.interpolated) naoSuportado("printf e format", c.token);
        if (!c.native && !c.target) naoSuportado("criação de erro", c.token);
        if (c.implicit_method) naoSuportado("chamada de método", c.token);

        if (c.target) {   // função da Cinza: argumentos na ordem, chamada na posição da chamada
            auto it = funcao_de.find(c.target);
            if (it == funcao_de.end()) naoSuportado("chamada a '" + c.function_name + "'", c.token);
            const std::uint32_t fi = it->second;
            std::vector<Operando> args;
            for (std::size_t i = 0; i < c.arguments.size(); ++i)
                args.push_back(valorDe(*c.arguments[i], m.funcoes[fi].params[i]));
            const Tipo ret = m.funcoes[fi].ret;
            Instr& in = poe(Op::Chama, c.token);
            in.aux = fi;
            in.args = std::move(args);
            if (ret == Tipo::Void) return Operando::boolean(false);   // sem valor (ExprStmt)
            in.dst = f->novoTemp(ret);
            return Operando::temp(in.dst);
        }

        // nativa: cada argumento embrulhado (sem posse), o resultado com posse
        const NativeFn* fn = c.native;
        if (fn == findPrelude("range")) naoSuportado("range fora do for", c.token);
        std::vector<Operando> args;
        for (const auto& a : c.arguments) {
            Operando o;
            if (a->node_kind == NodeKind::Literal && std::holds_alternative<std::string>(static_cast<const LiteralExpr&>(*a).value))
                o = Operando::literal(m.literal(std::get<std::string>(static_cast<const LiteralExpr&>(*a).value)));
            else
                o = expr(*a);
            args.push_back(Operando::temp(temp(Op::Embrulha, Tipo::Valor, {o}, a->token)));
        }
        const Tipo ret = c.resolved_type ? tipoDe(c.resolved_type, c.token) : Tipo::Void;
        std::uint32_t r = temp(Op::ChamaNativa, Tipo::Valor, std::move(args), c.token,
                               m.nativa(moduloDaNativa(fn, c.token), fn->name));
        // nativa genérica (retorno "número"): o resultado na largura do tipo estático
        if (fn->ret->is(TK::TypeVar) && (ehInteiro(ret) || ehFlutuante(ret))) {
            Instr& cv = poe(Op::ConverteValor, c.token);
            cv.dst  = f->novoTemp(Tipo::Valor);
            cv.args = {Operando::temp(r)};
            cv.tipo = ret;
            r = cv.dst;
        }
        if (ret == Tipo::Void) return Operando::boolean(false);   // void: nada a soltar
        return Operando::temp(temp(Op::Desembrulha, ret, {Operando::temp(r)}, c.token));
    }

    // ── instruções ───────────────────────────────────────────────────────

    void instrucao(const Stmt& s) {
        switch (s.node_kind) {
            case NodeKind::Block:
                for (const auto& x : static_cast<const BlockStmt&>(s).statements)
                    if (x) instrucao(*x);
                break;
            case NodeKind::VarDecl: {
                const auto& v = static_cast<const VarDeclStmt&>(s);
                if (v.res.kind != Resolution::Kind::Local) naoSuportado("variável não local (erro interno)", s.token);
                if (!v.initializer) naoSuportado("list e dict", s.token);
                const Tipo t = v.type->kind == Type::Kind::VAR
                                   ? tipoDe(v.initializer->resolved_type, s.token)
                                   : tipoDe(TypeChecker::fromAst(v.type.get()), s.token);
                const Operando val = valorDe(*v.initializer, t);
                Instr& g = poe(Op::Grava, s.token);
                g.aux = slotVar(v.res.slot, t);
                g.args = {val};
                break;
            }
            case NodeKind::Assign: atribuicao(static_cast<const AssignStmt&>(s)); break;
            case NodeKind::ExprStmt: expr(*static_cast<const ExprStmt&>(s).expression); break;
            case NodeKind::If: {
                const auto& i = static_cast<const IfStmt&>(s);
                const Operando c = valorDe(*i.condition, Tipo::Bool);
                const std::uint32_t entao = f->novoBloco("entao");
                const std::uint32_t senao = i.else_branch ? f->novoBloco("senao") : 0;
                const std::uint32_t fim = f->novoBloco("fimse");
                brc(c, entao, i.else_branch ? senao : fim, s.token);
                entra(entao);
                instrucao(*i.then_branch);
                br(fim, s.token);
                if (i.else_branch) {
                    entra(senao);
                    instrucao(*i.else_branch);
                    br(fim, s.token);
                }
                entra(fim);
                break;
            }
            case NodeKind::While: {
                const auto& w = static_cast<const WhileStmt&>(s);
                const std::uint32_t cabeca = f->novoBloco("enquanto");
                const std::uint32_t corpo_ = f->novoBloco("corpo");
                const std::uint32_t fim = f->novoBloco("fimenquanto");
                br(cabeca, s.token);
                entra(cabeca);
                if (literalVerdadeiro(*w.condition)) {   // while (true): sem teste
                    br(corpo_, s.token);
                } else {
                    const Operando c = valorDe(*w.condition, Tipo::Bool);
                    brc(c, corpo_, fim, s.token);
                }
                entra(corpo_);
                lacos.push_back({cabeca, fim});
                instrucao(*w.body);
                lacos.pop_back();
                br(cabeca, s.token);
                entra(fim);
                break;
            }
            case NodeKind::For: paraRange(static_cast<const ForStmt&>(s)); break;
            case NodeKind::Return: {
                const auto& r = static_cast<const ReturnStmt&>(s);
                if (r.value) {
                    const Operando v = valorDe(*r.value, f->ret);
                    poe(Op::Ret, s.token).args = {v};
                } else {
                    poe(Op::Ret, s.token);
                }
                break;
            }
            case NodeKind::Break:
                if (lacos.empty()) naoSuportado("break fora de laço (erro interno)", s.token);
                poe(Op::Br, s.token).alvo = lacos.back().sai;
                break;
            case NodeKind::Continue:
                if (lacos.empty()) naoSuportado("continue fora de laço (erro interno)", s.token);
                poe(Op::Br, s.token).alvo = lacos.back().continua;
                break;
            case NodeKind::Try:   naoSuportado("try/except", s.token);
            case NodeKind::Throw: naoSuportado("throw", s.token);
            default:              naoSuportado("esta instrução", s.token);
        }
    }

    void atribuicao(const AssignStmt& s) {
        if (s.target->node_kind != NodeKind::Identifier) naoSuportado("atribuição a campo ou índice", s.token);
        const auto& id = static_cast<const IdentifierExpr&>(*s.target);
        if (id.res.kind != Resolution::Kind::Local) naoSuportado("atribuição a campo", s.token);
        const Tipo t = tipoDe(id.resolved_type, s.token);
        const std::uint32_t slot = slotVar(id.res.slot, t);
        if (s.op == TokenType::OP_ASSIGN) {
            const Operando v = valorDe(*s.value, t);
            Instr& g = poe(Op::Grava, s.token);
            g.aux = slot;
            g.args = {v};
            return;
        }
        // a op= b: o valor primeiro, depois o valor atual do alvo (spec 5.2); a conta na
        // posição da atribuição, como na CVM
        const Operando v = valorDe(*s.value, t);
        Instr& c = poe(Op::Carrega, s.token);
        c.dst = f->novoTemp(t);
        c.aux = slot;
        const Operando atual_ = Operando::temp(c.dst);
        const std::uint32_t r = temp(Op::Arit, t, {atual_, v}, s.token,
                                     static_cast<std::uint32_t>(compoundBaseOp(s.op)));
        Instr& g = poe(Op::Grava, s.token);
        g.aux = slot;
        g.args = {Operando::temp(r)};
    }

    // for (T i in range(ini, fim[, passo])): conta direto, como o FORRANGE da CVM — o
    // contador em i64; o iterador na largura de `fim` (já alargado pelo widen_to)
    void paraRange(const ForStmt& s) {
        if (s.iterable->node_kind != NodeKind::Call ||
            static_cast<const CallExpr&>(*s.iterable).native != findPrelude("range"))
            naoSuportado("for fora de range (list, dict, string)", s.token);
        const auto& rc = static_cast<const CallExpr&>(*s.iterable);
        Operando ini = expr(*rc.arguments[0]);
        Operando fim = expr(*rc.arguments[1]);
        Operando passo = rc.arguments.size() == 3 ? expr(*rc.arguments[2]) : Operando::i64(1);
        if (s.widen_to) {
            const Tipo w = tipoDe(s.widen_to, s.token);
            if (tipoOperando(ini) != w) ini = Operando::temp(temp(Op::Converte, w, {ini}, s.token));
            if (tipoOperando(fim) != w) fim = Operando::temp(temp(Op::Converte, w, {fim}, s.token));
        }
        const Tipo t = tipoOperando(fim);   // a largura do iterador
        if (!ehInteiro(t) || !ehInteiro(tipoOperando(ini)) || !ehInteiro(tipoOperando(passo)))
            naoSuportado("range sem inteiros (erro interno)", s.token);
        auto em64 = [&](Operando o) {
            if (tipoOperando(o) == Tipo::I64) return o;
            if (o.k == Operando::K::I32) return Operando::i64(o.i);
            return Operando::temp(temp(Op::Converte, Tipo::I64, {o}, s.token));
        };
        ini = em64(ini);
        fim = em64(fim);
        passo = em64(passo);
        // passo 0: o erro na posição da chamada, como a nativa
        poe(Op::ConferePasso, rc.token).args = {passo};

        const std::uint32_t ctr = f->novoSlot(Tipo::I64);
        Instr& g = poe(Op::Grava, s.token);
        g.aux = ctr;
        g.args = {ini};
        const std::uint32_t iter = slotVar(s.iter_slot, t);

        const std::uint32_t cabeca = f->novoBloco("para");
        const std::uint32_t corpo_ = f->novoBloco("corpo");
        const std::uint32_t fimb = f->novoBloco("fimpara");
        br(cabeca, s.token);
        entra(cabeca);
        Instr& c = poe(Op::Carrega, s.token);
        c.dst = f->novoTemp(Tipo::I64);
        c.aux = ctr;
        const Operando i = Operando::temp(c.dst);
        // passo > 0 ? i >= fim : i <= fim → acabou. Passo constante: o teste sai direto
        auto testa = [&](bool positivo) {
            const std::uint32_t acabou =
                temp(Op::Compara, Tipo::Bool, {i, fim}, s.token,
                     static_cast<std::uint32_t>(positivo ? TokenType::OP_GREATER_EQUAL : TokenType::OP_LESS_EQUAL));
            brc(Operando::temp(acabou), fimb, corpo_, s.token);
        };
        if (passo.k == Operando::K::I64) {
            testa(passo.i > 0);
        } else {
            const std::uint32_t pos = f->novoBloco("positivo"), neg = f->novoBloco("negativo");
            const std::uint32_t sinal = temp(Op::Compara, Tipo::Bool, {passo, Operando::i64(0)}, s.token,
                                             static_cast<std::uint32_t>(TokenType::OP_GREATER));
            brc(Operando::temp(sinal), pos, neg, s.token);
            entra(pos);
            testa(true);
            entra(neg);
            testa(false);
        }
        entra(corpo_);
        // o iterador recebe o contador; o próximo é calculado antes do corpo
        Operando valor = i;
        if (t == Tipo::I32) valor = Operando::temp(temp(Op::Converte, Tipo::I32, {i}, s.token));
        Instr& gi = poe(Op::Grava, s.token);
        gi.aux = iter;
        gi.args = {valor};
        Instr& p = poe(Op::PassoRange, s.token);
        p.dst = f->novoTemp(Tipo::I64);
        p.args = {i, passo, fim};
        p.tipo = t;
        Instr& gc = poe(Op::Grava, s.token);
        gc.aux = ctr;
        gc.args = {Operando::temp(p.dst)};
        lacos.push_back({cabeca, fimb});
        instrucao(*s.body);
        lacos.pop_back();
        br(cabeca, s.token);
        entra(fimb);
    }
};

}  // namespace

Modulo gera(const std::vector<Module>& modulos, const GlobalLayout& layout,
            const std::vector<std::string>& arquivos) {
    Modulo m;
    m.arquivos = arquivos;
    for (std::size_t i = 0; i + 1 < modulos.size(); ++i)
        if (!modulos[i].native) {
            const auto& imps = modulos.back().program->imports;
            naoSuportado("import de módulo da Cinza", imps.empty() ? Token() : imps.front().token);
        }
    const Program& prog = *modulos.back().program;
    Gerador g(m, layout);

    std::vector<const FunctionDecl*> funcoes;
    std::vector<const VarDeclStmt*> consts;
    for (const auto& s : prog.statements) {
        switch (s->node_kind) {
            case NodeKind::FunctionDecl: {
                const auto* fn = static_cast<const FunctionDecl*>(s.get());
                if (fn->type_params.empty()) funcoes.push_back(fn);   // N2: só as cópias
                break;
            }
            case NodeKind::VarDecl: consts.push_back(static_cast<const VarDeclStmt*>(s.get())); break;
            case NodeKind::ErrorDecl: break;   // só um nome de tipo: nada a gerar
            default: naoSuportado(nomeDeclaracao(s->node_kind), s->token);
        }
    }
    for (const auto& s : prog.instancias) {   // N2: cópias das genéricas
        if (s->node_kind != NodeKind::FunctionDecl) naoSuportado("structs genéricos", s->token);
        funcoes.push_back(static_cast<const FunctionDecl*>(s.get()));
    }

    for (const auto* fn : funcoes) g.declara(*fn);
    for (const auto* v : consts) g.declaraGlobal(*v);
    if (!consts.empty()) g.inicializacao(consts);
    for (const auto* fn : funcoes) g.corpo(*fn);
    return m;
}

}  // namespace cinza::cir
