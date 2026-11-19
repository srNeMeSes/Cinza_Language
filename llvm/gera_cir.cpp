// AST analisada → CIR. P4: números, bool, funções, if/while/for range, print (e as
// nativas de valores simples), const globais. P5: strings e a posse. O resto é
// NaoSuportado, com a posição: as etapas P6 a P9 (REVISAO_V2.md, seção P) vão ampliando.
//
// A ordem de avaliação e as posições dos erros são as do compilador da CVM
// (cvm/compiler.cpp): mesma posição de token em cada operação que pode falhar.
//
// Posse (llvm/DESENHO.md 4.3). Todo slot de tipo com referência é dono do valor e começa
// vazio; gravar nele solta o antigo depois (store.own). Um valor de expressão é
// emprestado (lido de um slot, um literal) ou tem posse (resultado do runtime, de uma
// nativa ou de uma função da Cinza). Quem só lê um valor com posse o põe num slot
// temporário da instrução, solto no fim dela; quem guarda (variável, return, const) fica
// com a posse — e faz retem se o valor era emprestado. Parâmetros chegam emprestados e a
// função os retém no próprio slot (assim reatribuir um parâmetro é como reatribuir uma
// local). Toda saída da função passa por um bloco só, que solta os slots.
#include "gera_cir.h"
#include "../natives.h"
#include "../operacoes.h"
#include "../semantic.h"
#include <map>
#include <set>
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

// O tipo da CIR de um tipo da linguagem (P5: escalares e string)
Tipo tipoDe(TypeRef t, const Token& tok) {
    if (!t) naoSuportado("expressão sem tipo (erro interno)", tok);
    switch (t->kind) {
        case TK::Void:   return Tipo::Void;
        case TK::Bool:   return Tipo::Bool;
        case TK::I32:    return Tipo::I32;
        case TK::I64:    return Tipo::I64;
        case TK::F32:    return Tipo::F32;
        case TK::F64:    return Tipo::F64;
        case TK::String: return Tipo::Str;
        default:         naoSuportado("o tipo '" + t->str() + "'", tok);
    }
}

bool ehRef(Tipo t) { return t == Tipo::Str; }
bool ehNumero(Tipo t) { return ehInteiro(t) || ehFlutuante(t); }

