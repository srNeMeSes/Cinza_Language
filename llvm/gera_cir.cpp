// AST analisada → CIR. P4: números, bool, funções, if/while/for range, print (e as
// nativas de valores simples), const globais. P5: strings e a posse. P6: list, dict e
// pair. P7: structs, classes, interfaces, enums e type(). P8: erros — throw, try/except/
// finally, valores de erro, exit. O resto é NaoSuportado, com a posição: a etapa P9
// (REVISAO_V2.md, seção P) amplia.
//
// Erros (llvm/DESENHO.md 5.3): toda operação que pode falhar leva o bloco tratador do ponto
// em que está (Instr::erro). Fora de um try é a saída de erro da função, que solta todos
// os slots donos e volta ao chamador com o erro pendente; o chamador confere depois da
// chamada. Dentro de um try, a captura: solta os temporários da instrução interrompida e
// compara o erro com os except, na ordem.
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

// O tipo da CIR de um tipo da linguagem
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
        case TK::List:   return Tipo::Lista;
        case TK::Dict:   return Tipo::Dict;
        case TK::Pair:   return Tipo::Par;
        case TK::Struct: return Tipo::Struct;
        case TK::Class: case TK::Interface: return Tipo::Obj;
        case TK::Enum:   return Tipo::Enum;
        case TK::Type:   return Tipo::TipoV;
        case TK::Error:  return Tipo::Erro;
        default:         naoSuportado("o tipo '" + t->str() + "'", tok);
    }
}

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

    // Os descritores do runtime (antes das funções: os tipos dos parâmetros os usam)
    void registraStruct(const StructDecl& st) {
        struct_de[st.name] = static_cast<std::uint32_t>(m.structs.size());
        DescStruct d{st.name, {}};
        for (const auto& c : st.fields) d.campos.push_back(c.name);
        m.structs.push_back(std::move(d));
        struct_decl.push_back(&st);
    }
    void registraClasse(const ClassDecl& c) {
        classe_de[c.class_name] = static_cast<std::uint32_t>(m.classes.size());
        m.classes.push_back({c.class_name, static_cast<std::uint32_t>(c.fields.size())});
        classe_decl.push_back(&c);
    }
    void registraEnum(const EnumDecl& e) {
        enum_de[&e] = static_cast<std::uint32_t>(m.enums.size());
        m.enums.push_back({e.name, e.members});
    }

    // 1ª passada: a assinatura de cada função (chamadas podem ir para as de depois). Um
    // método recebe o self (o objeto) antes dos parâmetros
    void declara(const FunctionDecl& fn, const ClassDecl* cls = nullptr) {
        std::vector<Tipo> ps;
        if (cls) ps.push_back(Tipo::Obj);
        for (const Parameter& p : fn.parameters) ps.push_back(tipoDe(TypeChecker::fromAst(p.type.get()), p.token));
        const std::uint32_t fi = novaFuncao("cz:::" + (cls ? cls->class_name + "." : std::string()) + fn.name,
                                            fn.trace_name, fn.token, std::move(ps),
                                            tipoDe(TypeChecker::fromAst(fn.return_type.get()), fn.token), false);
        funcao_de[&fn] = fi;
        if (!cls && fn.name == "main") {
            if (!fn.parameters.empty()) naoSuportado("main(list<string> args)", fn.token);
            m.main = static_cast<int>(fi);
        }
    }
    // Os métodos, o construtor ("new Classe" no trace) e o inicializador dos campos (oculto,
    // fora da pilha e do trace, como na CVM)
    void declaraClasse(const ClassDecl& c) {
        for (const FunctionDecl* mt : metodos(c)) declara(*mt, &c);
        if (c.constructor) {
            std::vector<Tipo> ps{Tipo::Obj};
            for (const Parameter& p : c.constructor->parameters)
                ps.push_back(tipoDe(TypeChecker::fromAst(p.type.get()), p.token));
            funcao_de[c.constructor.get()] = novaFuncao("cz:::" + c.class_name + ".new", c.constructor->trace_name,
                                                        c.constructor->token, std::move(ps), Tipo::Void, false);
        }
        if (!c.fields.empty())
            init_de[&c] = novaFuncao("cz:::" + c.class_name + ".init", "", c.token, {Tipo::Obj}, Tipo::Void, true);
    }
    static std::vector<const FunctionDecl*> metodos(const ClassDecl& c) {
        std::vector<const FunctionDecl*> v;
        for (const auto* lista : {&c.priv_methods, &c.pub_methods})
            for (const auto& mt : *lista)
                if (mt->node_kind == NodeKind::FunctionDecl) v.push_back(static_cast<const FunctionDecl*>(mt.get()));
        return v;
    }

    // const do nível superior: um global, escrito pela função de inicialização
    void declaraGlobal(const VarDeclStmt& v) {
        TypeRef t = v.type->kind == Type::Kind::VAR ? v.initializer->resolved_type
                                                    : TypeChecker::fromAst(v.type.get());
        global_de[v.res.slot] = static_cast<std::uint32_t>(m.globais.size());
        m.globais.push_back(tipoDe(t, v.token));
    }

    // 2ª passada: o corpo
    void corpo(const FunctionDecl& fn) { corpoDe(funcao_de.at(&fn), fn.parameters, *fn.body, fn.token); }
    void corpoClasse(const ClassDecl& c) {
        for (const FunctionDecl* mt : metodos(c)) corpo(*mt);
        if (c.constructor)
            corpoDe(funcao_de.at(c.constructor.get()), c.constructor->parameters, *c.constructor->body,
                    c.constructor->token);
        if (!c.fields.empty()) corpoInit(c);
    }

    void corpoDe(std::uint32_t fi, const std::vector<Parameter>& parametros, const Stmt& corpo_, const Token& tok) {
        comeca(fi);
        // os parâmetros chegam emprestados; a função fica dona deles no próprio slot (um
        // struct é clonado: a cópia da spec 5.1). Num método, o self vem antes (slot 0)
        const std::size_t off = f->params.size() - parametros.size();
        for (std::size_t i = 0; i < f->params.size(); ++i) f->novoSlot(f->params[i]);
        if (off) self_slot = 0;
        for (std::size_t i = 0; i < parametros.size(); ++i)
            slot_de[{static_cast<std::uint32_t>(i), f->params[i + off]}] = static_cast<std::uint32_t>(i + off);
        for (std::uint32_t i = 0; i < f->params.size(); ++i) {
            if (f->params[i] == Tipo::Struct) grava(i, chamaRt(Rt::StructCopia, {carrega(i, tok)}, tok), tok);
            else if (ehRef(f->params[i])) retem(carrega(i, tok), tok);
        }
        saida = f->novoBloco("saida");
        ret_slot = f->ret != Tipo::Void ? f->novoSlot(f->ret) : UINT32_MAX;
        instrucao(corpo_);
        if (!terminado) {
            // o semântico garante o return nas funções com valor
            if (f->ret == Tipo::Void) br(saida, corpo_.token);
            else poe(Op::Inalcancavel, corpo_.token);
        }
        // a saída: solta os slots donos (os temporários já estão vazios aqui)
        entra(saida);
        for (std::uint32_t s = 0; s < f->slots.size(); ++s)
            if (ehRef(f->slots[s]) && s != ret_slot && !slots_temp.count(s)) limpa(s, tok);
        if (ret_slot != UINT32_MAX) poe(Op::Ret, tok).args = {carrega(ret_slot, tok)};
        else poe(Op::Ret, tok);
        fechaErro(tok);
    }

    // A saída de erro: solta todos os slots donos — os temporários da instrução que falhou,
    // as locais, os parâmetros, um retorno pendente de um finally — e volta com o erro
    void fechaErro(const Token& tok) {
        entra(bloco_erro);
        tratadores.clear();
        for (std::uint32_t s = 0; s < f->slots.size(); ++s)
            if (ehRef(f->slots[s]) && s != self_emprestado) limpa(s, tok);
        poe(Op::RetErro, tok);
    }

    // O inicializador dos campos de uma classe (classInit da CVM): oculto, com o self
    // emprestado (o new segura o objeto); os campos na ordem da declaração
    void corpoInit(const ClassDecl& c) {
        comeca(init_de.at(&c));
        self_slot = f->novoSlot(Tipo::Obj);
        self_emprestado = self_slot;   // não é dono: a saída de erro não o solta
        for (std::size_t i = 0; i < c.fields.size(); ++i) {
            const auto& campo = c.fields[i];
            const Operando v = campo.initializer ? expr(*campo.initializer) : vazioDe(*campo.type, campo.token);
            chamaRt(Rt::GravaCampo, {embrulha(carrega(self_slot, campo.token), campo.token),
                                     Operando::i32(static_cast<std::int32_t>(i)), embrulha(v, campo.token)},
                    campo.token);
            fimInstrucao(campo.token);
        }
        poe(Op::Ret, c.token);
        fechaErro(c.token);
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
        fechaErro(consts.front()->token);
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
        fechaErro(tok);
    }