std::uint8_t kindDe(Tipo t) {
    switch (t) {
        case Tipo::I32: return static_cast<std::uint8_t>(Value::Kind::I32);
        case Tipo::I64: return static_cast<std::uint8_t>(Value::Kind::I64);
        case Tipo::F32: return static_cast<std::uint8_t>(Value::Kind::F32);
        default:        return static_cast<std::uint8_t>(Value::Kind::F64);
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

// O valor de uma expressão: o operando e se ele tem posse (só importa em str)
struct Val {
    Operando o;
    bool     dono = false;
};

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
        const Token& tok = fn.token;
        // os parâmetros chegam emprestados; a função fica dona deles no próprio slot
        for (std::size_t i = 0; i < fn.parameters.size(); ++i) {
            const std::uint32_t s = f->novoSlot(f->params[i]);
            slot_de[{static_cast<std::uint32_t>(i), f->params[i]}] = s;
        }
        for (std::size_t i = 0; i < fn.parameters.size(); ++i)
            if (ehRef(f->params[i])) retem(carrega(static_cast<std::uint32_t>(i), tok), tok);
        saida = f->novoBloco("saida");
        ret_slot = f->ret != Tipo::Void ? f->novoSlot(f->ret) : UINT32_MAX;
        instrucao(*fn.body);
        if (!terminado) {
            // o semântico garante o return nas funções com valor
            if (f->ret == Tipo::Void) br(saida, fn.body->token);
            else poe(Op::Inalcancavel, fn.body->token);
        }
        // a saída: solta os slots donos (os temporários já estão vazios aqui)
        entra(saida);
        for (std::uint32_t s = 0; s < f->slots.size(); ++s)
            if (ehRef(f->slots[s]) && s != ret_slot && !slots_temp.count(s)) limpa(s, tok);
        if (ret_slot != UINT32_MAX) poe(Op::Ret, tok).args = {carrega(ret_slot, tok)};
        else poe(Op::Ret, tok);
    }

    // A função oculta que inicializa os const, na ordem em que aparecem
    void inicializacao(const std::vector<const VarDeclStmt*>& consts) {
        m.init = oculta("cz.init", consts.front()->token);
        for (const VarDeclStmt* v : consts) {
            const std::uint32_t g = global_de.at(v->res.slot);
            const Operando val = posseTipo(*v->initializer, m.globais[g]);
            fimInstrucao(v->token);
            Instr& in = poe(Op::GravaG, v->token);   // depois do valor: poe invalida referências
            in.aux = g;
            in.args = {val};
        }
        poe(Op::Ret, consts.front()->token);
    }

    // A função oculta que solta os const com referência, depois da main
    void finalizacao(const Token& tok) {
        bool tem = false;
        for (Tipo t : m.globais) tem = tem || ehRef(t);
        if (!tem) return;
        m.fim = oculta("cz.fim", tok);
        for (std::uint32_t g = 0; g < m.globais.size(); ++g) {
            if (!ehRef(m.globais[g])) continue;
            Instr& c = poe(Op::CarregaG, tok);
            c.dst = f->novoTemp(m.globais[g]);
            c.aux = g;
            const Operando v = Operando::temp(c.dst);
            Instr& gv = poe(Op::GravaG, tok);
            gv.aux = g;
            gv.args = {Operando::nulo()};
            solta(v, tok);
        }
        poe(Op::Ret, tok);
    }

private:
    Modulo& m;
    Funcao* f = nullptr;
    std::uint32_t atual = 0;      // o bloco em que se escreve
    bool terminado = false;       // o bloco atual já terminou (br, ret...)
    std::uint32_t saida = 0;      // o bloco de saída da função
    std::uint32_t ret_slot = UINT32_MAX;
    std::unordered_map<const FunctionDecl*, std::uint32_t> funcao_de;
    std::unordered_map<std::uint32_t, std::uint32_t>       global_de;        // slot do B2 → global
    std::unordered_map<std::uint32_t, const NativeConst*>  consts_nativos;   // Math.pi...
    std::map<std::pair<std::uint32_t, Tipo>, std::uint32_t> slot_de;         // (slot do B2, tipo) → slot
    struct Laco { std::uint32_t continua, sai; };
    std::vector<Laco> lacos;
    // os temporários donos: os da instrução em andamento e os livres (vazios)
    std::vector<std::uint32_t> temps_uso, temps_livres;
    std::set<std::uint32_t>    slots_temp;

    void comeca(std::uint32_t fi) {
        f = &m.funcoes[fi];
        slot_de.clear();
        lacos.clear();
        temps_uso.clear();
        temps_livres.clear();
        slots_temp.clear();
        ret_slot = UINT32_MAX;
        atual = f->novoBloco("b");
        terminado = false;
    }

    int oculta(const char* nome, const Token& tok) {
        Funcao fo;
        fo.nome   = nome;
        fo.oculta = true;
        fo.decl   = tok;
        const int i = static_cast<int>(m.funcoes.size());
        m.funcoes.push_back(std::move(fo));
        comeca(static_cast<std::uint32_t>(i));
        return i;
    }

    // ── emissão ──────────────────────────────────────────────────────────
    static bool terminador(Op op) {
        return op == Op::Br || op == Op::BrC || op == Op::Ret || op == Op::Inalcancavel;
    }
    // A referência devolvida vale até a próxima instrução emitida
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
            case Operando::K::Nulo:    return Tipo::Str;
            case Operando::K::Vazio:   return Tipo::Valor;
        }
        return Tipo::Void;
    }

    // ── slots e posse ────────────────────────────────────────────────────
    Operando carrega(std::uint32_t slot, const Token& tok) {
        Instr& c = poe(Op::Carrega, tok);
        c.dst = f->novoTemp(f->slots[slot]);
        c.aux = slot;
        return Operando::temp(c.dst);
    }
    void grava(std::uint32_t slot, const Operando& v, const Token& tok) {
        Instr& g = poe(Op::Grava, tok);
        g.aux = slot;
        g.args = {v};
    }
    void retem(const Operando& v, const Token& tok) { poe(Op::Retem, tok).args = {v}; }
    void solta(const Operando& v, const Token& tok) { poe(Op::Solta, tok).args = {v}; }

    // store.own: o slot fica dono de `v` (que já tem posse); o valor antigo é solto depois
    void gravaDono(std::uint32_t slot, const Operando& v, const Token& tok) {
        if (!ehRef(f->slots[slot])) { grava(slot, v, tok); return; }
        const Operando velho = carrega(slot, tok);
        grava(slot, v, tok);
        solta(velho, tok);
    }
    // Solta o que o slot segura e o deixa vazio
    void limpa(std::uint32_t slot, const Token& tok) {
        const Operando v = carrega(slot, tok);
        grava(slot, Operando::nulo(), tok);
        solta(v, tok);
    }

    // Um valor com posse que só vai ser lido: fica num temporário da instrução
    Operando emprestado(const Val& v, const Token& tok) {
        if (v.dono && ehRef(tipoOperando(v.o))) {
            std::uint32_t s;
            if (!temps_livres.empty()) { s = temps_livres.back(); temps_livres.pop_back(); }
            else { s = f->novoSlot(Tipo::Str); slots_temp.insert(s); }
            temps_uso.push_back(s);
            grava(s, v.o, tok);   // o temporário está vazio: nada a soltar
        }
        return v.o;
    }
    // Um valor que vai ser guardado: com posse (retem, se era emprestado)
    Operando comPosse(const Val& v, const Token& tok) {
        if (ehRef(tipoOperando(v.o)) && !v.dono) retem(v.o, tok);
        return v.o;
    }
    // Fim de uma instrução da Cinza: solta os temporários dela
    void fimInstrucao(const Token& tok) {
        for (std::uint32_t s : temps_uso) {
            limpa(s, tok);
            temps_livres.push_back(s);
        }
        temps_uso.clear();
    }

    // O slot de uma variável local (o semântico pode reusar um slot do B2 em escopos
    // diferentes, com outro tipo: a chave leva o tipo)
    std::uint32_t slotVar(std::uint32_t b2, Tipo t) {
        auto [it, novo] = slot_de.try_emplace({b2, t}, 0);
        if (novo) it->second = f->novoSlot(t);
        return it->second;
    }

    // ── chamadas ao runtime ──────────────────────────────────────────────
    Operando chamaRt(Rt r, std::vector<Operando> args, const Token& pos) {
        const InfoRt& info = infoRt(r);
        Instr& in = poe(Op::ChamaRt, pos);
        in.aux  = static_cast<std::uint32_t>(r);
        in.args = std::move(args);
        if (info.ret == Tipo::Void) return Operando::boolean(false);
        in.dst = f->novoTemp(info.ret);
        return Operando::temp(in.dst);
    }
    Operando embrulha(const Operando& o, const Token& tok) {
        if (tipoOperando(o) == Tipo::Valor) return o;
        return Operando::temp(temp(Op::Embrulha, Tipo::Valor, {o}, tok));
    }
    // Um resultado `valor` com posse do runtime, no tipo estático
    Val desembrulha(const Operando& v, Tipo t, const Token& tok) {
        if (t == Tipo::Void) return {Operando::boolean(false), false};
        return {Operando::temp(temp(Op::Desembrulha, t, {v}, tok)), ehRef(t)};
    }

    // ── expressões ───────────────────────────────────────────────────────

    void confereTipo(const Operando& o, Tipo esperado, const Token& tok) {
        if (tipoOperando(o) != esperado)
            naoSuportado(std::string("tipos diferentes na CIR (") + nomeTipo(tipoOperando(o)) + " e " +
                         nomeTipo(esperado) + ", erro interno)", tok);
    }
    // O valor de `e` só para leitura (emprestado)
    Operando expr(const Expr& e) { return emprestado(valor(e), e.token); }
    // ... que precisa ter o tipo `esperado` (o semântico já inseriu as conversões)
    Operando exprTipo(const Expr& e, Tipo esperado) {
        const Operando o = expr(e);
        confereTipo(o, esperado, e.token);
        return o;
    }
    // ... com posse, para guardar
    Operando posseTipo(const Expr& e, Tipo esperado) {
        const Val v = valor(e);
        confereTipo(v.o, esperado, e.token);
        return comPosse(v, e.token);
    }

    Val valor(const Expr& e) {
        switch (e.node_kind) {
            case NodeKind::Literal:    return literal(static_cast<const LiteralExpr&>(e));
            case NodeKind::Identifier: return {identificador(static_cast<const IdentifierExpr&>(e)), false};
            case NodeKind::Binary:     return binaria(static_cast<const BinaryExpr&>(e));
            case NodeKind::Unary: {
                const auto& u = static_cast<const UnaryExpr&>(e);
                const Operando a = expr(*u.operand);
                const Tipo t = tipoOperando(a);
                if (u.op == TokenType::OP_NOT) return {Operando::temp(temp(Op::Nao, Tipo::Bool, {a}, e.token))};
                if (!ehNumero(t)) naoSuportado("negação de um valor que não é número (erro interno)", e.token);
                return {Operando::temp(temp(Op::Neg, t, {a}, e.token))};
            }
            case NodeKind::Cast:       return conversao(static_cast<const CastExpr&>(e));
            case NodeKind::Call:       return chamada(static_cast<const CallExpr&>(e));
            case NodeKind::IndexAccess: {
                const auto& ix = static_cast<const IndexAccessExpr&>(e);
                if (ix.eh_tipo) naoSuportado("tipo usado como valor", e.token);
                if (!ix.object->resolved_type || !ix.object->resolved_type->is(TK::String))
                    naoSuportado("acesso por índice em list e dict", e.token);
                const Operando o = expr(*ix.object);
                const Operando k = expr(*ix.index);
                const Operando r = chamaRt(Rt::Indice, {embrulha(o, e.token), embrulha(k, e.token)}, e.token);
                return desembrulha(r, Tipo::Str, e.token);
            }
            case NodeKind::Slice: {   // na ordem do fonte (spec 5.2); omitida = void
                const auto& sl = static_cast<const SliceExpr&>(e);
                std::vector<Operando> args{embrulha(exprTipo(*sl.object, Tipo::Str), e.token)};
                for (const Expr* p : {sl.start.get(), sl.end.get(), sl.step.get()})
                    args.push_back(p ? embrulha(expr(*p), e.token) : Operando::vazio());
                return desembrulha(chamaRt(Rt::Fatia, std::move(args), e.token), Tipo::Str, e.token);
            }
            case NodeKind::MethodCall: return metodo(static_cast<const MethodCallExpr&>(e));
            case NodeKind::ListLiteral: case NodeKind::DictLiteral: case NodeKind::PairLiteral:
                naoSuportado("list, dict e pair", e.token);
            case NodeKind::MemberAccess: naoSuportado("acesso a membro", e.token);
            case NodeKind::New:          naoSuportado("new", e.token);
            case NodeKind::TypeLiteral:  naoSuportado("tipo usado como valor", e.token);
            default:                     naoSuportado("esta expressão", e.token);
        }
    }

    Val literal(const LiteralExpr& lit) {
        if (const auto* b = std::get_if<bool>(&lit.value)) return {Operando::boolean(*b)};
        if (const auto* i = std::get_if<std::int64_t>(&lit.value)) {
            // como o LOADI32 da CVM: o literal i32 nos 32 bits
            if (tipoDe(lit.resolved_type, lit.token) == Tipo::I32)
                return {Operando::i32(static_cast<std::int32_t>(*i))};
            return {Operando::i64(*i)};
        }
        if (const auto* d = std::get_if<double>(&lit.value))
            return {tipoDe(lit.resolved_type, lit.token) == Tipo::F32 ? Operando::f32(*d) : Operando::f64(*d)};
        return {Operando::literal(m.literal(std::get<std::string>(lit.value)))};   // imortal: emprestado
    }

    Operando identificador(const IdentifierExpr& id) {
        switch (id.res.kind) {
            case Resolution::Kind::Local: {
                const Tipo t = tipoDe(id.resolved_type, id.token);
                auto it = slot_de.find({id.res.slot, t});
                if (it == slot_de.end()) naoSuportado("variável sem slot (erro interno)", id.token);
                return carrega(it->second, id.token);
            }
            case Resolution::Kind::Global: {
                if (auto c = consts_nativos.find(id.res.slot); c != consts_nativos.end()) {
                    const Value& v = c->second->value;   // Math.pi: o valor já pronto
                    switch (v.kind()) {
                        case Value::Kind::I32:    return Operando::i32(v.asI32());
                        case Value::Kind::I64:    return Operando::i64(v.asInt());
                        case Value::Kind::F32:    return Operando::f32(v.asF32());
                        case Value::Kind::F64:    return Operando::f64(v.asFloat());
                        case Value::Kind::BOOL:   return Operando::boolean(v.asBool());
                        case Value::Kind::STRING: return Operando::literal(m.literal(v.asString()));
                        default: naoSuportado("esta constante da biblioteca", id.token);
                    }
                }
                auto g = global_de.find(id.res.slot);
                if (g == global_de.end()) naoSuportado("const de outro módulo", id.token);
                Instr& in = poe(Op::CarregaG, id.token);   // emprestado: o global vive até o fim
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

    // a + b com string de um dos lados: o outro em texto (o CONCAT da CVM)
    Val concatena(const Operando& a, const Operando& b, const Token& tok) {
        return {chamaRt(Rt::Concat, {embrulha(a, tok), embrulha(b, tok)}, tok), true};
    }

    Val binaria(const BinaryExpr& e) {
        if (e.op == TokenType::OP_AND || e.op == TokenType::OP_OR) {
            // curto-circuito: o resultado passa por um slot (atravessa blocos)
            const std::uint32_t s = f->novoSlot(Tipo::Bool);
            const Operando a = exprTipo(*e.left, Tipo::Bool);
            grava(s, a, e.token);
            const std::uint32_t direita = f->novoBloco("direita"), fim = f->novoBloco("fim");
            if (e.op == TokenType::OP_AND) brc(a, direita, fim, e.token);
            else                           brc(a, fim, direita, e.token);
            entra(direita);
            grava(s, exprTipo(*e.right, Tipo::Bool), e.token);
            br(fim, e.token);
            entra(fim);
            return {carrega(s, e.token)};
        }
        // operandos na ordem do fonte
        const Operando a = expr(*e.left);
        const Tipo t = tipoOperando(a);
        const bool texto = e.left->resolved_type->is(TK::String) || e.right->resolved_type->is(TK::String);
        if (e.op == TokenType::OP_PLUS && texto) return concatena(a, expr(*e.right), e.token);
        const Operando b = exprTipo(*e.right, t);   // o semântico já deu aos dois o mesmo tipo
        const auto op = static_cast<std::uint32_t>(e.op);
        if (t == Tipo::Str) {
            switch (e.op) {
                case TokenType::OP_EQUAL:
                    return {chamaRt(Rt::StrIgual, {a, b}, e.token)};
                case TokenType::OP_NOT_EQUAL:
                    return {Operando::temp(temp(Op::Nao, Tipo::Bool, {chamaRt(Rt::StrIgual, {a, b}, e.token)}, e.token))};
                case TokenType::OP_LESS: case TokenType::OP_LESS_EQUAL:
                case TokenType::OP_GREATER: case TokenType::OP_GREATER_EQUAL: {
                    const Operando c = chamaRt(Rt::StrCompara, {a, b}, e.token);
                    return {Operando::temp(temp(Op::Compara, Tipo::Bool, {c, Operando::i32(0)}, e.token, op))};
                }
                default: naoSuportado("este operador em string", e.token);
            }
        }
        switch (e.op) {
            case TokenType::OP_EQUAL: case TokenType::OP_NOT_EQUAL:
                return {Operando::temp(temp(Op::Compara, Tipo::Bool, {a, b}, e.token, op))};
            case TokenType::OP_LESS: case TokenType::OP_LESS_EQUAL:
            case TokenType::OP_GREATER: case TokenType::OP_GREATER_EQUAL:
                if (!ehNumero(t)) naoSuportado("comparação de bool (erro interno)", e.token);
                return {Operando::temp(temp(Op::Compara, Tipo::Bool, {a, b}, e.token, op))};
            case TokenType::OP_PLUS: case TokenType::OP_MINUS: case TokenType::OP_MULTIPLY:
            case TokenType::OP_DIVIDE: case TokenType::OP_MODULO:
                if (!ehNumero(t)) naoSuportado("conta com bool (erro interno)", e.token);
                return {Operando::temp(temp(Op::Arit, t, {a, b}, e.token, op))};
            default:
                naoSuportado("este operador", e.token);
        }
    }

    Val conversao(const CastExpr& c) {
        const Tipo para = tipoDe(c.resolved_type, c.token);
        const Val v = valor(*c.operand);
        const Tipo de = tipoOperando(v.o);
        if (de == para) return v;   // cast<i64>(n) com n i64: o mesmo valor (e a mesma posse)
        const Operando a = emprestado(v, c.token);
        if (para == Tipo::Str)      // cast<string>(x)
            return {chamaRt(Rt::Texto, {embrulha(a, c.token)}, c.token), true};
        if (de == Tipo::Str && ehNumero(para)) {   // cast<T>(texto): ValueError/OverflowError
            const Operando r = chamaRt(Rt::DeTexto, {a, Operando::i32(kindDe(para))}, c.token);
            return desembrulha(r, para, c.token);
        }
        if (!ehNumero(para) || !ehNumero(de)) naoSuportado("esta conversão", c.token);
        return {Operando::temp(temp(Op::Converte, para, {a}, c.token))};
    }

    Val chamada(const CallExpr& c) {
        if (c.type_of) naoSuportado("type()", c.token);
        if (c.interpolated) return interpolacao(c);
        if (!c.native && !c.target) naoSuportado("criação de erro", c.token);
        if (c.implicit_method) naoSuportado("chamada de método", c.token);

        if (c.target) {   // função da Cinza: argumentos emprestados, na ordem; resultado com posse
            auto it = funcao_de.find(c.target);
            if (it == funcao_de.end()) naoSuportado("chamada a '" + c.function_name + "'", c.token);
            const std::uint32_t fi = it->second;
            std::vector<Operando> args;
            for (std::size_t i = 0; i < c.arguments.size(); ++i)
                args.push_back(exprTipo(*c.arguments[i], m.funcoes[fi].params[i]));
            const Tipo ret = m.funcoes[fi].ret;
            Instr& in = poe(Op::Chama, c.token);
            in.aux = fi;
            in.args = std::move(args);
            if (ret == Tipo::Void) return {Operando::boolean(false)};   // sem valor (ExprStmt)
            in.dst = f->novoTemp(ret);
            return {Operando::temp(in.dst), ehRef(ret)};
        }

        // nativa: cada argumento embrulhado (emprestado), o resultado com posse
        const NativeFn* fn = c.native;
        if (fn == findPrelude("range")) naoSuportado("range fora do for", c.token);
        std::vector<Operando> args;
        for (const auto& a : c.arguments) args.push_back(embrulha(expr(*a), a->token));
        const Tipo ret = c.resolved_type ? tipoDe(c.resolved_type, c.token) : Tipo::Void;
        std::uint32_t r = temp(Op::ChamaNativa, Tipo::Valor, std::move(args), c.token,
                               m.nativa(moduloDaNativa(fn, c.token), fn->name));
        // nativa genérica (retorno "número"): o resultado na largura do tipo estático
        if (fn->ret->is(TK::TypeVar) && ehNumero(ret)) {
            Instr& cv = poe(Op::ConverteValor, c.token);
            cv.dst  = f->novoTemp(Tipo::Valor);
            cv.args = {Operando::temp(r)};
            cv.tipo = ret;
            r = cv.dst;
        }
        return desembrulha(Operando::temp(r), ret, c.token);
    }

    // printf/format: o texto montado como na CVM — trecho fixo, ou {expr} formatado
    // (formatPart), juntados da esquerda para a direita; o printf o passa ao print
    Val interpolacao(const CallExpr& c) {
        const Token& tok = c.token;
        Val acc{Operando::literal(m.literal(""))};
        for (std::size_t i = 0; i < c.interp.size(); ++i) {
            const auto& parte = c.interp[i];
            Val pedaco;
            if (!parte.expr) {
                pedaco = {Operando::literal(m.literal(parte.text))};
            } else {
                const Operando v = expr(*parte.expr);
                pedaco = {chamaRt(Rt::Formata, {embrulha(v, tok), Operando::i32(parte.width),
                                                 Operando::i32(parte.precision)}, tok), true};
            }
            if (i == 0) { acc = pedaco; continue; }
            const Operando a = emprestado(acc, tok);
            acc = concatena(a, emprestado(pedaco, tok), tok);
        }
        if (!c.native) return acc;   // format: o texto
        const Operando texto = emprestado(acc, tok);
        temp(Op::ChamaNativa, Tipo::Valor, {embrulha(texto, tok)}, tok, m.nativa("", c.native->name));
        return {Operando::boolean(false)};
    }

    // Método embutido de string (spec 5.7): o objeto avaliado antes dos argumentos
    Val metodo(const MethodCallExpr& mc) {
        if (!mc.object->resolved_type || !mc.object->resolved_type->is(TK::String))
            naoSuportado(mc.object->resolved_type && (mc.object->resolved_type->is(TK::List) ||
                                                      mc.object->resolved_type->is(TK::Dict))
                             ? "métodos de list e dict" : "chamada de método", mc.token);
        if (mc.arguments.size() > 2) naoSuportado("método com mais de dois argumentos", mc.token);
        const Builtin b = builtinFor(Value::Kind::STRING, mc.method_name);
        std::vector<Operando> args{Operando::i32(static_cast<std::int32_t>(b)),
                                   embrulha(expr(*mc.object), mc.token)};
        for (const auto& a : mc.arguments) args.push_back(embrulha(expr(*a), mc.token));
        const Rt r = mc.arguments.empty() ? Rt::Metodo0 : mc.arguments.size() == 1 ? Rt::Metodo1 : Rt::Metodo2;
        const Tipo ret = mc.resolved_type ? tipoDe(mc.resolved_type, mc.token) : Tipo::Void;
        return desembrulha(chamaRt(r, std::move(args), mc.token), ret, mc.token);
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
                const Operando val = posseTipo(*v.initializer, t);
                gravaDono(slotVar(v.res.slot, t), val, s.token);
                fimInstrucao(s.token);
                break;
            }
            case NodeKind::Assign: atribuicao(static_cast<const AssignStmt&>(s)); break;
            case NodeKind::ExprStmt:
                emprestado(valor(*static_cast<const ExprStmt&>(s).expression), s.token);
                fimInstrucao(s.token);
                break;
            case NodeKind::If: {
                const auto& i = static_cast<const IfStmt&>(s);
                const Operando c = exprTipo(*i.condition, Tipo::Bool);
                fimInstrucao(s.token);
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
                    const Operando c = exprTipo(*w.condition, Tipo::Bool);
                    fimInstrucao(s.token);
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
            case NodeKind::For: {
                const auto& fs = static_cast<const ForStmt&>(s);
                if (fs.iterable->node_kind == NodeKind::Call &&
                    static_cast<const CallExpr&>(*fs.iterable).native == findPrelude("range"))
                    paraRange(fs);
                else if (fs.iterable->resolved_type && fs.iterable->resolved_type->is(TK::String))
                    paraTexto(fs);
                else
                    naoSuportado("for em list e dict", s.token);
                break;
            }
            case NodeKind::Return: {
                const auto& r = static_cast<const ReturnStmt&>(s);
                if (r.value) {
                    const Operando v = posseTipo(*r.value, f->ret);   // o chamador recebe com posse
                    fimInstrucao(s.token);
                    grava(ret_slot, v, s.token);
                }
                poe(Op::Br, s.token).alvo = saida;
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
            gravaDono(slot, posseTipo(*s.value, t), s.token);
            fimInstrucao(s.token);
            return;
        }
        // a op= b: o valor primeiro, depois o valor atual do alvo (spec 5.2); a conta na
        // posição da atribuição, como na CVM. Em string, += concatena (o valor em texto)
        if (t == Tipo::Str) {
            const Operando v = expr(*s.value);
            const Operando atual_ = carrega(slot, s.token);
            gravaDono(slot, concatena(atual_, v, s.token).o, s.token);
            fimInstrucao(s.token);
            return;
        }
        const Operando v = exprTipo(*s.value, t);
        const Operando atual_ = carrega(slot, s.token);
        const std::uint32_t r = temp(Op::Arit, t, {atual_, v}, s.token,
                                     static_cast<std::uint32_t>(compoundBaseOp(s.op)));
        grava(slot, Operando::temp(r), s.token);
        fimInstrucao(s.token);
    }

    // for (T i in range(ini, fim[, passo])): conta direto, como o FORRANGE da CVM — o
    // contador em i64; o iterador na largura de `fim` (já alargado pelo widen_to)
    void paraRange(const ForStmt& s) {
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
        fimInstrucao(s.token);

        const std::uint32_t ctr = f->novoSlot(Tipo::I64);
        grava(ctr, ini, s.token);
        const std::uint32_t iter = slotVar(s.iter_slot, t);

        const std::uint32_t cabeca = f->novoBloco("para");
        const std::uint32_t corpo_ = f->novoBloco("corpo");
        const std::uint32_t fimb = f->novoBloco("fimpara");
        br(cabeca, s.token);
        entra(cabeca);
        const Operando i = carrega(ctr, s.token);
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
        Operando valor_ = i;
        if (t == Tipo::I32) valor_ = Operando::temp(temp(Op::Converte, Tipo::I32, {i}, s.token));
        grava(iter, valor_, s.token);
        Instr& p = poe(Op::PassoRange, s.token);
        p.dst = f->novoTemp(Tipo::I64);
        p.args = {i, passo, fim};
        p.tipo = t;
        grava(ctr, Operando::temp(p.dst), s.token);
        lacos.push_back({cabeca, fimb});
        instrucao(*s.body);
        lacos.pop_back();
        br(cabeca, s.token);
        entra(fimb);
    }

    // for (string c in texto): um caractere UTF-8 por vez, como o FORNEXT da CVM. A string
    // é imutável: a cópia da spec 5.5 é a referência guardada num slot do laço
    void paraTexto(const ForStmt& s) {
        const std::uint32_t col = f->novoSlot(Tipo::Str), pos = f->novoSlot(Tipo::I64);
        gravaDono(col, posseTipo(*s.iterable, Tipo::Str), s.token);
        fimInstrucao(s.token);
        grava(pos, Operando::i64(0), s.token);
        const std::uint32_t iter = slotVar(s.iter_slot, Tipo::Str);

        const std::uint32_t cabeca = f->novoBloco("paratexto");
        const std::uint32_t corpo_ = f->novoBloco("corpo");
        const std::uint32_t fimb = f->novoBloco("fimparatexto");
        br(cabeca, s.token);
        entra(cabeca);
        const Operando texto = carrega(col, s.token);
        const Operando i = carrega(pos, s.token);
        const Operando n = chamaRt(Rt::StrBytes, {texto}, s.token);
        const std::uint32_t acabou = temp(Op::Compara, Tipo::Bool, {i, n}, s.token,
                                          static_cast<std::uint32_t>(TokenType::OP_GREATER_EQUAL));
        brc(Operando::temp(acabou), fimb, corpo_, s.token);
        entra(corpo_);
        const Operando ch = chamaRt(Rt::StrCaractere, {texto, i}, s.token);   // com posse
        const Operando len = chamaRt(Rt::StrTamCaractere, {texto, i}, s.token);
        grava(pos, Operando::temp(temp(Op::Arit, Tipo::I64, {i, len}, s.token,
                                       static_cast<std::uint32_t>(TokenType::OP_PLUS))), s.token);
        gravaDono(iter, ch, s.token);
        lacos.push_back({cabeca, fimb});
        instrucao(*s.body);
        lacos.pop_back();
        br(cabeca, s.token);
        entra(fimb);
        limpa(col, s.token);   // a referência do laço (o break também chega aqui)
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
    if (!consts.empty()) {
        g.inicializacao(consts);
        g.finalizacao(consts.front()->token);
    }
    for (const auto* fn : funcoes) g.corpo(*fn);
    return m;
}

}  // namespace cinza::cir