private:
    Modulo& m;
    Funcao* f = nullptr;
    std::uint32_t atual = 0;      // o bloco em que se escreve
    bool terminado = false;       // o bloco atual já terminou (br, ret...)
    std::uint32_t saida = 0;      // o bloco de saída da função
    std::uint32_t ret_slot = UINT32_MAX;
    std::unordered_map<const void*, std::uint32_t>        funcao_de;   // FunctionDecl ou Constructor
    std::unordered_map<const ClassDecl*, std::uint32_t>   init_de;
    std::unordered_map<std::string, std::uint32_t>        struct_de, classe_de;
    std::vector<const StructDecl*>                        struct_decl;
    std::vector<const ClassDecl*>                         classe_decl;
    std::unordered_map<const EnumDecl*, std::uint32_t>    enum_de;
    std::map<TypeRef, std::uint32_t>                      tipo_de;
    std::map<std::pair<const InterfaceDecl*, int>, std::uint32_t> tabela_de;
    std::uint32_t self_slot = UINT32_MAX;
    std::unordered_map<std::uint32_t, std::uint32_t>       global_de;        // slot do B2 → global
    std::unordered_map<std::uint32_t, const NativeConst*>  consts_nativos;   // Math.pi...
    std::map<std::pair<std::uint32_t, Tipo>, std::uint32_t> slot_de;         // (slot do B2, tipo) → slot
    struct Laco { std::uint32_t continua, sai; };
    std::vector<Laco> lacos;
    // erros: a saída de erro da função e a pilha de tratadores (o do topo vale para as
    // operações emitidas agora)
    std::uint32_t bloco_erro = 0;
    std::vector<std::uint32_t> tratadores;
    std::uint32_t self_emprestado = UINT32_MAX;
    // try com finally em tradução (desenho 5.3): o motivo pendente (0 normal, 1 return,
    // 2 break, 3 continue, 4 erro) e o que ele leva
    struct Finally {
        std::size_t   lacos;          // lacos.size() quando o try começou
        std::uint32_t bloco;          // o início do finally
        std::uint32_t motivo;         // slot i32
        std::uint32_t valor;          // slot do retorno pendente (UINT32_MAX: função void)
        std::uint32_t erro;           // slot do erro pendente
        bool usa_return = false, usa_break = false, usa_continue = false;
    };
    std::vector<Finally> finallys;
    // os temporários donos: os da instrução em andamento e os livres (vazios)
    std::vector<std::uint32_t> temps_uso;
    std::map<Tipo, std::vector<std::uint32_t>> temps_livres;   // por tipo
    std::set<std::uint32_t>    slots_temp;

    void comeca(std::uint32_t fi) {
        f = &m.funcoes[fi];
        slot_de.clear();
        lacos.clear();
        temps_uso.clear();
        temps_livres.clear();
        slots_temp.clear();
        ret_slot = UINT32_MAX;
        self_slot = UINT32_MAX;
        self_emprestado = UINT32_MAX;
        finallys.clear();
        atual = f->novoBloco("b");
        bloco_erro = f->novoBloco("erro");
        tratadores = {bloco_erro};
        terminado = false;
    }

    std::uint32_t novaFuncao(std::string nome, std::string trace, const Token& decl, std::vector<Tipo> params,
                             Tipo ret, bool oculta_) {
        Funcao fn;
        fn.nome       = std::move(nome);
        fn.nome_trace = std::move(trace);
        fn.decl       = decl;
        fn.params     = std::move(params);
        fn.ret        = ret;
        fn.oculta     = oculta_;
        m.funcoes.push_back(std::move(fn));
        return static_cast<std::uint32_t>(m.funcoes.size() - 1);
    }

    // Um tipo para type() e os literais de tipo: a tabela do módulo, refeita na partida
    std::uint32_t tipoIdx(TypeRef t) {
        if (auto it = tipo_de.find(t); it != tipo_de.end()) return it->second;
        DescTipo d;
        d.kind = static_cast<std::uint8_t>(t->kind);
        d.nome = t->name;
        for (TypeRef p : t->params) d.params.push_back(tipoIdx(p));
        const auto i = static_cast<std::uint32_t>(m.tipos.size());
        m.tipos.push_back(std::move(d));
        tipo_de[t] = i;
        return i;
    }

    // A tabela do método k da interface: a função de cada classe que a cumpre
    std::uint32_t tabela(const InterfaceDecl* iface, int k) {
        auto [it, novo] = tabela_de.try_emplace({iface, k}, 0);
        if (!novo) return it->second;
        const auto& mt = iface->methods[static_cast<std::size_t>(k)];
        TabelaIface t;
        t.nome = iface->name + "." + mt.name;
        t.params.push_back(Tipo::Obj);
        for (const Parameter& p : mt.parameters) t.params.push_back(tipoDe(TypeChecker::fromAst(p.type.get()), p.token));
        t.ret = tipoDe(TypeChecker::fromAst(mt.return_type.get()), mt.token);
        t.funcoes.assign(m.classes.size(), -1);
        for (std::size_t ci = 0; ci < classe_decl.size(); ++ci)
            for (const auto& [i, fns] : classe_decl[ci]->itables)
                if (i == iface) t.funcoes[ci] = static_cast<int>(funcao_de.at(fns[static_cast<std::size_t>(k)]));
        it->second = static_cast<std::uint32_t>(m.tabelas.size());
        m.tabelas.push_back(std::move(t));
        return it->second;
    }

    // list<T> e dict<K, V> sem inicializador: vazios (com posse, num temporário)
    Operando vazioDe(const Type& t, const Token& tok) {
        return emprestado({chamaRt(t.kind == Type::Kind::DICT ? Rt::DictNovo : Rt::ListaNova, {}, tok), true}, tok);
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
        b.instr.back().erro = tratadores.empty() ? bloco_erro : tratadores.back();
        terminado = terminador(op) || op == Op::RetErro;
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
            case Operando::K::DescStruct: case Operando::K::DescClasse: return Tipo::Desc;
            case Operando::K::Enum:    return Tipo::Enum;
            case Operando::K::TipoLit: return Tipo::TipoV;
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
        const Tipo t = tipoOperando(v.o);
        if (v.dono && ehRef(t)) {
            std::uint32_t s;
            auto& livres = temps_livres[t];
            if (!livres.empty()) { s = livres.back(); livres.pop_back(); }
            else { s = f->novoSlot(t); slots_temp.insert(s); }
            temps_uso.push_back(s);
            grava(s, v.o, tok);   // o temporário está vazio: nada a soltar
        }
        return v.o;
    }
    // Um valor que vai ser guardado: com posse — retem, se era emprestado; um struct
    // emprestado é clonado (struct é valor: cada slot tem o seu, e mudar um campo não
    // aparece em outro lugar)
    Operando comPosse(const Val& v, const Token& tok) {
        const Tipo t = tipoOperando(v.o);
        if (!ehRef(t) || v.dono) return v.o;
        if (t == Tipo::Struct) return chamaRt(Rt::StructCopia, {v.o}, tok);
        retem(v.o, tok);
        return v.o;
    }
    // Fim de uma instrução da Cinza: solta os temporários dela
    void fimInstrucao(const Token& tok) {
        for (std::uint32_t s : temps_uso) {
            limpa(s, tok);
            temps_livres[f->slots[s]].push_back(s);
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
        if (ehValorDireto(tipoOperando(o))) return o;   // valor, enum, tipo: já é um CzValor
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
            case NodeKind::Identifier: {
                const auto& id = static_cast<const IdentifierExpr&>(e);
                if (id.res.kind == Resolution::Kind::Field)   // campo do objeto atual: uma cópia
                    return campo(self(id.token), id.res.slot, tipoDe(id.resolved_type, id.token), id.token);
                return {identificador(id), false};
            }
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
            case NodeKind::IndexAccess: {   // list, dict e string: a regra da CVM (indexGet)
                const auto& ix = static_cast<const IndexAccessExpr&>(e);
                if (ix.eh_tipo) return valor(*ix.como_tipo);   // N5: Caixa[Ponto] escrito como valor
                const Operando o = expr(*ix.object);
                const Operando k = expr(*ix.index);
                const Operando r = chamaRt(Rt::Indice, {embrulha(o, e.token), embrulha(k, e.token)}, e.token);
                return desembrulha(r, tipoDe(e.resolved_type, e.token), e.token);
            }
            case NodeKind::ListLiteral: {   // os elementos na ordem do fonte
                std::vector<Operando> args;
                for (const auto& el : static_cast<const ListLiteralExpr&>(e).elements)
                    args.push_back(embrulha(expr(*el), el->token));
                return {chamaRt(Rt::ListaNova, std::move(args), e.token), true};
            }
            case NodeKind::DictLiteral: {   // chave e valor, par a par
                std::vector<Operando> args;
                for (const auto& [k, v] : static_cast<const DictLiteralExpr&>(e).pairs) {
                    args.push_back(embrulha(expr(*k), k->token));
                    args.push_back(embrulha(expr(*v), v->token));
                }
                return {chamaRt(Rt::DictNovo, std::move(args), e.token), true};
            }
            case NodeKind::PairLiteral: {
                const auto& pr = static_cast<const PairLiteralExpr&>(e);
                const Operando a = embrulha(expr(*pr.first), e.token);
                const Operando b = embrulha(expr(*pr.second), e.token);
                return {chamaRt(Rt::ParNovo, {a, b}, e.token), true};
            }
            case NodeKind::MemberAccess: {
                const auto& ma = static_cast<const MemberAccessExpr&>(e);
                if (ma.enum_decl) {   // Cor.Verde: uma constante
                    auto en = enum_de.find(ma.enum_decl);
                    if (en == enum_de.end()) naoSuportado("enum de outro módulo", e.token);
                    return {Operando::enumerado(en->second, ma.field_index)};
                }
                TypeRef ot = ma.object->resolved_type;
                if (ot && ot->is(TK::Error)) {   // e.kind, e.message, e.line, e.column
                    const Operando o = exprTipo(*ma.object, Tipo::Erro);
                    const std::string& n = ma.member_name;
                    const std::int32_t qual = n == "kind" ? 0 : n == "message" ? 1 : n == "line" ? 2 : 3;
                    return desembrulha(chamaRt(Rt::ErroCampo, {o, Operando::i32(qual)}, e.token),
                                       tipoDe(e.resolved_type, e.token), e.token);
                }
                if (ot && (ot->is(TK::Struct) || ot->is(TK::Class))) {   // objeto ou struct: o índice do campo
                    const Operando o = expr(*ma.object);
                    return campo(o, static_cast<std::uint32_t>(ma.field_index), tipoDe(e.resolved_type, e.token), e.token);
                }
                if (!ot || !ot->is(TK::Pair)) naoSuportado("acesso a membro", e.token);
                const Operando p = embrulha(expr(*ma.object), e.token);
                const Operando r = chamaRt(Rt::ParCampo, {p, Operando::i32(ma.member_name == "first" ? 0 : 1)}, e.token);
                return desembrulha(r, tipoDe(e.resolved_type, e.token), e.token);
            }
            case NodeKind::Slice: {   // na ordem do fonte (spec 5.2); omitida = void
                const auto& sl = static_cast<const SliceExpr&>(e);
                std::vector<Operando> args{embrulha(exprTipo(*sl.object, Tipo::Str), e.token)};
                for (const Expr* p : {sl.start.get(), sl.end.get(), sl.step.get()})
                    args.push_back(p ? embrulha(expr(*p), e.token) : Operando::vazio());
                return desembrulha(chamaRt(Rt::Fatia, std::move(args), e.token), Tipo::Str, e.token);
            }
            case NodeKind::MethodCall: return metodo(static_cast<const MethodCallExpr&>(e));
            case NodeKind::New:          return novo(static_cast<const NewExpr&>(e));
            case NodeKind::TypeLiteral:   // i64, list<i64>... escrito como valor
                return {Operando::tipoLit(tipoIdx(static_cast<const TypeLiteralExpr&>(e).value))};
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
            case Resolution::Kind::Self:
                return self(id.token);
            case Resolution::Kind::Type:   // Pessoa, Cor... usado como valor (type(x) == Pessoa)
                return Operando::tipoLit(tipoIdx(id.type_value));
            case Resolution::Kind::Field:
                naoSuportado("campo lido como lugar (erro interno)", id.token);
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
        // list, dict, pair, struct, objeto, enum e tipo: o == da CVM (desce nos contêineres)
        if ((ehRef(t) && t != Tipo::Str) || t == Tipo::Enum || t == Tipo::TipoV) {
            const Operando ig = chamaRt(Rt::Igual, {embrulha(a, e.token), embrulha(b, e.token)}, e.token);
            if (e.op == TokenType::OP_EQUAL) return {ig};
            if (e.op == TokenType::OP_NOT_EQUAL) return {Operando::temp(temp(Op::Nao, Tipo::Bool, {ig}, e.token))};
            naoSuportado("este operador em " + std::string(nomeTipo(t)), e.token);
        }
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
        if (c.type_of) {   // type(x): o tipo real, com o estático (runtimeType)
            const Operando x = embrulha(expr(*c.arguments[0]), c.token);
            const Operando r = chamaRt(Rt::TipoDe, {x, Operando::tipoLit(tipoIdx(c.type_of))}, c.token);
            return desembrulha(r, Tipo::TipoV, c.token);
        }
        if (c.interpolated) return interpolacao(c);
        if (!c.native && !c.target) {   // Tipo("mensagem"): um erro, na posição da chamada
            const Operando msg = c.arguments.empty() ? Operando::nulo() : exprTipo(*c.arguments[0], Tipo::Str);
            return {chamaRt(Rt::ErroNovo, {Operando::literal(m.literal(c.function_name)), msg,
                                           Operando::i32(c.token.file_id), Operando::i32(c.token.line),
                                           Operando::i32(c.token.column)}, c.token), true};
        }

        if (c.target) {   // função da Cinza: argumentos emprestados, na ordem; resultado com posse
            auto it = funcao_de.find(c.target);
            if (it == funcao_de.end()) naoSuportado("chamada a '" + c.function_name + "'", c.token);
            std::vector<Operando> args;
            if (c.implicit_method) args.push_back(self(c.token));   // m() dentro de um método
            return chamaFuncao(it->second, std::move(args), c.arguments, c.token);
        }

        // nativa: cada argumento embrulhado (emprestado), o resultado com posse
        const NativeFn* fn = c.native;
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
            // cada pedaço num temporário assim que existe: o seguinte pode falhar
            if (i == 0) { acc = {emprestado(pedaco, tok)}; continue; }
            const Operando p = emprestado(pedaco, tok);
            acc = {emprestado(concatena(acc.o, p, tok), tok)};
        }
        if (!c.native) return acc;   // format: o texto (emprestado do temporário)
        const Operando texto = emprestado(acc, tok);
        temp(Op::ChamaNativa, Tipo::Valor, {embrulha(texto, tok)}, tok, m.nativa("", c.native->name));
        return {Operando::boolean(false)};
    }

    // Método embutido de list, dict ou string (spec 5.7): o objeto avaliado antes dos
    // argumentos; a regra é o callBuiltin da CVM (add e remove mudam o objeto, que é
    // compartilhado)
    Val metodo(const MethodCallExpr& mc) {
        TypeRef ot = mc.object->resolved_type;
        if (ot && (ot->is(TK::Class) || ot->is(TK::Interface))) {
            // o objeto vira o self, antes dos argumentos; pela interface, o método sai da
            // tabela da classe do objeto
            std::vector<Operando> args{exprTipo(*mc.object, Tipo::Obj)};
            if (!mc.iface) {
                auto it = funcao_de.find(mc.target);
                if (it == funcao_de.end()) naoSuportado("chamada a '" + mc.method_name + "'", mc.token);
                return chamaFuncao(it->second, std::move(args), mc.arguments, mc.token);
            }
            const std::uint32_t ti = tabela(mc.iface, mc.iface_method);
            for (std::size_t i = 0; i < mc.arguments.size(); ++i)
                args.push_back(exprTipo(*mc.arguments[i], m.tabelas[ti].params[i + 1]));
            const Tipo ret = m.tabelas[ti].ret;
            Instr& in = poe(Op::ChamaIface, mc.token);
            in.aux = ti;
            in.args = std::move(args);
            if (ret == Tipo::Void) return {Operando::boolean(false)};
            in.dst = f->novoTemp(ret);
            return {Operando::temp(in.dst), ehRef(ret)};
        }
        if (!ot || !(ot->is(TK::String) || ot->is(TK::List) || ot->is(TK::Dict)))
            naoSuportado("chamada de método", mc.token);
        if (mc.arguments.size() > 2) naoSuportado("método com mais de dois argumentos", mc.token);
        const Value::Kind k = ot->is(TK::List) ? Value::Kind::LIST
                            : ot->is(TK::Dict) ? Value::Kind::DICT : Value::Kind::STRING;
        const Builtin b = builtinFor(k, mc.method_name);
        std::vector<Operando> args{Operando::i32(static_cast<std::int32_t>(b)),
                                   embrulha(expr(*mc.object), mc.token)};
        for (const auto& a : mc.arguments) args.push_back(embrulha(expr(*a), mc.token));
        const Rt r = mc.arguments.empty() ? Rt::Metodo0 : mc.arguments.size() == 1 ? Rt::Metodo1 : Rt::Metodo2;
        const Tipo ret = mc.resolved_type ? tipoDe(mc.resolved_type, mc.token) : Tipo::Void;
        return desembrulha(chamaRt(r, std::move(args), mc.token), ret, mc.token);
    }

    // Uma chamada de função da Cinza: os `antes` (o self) e os argumentos emprestados, na
    // ordem; o resultado com posse
    Val chamaFuncao(std::uint32_t fi, std::vector<Operando> args, const std::vector<ExprPtr>& argumentos,
                    const Token& tok) {
        const std::size_t off = args.size();
        for (std::size_t i = 0; i < argumentos.size(); ++i)
            args.push_back(exprTipo(*argumentos[i], m.funcoes[fi].params[i + off]));
        const Tipo ret = m.funcoes[fi].ret;
        Instr& in = poe(Op::Chama, tok);
        in.aux = fi;
        in.args = std::move(args);
        if (ret == Tipo::Void) return {Operando::boolean(false)};   // sem valor (ExprStmt)
        in.dst = f->novoTemp(ret);
        return {Operando::temp(in.dst), ehRef(ret)};
    }

    Operando self(const Token& tok) {
        if (self_slot == UINT32_MAX) naoSuportado("self fora de um método (erro interno)", tok);
        return carrega(self_slot, tok);
    }
    // obj.campo (objeto ou struct): uma cópia do campo, com posse
    Val campo(const Operando& obj, std::uint32_t i, Tipo t, const Token& tok) {
        const Operando r = chamaRt(Rt::Campo, {embrulha(obj, tok), Operando::i32(static_cast<std::int32_t>(i))}, tok);
        return desembrulha(r, t, tok);
    }
    void gravaCampo(const Operando& obj, std::uint32_t i, const Operando& v, const Token& tok) {
        chamaRt(Rt::GravaCampo, {embrulha(obj, tok), Operando::i32(static_cast<std::int32_t>(i)), embrulha(v, tok)}, tok);
    }

    // new Struct(...) / new Classe(...), na ordem do compilador da CVM
    Val novo(const NewExpr& n) {
        const Token& tok = n.token;
        if (auto it = struct_de.find(n.class_name); it != struct_de.end()) {
            // struct: os campos na ordem — os argumentos, ou os valores iniciais (que só
            // enxergam os const globais); pelo nome (N5): os informados na ordem escrita,
            // depois os omitidos
            const StructDecl& st = *struct_decl[it->second];
            std::vector<Operando> campos(st.fields.size());
            auto inicial = [&](std::size_t i) {
                const auto& c = st.fields[i];
                campos[i] = embrulha(c.initializer ? expr(*c.initializer) : vazioDe(*c.type, c.token), c.token);
            };
            if (!n.arg_fields.empty()) {
                std::vector<bool> dado(st.fields.size(), false);
                for (std::size_t i = 0; i < n.arguments.size(); ++i) {
                    const auto k = static_cast<std::size_t>(n.arg_fields[i]);
                    campos[k] = embrulha(expr(*n.arguments[i]), tok);
                    dado[k] = true;
                }
                for (std::size_t i = 0; i < st.fields.size(); ++i)
                    if (!dado[i]) inicial(i);
            } else if (!n.arguments.empty()) {
                for (std::size_t i = 0; i < n.arguments.size(); ++i) campos[i] = embrulha(expr(*n.arguments[i]), tok);
            } else {
                for (std::size_t i = 0; i < st.fields.size(); ++i) inicial(i);
            }
            std::vector<Operando> args{Operando::descStruct(it->second)};
            args.insert(args.end(), campos.begin(), campos.end());
            return {chamaRt(Rt::StructNovo, std::move(args), tok), true};
        }
        // classe: argumentos → objeto → campos (inicializador) → construtor
        auto ci = classe_de.find(n.class_name);
        if (ci == classe_de.end()) naoSuportado("new de '" + n.class_name + "'", tok);
        const ClassDecl& c = *classe_decl[ci->second];
        std::vector<Operando> args;
        if (c.constructor) {
            const Funcao& ctor = m.funcoes[funcao_de.at(c.constructor.get())];
            for (std::size_t i = 0; i < n.arguments.size(); ++i) args.push_back(exprTipo(*n.arguments[i], ctor.params[i + 1]));
        }
        // o objeto fica num temporário da instrução: se o inicializador ou o construtor
        // falhar, a saída de erro o solta; o resultado é emprestado dele
        const Operando o = emprestado({chamaRt(Rt::ObjNovo, {Operando::descClasse(ci->second)}, tok), true}, tok);
        if (auto in = init_de.find(&c); in != init_de.end()) {
            Instr& ch = poe(Op::Chama, tok);
            ch.aux = in->second;
            ch.args = {o};
        }
        if (c.constructor) {
            args.insert(args.begin(), o);
            Instr& ch = poe(Op::Chama, tok);
            ch.aux = funcao_de.at(c.constructor.get());
            ch.args = std::move(args);
        }
        return {o, false};
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
                if (!v.initializer) {   // list<T> e dict<K, V> nascem vazios
                    const Tipo t = tipoDe(TypeChecker::fromAst(v.type.get()), s.token);
                    const Operando vazio = chamaRt(t == Tipo::Dict ? Rt::DictNovo : Rt::ListaNova, {}, s.token);
                    gravaDono(slotVar(v.res.slot, t), vazio, s.token);
                    break;
                }
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
                    paraColecao(fs);
                break;
            }
            case NodeKind::Return: {
                const auto& r = static_cast<const ReturnStmt&>(s);
                Operando v = Operando::boolean(false);
                const bool tem = r.value != nullptr;
                if (tem) {
                    v = posseTipo(*r.value, f->ret);   // o chamador recebe com posse
                    fimInstrucao(s.token);
                }
                retorna(tem, v, s.token);
                break;
            }
            case NodeKind::Break:
                if (lacos.empty()) naoSuportado("break fora de laço (erro interno)", s.token);
                quebra(false, s.token);
                break;
            case NodeKind::Continue:
                if (lacos.empty()) naoSuportado("continue fora de laço (erro interno)", s.token);
                quebra(true, s.token);
                break;
            case NodeKind::Try: tenta(static_cast<const TryStmt&>(s)); break;
            case NodeKind::Throw: {   // throw e: o erro pendente passa a ser ele (a posição dele)
                const Operando e = exprTipo(*static_cast<const ThrowStmt&>(s).value, Tipo::Erro);
                chamaRt(Rt::Lanca, {e}, s.token);
                poe(Op::Inalcancavel, s.token);
                break;
            }
            default:              naoSuportado("esta instrução", s.token);
        }
    }

    // ── saídas e try (desenho 5.3; o tryStmt da CVM) ─────────────────────

    // return: dentro de um try com finally, guarda o valor e o motivo e vai para o finally
    // (o despacho no fim dele faz o return de verdade, no contexto de fora)
    void retorna(bool tem, const Operando& v, const Token& tok) {
        if (!finallys.empty()) {
            Finally& fz = finallys.back();
            if (tem) gravaDono(fz.valor, v, tok);
            grava(fz.motivo, Operando::i32(1), tok);
            fz.usa_return = true;
            poe(Op::Br, tok).alvo = fz.bloco;
            return;
        }
        if (tem) grava(ret_slot, v, tok);
        poe(Op::Br, tok).alvo = saida;
    }

    // break/continue: só passa pelo finally se sair do try (o laço é de fora dele)
    void quebra(bool continua, const Token& tok) {
        if (!finallys.empty() && finallys.back().lacos == lacos.size()) {
            Finally& fz = finallys.back();
            grava(fz.motivo, Operando::i32(continua ? 3 : 2), tok);
            (continua ? fz.usa_continue : fz.usa_break) = true;
            poe(Op::Br, tok).alvo = fz.bloco;
            return;
        }
        poe(Op::Br, tok).alvo = continua ? lacos.back().continua : lacos.back().sai;
    }

    // Os temporários da instrução interrompida por um erro (os das outras estão vazios)
    void limpaTemporarios(const Token& tok) {
        for (std::uint32_t s : slots_temp) limpa(s, tok);
    }

    void tenta(const TryStmt& t) {
        const Token& tok = t.token;
        const std::uint32_t fora = tratadores.back();   // o tratador de fora do try
        const bool tem_finally = t.finally_block != nullptr;
        if (tem_finally) {
            Finally fz;
            fz.lacos  = lacos.size();
            fz.bloco  = f->novoBloco("finally");
            fz.motivo = f->novoSlot(Tipo::I32);
            fz.valor  = f->ret != Tipo::Void ? f->novoSlot(f->ret) : UINT32_MAX;
            fz.erro   = f->novoSlot(Tipo::Erro);
            finallys.push_back(fz);
        }
        const std::uint32_t captura = f->novoBloco("captura");
        // o pega-tudo do finally: um erro do try (sem except que case) ou de um except roda o
        // finally e é relançado no fim dele
        const std::uint32_t pega_tudo = tem_finally ? f->novoBloco("pegatudo") : 0;
        const std::uint32_t fim = f->novoBloco("fimtry");
        auto sair = [&] {   // fim normal do try ou de um except
            if (tem_finally) {
                grava(finallys.back().motivo, Operando::i32(0), tok);
                poe(Op::Br, tok).alvo = finallys.back().bloco;
            } else {
                br(fim, tok);
            }
        };

        tratadores.push_back(captura);
        instrucao(*t.body);
        tratadores.pop_back();
        if (!terminado) sair();

        // a captura: o erro pendente, comparado com os except na ordem
        entra(captura);
        tratadores.push_back(tem_finally ? pega_tudo : fora);
        limpaTemporarios(tok);
        for (const ExceptClause& h : t.handlers) {
            const std::uint32_t corpo_h = f->novoBloco("except"), proximo = f->novoBloco("proximo");
            const Operando casa = chamaRt(Rt::ErroCasa, {Operando::literal(m.literal(h.type_name))}, h.token);
            brc(casa, corpo_h, proximo, h.token);
            entra(corpo_h);
            gravaDono(slotVar(h.var_slot, Tipo::Erro), chamaRt(Rt::PegaErro, {}, h.token), h.token);
            instrucao(*h.body);
            if (!terminado) sair();
            entra(proximo);
        }
        // nenhum casou: o erro continua pendente
        poe(Op::Br, tok).alvo = tem_finally ? pega_tudo : fora;
        tratadores.pop_back();

        if (tem_finally) {
            Finally fz = finallys.back();
            finallys.pop_back();   // o finally e o despacho são do contexto de fora
            entra(pega_tudo);
            limpaTemporarios(tok);
            gravaDono(fz.erro, chamaRt(Rt::PegaErro, {}, tok), tok);
            grava(fz.motivo, Operando::i32(4), tok);
            br(fz.bloco, tok);

            entra(fz.bloco);
            instrucao(*t.finally_block);
            if (!terminado) {
                // o despacho do motivo pendente
                const Operando motivo = carrega(fz.motivo, tok);
                auto caso = [&](std::int32_t codigo, auto&& acao) {
                    const std::uint32_t sim = f->novoBloco("motivo"), nao = f->novoBloco("outro");
                    const Operando c = Operando::temp(temp(Op::Compara, Tipo::Bool, {motivo, Operando::i32(codigo)}, tok,
                                                           static_cast<std::uint32_t>(TokenType::OP_EQUAL)));
                    brc(c, sim, nao, tok);
                    entra(sim);
                    acao();
                    entra(nao);
                };
                if (fz.usa_return)
                    caso(1, [&] {
                        if (fz.valor == UINT32_MAX) { retorna(false, Operando::boolean(false), tok); return; }
                        const Operando v = carrega(fz.valor, tok);   // move: o slot fica vazio
                        if (ehRef(f->slots[fz.valor])) grava(fz.valor, Operando::nulo(), tok);
                        retorna(true, v, tok);
                    });
                if (fz.usa_break) caso(2, [&] { quebra(false, tok); });
                if (fz.usa_continue) caso(3, [&] { quebra(true, tok); });
                caso(4, [&] {   // o erro volta ao estado, com a posição e o trace de antes
                    const Operando e = carrega(fz.erro, tok);
                    grava(fz.erro, Operando::nulo(), tok);
                    chamaRt(Rt::Relanca, {e}, tok);
                    poe(Op::Inalcancavel, tok);
                });
                br(fim, tok);
            }
        }
        entra(fim);
    }

    void atribuicao(const AssignStmt& s) {
        if (s.target->node_kind == NodeKind::IndexAccess || s.target->node_kind == NodeKind::MemberAccess) {
            atribuicaoLugar(s);
            return;
        }
        const auto& id = static_cast<const IdentifierExpr&>(*s.target);
        if (id.res.kind == Resolution::Kind::Field) {   // campo do objeto atual
            const Tipo t = tipoDe(id.resolved_type, s.token);
            Operando v = s.op == TokenType::OP_ASSIGN ? exprTipo(*s.value, t) : expr(*s.value);
            if (s.op != TokenType::OP_ASSIGN) {   // o valor atual, lido depois do valor (spec 5.2)
                const Operando atual_ = emprestado(campo(self(s.token), id.res.slot, t, s.token), s.token);
                v = emprestado(contaComposta(s.op, t, atual_, v, s.token), s.token);
            }
            gravaCampo(self(s.token), id.res.slot, v, s.token);
            fimInstrucao(s.token);
            return;
        }
        if (id.res.kind != Resolution::Kind::Local) naoSuportado("atribuição a este alvo", s.token);
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

    // a op b, com os valores já avaliados (a conta de uma atribuição composta)
    Val contaComposta(TokenType op, Tipo t, const Operando& atual_, const Operando& v, const Token& tok) {
        if (t == Tipo::Str) return concatena(atual_, v, tok);
        if (!ehNumero(t)) naoSuportado("atribuição composta neste tipo", tok);
        return {Operando::temp(temp(Op::Arit, t, {atual_, v}, tok, static_cast<std::uint32_t>(compoundBaseOp(op))))};
    }

    // x[i].c = v, o.lista[0] += v, self.p.x = v (B5): a ordem da spec 5.2, como o
    // assignPlace da CVM — a base e os índices da esquerda para a direita, depois o valor;
    // só então o caminho até o lugar, o valor atual (atribuição composta) e a gravação.
    // Struct é valor: cada nível que é struct é lido como cópia, alterado e devolvido ao
    // nível de cima; list, dict e objeto são compartilhados (a mudança vale no lugar). A
    // base local é o próprio slot (um struct local muda no lugar: o slot é o dono único)
    void atribuicaoLugar(const AssignStmt& s) {
        const Token& tok = s.token;
        std::vector<const Expr*> passos;   // da base para o alvo (IndexAccess ou MemberAccess)
        const Expr* e = s.target.get();
        while (e->node_kind == NodeKind::IndexAccess || e->node_kind == NodeKind::MemberAccess) {
            passos.insert(passos.begin(), e);
            e = e->node_kind == NodeKind::IndexAccess ? static_cast<const IndexAccessExpr*>(e)->object.get()
                                                      : static_cast<const MemberAccessExpr*>(e)->object.get();
        }
        auto indiceDoCampo = [](const Expr* p) {
            return static_cast<std::uint32_t>(static_cast<const MemberAccessExpr*>(p)->field_index);
        };
        for (const Expr* p : passos)
            if (p->node_kind == NodeKind::MemberAccess) {
                TypeRef ot = static_cast<const MemberAccessExpr*>(p)->object->resolved_type;
                if (!ot || !(ot->is(TK::Struct) || ot->is(TK::Class))) naoSuportado("atribuição a este membro", p->token);
            }
        // a base: um campo do objeto atual é lido depois do valor (e devolvido, se struct)
        const bool campo_base = e->node_kind == NodeKind::Identifier &&
                                static_cast<const IdentifierExpr*>(e)->res.kind == Resolution::Kind::Field;
        const std::uint32_t campo_idx = campo_base ? static_cast<const IdentifierExpr*>(e)->res.slot : 0;
        const Tipo tb = tipoDe(e->resolved_type, tok);
        Operando base = campo_base ? Operando{} : expr(*e);
        std::vector<Operando> chaves(passos.size());
        for (std::size_t i = 0; i < passos.size(); ++i)
            if (passos[i]->node_kind == NodeKind::IndexAccess)
                chaves[i] = embrulha(expr(*static_cast<const IndexAccessExpr*>(passos[i])->index), passos[i]->token);
        const Tipo tt = tipoDe(s.target->resolved_type, tok);
        Operando v = expr(*s.value);
        if (campo_base) base = emprestado(campo(self(tok), campo_idx, tb, tok), tok);

        auto ler = [&](std::size_t i, const Operando& de) {
            const Expr* p = passos[i];
            const Tipo tp = tipoDe(p->resolved_type, tok);
            if (p->node_kind == NodeKind::IndexAccess) {
                const Operando r = chamaRt(Rt::IndiceLugar, {embrulha(de, tok), chaves[i]}, p->token);
                return emprestado(desembrulha(r, tp, tok), tok);
            }
            return emprestado(campo(de, indiceDoCampo(p), tp, p->token), tok);
        };
        auto gravar = [&](std::size_t i, const Operando& em, const Operando& valor_) {
            const Expr* p = passos[i];
            if (p->node_kind == NodeKind::IndexAccess)
                chamaRt(Rt::GravaIndice, {embrulha(em, tok), chaves[i], embrulha(valor_, tok)}, p->token);
            else
                gravaCampo(em, indiceDoCampo(p), valor_, p->token);
        };

        std::vector<Operando> nivel{base};   // nivel[i]: o valor de que parte o passo i
        for (std::size_t i = 0; i + 1 < passos.size(); ++i) nivel.push_back(ler(i, nivel[i]));
        const std::size_t ult = passos.size() - 1;
        if (s.op != TokenType::OP_ASSIGN) {   // o valor atual, lido depois do lado direito
            const Operando atual_ = ler(ult, nivel[ult]);
            v = emprestado(contaComposta(s.op, tt, atual_, v, tok), tok);
        }
        gravar(ult, nivel[ult], v);   // a gravação copia (retém, ou clona um struct)
        // devolve, de dentro para fora, cada nível que é struct
        for (std::size_t j = ult; j >= 1; --j)
            if (passos[j - 1]->resolved_type->is(TK::Struct)) gravar(j - 1, nivel[j - 1], nivel[j]);
        if (campo_base && e->resolved_type->is(TK::Struct)) gravaCampo(self(tok), campo_idx, base, tok);
        fimInstrucao(tok);
    }

    // for (T x in lista/dict): os elementos copiados no início (spec 5.5; o FORPREP da
    // CVM), percorridos por índice; for (i64 x in list<i32>) alarga cada um
    void paraColecao(const ForStmt& s) {
        TypeRef ct = s.iterable->resolved_type;
        if (!ct || !(ct->is(TK::List) || ct->is(TK::Dict))) naoSuportado("for neste tipo", s.token);
        const Tipo te = ct->is(TK::Dict) ? Tipo::Par : tipoDe(ct->params[0], s.token);   // o elemento
        const Tipo ti = s.widen_to ? tipoDe(s.widen_to, s.token)
                      : s.type_iterator->kind == Type::Kind::VAR ? te
                      : tipoDe(TypeChecker::fromAst(s.type_iterator.get()), s.token);
        const std::uint32_t col = f->novoSlot(Tipo::Lista), idx = f->novoSlot(Tipo::I64);
        const Operando c0 = embrulha(expr(*s.iterable), s.token);
        gravaDono(col, chamaRt(Rt::Elementos, {c0}, s.token), s.token);
        fimInstrucao(s.token);
        grava(idx, Operando::i64(0), s.token);
        const std::uint32_t iter = slotVar(s.iter_slot, ti);

        const std::uint32_t cabeca = f->novoBloco("paracol");
        const std::uint32_t corpo_ = f->novoBloco("corpo");
        const std::uint32_t fimb = f->novoBloco("fimparacol");
        br(cabeca, s.token);
        entra(cabeca);
        const Operando lista = carrega(col, s.token);
        const Operando i = carrega(idx, s.token);
        const Operando n = chamaRt(Rt::ListaTam, {lista}, s.token);
        const std::uint32_t acabou = temp(Op::Compara, Tipo::Bool, {i, n}, s.token,
                                          static_cast<std::uint32_t>(TokenType::OP_GREATER_EQUAL));
        brc(Operando::temp(acabou), fimb, corpo_, s.token);
        entra(corpo_);
        Operando el = desembrulha(chamaRt(Rt::ListaElem, {lista, i}, s.token), te, s.token).o;   // com posse
        if (ti != te) {
            if (!ehNumero(ti) || !ehNumero(te)) naoSuportado("for com conversão (erro interno)", s.token);
            el = Operando::temp(temp(Op::Converte, ti, {el}, s.token));
        }
        gravaDono(iter, el, s.token);
        grava(idx, Operando::temp(temp(Op::Arit, Tipo::I64, {i, Operando::i64(1)}, s.token,
                                       static_cast<std::uint32_t>(TokenType::OP_PLUS))), s.token);
        lacos.push_back({cabeca, fimb});
        instrucao(*s.body);
        lacos.pop_back();
        br(cabeca, s.token);
        entra(fimb);
        limpa(col, s.token);
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
        gravaDono(iter, ch, s.token);   // antes de qualquer operação que possa falhar
        const Operando len = chamaRt(Rt::StrTamCaractere, {texto, i}, s.token);
        grava(pos, Operando::temp(temp(Op::Arit, Tipo::I64, {i, len}, s.token,
                                       static_cast<std::uint32_t>(TokenType::OP_PLUS))), s.token);
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
    std::vector<const ClassDecl*> classes;
    for (const auto& s : prog.statements) {
        switch (s->node_kind) {
            case NodeKind::FunctionDecl: {
                const auto* fn = static_cast<const FunctionDecl*>(s.get());
                if (fn->type_params.empty()) funcoes.push_back(fn);   // N2: só as cópias
                break;
            }
            case NodeKind::VarDecl: consts.push_back(static_cast<const VarDeclStmt*>(s.get())); break;
            case NodeKind::StructDecl: {
                const auto* st = static_cast<const StructDecl*>(s.get());
                if (st->type_params.empty()) g.registraStruct(*st);   // N5: só as cópias existem
                break;
            }
            case NodeKind::ClassDecl: classes.push_back(static_cast<const ClassDecl*>(s.get())); break;
            case NodeKind::EnumDecl: g.registraEnum(*static_cast<const EnumDecl*>(s.get())); break;
            case NodeKind::InterfaceDecl: break;   // as tabelas saem das classes
            case NodeKind::ErrorDecl: break;   // só um nome de tipo: nada a gerar
            default: naoSuportado(nomeDeclaracao(s->node_kind), s->token);
        }
    }
    for (const auto& s : prog.instancias) {   // N2/N5: cópias das funções e dos structs genéricos
        if (s->node_kind == NodeKind::StructDecl) g.registraStruct(*static_cast<const StructDecl*>(s.get()));
        else funcoes.push_back(static_cast<const FunctionDecl*>(s.get()));
    }
    for (const auto* c : classes) g.registraClasse(*c);

    for (const auto* fn : funcoes) g.declara(*fn);
    for (const auto* c : classes) g.declaraClasse(*c);
    for (const auto* v : consts) g.declaraGlobal(*v);
    if (!consts.empty()) {
        g.inicializacao(consts);
        g.finalizacao(consts.front()->token);
    }
    for (const auto* fn : funcoes) g.corpo(*fn);
    for (const auto* c : classes) g.corpoClasse(*c);
    return m;
}

}  // namespace cinza::cir
