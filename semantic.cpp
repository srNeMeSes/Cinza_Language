#include "semantic.h"
#include "parser.h"          // printf/format: as expressões dentro do texto
#include "runtime_error.h"   // C4: builtinErrorKinds
#include <sstream>
#include <algorithm>
#include <functional>
#include <set>

namespace cinza {

using TK = TypeInfo::Kind;

// ============================================================================
// TYPE CHECKER
// B1: opera sobre TypeRef (tipos únicos do TypeContext); igualdade de tipos
// é comparação de ponteiros e os parâmetros são lidos direto de `params`.
// ============================================================================

// v2.00 #19: Promoção numérica centralizada — única fonte de verdade
// int+int→int  |  int+decimal→decimal  |  decimal+int→decimal  |  decimal+decimal→decimal
TypeRef TypeChecker::promoteNumeric(TypeRef l, TypeRef r) {
    auto& types = TypeContext::instance();
    return (l->is(TK::Decimal) || r->is(TK::Decimal)) ? types.decimalType() : types.intType();
}

// Tipo escrito na AST → TypeRef único
TypeRef TypeChecker::fromAst(const Type* type) {
    auto& types = TypeContext::instance();
    if (!type) return types.voidType();

    auto param = [&](size_t i) -> TypeRef {
        return i < type->type_params.size() ? fromAst(type->type_params[i].get())
                                            : types.varType();
    };

    switch (type->kind) {
        case Type::Kind::INT:     return types.intType();
        case Type::Kind::DECIMAL: return types.decimalType();
        case Type::Kind::STRING:  return types.stringType();
        case Type::Kind::BOOL:    return types.boolType();
        case Type::Kind::VOID:    return types.voidType();
        case Type::Kind::VAR:     return types.varType();
        case Type::Kind::LIST:    return types.list(param(0));
        case Type::Kind::DICT:    return types.dict(param(0), param(1));
        case Type::Kind::PAIR:    return types.pair(param(0), param(1));
        case Type::Kind::OP: {
            std::vector<TypeRef> membros;
            for (const auto& p : type->type_params) membros.push_back(fromAst(p.get()));
            return types.op(std::move(membros));
        }
        case Type::Kind::CUSTOM:  return types.named(type->name);   // class ou struct (C3)
    }
    return types.varType();
}

// A3: genéricos são invariantes. Dois tipos genéricos só são compatíveis se
// forem o mesmo tipo, admitindo "var" no valor para parâmetros ainda não
// resolvidos (literal vazio sem contexto, ex.: list<var>).
static bool sameGenericType(TypeRef declared, TypeRef value) {
    if (declared == value || value->is(TK::Var)) return true;
    if (!declared->isGeneric() || declared->kind != value->kind) return false;
    if (declared->params.size() != value->params.size()) return false;
    for (size_t i = 0; i < declared->params.size(); ++i)
        if (!sameGenericType(declared->params[i], value->params[i])) return false;
    return true;
}

// Checa se `value` é atribuível a `declared`.
// Regras de coerção da Cinza v2:
//   - tipos iguais → sempre OK
//   - "var" declarado → aceita qualquer PRIMITIVO (int/decimal/string/bool)
//   - int → decimal (promoção numérica)
//   - A3: list/dict/pair são invariantes (list<int> NÃO cabe em list<decimal>);
//     literais se adaptam ao tipo esperado em analyzeExpr(expr, expected)
// op<...>: há op em algum ponto do tipo
bool TypeChecker::containsOp(TypeRef t) {
    if (t->is(TK::Op)) return true;
    for (TypeRef p : t->params)
        if (containsOp(p)) return true;
    return false;
}

// op<...>: os tipos concretos que um tipo com op pode ter travado
std::vector<TypeRef> TypeChecker::expand(TypeRef t) {
    auto& types = TypeContext::instance();
    if (!containsOp(t)) return {t};
    if (t->is(TK::Op)) {
        std::vector<TypeRef> out;
        for (TypeRef m : t->params)
            for (TypeRef e : expand(m)) out.push_back(e);
        return out;
    }
    // genérico: combinações dos parâmetros
    std::vector<std::vector<TypeRef>> combos{{}};
    for (TypeRef p : t->params) {
        std::vector<std::vector<TypeRef>> novos;
        for (const auto& c : combos)
            for (TypeRef e : expand(p)) {
                auto n = c;
                n.push_back(e);
                novos.push_back(std::move(n));
            }
        combos = std::move(novos);
    }
    std::vector<TypeRef> out;
    for (const auto& c : combos) {
        if (t->is(TK::List)) out.push_back(types.list(c[0]));
        else if (t->is(TK::Dict)) out.push_back(types.dict(c[0], c[1]));
        else out.push_back(types.pair(c[0], c[1]));
    }
    return out;
}

// Atribuição entre tipos sem op (as regras de sempre)
static bool assignableConcrete(TypeRef declared, TypeRef value);

// op<...>: travar — cada tipo possível do valor cabe em algum tipo do destino
bool TypeChecker::isAssignable(TypeRef declared, TypeRef value) {
    if (declared == value) return true;
    if (!containsOp(declared) && !containsOp(value)) return assignableConcrete(declared, value);
    const auto destinos = expand(declared);
    for (TypeRef v : expand(value)) {
        bool cabe = false;
        for (TypeRef d : destinos)
            if (assignableConcrete(d, v)) { cabe = true; break; }
        if (!cabe) return false;
    }
    return true;
}

// op<...>: trocar o valor de quem travou num tipo desconhecido — o valor novo
// precisa caber em todos os tipos possíveis
bool TypeChecker::reassignable(TypeRef declared, TypeRef value) {
    for (TypeRef d : expand(declared))
        for (TypeRef v : expand(value))
            if (!assignableConcrete(d, v)) return false;
    return true;
}

static bool assignableConcrete(TypeRef declared, TypeRef value) {
    if (declared == value) return true;

    // interface: aceita objeto de classe que a cumpre (nunca o contrário)
    if (declared->is(TK::Interface) && value->is(TK::Class))
        return TypeContext::instance().implements(value->name, declared->name);

    // v2.00 #2: var só aceita primitivos — não aceita list/dict/pair/class
    if (declared->is(TK::Var)) return value->isPrimitive();
    if (value->is(TK::Var))    return true;   // valor var primitivo ainda não resolvido

    // Promoção numérica: int pode ser atribuído a decimal (v2.00 #19)
    if (declared->is(TK::Decimal) && value->is(TK::Int)) return true;

    // C4: Error é a raiz de todos os tipos de erro
    if (declared->is(TK::Error) && declared->name == "Error" && value->is(TK::Error)) return true;

    return sameGenericType(declared, value);
}

// Retorna o tipo resultante de uma operação binária, ou lança SemanticError.
TypeRef TypeChecker::checkBinaryOp(TypeRef left, TokenType op, TypeRef right,
                                   const Token& tok) {
    auto& types = TypeContext::instance();
    const std::string& l = left->str();
    const std::string& r = right->str();
    // C1: operador como escrito no código (and ou &&; += numa atribuição composta)
    const std::string op_text = tok.lexeme.empty() ? tokenTypeToOperatorString(op) : tok.lexeme;

    // op<...>: o tipo travado é desconhecido aqui, então a operação precisa
    // valer para todos os tipos possíveis. O resultado é o tipo (ou o op) dos
    // resultados possíveis. Nada é conferido em runtime.
    if (containsOp(left) || containsOp(right)) {
        std::vector<TypeRef> resultados;
        for (TypeRef a : expand(left))
            for (TypeRef b : expand(right)) {
                TypeRef res;
                try {
                    res = checkBinaryOp(a, op, b, tok);
                } catch (const SemanticError&) {
                    throw SemanticError(
                        "Operador '" + op_text + "' entre '" + l + "' e '" + r + "' não vale para "
                        "todos os tipos do op (falha com '" + a->str() + "' e '" + b->str() +
                        "'). Trate cada tipo com if (type(x) == ...).", tok);
                }
                if (std::find(resultados.begin(), resultados.end(), res) == resultados.end())
                    resultados.push_back(res);
            }
        return resultados.size() == 1 ? resultados[0] : types.op(std::move(resultados));
    }

    switch (op) {

        // ── Operadores aritméticos ───────────────────────────────────────
        case TokenType::OP_PLUS:
            if (left->isNumeric() && right->isNumeric())               return promoteNumeric(left, right);
            if (left->is(TK::String) && right->is(TK::String))         return types.stringType();
            // v2.00 #3: concatenação string + primitivo
            if (left->is(TK::String) && right->isPrimitive())          return types.stringType();
            if (left->isPrimitive() && right->is(TK::String))          return types.stringType();
            throw SemanticError(
                "Operador '" + op_text + "' não é suportado entre '" + l + "' e '" + r + "'", tok);

        case TokenType::OP_MINUS:
        case TokenType::OP_MULTIPLY:
        case TokenType::OP_DIVIDE:
        case TokenType::OP_MODULO:
            if (left->isNumeric() && right->isNumeric()) return promoteNumeric(left, right);
            throw SemanticError(
                "Operador '" + op_text +
                "' requer tipos numéricos, mas recebeu '" + l + "' e '" + r + "'", tok);

        // ── Comparadores de ordem ────────────────────────────────────────
        case TokenType::OP_LESS:
        case TokenType::OP_LESS_EQUAL:
        case TokenType::OP_GREATER:
        case TokenType::OP_GREATER_EQUAL:
            if (left->isNumeric() && right->isNumeric())       return types.boolType();
            if (left->is(TK::String) && right->is(TK::String)) return types.boolType();
            throw SemanticError(
                "Comparação '" + tokenTypeToOperatorString(op) +
                "' não é suportada entre '" + l + "' e '" + r + "'", tok);

        // ── Igualdade ────────────────────────────────────────────────────
        case TokenType::OP_EQUAL:
        case TokenType::OP_NOT_EQUAL:
            if (left == right)                           return types.boolType();
            if (left->isNumeric() && right->isNumeric()) return types.boolType();
            throw SemanticError(
                "Comparação '" + tokenTypeToOperatorString(op) +
                "' não é suportada entre '" + l + "' e '" + r + "'", tok);

        // ── Operadores lógicos ───────────────────────────────────────────
        case TokenType::OP_AND:
        case TokenType::OP_OR:
            if (left->is(TK::Bool) && right->is(TK::Bool)) return types.boolType();
            throw SemanticError(
                "Operador lógico '" + op_text +
                "' requer 'bool' nos dois lados, mas recebeu '" + l + "' e '" + r + "'", tok);

        default:
            throw SemanticError("Operador binário desconhecido", tok);
    }
}

// Retorna o tipo resultante de uma operação unária, ou lança SemanticError.
TypeRef TypeChecker::checkUnaryOp(TokenType op, TypeRef operand, const Token& tok) {
    // op<...>: como no binário, precisa valer para todos os tipos
    if (containsOp(operand)) {
        std::vector<TypeRef> resultados;
        for (TypeRef m : expand(operand)) {
            TypeRef res;
            try {
                res = checkUnaryOp(op, m, tok);
            } catch (const SemanticError&) {
                throw SemanticError("Operador '" + tok.lexeme + "' em '" + operand->str() +
                                    "' não vale para todos os tipos do op (falha com '" + m->str() +
                                    "'). Trate cada tipo com if (type(x) == ...).", tok);
            }
            if (std::find(resultados.begin(), resultados.end(), res) == resultados.end())
                resultados.push_back(res);
        }
        return resultados.size() == 1 ? resultados[0]
                                      : TypeContext::instance().op(std::move(resultados));
    }
    switch (op) {
        case TokenType::OP_MINUS:
            if (operand->isNumeric()) return operand;
            throw SemanticError(
                "Operador '-' unário requer tipo numérico, mas recebeu '" + operand->str() + "'", tok);

        case TokenType::OP_NOT:
            if (operand->is(TK::Bool)) return operand;
            throw SemanticError(
                "Operador '" + tok.lexeme + "' requer 'bool', mas recebeu '" + operand->str() + "'", tok);

        default:
            throw SemanticError("Operador unário desconhecido", tok);
    }
}

// ============================================================================
// SYMBOL TABLE
// ============================================================================

// cria um novo escopo vazio no topo da pilha
void SymbolTable::pushScope() {
    scopes.emplace_back();
}

// remove o escopo atual, preservando o escopo global
void SymbolTable::popScope() {
    if (scopes.size() > 1) scopes.pop_back();
}

// decrara os Symbols e evita redecração
void SymbolTable::declare(const Symbol& sym, const Token& tok) {
    auto& current = scopes.back();                          // Obtém o escopo atual (topo da pilha)
    if (current.count(sym.name)) {                          // Verifica se já existe um símbolo com o mesmo nome neste escopo
        throw SemanticError(
            "Declaração duplicada: '" + sym.name +          // Erro semântico: não é permitido redeclarar no mesmo escopo
            "' já foi declarado neste escopo", tok);
    }
    current[sym.name] = sym;                                // Insere o símbolo no escopo atual
}

// faz uma busca por um IDENTIFIER no escopo atual até a base (global)
Symbol* SymbolTable::resolve(const std::string& name) {
    // Percorre do topo (mais interno) para a base (global)
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        // Procura o nome no escopo atual
        auto found = it->find(name);
        // Se encontrou, retorna ponteiro para o símbolo
        if (found != it->end()) return &found->second;
    }
     // Não encontrado em nenhum escopo
    return nullptr;
}

// C5: busca só nos escopos locais (sem o global)
const Symbol* SymbolTable::resolveLocal(const std::string& name) const {
    for (size_t i = scopes.size(); i-- > 1;) {
        auto found = scopes[i].find(name);
        if (found != scopes[i].end()) return &found->second;
    }
    return nullptr;
}

// C5: busca só no escopo global
const Symbol* SymbolTable::resolveGlobal(const std::string& name) const {
    auto found = scopes[0].find(name);
    return found != scopes[0].end() ? &found->second : nullptr;
}

// sobrecarga de 'resolve' versão constante
const Symbol* SymbolTable::resolve(const std::string& name) const {
    for (auto it = scopes.crbegin(); it != scopes.crend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

// ============================================================================
// SEMANTIC ANALYZER — UTILIDADES
// ============================================================================

[[noreturn]] void SemanticAnalyzer::throwError(const std::string& msg,
                                               const Token& tok) const {
    throw SemanticError(msg, tok);
}

[[noreturn]] void SemanticAnalyzer::throwError(const std::string& msg,
                                               int line, int col) const {
    throw SemanticError(msg, line, col);
}

// ============================================================================
// SEMANTIC ANALYZER — PRÉ-REGISTRO
// Percorre o topo da AST e registra nomes de funções e classes no escopo
// global ANTES de analisar corpos. Isso permite referências para frente:
//   fn a() { b(); }
//   fn b() { a(); }
// ============================================================================

// faz o pré registro de funções e classes
void SemanticAnalyzer::preRegisterDeclarations(const Program& program) {
    registering = true;   // op<...>: tipo desconhecido é checado na análise do corpo
    struct Fim { bool& f; ~Fim() { f = false; } } fim{registering};
    // C3: nomes de struct antes de tudo, para que `Ponto p` vire tipo struct
    // (e não class) em qualquer assinatura registrada a seguir
    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::StructDecl)
            types.declareStruct(static_cast<const StructDecl*>(stmt.get())->name);

    // enum: também antes de tudo, para `Cor c` virar tipo enum em qualquer assinatura
    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::EnumDecl)
            registerEnum(*static_cast<const EnumDecl*>(stmt.get()));

    // interface: os nomes antes de tudo (uma assinatura pode citar outra
    // interface declarada mais abaixo); as assinaturas depois
    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::InterfaceDecl)
            types.declareInterface(static_cast<const InterfaceDecl*>(stmt.get())->name);

    // C4: tipos de erro declarados com `error Nome;` também antes de tudo
    // (os embutidos são registrados uma vez, em analyze)
    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::ErrorDecl) {
            auto* e = static_cast<const ErrorDecl*>(stmt.get());
            registerErrorType(e->name, e->token);
        }

    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::InterfaceDecl)
            registerInterface(*static_cast<const InterfaceDecl*>(stmt.get()));

    for (const auto& stmt : program.statements) {
        if (stmt->node_kind == NodeKind::FunctionDecl) {
            registerFunction(*static_cast<const FunctionDecl*>(stmt.get()));
        } else if (stmt->node_kind == NodeKind::ClassDecl) {
            registerClass(*static_cast<const ClassDecl*>(stmt.get()));
        } else if (stmt->node_kind == NodeKind::StructDecl) {
            registerStruct(*static_cast<const StructDecl*>(stmt.get()));
        }
    }
}

// C3: registra um struct em struct_table e como nome de tipo no escopo global
void SemanticAnalyzer::registerStruct(const StructDecl& st) {
    StructInfo info;
    info.name = st.name;
    info.decl = &st;
    for (const auto& field : st.fields) {
        for (const auto& [nome, _] : info.fields)
            if (nome == field.name)
                throwError("Campo duplicado '" + field.name + "' no struct '" + st.name + "'",
                           field.token);
        info.fields.emplace_back(field.name, typeOf(field.type.get()));
    }
    struct_table[st.name] = std::move(info);

    Symbol sym;
    sym.name          = st.name;
    sym.kind          = Symbol::Kind::CLASS;   // nome de tipo
    sym.resolved_type = types.structType(st.name);
    sym.line          = st.token.line;
    sym.column        = st.token.column;
    sym.module        = current_module;   // C5
    symbol_table.declare(sym, st.token);
}

// interface Forma { ... }: nome de tipo no escopo global e as assinaturas
void SemanticAnalyzer::registerInterface(const InterfaceDecl& in) {
    InterfaceInfo info;
    info.decl = &in;
    for (const auto& m : in.methods) {
        if (info.indexOf(m.name) >= 0)
            throwError("Método '" + m.name + "' repetido na interface '" + in.name + "'", m.token);
        MethodInfo mi;
        mi.return_type = typeOf(m.return_type.get());
        mi.is_public   = true;
        mi.line        = m.token.line;
        for (const auto& p : m.parameters) mi.param_types.push_back(typeOf(p.type.get()));
        info.nomes.push_back(m.name);
        info.metodos.push_back(std::move(mi));
    }
    interface_table[in.name] = std::move(info);

    Symbol sym;
    sym.name          = in.name;
    sym.kind          = Symbol::Kind::CLASS;   // nome de tipo
    sym.resolved_type = types.interfaceType(in.name);
    sym.line          = in.token.line;
    sym.column        = in.token.column;
    sym.module        = current_module;
    symbol_table.declare(sym, in.token);
}

// Assinatura de um método para mensagens: area() -> decimal
static std::string assinatura(const std::string& nome, const MethodInfo& mi) {
    std::string s = nome + "(";
    for (size_t i = 0; i < mi.param_types.size(); ++i)
        s += (i ? ", " : "") + stripModulePrefixes(mi.param_types[i]->str());
    s += ")";
    if (!mi.return_type->is(TK::Void)) s += " -> " + stripModulePrefixes(mi.return_type->str());
    return s;
}

void SemanticAnalyzer::checkInterfaces(ClassDecl* cls) {
    const ClassInfo& ci = class_table.at(cls->class_name);
    auto procura = [](const std::vector<StmtPtr>& metodos, const std::string& n) -> FunctionDecl* {
        for (const auto& m : metodos)
            if (m->node_kind == NodeKind::FunctionDecl &&
                static_cast<FunctionDecl*>(m.get())->name == n)
                return static_cast<FunctionDecl*>(m.get());
        return nullptr;
    };
    cls->itables.clear();
    for (size_t k = 0; k < ci.interfaces.size(); ++k) {
        const InterfaceInfo& ii = interface_table.at(ci.interfaces[k]);
        const Token& tok = cls->interface_tokens[k];
        const std::string classe = stripModulePrefixes(cls->class_name);
        const std::string iface  = stripModulePrefixes(ii.decl->name);
        std::vector<const FunctionDecl*> tabela;
        for (size_t i = 0; i < ii.nomes.size(); ++i) {
            const std::string& nome = ii.nomes[i];
            const MethodInfo& exigido = ii.metodos[i];
            FunctionDecl* fn = procura(cls->pub_methods, nome);
            if (!fn) {
                if (procura(cls->priv_methods, nome))
                    throwError("A classe '" + classe + "' cumpre '" + iface + "', então '" + nome +
                               "' precisa estar em pub { }.", tok);
                throwError("A classe '" + classe + "' declara ': " + iface + "', mas não tem o "
                           "método " + assinatura(nome, exigido) + ".", tok);
            }
            const MethodInfo& tem = ci.methods.at(nome);
            bool igual = tem.return_type == exigido.return_type &&
                         tem.param_types.size() == exigido.param_types.size();
            for (size_t p = 0; igual && p < tem.param_types.size(); ++p)
                igual = tem.param_types[p] == exigido.param_types[p];
            if (!igual)
                throwError("O método " + assinatura(nome, tem) + " da classe '" + classe + "' não "
                           "tem a assinatura exigida por '" + iface + "': " +
                           assinatura(nome, exigido) + ".", fn->token);
            tabela.push_back(fn);
        }
        cls->itables.emplace_back(ii.decl, std::move(tabela));
    }
}

// enum Cor { ... }: vira nome de tipo no escopo global (repetir o nome de
// outra declaração é "Declaração duplicada"); os valores não podem se repetir
void SemanticAnalyzer::registerEnum(const EnumDecl& en) {
    for (size_t i = 0; i < en.members.size(); ++i)
        for (size_t j = 0; j < i; ++j)
            if (en.members[i] == en.members[j])
                throwError("Valor '" + en.members[i] + "' repetido no enum '" + en.name + "'",
                           en.member_tokens[i]);
    types.declareEnum(en.name);
    enum_table[en.name] = &en;

    Symbol sym;
    sym.name          = en.name;
    sym.kind          = Symbol::Kind::CLASS;   // nome de tipo
    sym.resolved_type = types.enumType(en.name);
    sym.line          = en.token.line;
    sym.column        = en.token.column;
    sym.module        = current_module;
    symbol_table.declare(sym, en.token);
}

bool SemanticAnalyzer::isKnownType(const std::string& name) const {
    return class_table.count(name) || struct_table.count(name) || enum_table.count(name) ||
           interface_table.count(name) ||
           TypeContext::instance().isError(name);
}

// C4: tipo de erro, filho de Error. Vira nome de tipo no escopo global, então
// repetir um nome (de erro, class, struct ou fn) é "Declaração duplicada".
void SemanticAnalyzer::registerErrorType(const std::string& name, const Token& tok) {
    types.declareError(name);
    Symbol sym;
    sym.name          = name;
    sym.kind          = Symbol::Kind::CLASS;   // nome de tipo
    sym.resolved_type = types.errorType(name);
    sym.line          = tok.line;
    sym.column        = tok.column;
    sym.module        = current_module;   // C5 (-1 nos embutidos)
    symbol_table.declare(sym, tok);
}

// C3: um struct não pode conter a si mesmo por valor, direta ou indiretamente
// (A { B b; } e B { A a; }): o valor seria infinito. list/dict são referências
// e podem ficar vazios, então list<A> dentro de A é permitido; pair é valor.
void SemanticAnalyzer::checkStructCycles() {
    std::vector<std::string> caminho;
    std::set<std::string>    verificados;

    std::function<void(TypeRef, const Token&)> visita = [&](TypeRef t, const Token& tok) {
        if (t->is(TK::Pair) || t->is(TK::Op)) {   // op<...> guarda o valor dentro
            for (TypeRef p : t->params) visita(p, tok);
            return;
        }
        if (!t->is(TK::Struct) || verificados.count(t->name)) return;

        auto ciclo = std::find(caminho.begin(), caminho.end(), t->name);
        if (ciclo != caminho.end()) {
            std::string texto;
            for (auto it = ciclo; it != caminho.end(); ++it) texto += *it + " → ";
            texto += t->name;
            throwError("Struct '" + t->name + "' contém a si mesmo por valor (" + texto +
                       "): o valor seria infinito. Use list<" + t->name +
                       "> ou uma class para estruturas recursivas.", tok);
        }

        auto it = struct_table.find(t->name);
        if (it == struct_table.end()) return;
        caminho.push_back(t->name);
        for (const auto& [nome, tipo] : it->second.fields) visita(tipo, it->second.decl->token);
        caminho.pop_back();
        verificados.insert(t->name);
    };

    for (const auto& [nome, info] : struct_table)
        visita(types.structType(nome), info.decl->token);
}

// Registra uma função no escopo global como Symbol::Kind::FUNCTION.
void SemanticAnalyzer::registerFunction(const FunctionDecl& fn) {
    Symbol sym;
    sym.name          = fn.name;
    sym.kind          = Symbol::Kind::FUNCTION;

    sym.return_type   = typeOf(fn.return_type.get());
    sym.resolved_type = sym.return_type;
    sym.line          = fn.token.line;
    sym.column        = fn.token.column;
    sym.module        = current_module;   // C5
    sym.fn_decl       = &fn;

    for (const auto& param : fn.parameters) {
        sym.param_types.push_back(typeOf(param.type.get()));
    }

    symbol_table.declare(sym, fn.token);
}

// Registra uma classe em class_table E como Symbol::Kind::CLASS no escopo global.
void SemanticAnalyzer::registerClass(const ClassDecl& cls) {
    ClassInfo info;
    info.name = cls.class_name;
    info.line = cls.token.line;

    // class X : Forma — a relação vale já no pré-registro (para qualquer função
    // aceitar X onde se pede Forma); a conferência dos métodos vem na análise
    for (size_t k = 0; k < cls.interfaces.size(); ++k) {
        const std::string nome = qualifyName(cls.interfaces[k], cls.interface_tokens[k]);
        if (!interface_table.count(nome))
            throwError("'" + cls.interfaces[k] + "' não é uma interface: depois de ':' vêm só "
                       "interfaces (a Cinza não tem herança).", cls.interface_tokens[k]);
        for (const auto& ja : info.interfaces)
            if (ja == nome)
                throwError("Interface '" + cls.interfaces[k] + "' repetida na classe '" +
                           cls.class_name + "'", cls.interface_tokens[k]);
        info.interfaces.push_back(nome);
        types.declareImplements(cls.class_name, nome);
    }

    // Campos privados (fora do pub{})
    int indice = 0;
    for (const auto& field : cls.fields) {
        FieldInfo fi;
        fi.type      = typeOf(field.type.get());
        fi.is_public = false;
        fi.line      = field.token.line;
        fi.index     = indice++;   // B2
        info.fields[field.name] = fi;
    }

    // Construtor
    if (cls.constructor) {
        for (const auto& param : cls.constructor->parameters) {
            info.ctor_param_types.push_back(typeOf(param.type.get()));
        }
    }

    // Métodos privados (fora do pub{}) e públicos (dentro do pub{})
    auto registerMethods = [&](const std::vector<StmtPtr>& methods, bool is_public) {
        for (const auto& mptr : methods) {
            if (mptr->node_kind != NodeKind::FunctionDecl) continue;
            const auto* fn = static_cast<const FunctionDecl*>(mptr.get());
            MethodInfo mi;
            mi.return_type = typeOf(fn->return_type.get());
            mi.is_public   = is_public;
            mi.line        = fn->token.line;
            mi.decl        = fn;
            for (const auto& p : fn->parameters) {
                mi.param_types.push_back(typeOf(p.type.get()));
            }
            if (info.methods.count(fn->name))   // Revisão: antes o segundo sumia em silêncio
                throwError("Método '" + fn->name + "' declarado mais de uma vez na classe '" +
                           cls.class_name + "' (dentro e fora de pub { } também conta).", fn->token);
            info.methods[fn->name] = mi;
        }
    };
    registerMethods(cls.priv_methods, false);
    registerMethods(cls.pub_methods,  true);

    // Registra na class_table
    class_table[cls.class_name] = std::move(info);

    // Registra no escopo global como CLASS (para checar em declarações de variável)
    Symbol sym;
    sym.name          = cls.class_name;
    sym.kind          = Symbol::Kind::CLASS;

    sym.resolved_type = types.classType(cls.class_name);
    sym.line          = cls.token.line;
    sym.column        = cls.token.column;
    sym.module        = current_module;   // C5
    symbol_table.declare(sym, cls.token);
}

// ============================================================================
// SEMANTIC ANALYZER — PONTO DE ENTRADA
// ============================================================================

void SemanticAnalyzer::analyze(Program& program) {
    std::vector<ModuleUnit> units{ModuleUnit{&program, "", {}}};
    analyze(units);
}

// C5: vários módulos. Dependências vêm antes de quem as importa; o principal
// por último.
void SemanticAnalyzer::analyze(std::vector<ModuleUnit>& units) {
    // Embutidos (tipos de erro), visíveis em todos os módulos
    current_module = -1;
    for (const auto& k : builtinErrorKinds())
        registerErrorType(k, Token());

    // Passo 0: escopo global de cada módulo. Nos importados, os nomes do
    // nível superior ganham o prefixo do módulo no próprio AST.
    for (size_t i = 0; i < units.size(); ++i) {
        ModuleUnit& u = units[i];
        ModuleScope ms;
        ms.prefix  = u.prefix;
        ms.is_main = (i + 1 == units.size());

        // Fase 7: módulo nativo — tudo é exportado; funções e constantes entram
        // no escopo global com o nome completo ("Math::sqrt", "Math::pi")
        if (u.native) {
            for (const NativeFn& fn : u.native->fns) {
                const std::string completo = ms.prefix + "::" + fn.name;
                ms.globals[fn.name] = completo;
                ms.pub.insert(fn.name);
                native_fns[completo] = &fn;
                Symbol sym;
                sym.name          = completo;
                sym.kind          = Symbol::Kind::FUNCTION;
                sym.resolved_type = fn.ret;
                sym.module        = static_cast<int>(i);
                symbol_table.declare(sym, Token());
            }
            for (const NativeConst& c : u.native->consts) {
                const std::string completo = ms.prefix + "::" + c.name;
                ms.globals[c.name] = completo;
                ms.pub.insert(c.name);
                Symbol sym;
                sym.name          = completo;
                sym.kind          = Symbol::Kind::VAR;
                sym.resolved_type = c.type;
                sym.is_const      = true;
                sym.module        = static_cast<int>(i);
                sym.res           = newGlobal();   // B2
                global_layout.natives.emplace_back(sym.res.slot, &c);
                symbol_table.declare(sym, Token());
            }
        }

        for (auto& stmt : u.program->statements) {
            std::string* nome = nullptr;
            switch (stmt->node_kind) {
                case NodeKind::FunctionDecl: nome = &static_cast<FunctionDecl*>(stmt.get())->name;     break;
                case NodeKind::ClassDecl:    nome = &static_cast<ClassDecl*>(stmt.get())->class_name;  break;
                case NodeKind::StructDecl:   nome = &static_cast<StructDecl*>(stmt.get())->name;       break;
                case NodeKind::ErrorDecl:    nome = &static_cast<ErrorDecl*>(stmt.get())->name;        break;
                case NodeKind::EnumDecl:     nome = &static_cast<EnumDecl*>(stmt.get())->name;         break;
                case NodeKind::InterfaceDecl: nome = &static_cast<InterfaceDecl*>(stmt.get())->name;  break;
                case NodeKind::VarDecl:      nome = &static_cast<VarDeclStmt*>(stmt.get())->name;      break;
                default: break;
            }
            if (!nome) continue;
            if (!ms.is_main && *nome == "main" && stmt->node_kind == NodeKind::FunctionDecl) {
                throwError("'main' não pode ser declarada num módulo importado: só o arquivo "
                           "principal tem 'main'.", stmt->token);
            }
            const std::string curto    = *nome;
            const std::string completo = ms.prefix.empty() ? curto : ms.prefix + "::" + curto;
            *nome = completo;
            ms.globals[curto] = completo;
            if (stmt->is_pub) ms.pub.insert(curto);
        }
        for (const auto& [alias, idx] : u.imports)
            ms.imports[alias] = static_cast<int>(idx);
        modules.push_back(std::move(ms));
    }

    // Passos 1 a 3 por módulo, na ordem
    for (size_t i = 0; i < units.size(); ++i) {
        if (units[i].native) continue;   // Fase 7: nada a analisar
        current_module = static_cast<int>(i);
        Program& program = *units[i].program;

        // Passo 1: registrar todos os nomes de topo antes de analisar corpos.
        preRegisterDeclarations(program);
        checkStructCycles();   // C3

        // Passo 2: analisar cada statement na ordem.
        for (const auto& stmt : program.statements) {
            analyzeStmt(stmt.get());
        }

        // Passo 3 (Fase 2.5): o programa começa em main (só no principal)
        if (modules[i].is_main) checkMain(program);
    }
}

// ============================================================================
// C5 — RESOLUÇÃO DE NOMES ENTRE MÓDULOS
// ============================================================================

std::string SemanticAnalyzer::qualifyName(const std::string& name, const Token& tok) const {
    if (current_module < 0) return name;
    if (name.find("::") != std::string::npos) return name;   // já é nome completo

    const ModuleScope& ms = modules[static_cast<size_t>(current_module)];
    const auto ponto = name.find('.');
    if (ponto != std::string::npos) {
        const std::string alias = name.substr(0, ponto);
        const std::string resto = name.substr(ponto + 1);
        auto it = ms.imports.find(alias);
        if (it == ms.imports.end()) return name;
        const ModuleScope& dep = modules[static_cast<size_t>(it->second)];
        auto g = dep.globals.find(resto);
        if (g == dep.globals.end())
            throwError("'" + resto + "' não existe no módulo '" + alias + "'", tok);
        if (!dep.pub.count(resto))
            throwError("'" + resto + "' é privado no módulo '" + alias + "': declare-o com "
                       "'pub' para exportá-lo.", tok);
        return g->second;
    }
    auto g = ms.globals.find(name);
    return g != ms.globals.end() ? g->second : name;
}

const Symbol* SemanticAnalyzer::lookup(const std::string& name, const Token& tok,
                                       std::string& full) {
    full = name;
    const bool simples = name.find('.') == std::string::npos &&
                         name.find("::") == std::string::npos;
    if (simples)
        if (const Symbol* s = symbol_table.resolveLocal(name)) return s;

    full = qualifyName(name, tok);
    const Symbol* s = symbol_table.resolveGlobal(full);
    // Global de outro módulo, escrito sem qualificação: não é visível daqui
    if (s && simples && full == name && s->module != -1 && s->module != current_module)
        return nullptr;
    return s;
}

const Symbol* SemanticAnalyzer::findSymbol(const std::string& name) const {
    if (name.find("::") != std::string::npos) return symbol_table.resolveGlobal(name);
    if (const Symbol* s = symbol_table.resolveLocal(name)) return s;
    std::string full = name;
    if (current_module >= 0) {
        const ModuleScope& ms = modules[static_cast<size_t>(current_module)];
        auto g = ms.globals.find(name);
        if (g != ms.globals.end()) full = g->second;
    }
    const Symbol* s = symbol_table.resolveGlobal(full);
    if (s && full == name && s->module != -1 && s->module != current_module) return nullptr;
    return s;
}

TypeRef SemanticAnalyzer::typeOf(Type* type) {
    std::function<void(Type*)> qualifica = [&](Type* t) {
        if (!t) return;
        if (t->kind == Type::Kind::CUSTOM && !t->qualified) {
            t->name      = qualifyName(t->name, t->token);
            t->qualified = true;
        }
        for (auto& p : t->type_params) qualifica(p.get());
    };
    qualifica(type);
    checkOpType(type);
    return TypeChecker::fromAst(type);
}

// op<...>: pelo menos 2 tipos, sem repetir, sem void/var, sem op dentro de op e
// no máximo um list, um dict e um pair (regra 9: a coleção não guarda o tipo dos
// elementos em runtime, então type() não saberia qual deles é)
void SemanticAnalyzer::checkOpType(const Type* type) {
    if (!type) return;
    if (type->kind == Type::Kind::OP) {
        if (type->type_params.size() < 2)
            throwError("'op' precisa de pelo menos 2 tipos, ex.: op<int, string>. Com um tipo só, "
                       "use o próprio tipo.", type->token);
        std::vector<TypeRef> vistos;
        int listas = 0, dicts = 0, pares = 0;
        for (const auto& p : type->type_params) {
            if (p->kind == Type::Kind::OP)
                throwError("'op' dentro de 'op' não é permitido: junte todos os tipos num só "
                           "op<...>.", p->token);
            if (TypeChecker::fromAst(p.get())->is(TK::Interface))
                throwError("'op' não aceita interface: a própria interface já aceita objetos de "
                           "várias classes.", p->token);
            if (TypeChecker::containsOp(TypeChecker::fromAst(p.get())))
                throwError("Um tipo do 'op' não pode conter outro op (ex.: op<list<op<...>>, "
                           "int>).", p->token);
            if (p->kind == Type::Kind::VOID)
                throwError("'void' não pode ser um tipo do 'op'.", p->token);
            if (p->kind == Type::Kind::VAR)
                throwError("'var' não pode ser um tipo do 'op': liste os tipos aceitos.", p->token);
            if (p->kind == Type::Kind::CUSTOM && !registering && !isKnownType(p->name))
                throwError("Tipo desconhecido '" + p->name + "' no op", p->token);
            TypeRef t = TypeChecker::fromAst(p.get());
            if (std::find(vistos.begin(), vistos.end(), t) != vistos.end())
                throwError("Tipo '" + t->str() + "' repetido no op: cada tipo aparece uma vez.",
                           p->token);
            vistos.push_back(t);
            listas += t->is(TK::List);
            dicts  += t->is(TK::Dict);
            pares  += t->is(TK::Pair);
        }
        if (listas > 1 || dicts > 1 || pares > 1)
            throwError("Um 'op' aceita no máximo um list, um dict e um pair: em runtime a coleção "
                       "não guarda o tipo dos elementos, então type() não saberia qual deles ela "
                       "é.", type->token);
    }
    for (const auto& p : type->type_params) checkOpType(p.get());
}

// op<...>: `promover(c, n)` — um literal do tipo n pode ser adaptado a c
// (mesmo tipo, int → decimal, ou coleção com os parâmetros adaptáveis)
static bool promover(TypeRef c, TypeRef n) {
    if (c == n || n->is(TK::Var)) return true;
    if (c->is(TK::Decimal) && n->is(TK::Int)) return true;
    if (c->isGeneric() && c->kind == n->kind && c->params.size() == n->params.size()) {
        for (size_t i = 0; i < c->params.size(); ++i)
            if (!promover(c->params[i], n->params[i])) return false;
        return true;
    }
    return false;
}

// op<...>: um literal de coleção num lugar com op trava num dos tipos
// concretos do destino: o próprio tipo natural, se ele for um deles, ou o
// primeiro para o qual o literal pode ser adaptado ([1, 2] em
// list<op<decimal, string>> vira list<decimal>). Todos os elementos ficam
// do mesmo tipo.
TypeRef SemanticAnalyzer::lockCollectionLiteral(Expr* expr, TypeRef natural, TypeRef target) {
    const auto possiveis = TypeChecker::expand(target);
    if (std::find(possiveis.begin(), possiveis.end(), natural) != possiveis.end()) return natural;
    for (TypeRef c : possiveis) {
        if (!promover(c, natural)) continue;
        switch (expr->node_kind) {
            case NodeKind::ListLiteral: {
                auto* list = static_cast<ListLiteralExpr*>(expr);
                for (auto& elem : list->elements) coerceInPlace(elem, c->elem());
                break;
            }
            case NodeKind::DictLiteral: {
                auto* dict = static_cast<DictLiteralExpr*>(expr);
                for (auto& [k, v] : dict->pairs) {
                    coerceInPlace(k, c->key());
                    coerceInPlace(v, c->value());
                }
                break;
            }
            case NodeKind::PairLiteral: {
                auto* pair = static_cast<PairLiteralExpr*>(expr);
                coerceInPlace(pair->first,  c->first());
                coerceInPlace(pair->second, c->second());
                break;
            }
            default: break;
        }
        return expr->resolved_type = c;
    }
    return natural;   // quem chamou reporta a incompatibilidade
}

// op<...>: `type(x) == T` (ou `T == type(x)`), com x uma variável, parâmetro ou
// campo cujo tipo tem op. Devolve o símbolo de x e o tipo T. Como o tipo
// travado nunca muda, dentro do if x é sempre T.
const Symbol* SemanticAnalyzer::narrowingOf(const Expr* cond, TypeRef& narrowed) {
    if (cond->node_kind != NodeKind::Binary) return nullptr;
    auto* bin = static_cast<const BinaryExpr*>(cond);
    if (bin->op != TokenType::OP_EQUAL) return nullptr;

    auto tipoLiteral = [](const Expr* e) -> TypeRef {
        if (e->node_kind == NodeKind::TypeLiteral) return static_cast<const TypeLiteralExpr*>(e)->value;
        if (e->node_kind == NodeKind::Identifier) {
            auto* id = static_cast<const IdentifierExpr*>(e);
            if (id->res.kind == Resolution::Kind::Type) return id->type_value;
        }
        return nullptr;
    };
    auto variavel = [](const Expr* e) -> const IdentifierExpr* {
        if (e->node_kind != NodeKind::Call) return nullptr;
        auto* call = static_cast<const CallExpr*>(e);
        if (!call->type_of || call->arguments.size() != 1) return nullptr;
        if (call->arguments[0]->node_kind != NodeKind::Identifier) return nullptr;
        return static_cast<const IdentifierExpr*>(call->arguments[0].get());
    };

    const IdentifierExpr* id = variavel(bin->left.get());
    TypeRef t = tipoLiteral(bin->right.get());
    if (!id || !t) { id = variavel(bin->right.get()); t = tipoLiteral(bin->left.get()); }
    if (!id || !t) return nullptr;

    const Symbol* sym = findSymbol(id->name);
    if (!sym || !sym->resolved_type->is(TK::Op)) return nullptr;
    if (!sym->resolved_type->hasMember(t))
        throwError("'" + id->name + "' é '" + sym->resolved_type->str() + "' e nunca será '" +
                   t->str() + "': essa condição é sempre falsa.", cond->token);
    narrowed = t;
    return sym;
}

void SemanticAnalyzer::checkNotAlias(const std::string& name, const Token& tok) const {
    if (current_module >= 0 && modules[static_cast<size_t>(current_module)].imports.count(name))
        throwError("'" + name + "' já é o apelido de um módulo importado; escolha outro nome.",
                   tok);
}

// ============================================================================
// FASE 2.5 — MAIN E NÍVEL SUPERIOR
// ============================================================================

static const char* MAIN_FORMAS =
    "Formas aceitas: 'fn main()', 'fn main() -> void', "
    "'fn main(list<string> args)', 'fn main(list<string> args) -> void'.";

void SemanticAnalyzer::checkMain(const Program& program) {
    const FunctionDecl* main_fn = nullptr;
    for (const auto& stmt : program.statements) {
        if (stmt->node_kind != NodeKind::FunctionDecl) continue;
        const auto* fn = static_cast<const FunctionDecl*>(stmt.get());
        if (fn->name == "main")
            main_fn = fn;   // duplicata já foi rejeitada por SymbolTable::declare
    }

    if (!main_fn) {
        throwError(std::string("Função 'main' não encontrada: todo programa começa por ela. ") +
                   MAIN_FORMAS, 1, 1);
    }

    const bool ret_ok = typeOf(main_fn->return_type.get()) == types.voidType();
    bool params_ok = main_fn->parameters.empty();
    if (main_fn->parameters.size() == 1) {
        params_ok = typeOf(main_fn->parameters[0].type.get()) ==
                    types.list(types.stringType());
    }

    if (!ret_ok || !params_ok) {
        throwError(std::string("Assinatura inválida para 'main': ela é sempre void e só aceita "
                               "um parâmetro opcional do tipo list<string>. ") + MAIN_FORMAS,
                   main_fn->token);
    }
}

bool SemanticAnalyzer::isConstantExpr(const Expr* expr) const {
    switch (expr->node_kind) {
        case NodeKind::Literal:
            return true;
        case NodeKind::Cast:
            return isConstantExpr(static_cast<const CastExpr*>(expr)->operand.get());
        case NodeKind::Unary:
            return isConstantExpr(static_cast<const UnaryExpr*>(expr)->operand.get());
        case NodeKind::Binary: {
            auto* b = static_cast<const BinaryExpr*>(expr);
            return isConstantExpr(b->left.get()) && isConstantExpr(b->right.get());
        }
        case NodeKind::Identifier: {
            // Só const globais já declarados (a análise é em ordem, então um const
            // declarado mais abaixo ainda não existe aqui)
            const Symbol* sym = findSymbol(static_cast<const IdentifierExpr*>(expr)->name);
            return sym && sym->kind == Symbol::Kind::VAR && sym->is_const;
        }
        case NodeKind::MemberAccess:   // Cor.Verde
            return static_cast<const MemberAccessExpr*>(expr)->enum_decl != nullptr;
        default:
            return false;   // chamada, new, literal de list/dict/pair, acesso a membro...
    }
}

// ============================================================================
// SEMANTIC ANALYZER — DISPATCHER DE STATEMENTS
// ============================================================================

// verifica qual o tipo do stmt e chama a função específica pra analisar
void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (!stmt) return;

    // B3: switch no NodeKind em vez de cadeia de dynamic_cast
    switch (stmt->node_kind) {
        case NodeKind::VarDecl:         analyzeVarDecl        (static_cast<VarDeclStmt*>(stmt));         return;
        case NodeKind::Assign:          analyzeAssign         (static_cast<AssignStmt*>(stmt));          return;
        case NodeKind::ExprStmt:        analyzeExprStmt       (static_cast<ExprStmt*>(stmt));            return;
        case NodeKind::Block:           analyzeBlock          (static_cast<BlockStmt*>(stmt));           return;
        case NodeKind::If:              analyzeIf             (static_cast<IfStmt*>(stmt));              return;
        case NodeKind::While:           analyzeWhile          (static_cast<WhileStmt*>(stmt));           return;
        case NodeKind::For:             analyzeFor            (static_cast<ForStmt*>(stmt));             return;
        case NodeKind::Return:          analyzeReturn         (static_cast<ReturnStmt*>(stmt));          return;
        case NodeKind::Break:
        case NodeKind::Continue:        analyzeLoopControl    (stmt);                                    return;
        case NodeKind::Try:             analyzeTry            (static_cast<TryStmt*>(stmt));             return;
        case NodeKind::Throw:           analyzeThrow          (static_cast<ThrowStmt*>(stmt));           return;
        case NodeKind::ErrorDecl:       return;   // C4: registrado no pré-registro
        case NodeKind::EnumDecl:        return;   // registrado no pré-registro
        case NodeKind::InterfaceDecl: {           // tipos das assinaturas existem?
            for (const auto& m : static_cast<InterfaceDecl*>(stmt)->methods) {
                auto confere = [&](Type* t) {
                    if (t->kind == Type::Kind::CUSTOM && !isKnownType(t->name))
                        throwError("Tipo desconhecido '" + t->name + "' na interface", m.token);
                };
                confere(m.return_type.get());
                for (const auto& p : m.parameters) confere(p.type.get());
            }
            return;
        }
        case NodeKind::FunctionDecl:    analyzeFunctionDecl   (static_cast<FunctionDecl*>(stmt));        return;
        case NodeKind::ClassDecl:       analyzeClassDecl      (static_cast<ClassDecl*>(stmt));           return;
        case NodeKind::StructDecl:      analyzeStructDecl     (static_cast<StructDecl*>(stmt));          return;
        default:                        break;
    }
    throwError("Tipo de statement não reconhecido pelo analisador semântico",
               stmt->token);
}

// ============================================================================
// SEMANTIC ANALYZER — STATEMENTS
// ============================================================================

// Bloco { stmt* } — empurra e desempilha seu próprio escopo
void SemanticAnalyzer::analyzeBlock(BlockStmt* block) {
    symbol_table.pushScope();
    analyzeStatements(block->statements);
    symbol_table.popScope();
}

static bool hasBreak(const Stmt* s);

// exit(...) embutido como instrução: encerra o caminho, como throw (uma função
// do usuário chamada exit tem precedência e é uma chamada comum)
static bool isExitStmt(const Stmt* s) {
    if (!s || s->node_kind != NodeKind::ExprStmt) return false;
    const Expr* e = static_cast<const ExprStmt*>(s)->expression.get();
    return e && e->node_kind == NodeKind::Call &&
           static_cast<const CallExpr*>(e)->native == findPrelude("exit");
}

// Revisão: a instrução sempre sai do bloco (return, throw, exit, break, continue,
// if/else com os dois ramos saindo, while (true) sem break, try com o bloco e
// todos os except saindo)? O que vier depois dela nunca é executado.
static bool alwaysLeaves(const Stmt* s) {
    if (!s) return false;
    if (isExitStmt(s)) return true;
    switch (s->node_kind) {
        case NodeKind::Return: case NodeKind::Throw:
        case NodeKind::Break:  case NodeKind::Continue:
            return true;
        case NodeKind::Block:
            for (const auto& x : static_cast<const BlockStmt*>(s)->statements)
                if (alwaysLeaves(x.get())) return true;
            return false;
        case NodeKind::If: {
            auto* i = static_cast<const IfStmt*>(s);
            return i->else_branch && alwaysLeaves(i->then_branch.get()) &&
                   alwaysLeaves(i->else_branch.get());
        }
        case NodeKind::While: {
            auto* w = static_cast<const WhileStmt*>(s);
            if (w->condition->node_kind != NodeKind::Literal) return false;
            auto* lit = static_cast<const LiteralExpr*>(w->condition.get());
            return std::holds_alternative<bool>(lit->value) && std::get<bool>(lit->value) &&
                   !hasBreak(w->body.get());
        }
        case NodeKind::Try: {
            auto* t = static_cast<const TryStmt*>(s);
            if (!alwaysLeaves(t->body.get())) return false;
            for (const auto& h : t->handlers)
                if (!alwaysLeaves(h.body.get())) return false;
            return true;
        }
        default:
            return false;
    }
}

// Revisão: código morto é erro de compilação (decisão de 2026-10-01)
void SemanticAnalyzer::analyzeStatements(const std::vector<StmtPtr>& stmts) {
    for (size_t i = 0; i < stmts.size(); ++i) {
        if (!stmts[i]) continue;
        analyzeStmt(stmts[i].get());
        if (alwaysLeaves(stmts[i].get()) && i + 1 < stmts.size() && stmts[i + 1])
            throwError("Código inalcançável: esta instrução nunca é executada, porque a anterior "
                       "sempre sai do bloco (return, break, continue, throw ou exit).",
                       stmts[i + 1]->token);
    }
}

// A4: procura void na árvore do tipo, não no texto ("Avoider" contém "void")
static bool containsVoid(const Type* type) {
    if (!type) return false;
    if (type->kind == Type::Kind::VOID) return true;
    for (const auto& param : type->type_params)
        if (containsVoid(param.get())) return true;
    return false;
}

// A10: primeira chave de dict inválida na árvore do tipo (nullptr se todas forem
// int/string/bool). decimal é proibido: comparação exata de ponto flutuante.
static TypeRef invalidDictKey(TypeRef type) {
    if (type->is(TK::Dict)) {
        TypeRef k = type->key();
        if (!k->is(TK::Int) && !k->is(TK::String) && !k->is(TK::Bool) && !k->is(TK::Enum) &&
            !k->is(TK::Var))   // var em tipo composto tem erro próprio
            return k;
    }
    for (TypeRef param : type->params)
        if (TypeRef bad = invalidDictKey(param)) return bad;
    return nullptr;
}

static std::string dictKeyMessage(TypeRef key_type) {
    if (key_type->is(TK::Decimal))
        return "Chave de dict não pode ser 'decimal': comparação exata de ponto "
               "flutuante não é confiável. Use int, string ou bool.";
    return "Chave de dict deve ser int, string, bool ou enum, mas recebeu '" + key_type->str() + "'. "
           "Tipos compostos como list<>, pair<> e classes não podem ser chaves.";
}

// C3: const protege valores e coleções, mas não se aplica diretamente a um
// objeto de classe (a linguagem não sabe se um método altera o objeto).
// Em `const list<Pessoa>` a lista é const; os objetos dentro dela não.
// op<...>: mensagens
static std::string opNeedsNarrowing(const std::string& oque, TypeRef t) {
    return oque + " não vale para todos os tipos de '" + t->str() + "'. Trate cada tipo com "
           "if (type(x) == ...) { ... }: dentro do if, x tem o tipo testado.";
}
static std::string readOnlyMessage(TypeRef t) {
    return "'" + t->str() + "' é somente leitura aqui: o tipo travado dos elementos não é "
           "conhecido (ex.: coleção recebida por parâmetro), e alterar poderia quebrar o tipo "
           "da coleção original.";
}
static std::string lockMessage(const std::string& alvo, TypeRef t, TypeRef v) {
    return "'" + alvo + "' tem o tipo travado num dos tipos de '" + t->str() + "' (não se sabe "
           "qual aqui), e '" + v->str() + "' não serve para todos eles. Trate cada tipo com "
           "if (type(x) == ...).";
}

// C3: tipo de classe, ou op<...> que aceita uma classe (const não se aplica)
static bool isClassLike(TypeRef t) {
    if (t->is(TK::Class) || t->is(TK::Interface)) return true;
    if (t->is(TK::Op))
        for (TypeRef m : t->params)
            if (m->is(TK::Class)) return true;
    return false;
}

static std::string constClassMessage(TypeRef type) {
    return "'const' não pode ser usado em objeto de classe ('" + type->str() + "'): "
           "const protege valores e coleções, mas a linguagem não tem como garantir "
           "que os métodos não alteram o objeto. Remova o 'const'.";
}

// Declaração de variável:  tipo nome [= inicializador];
void SemanticAnalyzer::analyzeVarDecl(VarDeclStmt* stmt) {
    TypeRef declared_type = typeOf(stmt->type.get());

    // v2.00 #5/#15: void é ILEGAL em variáveis, listas, dicts, pares, parâmetros
    if (containsVoid(stmt->type.get())) {
        throwError(
            "Tipo 'void' não pode ser usado em variáveis. "
            "'void' é reservado exclusivamente para retorno de funções.",
            stmt->token);
    }

    // v2.00 #2: var proibido em tipos compostos
    if (declared_type->is(TK::List) && declared_type->elem()->is(TK::Var)) {
        throwError(
            "Tipo 'var' não é permitido dentro de 'list<>'. "
            "Use um tipo primitivo explícito: list<int>, list<string>, etc.",
            stmt->token);
    }
    if (declared_type->is(TK::Dict) &&
        (declared_type->key()->is(TK::Var) || declared_type->value()->is(TK::Var))) {
        throwError(
            "Tipo 'var' não é permitido dentro de 'dict<>'. "
            "Use tipos primitivos explícitos: dict<string, int>, etc.",
            stmt->token);
    }
    if (declared_type->is(TK::Pair) &&
        (declared_type->first()->is(TK::Var) || declared_type->second()->is(TK::Var))) {
        throwError(
            "Tipo 'var' não é permitido dentro de 'pair<>'. "
            "Use tipos explícitos: pair<string, int>, etc.",
            stmt->token);
    }

    // A10: chaves de dict (inclusive aninhados) só int, string ou bool
    if (TypeRef bad = invalidDictKey(declared_type))
        throwError(dictKeyMessage(bad), stmt->token);

    // Tipo CUSTOM deve ser uma classe declarada
    if (stmt->type->kind == Type::Kind::CUSTOM) {
        if (!isKnownType(stmt->type->name)) {
            throwError("Tipo desconhecido '" + stmt->type->name + "'", stmt->token);
        }
    }

    // C3: const não se aplica a objeto de classe
    if (stmt->is_const && isClassLike(declared_type))
        throwError(constClassMessage(declared_type), stmt->token);

    if (symbol_table.depth() > 1) checkNotAlias(stmt->name, stmt->token);   // C5

    Symbol sym;
    sym.name     = stmt->name;
    sym.kind     = Symbol::Kind::VAR;
    sym.line     = stmt->token.line;
    sym.column   = stmt->token.column;
    sym.is_const = stmt->is_const;
    sym.module   = current_module;   // C5: vale para os const globais

    if (stmt->initializer) {
        const bool is_var = declared_type->is(TK::Var);
        TypeRef init_type = analyzeExpr(stmt->initializer.get(),
                                        is_var ? nullptr : declared_type);

        if (is_var) {
            // var infere o tipo do inicializador — apenas primitivos
            if (!init_type->isPrimitive()) {
                throwError(
                    "Inferência 'var' só é permitida para tipos primitivos "
                    "(int, decimal, string, bool), mas recebeu '" + init_type->str() + "'. "
                    "Declare o tipo explicitamente.",
                    stmt->token);
            }
            sym.resolved_type = init_type;
        } else {
            if (!TypeChecker::isAssignable(declared_type, init_type)) {
                throwError(
                    "Tipo incompatível na declaração de '" + stmt->name +
                    "': declarado '" + declared_type->str() +
                    "', mas atribuído '" + init_type->str() + "'",
                    stmt->token);
            }
            coerceInPlace(stmt->initializer, declared_type);
            sym.resolved_type = declared_type;
        }
    } else {
        // Sem inicializador: somente list<T> e dict<K,V> devem chegar aqui,
        // porque o parser rejeita declarações sem '=' para qualquer outro tipo.
        // Blindagem defensiva: se um tipo não-coleção chegar até aqui, há
        // inconsistência na pipeline (parser permitiu o que não deveria).
        if (!declared_type->is(TK::List) && !declared_type->is(TK::Dict)) {
            throwError(
                "Erro interno: variável '" + stmt->name + "' de tipo '" + declared_type->str() +
                "' chegou ao semântico sem inicializador. "
                "O parser deveria ter rejeitado esta declaração.",
                stmt->token);
        }
        sym.resolved_type = declared_type;
    }

    // Fase 2.5: no nível superior só há const (garantido pelo parser), e o
    // inicializador precisa ser uma expressão constante
    if (symbol_table.depth() == 1 && stmt->initializer &&
        !isConstantExpr(stmt->initializer.get())) {
        throwError(
            "Inicializador do const global '" + stmt->name + "' precisa ser uma expressão "
            "constante: literais, operadores e outros const já declarados. Chamadas de "
            "função, 'new' e literais de list/dict/pair não são permitidos.",
            stmt->token);
    }

    // op<...>: precisa de valor para travar; com valor de tipo conhecido, a
    // variável passa a ter esse tipo (op<decimal, int> x = 1; → x é int)
    if (TypeChecker::containsOp(declared_type)) {
        if (!stmt->initializer)
            throwError("'" + stmt->name + "' tem op no tipo e precisa de um valor inicial: é ele "
                       "que trava o tipo.", stmt->token);
        sym.resolved_type = stmt->initializer->resolved_type;
    }

    // B2: o slot vem depois do inicializador (`int x = x;` não enxerga o novo x)
    sym.res   = (symbol_table.depth() == 1) ? newGlobal() : newLocal();
    stmt->res = sym.res;
    symbol_table.declare(sym, stmt->token);
}

// B5: descrição textual de um alvo de atribuição, para mensagens de erro
// (x, obj.campo, m[...][...], self.nome)
static std::string lvalueName(const Expr* e) {
    switch (e->node_kind) {
        case NodeKind::Identifier:
            return static_cast<const IdentifierExpr*>(e)->name;
        case NodeKind::MemberAccess: {
            auto* m = static_cast<const MemberAccessExpr*>(e);
            return lvalueName(m->object.get()) + "." + m->member_name;
        }
        case NodeKind::IndexAccess:
            return lvalueName(static_cast<const IndexAccessExpr*>(e)->object.get()) + "[...]";
        case NodeKind::Call:
            return stripModulePrefixes(static_cast<const CallExpr*>(e)->function_name) + "()";
        case NodeKind::MethodCall: {
            auto* m = static_cast<const MethodCallExpr*>(e);
            return lvalueName(m->object.get()) + "." + m->method_name + "()";
        }
        case NodeKind::New:
            return "new " + stripModulePrefixes(static_cast<const NewExpr*>(e)->class_name) + "(...)";
        default:
            return "(expressão)";
    }
}

static std::string constMutationMessage(const Symbol* root, const std::string& alvo) {
    return "Constante '" + root->name + "' não pode ser modificada: '" + alvo +
           "' altera o conteúdo de '" + root->name + "', que é const. Objetos de classe "
           "dentro de um recipiente const continuam alteráveis; o recipiente, não.";
}

// B5: variável const de onde vem o recipiente, ou nullptr. Para ao passar por
// um objeto de classe (C3: objetos nunca são const, mesmo dentro de list const).
const Symbol* SemanticAnalyzer::constRoot(const Expr* container) const {
    if (container->resolved_type &&
        (container->resolved_type->is(TK::Class) || container->resolved_type->is(TK::Interface)))
        return nullptr;

    switch (container->node_kind) {
        case NodeKind::Identifier: {
            const auto& name = static_cast<const IdentifierExpr*>(container)->name;
            if (name == "self") return nullptr;
            const Symbol* sym = findSymbol(name);
            return (sym && sym->is_const) ? sym : nullptr;
        }
        case NodeKind::MemberAccess:
            return constRoot(static_cast<const MemberAccessExpr*>(container)->object.get());
        case NodeKind::IndexAccess:
            return constRoot(static_cast<const IndexAccessExpr*>(container)->object.get());
        case NodeKind::Cast:   // op<...> estreitado: o valor é o mesmo
            return constRoot(static_cast<const CastExpr*>(container)->operand.get());
        default:
            return nullptr;   // valor temporário (chamada, new, literal)
    }
}

// B5: lado esquerdo de uma atribuição. Reaproveita a análise de leitura
// (existência, visibilidade, tipos de índice) e acrescenta as regras de escrita.
TypeRef SemanticAnalyzer::analyzeLValue(Expr* target) {
    switch (target->node_kind) {
        case NodeKind::Identifier: {
            auto* id = static_cast<IdentifierExpr*>(target);
            if (id->name == "self") {
                throwError("'self' não pode ser reatribuído: ele é sempre o objeto atual. "
                           "Para alterá-lo, atribua aos campos (self.campo = ...).", id->token);
            }
            std::string full;
            const Symbol* sym = lookup(id->name, id->token, full);
            if (!sym) {
                throwError("Variável '" + id->name + "' não declarada", id->token);
            }
            id->name = full;   // C5: global de módulo vira nome completo
            id->res  = sym->res;   // B2
            if (sym->kind == Symbol::Kind::FUNCTION) {
                throwError("'" + id->name + "' é uma função e não pode ser reatribuída",
                           id->token);
            }
            if (sym->kind == Symbol::Kind::CLASS) {
                throwError("'" + id->name + "' é uma classe e não pode ser atribuída",
                           id->token);
            }
            // const: reatribuição é sempre proibida
            if (sym->is_const) {
                throwError(
                    "Constante '" + id->name + "' não pode ser reatribuída. "
                    "Remova o 'const' na declaração se precisar de variável mutável.",
                    id->token);
            }
            return id->resolved_type = sym->resolved_type;
        }

        case NodeKind::MemberAccess: {
            auto* m = static_cast<MemberAccessExpr*>(target);
            TypeRef type = analyzeMemberAccess(m);   // campo existe e é visível
            if (m->enum_decl)
                throwError("'" + lvalueName(m) + "' é um valor de enum e não pode receber "
                           "atribuição.", m->token);
            if (m->object->resolved_type->is(TK::Error)) {
                throwError("Os campos de um erro são somente leitura: não é possível "
                           "atribuir a '" + lvalueName(m) + "'.", m->token);
            }
            if (m->object->resolved_type->is(TK::Pair)) {
                throwError("pair é imutável: não é possível atribuir a '" + lvalueName(m) +
                           "'. Crie um novo par, ex.: p = {a, b};", m->token);
            }
            if (const Symbol* root = constRoot(m->object.get()))
                throwError(constMutationMessage(root, lvalueName(m)), m->token);
            // Revisão: struct é valor; num struct temporário (retorno de função,
            // new) a atribuição iria para uma cópia que ninguém vê
            if (m->object->resolved_type->is(TK::Struct)) {
                const Expr* base = m->object.get();
                while (base->node_kind == NodeKind::MemberAccess &&
                       static_cast<const MemberAccessExpr*>(base)->object->resolved_type->is(TK::Struct))
                    base = static_cast<const MemberAccessExpr*>(base)->object.get();
                if (base->node_kind == NodeKind::Call || base->node_kind == NodeKind::MethodCall ||
                    base->node_kind == NodeKind::New)
                    throwError("Atribuição a '" + lvalueName(m) + "' não tem efeito: o struct é "
                               "uma cópia temporária. Guarde-o numa variável, altere e use a "
                               "variável.", m->token);
            }
            return type;
        }

        case NodeKind::IndexAccess: {
            auto* ix = static_cast<IndexAccessExpr*>(target);
            TypeRef type = analyzeIndexAccess(ix);   // tipo do índice/chave, índice negativo
            if (ix->object->resolved_type->is(TK::String))
                throwError("Strings são imutáveis: '" + lvalueName(ix) + "' não pode ser alterado. "
                           "Monte um texto novo (concatenação, fatias, Strings.replace).", ix->token);
            if (TypeChecker::containsOp(ix->object->resolved_type))   // op<...>
                throwError(readOnlyMessage(ix->object->resolved_type), ix->token);
            if (const Symbol* root = constRoot(ix->object.get()))
                throwError(constMutationMessage(root, lvalueName(ix)), ix->token);
            return type;
        }

        default:
            throwError("O lado esquerdo da atribuição não é atribuível", target->token);
    }
}

// B5: atribuição com lvalue genérico:  alvo = valor;
//   x = v;   obj.campo = v;   m[i][j] = v;   obj.lista[0] = v;   self.nome = v;
void SemanticAnalyzer::analyzeAssign(AssignStmt* stmt) {
    TypeRef target_type = analyzeLValue(stmt->target.get());

    // C1: a op= b — o resultado de `a op b` precisa caber no tipo de `a`
    // (int x = 1; x += 1.5; é erro: int + decimal dá decimal)
    if (stmt->op != TokenType::OP_ASSIGN) {
        TypeRef val_type = analyzeExpr(stmt->value.get());
        Token op_tok  = stmt->token;
        op_tok.lexeme = tokenTypeToOperatorString(stmt->op);
        TypeRef result = TypeChecker::checkBinaryOp(target_type, compoundBaseOp(stmt->op),
                                                    val_type, op_tok);
        if (!TypeChecker::isAssignable(target_type, result)) {
            throwError(
                "Operador '" + op_tok.lexeme + "' em '" + lvalueName(stmt->target.get()) +
                "' produz '" + result->str() + "', que não cabe no tipo '" +
                target_type->str() + "' do alvo",
                stmt->token);
        }
        // op<...>: alvo de tipo travado desconhecido — o resultado precisa servir
        // para todos os tipos dele
        if (TypeChecker::containsOp(target_type)) {
            if (!TypeChecker::reassignable(target_type, result))
                throwError(lockMessage(lvalueName(stmt->target.get()), target_type, result),
                           stmt->token);
            stmt->keep_lock = true;
        }
        return;
    }

    TypeRef val_type    = analyzeExpr(stmt->value.get(), target_type);   // A3

    if (!TypeChecker::isAssignable(target_type, val_type)) {
        const Expr* t = stmt->target.get();
        if (t->node_kind == NodeKind::IndexAccess) {
            TypeRef container = static_cast<const IndexAccessExpr*>(t)->object->resolved_type;
            if (container->is(TK::List))
                throwError("Valor de tipo '" + val_type->str() +
                           "' incompatível com elemento da lista '" + target_type->str() + "'",
                           stmt->token);
            throwError("Valor de tipo '" + val_type->str() +
                       "' incompatível com V='" + target_type->str() + "' do dicionário",
                       stmt->token);
        }
        throwError(
            "Tipo incompatível na atribuição de '" + lvalueName(t) +
            "': esperado '" + target_type->str() +
            "', mas recebeu '" + val_type->str() + "'",
            stmt->token);
    }
    // op<...>: alvo de tipo travado desconhecido — o valor novo precisa servir
    // para todos os tipos dele (o executor mantém o tipo travado)
    if (TypeChecker::containsOp(target_type)) {
        if (!TypeChecker::reassignable(target_type, val_type))
            throwError(lockMessage(lvalueName(stmt->target.get()), target_type, val_type),
                       stmt->token);
        stmt->keep_lock = true;
        return;
    }
    coerceInPlace(stmt->value, target_type);   // A2
}

// Expressão-statement:  chamada de função ou método usado como instrução.
void SemanticAnalyzer::analyzeExprStmt(ExprStmt* stmt) {
    analyzeExpr(stmt->expression.get());
}

// Índice negativo literal (-1 ou -(1)) em lista é erro semântico
static bool isNegativeIntLiteral(const Expr* index) {
    if (index->node_kind == NodeKind::Literal) {
        const auto* lit = static_cast<const LiteralExpr*>(index);
        return std::holds_alternative<std::int64_t>(lit->value) &&
               std::get<std::int64_t>(lit->value) < 0;
    }
    if (index->node_kind == NodeKind::Unary) {
        const auto* un = static_cast<const UnaryExpr*>(index);
        return un->op == TokenType::OP_MINUS &&
               un->operand->node_kind == NodeKind::Literal &&
               std::holds_alternative<std::int64_t>(
                   static_cast<const LiteralExpr*>(un->operand.get())->value);
    }
    return false;
}


// If-else:  if (cond) then [else else_branch]
//   - Condição deve ser bool
void SemanticAnalyzer::analyzeIf(IfStmt* stmt) {
    TypeRef cond_type = analyzeExpr(stmt->condition.get());
    if (!cond_type->is(TK::Bool)) {
        throwError(
            "Condição do 'if' deve ser 'bool', mas recebeu '" + cond_type->str() + "'",
            stmt->token);
    }
    // op<...>: dentro de `if (type(x) == T)`, x é T (o tipo travado nunca muda)
    TypeRef estreito = nullptr;
    if (const Symbol* sym = narrowingOf(stmt->condition.get(), estreito)) {
        Symbol copia        = *sym;
        copia.resolved_type = estreito;
        symbol_table.pushScope();
        symbol_table.declare(copia, stmt->token);
        analyzeStmt(stmt->then_branch.get());
        symbol_table.popScope();
    } else {
        analyzeStmt(stmt->then_branch.get());
    }
    if (stmt->else_branch) analyzeStmt(stmt->else_branch.get());
}

// While:  while (cond) body
//   - Condição deve ser bool
void SemanticAnalyzer::analyzeWhile(WhileStmt* stmt) {
    TypeRef cond_type = analyzeExpr(stmt->condition.get());
    if (!cond_type->is(TK::Bool)) {
        throwError(
            "Condição do 'while' deve ser 'bool', mas recebeu '" + cond_type->str() + "'",
            stmt->token);
    }
    ++loop_depth;
    analyzeStmt(stmt->body.get());
    --loop_depth;
}

// C2: break/continue só dentro de laço (while ou for) da própria função
void SemanticAnalyzer::analyzeLoopControl(Stmt* stmt) {
    if (loop_depth == 0) {
        const std::string kw = stmt->node_kind == NodeKind::Break ? "break" : "continue";
        // C4: o laço de fora do finally não vale dentro dele
        if (finally_depth > 0)
            throwError("'" + kw + "' não pode sair de um bloco 'finally': o finally sempre "
                       "termina normalmente.", stmt->token);
        throwError("'" + kw + "' só pode ser usado dentro de um laço (while ou for).",
                   stmt->token);
    }
}

// For:  for (tipo iterador in iterável) body
//   - Iterável deve ser list<T>
//   - Tipo do iterador deve ser compatível com T (ou `var` para inferir)
void SemanticAnalyzer::analyzeFor(ForStmt* stmt) {
    // tipo lista list<T>
    TypeRef iter_type = analyzeExpr(stmt->iterable.get());
    if (iter_type->is(TK::Op))
        throwError(opNeedsNarrowing("'for'", iter_type), stmt->token);

    // C2: list<T> → T; dict<K,V> → pair<K,V> (em ordem de chave); string → string
    //     de um caractere (code point UTF-8)
    TypeRef elem_type = nullptr;
    if (iter_type->is(TK::List))        elem_type = iter_type->elem();
    else if (iter_type->is(TK::Dict))   elem_type = types.pair(iter_type->key(), iter_type->value());
    else if (iter_type->is(TK::String)) elem_type = types.stringType();
    else {
        throwError(
            "'for' requer 'list<T>', 'dict<K,V>' ou 'string' como iterável, mas recebeu '" +
            iter_type->str() + "'",
            stmt->token);
    }

    TypeRef declared_iter = typeOf(stmt->type_iterator.get());

    // Se `var`, infere pelo tipo do elemento
    if (declared_iter->is(TK::Var)) {
        declared_iter = elem_type;
    } else if (!TypeChecker::isAssignable(declared_iter, elem_type)) {
        throwError(
            "Tipo do iterador '" + declared_iter->str() +
            "' é incompatível com o elemento da lista '" + elem_type->str() + "'",
            stmt->token);
    } else if (TypeChecker::containsOp(declared_iter) && !TypeChecker::containsOp(elem_type)) {
        declared_iter = elem_type;   // op<...>: trava no tipo do elemento, que é conhecido
    }

    // Escopo próprio do for: o iterador só existe dentro do corpo
    symbol_table.pushScope();

    Symbol iter_sym;
    iter_sym.name          = stmt->iterator_name;
    iter_sym.kind          = Symbol::Kind::VAR;
    iter_sym.resolved_type = declared_iter;
    iter_sym.line          = stmt->token.line;
    iter_sym.column        = stmt->token.column;
    iter_sym.res           = newLocal();   // B2
    stmt->iter_slot        = iter_sym.res.slot;

    symbol_table.declare(iter_sym, stmt->token);

    ++loop_depth;
    analyzeStmt(stmt->body.get());
    --loop_depth;

    symbol_table.popScope();
}

// Return:  return [expr];
//   - Dentro de função void: só 'return;' sem valor (A10)
//   - Dentro de função não-void: expr deve ter tipo compatível com retorno
//   - Fora de função: erro
void SemanticAnalyzer::analyzeReturn(ReturnStmt* stmt) {
    if (!inside_function) {
        throwError("'return' fora de função", stmt->token);
    }
    // C4: finally sempre termina normalmente (não pode trocar o fluxo pendente)
    if (finally_depth > 0) {
        throwError("'return' não é permitido dentro de 'finally': o finally sempre "
                   "termina normalmente.", stmt->token);
    }

    if (current_function_return_type->is(TK::Void)) {
        // A10: 'return;' sem valor permite saída antecipada
        if (stmt->value) {
            throwError(
                "Função void não pode retornar um valor. Use 'return;' para sair "
                "antecipadamente, ou declare o tipo de retorno.",
                stmt->token);
        }
        return;
    }

    // Função não-void: precisa ter expressão de retorno
    if (!stmt->value) {
        throwError(
            "Função com retorno '" + current_function_return_type->str() +
            "' precisa retornar um valor no 'return'",
            stmt->token);
    }

    TypeRef ret_type = analyzeExpr(stmt->value.get(), current_function_return_type);
    if (!TypeChecker::isAssignable(current_function_return_type, ret_type)) {
        throwError(
            "Tipo de retorno incompatível: esperado '" +
            current_function_return_type->str() + "', mas retornando '" + ret_type->str() + "'",
            stmt->token);
    }
    coerceInPlace(stmt->value, current_function_return_type);
}

// C4: try { } except (Tipo e) { } ... finally { }
void SemanticAnalyzer::analyzeTry(TryStmt* stmt) {
    analyzeStmt(stmt->body.get());

    std::set<std::string> tratados;
    for (size_t i = 0; i < stmt->handlers.size(); ++i) {
        ExceptClause& h = stmt->handlers[i];
        h.type_name = qualifyName(h.type_name, h.token);   // C5

        if (!types.isError(h.type_name)) {
            throwError("Tipo de erro desconhecido '" + h.type_name + "' no except. Tipos "
                       "embutidos: Error, ValueError, IndexError, KeyError, ZeroDivisionError, "
                       "OverflowError, IOError, StackOverflowError, TypeError; ou declare um com "
                       "'error " + h.type_name + ";'.", h.token);
        }
        if (tratados.count(h.type_name)) {
            throwError("'except (" + h.type_name + " ...)' repetido: o primeiro já trata "
                       "esse erro, este nunca seria alcançado.", h.token);
        }
        tratados.insert(h.type_name);
        // Error captura tudo: nada depois dele seria alcançado
        if (h.type_name == "Error" && i + 1 < stmt->handlers.size()) {
            throwError("'except (Error " + h.var_name + ")' captura qualquer erro e precisa ser "
                       "o último: os 'except' seguintes nunca seriam alcançados.",
                       stmt->handlers[i + 1].token);
        }

        // o erro só existe dentro do bloco do except
        symbol_table.pushScope();
        Symbol e;
        e.name          = h.var_name;
        e.kind          = Symbol::Kind::VAR;
        e.resolved_type = types.errorType(h.type_name);
        e.line          = h.token.line;
        e.column        = h.token.column;
        e.res           = newLocal();   // B2
        h.var_slot      = e.res.slot;
        symbol_table.declare(e, h.token);
        analyzeStmt(h.body.get());
        symbol_table.popScope();
    }

    if (stmt->finally_block) {
        const int saved_loops = loop_depth;
        loop_depth = 0;   // break/continue não podem sair do finally
        ++finally_depth;
        analyzeStmt(stmt->finally_block.get());
        --finally_depth;
        loop_depth = saved_loops;
    }
}

// C4: throw expr;  — expr precisa ser um erro
void SemanticAnalyzer::analyzeThrow(ThrowStmt* stmt) {
    TypeRef t = analyzeExpr(stmt->value.get());
    // Revisão: `throw Falhou;` — o nome do tipo, sem criar o erro
    if (stmt->value->node_kind == NodeKind::Identifier) {
        auto* id = static_cast<const IdentifierExpr*>(stmt->value.get());
        if (id->res.kind == Resolution::Kind::Type && id->type_value->is(TK::Error)) {
            const std::string n = stripModulePrefixes(id->name);
            throwError("'" + n + "' é um tipo de erro: crie o erro para lançá-lo, ex.: throw " + n +
                       "(\"mensagem\");", stmt->token);
        }
    }
    if (!t->is(TK::Error)) {
        throwError("'throw' requer um erro, ex.: throw ValueError(\"mensagem\"); "
                   "mas recebeu '" + t->str() + "'", stmt->token);
    }
}

// Declaração de função:  fn nome(params) -> tipo { body }
//   - Não re-registra (já feito em preRegisterDeclarations)
//   - Empurra escopo para parâmetros
//   - Verifica regras de retorno
void SemanticAnalyzer::analyzeFunctionDecl(FunctionDecl* stmt) {
    TypeRef ret_type = typeOf(stmt->return_type.get());

    // A4: void só pode ser o retorno inteiro (-> void), nunca parâmetro de tipo
    if (!ret_type->is(TK::Void) && containsVoid(stmt->return_type.get())) {
        throwError(
            "Tipo de retorno '" + ret_type->str() + "' inválido em '" + stmt->name + "': "
            "'void' só pode ser o retorno inteiro da função (-> void), "
            "nunca parâmetro de list, dict ou pair.",
            stmt->token);
    }
    for (const auto& param : stmt->parameters) {
        if (containsVoid(param.type.get())) {
            throwError(
                "Tipo 'void' não pode ser usado no parâmetro '" + param.name + "'. "
                "'void' é reservado exclusivamente para retorno de funções.",
                param.token);
        }
        TypeRef ptype = typeOf(param.type.get());
        if (TypeRef bad = invalidDictKey(ptype))
            throwError(dictKeyMessage(bad), param.token);
        // C3: const não se aplica a objeto de classe
        if (param.is_const && isClassLike(ptype))
            throwError(constClassMessage(ptype), param.token);
    }
    if (TypeRef bad = invalidDictKey(ret_type))
        throwError(dictKeyMessage(bad), stmt->token);

    TypeRef saved_ret    = current_function_return_type;
    bool    saved_inside = inside_function;
    int     saved_loops  = loop_depth;

    current_function_return_type = ret_type;
    inside_function              = true;
    loop_depth                   = 0;   // C2: o laço de quem chama não vale aqui
    const std::uint32_t saved_slot = next_slot;
    next_slot                    = 0;   // B2: parâmetros ocupam os slots 0..n-1
    stmt->trace_name = stripModulePrefixes(current_class_name.empty()
                                           ? stmt->name
                                           : current_class_name + "." + stmt->name);

    symbol_table.pushScope();

    for (const auto& param : stmt->parameters) {
        if (param.type->kind == Type::Kind::CUSTOM &&
            !isKnownType(param.type->name)) {
            throwError("Tipo desconhecido '" + param.type->name +
                       "' no parâmetro '" + param.name + "'", param.token);
        }

        checkNotAlias(param.name, param.token);   // C5

        Symbol p;
        p.name          = param.name;
        p.kind          = Symbol::Kind::PARAMETER;

        p.resolved_type = typeOf(param.type.get());
        p.is_const      = param.is_const;  // const: parâmetro não pode ser reatribuído
        p.line          = param.token.line;
        p.column        = param.token.column;
        p.res           = newLocal();      // B2
        symbol_table.declare(p, param.token);
    }

    if (stmt->body->node_kind == NodeKind::Block) {
        analyzeStatements(static_cast<BlockStmt*>(stmt->body.get())->statements);
    } else {
        analyzeStmt(stmt->body.get());
    }

    if (!ret_type->is(TK::Void)) {
        if (!allPathsReturn(stmt->body.get())) {
            throwError(
                "Função '" + stmt->name +
                "' não retorna em todos os caminhos de execução",
                stmt->token);
        }
    }

    symbol_table.popScope();

    stmt->num_slots              = next_slot;   // B2
    next_slot                    = saved_slot;
    current_function_return_type = saved_ret;
    inside_function              = saved_inside;
    loop_depth                   = saved_loops;
}

// C3: struct Nome { campos }  — tipos e inicializadores dos campos. Os
// inicializadores são avaliados no escopo global (não enxergam outros campos).
void SemanticAnalyzer::analyzeStructDecl(StructDecl* stmt) {
    for (auto& field : stmt->fields) {
        if (containsVoid(field.type.get())) {
            throwError(
                "Tipo 'void' não pode ser usado no campo '" + field.name + "'. "
                "'void' é reservado exclusivamente para retorno de funções.",
                field.token);
        }
        if (field.type->kind == Type::Kind::CUSTOM && !isKnownType(field.type->name)) {
            throwError("Tipo desconhecido '" + field.type->name + "' no campo '" +
                       field.name + "'", field.token);
        }

        TypeRef ftype = typeOf(field.type.get());
        if (TypeRef bad = invalidDictKey(ftype))
            throwError(dictKeyMessage(bad), field.token);

        if (field.initializer) {
            TypeRef init_type = analyzeExpr(field.initializer.get(), ftype);
            if (!TypeChecker::isAssignable(ftype, init_type)) {
                throwError(
                    "Campo '" + field.name + "': tipo '" + init_type->str() +
                    "' é incompatível com '" + ftype->str() + "'",
                    field.token);
            }
            coerceInPlace(field.initializer, ftype);
        } else if (TypeChecker::containsOp(ftype)) {
            throwError("Campo '" + field.name + "' tem op no tipo e precisa de um valor "
                       "inicial: é ele que trava o tipo.", field.token);
        } else if (!ftype->is(TK::List) && !ftype->is(TK::Dict)) {
            throwError(
                "Erro interno: campo '" + field.name + "' de tipo '" + ftype->str() +
                "' chegou ao semântico sem inicializador. "
                "O parser deveria ter rejeitado esta declaração.",
                field.token);
        }
    }
}

// Declaração de classe:  class Nome { campos... pub { métodos... } }
//   - Não re-registra (já feito em registerClass)
//   - Analisa campos, construtor e métodos com acesso à classe
void SemanticAnalyzer::analyzeClassDecl(ClassDecl* stmt) {
    // Verifica que a classe foi registrada (sempre verdade se chegou aqui)
    auto it = class_table.find(stmt->class_name);
    if (it == class_table.end()) {
        throwError("Classe '" + stmt->class_name + "' não registrada", stmt->token);
    }

    checkInterfaces(stmt);   // class X : Forma

    std::string saved_class = current_class_name;
    current_class_name      = stmt->class_name;

    // ── Escopo dos campos (visível dentro de todos os métodos da classe) ──
    symbol_table.pushScope();

    // Registra campos no escopo da classe para que sejam visíveis
    // dentro de todos os métodos. Campos list<T>/dict<K,V> sem inicializador
    // têm inicialização vazia implícita; os demais obrigatoriamente possuem
    // inicializador (garantido pelo parser).
    for (auto& field : stmt->fields) {
        // A4: void é proibido em campos (inclusive list<void> etc.)
        if (containsVoid(field.type.get())) {
            throwError(
                "Tipo 'void' não pode ser usado no campo '" + field.name + "'. "
                "'void' é reservado exclusivamente para retorno de funções.",
                field.token);
        }

        Symbol fsym;
        fsym.name          = field.name;
        fsym.kind          = Symbol::Kind::VAR;
        fsym.resolved_type = typeOf(field.type.get());
        fsym.line          = field.token.line;
        fsym.column        = field.token.column;
        fsym.res           = {Resolution::Kind::Field,
                              static_cast<std::uint32_t>(&field - stmt->fields.data())};   // B2

        if (TypeRef bad = invalidDictKey(fsym.resolved_type))
            throwError(dictKeyMessage(bad), field.token);

        // Analisa inicializador do campo (se houver).
        // Invariante: campo sem inicializador só é válido para list<T>/dict<K,V>.
        if (field.initializer) {
            TypeRef init_type = analyzeExpr(field.initializer.get(), fsym.resolved_type);
            if (!TypeChecker::isAssignable(fsym.resolved_type, init_type)) {
                throwError(
                    "Campo '" + field.name + "': tipo '" + init_type->str() +
                    "' é incompatível com '" + fsym.resolved_type->str() + "'",
                    field.token);
            }
            coerceInPlace(field.initializer, fsym.resolved_type);
        } else if (TypeChecker::containsOp(fsym.resolved_type)) {
            throwError("Campo '" + field.name + "' tem op no tipo e precisa de um valor "
                       "inicial: é ele que trava o tipo.", field.token);
        } else {
            // Blindagem defensiva: campo não-coleção sem inicializador indica
            // inconsistência na pipeline — o parser deveria ter rejeitado.
            if (!fsym.resolved_type->is(TK::List) && !fsym.resolved_type->is(TK::Dict)) {
                throwError(
                    "Erro interno: campo '" + field.name + "' de tipo '" +
                    fsym.resolved_type->str() + "' chegou ao semântico sem inicializador. "
                    "O parser deveria ter rejeitado esta declaração.",
                    field.token);
            }
        }

        symbol_table.declare(fsym, field.token);
    }

    // ── Analisa o construtor ──────────────────────────────────────────────
    if (stmt->constructor) {
        auto& ctor = *stmt->constructor;

        TypeRef saved_ret    = current_function_return_type;
        bool    saved_inside = inside_function;
        current_function_return_type = types.voidType();
        inside_function              = true;
        const std::uint32_t saved_slot = next_slot;
        next_slot                    = 0;   // B2
        ctor.trace_name = "new " + stripModulePrefixes(stmt->class_name);

        symbol_table.pushScope();

        for (const auto& param : ctor.parameters) {
            if (containsVoid(param.type.get())) {
                throwError(
                    "Tipo 'void' não pode ser usado no parâmetro '" + param.name + "'. "
                    "'void' é reservado exclusivamente para retorno de funções.",
                    param.token);
            }

            Symbol p;
            p.name          = param.name;
            p.kind          = Symbol::Kind::PARAMETER;

            p.resolved_type = typeOf(param.type.get());
            p.is_const      = param.is_const;
            p.line          = param.token.line;
            p.column        = param.token.column;
            p.res           = newLocal();   // B2

            if (TypeRef bad = invalidDictKey(p.resolved_type))
                throwError(dictKeyMessage(bad), param.token);
            // C3: const não se aplica a objeto de classe
            if (p.is_const && isClassLike(p.resolved_type))
                throwError(constClassMessage(p.resolved_type), param.token);

            symbol_table.declare(p, param.token);
        }

        // Analisa o corpo do construtor
        if (ctor.body->node_kind == NodeKind::Block) {
            analyzeStatements(static_cast<BlockStmt*>(ctor.body.get())->statements);
        } else {
            analyzeStmt(ctor.body.get());
        }

        symbol_table.popScope();

        ctor.num_slots               = next_slot;   // B2
        next_slot                    = saved_slot;
        current_function_return_type = saved_ret;
        inside_function              = saved_inside;
    }

    // ── Analisa métodos privados e públicos ───────────────────────────────
    for (auto* methods : {&stmt->priv_methods, &stmt->pub_methods}) {
        for (const auto& mptr : *methods) {
            if (mptr->node_kind == NodeKind::FunctionDecl)
                analyzeFunctionDecl(static_cast<FunctionDecl*>(mptr.get()));
        }
    }

    // Remove o escopo dos campos da classe
    symbol_table.popScope();

    current_class_name = saved_class;
}

// ============================================================================
// SEMANTIC ANALYZER — DISPATCHER DE EXPRESSÕES
// Cada função de análise:
//   1. Analisa recursivamente os filhos
//   2. Determina o tipo resultante
//   3. Anota expr->resolved_type
//   4. Retorna o tipo
// ============================================================================
TypeRef SemanticAnalyzer::analyzeExpr(Expr* expr, TypeRef expected) {
    if (!expr) return types.voidType();

    // op<...>: um literal de coleção num lugar com op é analisado pelo tipo
    // natural e travado num dos tipos concretos do destino (todos os
    // elementos do mesmo tipo)
    if (expected && TypeChecker::containsOp(expected) &&
        (expr->node_kind == NodeKind::ListLiteral || expr->node_kind == NodeKind::DictLiteral ||
         expr->node_kind == NodeKind::PairLiteral)) {
        TypeRef natural = analyzeExpr(expr, nullptr);
        return lockCollectionLiteral(expr, natural, expected);
    }
    if (expected && TypeChecker::containsOp(expected)) expected = nullptr;

    // B3: switch no NodeKind em vez de cadeia de dynamic_cast
    switch (expr->node_kind) {
        case NodeKind::Literal:      return analyzeLiteral     (static_cast<LiteralExpr*>(expr));
        case NodeKind::Identifier:   return analyzeIdentifier  (static_cast<IdentifierExpr*>(expr));
        case NodeKind::Binary:       return analyzeBinary      (static_cast<BinaryExpr*>(expr));
        case NodeKind::Unary:        return analyzeUnary       (static_cast<UnaryExpr*>(expr));
        case NodeKind::Call:         return analyzeCall        (static_cast<CallExpr*>(expr));
        case NodeKind::MethodCall:   return analyzeMethodCall  (static_cast<MethodCallExpr*>(expr));
        case NodeKind::MemberAccess: return analyzeMemberAccess(static_cast<MemberAccessExpr*>(expr));
        case NodeKind::IndexAccess:  return analyzeIndexAccess (static_cast<IndexAccessExpr*>(expr));
        case NodeKind::Slice:        return analyzeSlice       (static_cast<SliceExpr*>(expr));
        case NodeKind::New:         return analyzeNew         (static_cast<NewExpr*>(expr));
        case NodeKind::ListLiteral:  return analyzeListLiteral (static_cast<ListLiteralExpr*>(expr), expected);
        case NodeKind::DictLiteral:  return analyzeDictLiteral (static_cast<DictLiteralExpr*>(expr), expected);
        case NodeKind::PairLiteral:  return analyzePairLiteral (static_cast<PairLiteralExpr*>(expr), expected);
        case NodeKind::TypeLiteral: {   // op<...>: type(x) == int
            auto* e  = static_cast<TypeLiteralExpr*>(expr);
            e->value = typeOf(e->type.get());
            return e->resolved_type = types.typeType();
        }
        case NodeKind::Cast: {
            // Defensivo: CastExpr só é inserido depois da análise (A2) e, sem a
            // reanálise por chamada (removida em A7), não deveria voltar aqui.
            auto* e = static_cast<CastExpr*>(expr);
            analyzeExpr(e->operand.get());
            return e->resolved_type;
        }
        default: break;
    }
    throwError("Tipo de expressão não reconhecido pelo analisador semântico",
               expr->token);
}

// ============================================================================
// SEMANTIC ANALYZER — EXPRESSÕES
// ============================================================================

// Literal:  42, 3.14, "hello", true/false
TypeRef SemanticAnalyzer::analyzeLiteral(LiteralExpr* expr) {
    TypeRef type = nullptr;

    if      (std::holds_alternative<std::int64_t>(expr->value)) type = types.intType();
    else if (std::holds_alternative<double>      (expr->value)) type = types.decimalType();
    else if (std::holds_alternative<std::string> (expr->value)) type = types.stringType();
    else if (std::holds_alternative<bool>        (expr->value)) type = types.boolType();
    else throwError("Literal com tipo desconhecido", expr->token);

    return expr->resolved_type = type;
}

// Identificador:  variavel_name — deve estar declarado
TypeRef SemanticAnalyzer::analyzeIdentifier(IdentifierExpr* expr) {
    // A10/B5: self é o objeto atual; só existe dentro de classe
    if (expr->name == "self") {
        if (current_class_name.empty()) {
            throwError("'self' só pode ser usado dentro de uma classe (métodos, construtor "
                       "ou inicializadores de campo).", expr->token);
        }
        expr->res = {Resolution::Kind::Self, 0};   // B2
        return expr->resolved_type = types.classType(current_class_name);
    }

    std::string full;
    const Symbol* sym = lookup(expr->name, expr->token, full);

    if (!sym) {
        throwError("Variável '" + expr->name + "' não declarada", expr->token);
    }
    expr->name = full;       // C5: global de módulo vira nome completo
    expr->res  = sym->res;   // B2
    // Revisão: função não é valor (antes passava e falhava em runtime)
    if (sym->kind == Symbol::Kind::FUNCTION)
        throwError("'" + expr->name + "' é uma função: chame-a com " + expr->name + "(...). Na "
                   "Cinza funções não são valores.", expr->token);
    // op<...>: nome de class/struct/erro usado como valor é um tipo (type(x) == Pessoa)
    if (sym->kind == Symbol::Kind::CLASS) {
        expr->res        = {Resolution::Kind::Type, 0};
        expr->type_value = sym->resolved_type;
        return expr->resolved_type = types.typeType();
    }
    return expr->resolved_type = sym->resolved_type;
}

// Operação binária:  left OP right
TypeRef SemanticAnalyzer::analyzeBinary(BinaryExpr* expr) {
    TypeRef left_type  = analyzeExpr(expr->left.get());
    TypeRef right_type = analyzeExpr(expr->right.get());

    return expr->resolved_type =
        TypeChecker::checkBinaryOp(left_type, expr->op, right_type, expr->token);
}

// Operação unária:  OP operand
TypeRef SemanticAnalyzer::analyzeUnary(UnaryExpr* expr) {
    TypeRef operand_type = analyzeExpr(expr->operand.get());

    return expr->resolved_type =
        TypeChecker::checkUnaryOp(expr->op, operand_type, expr->token);
}

// Chamada de função:  nome(args)
//   - Método da própria classe → função do usuário → Tipo("msg") de erro →
//     nativa do prelude (print, range)
//   - Verifica existência, aridade e tipos dos argumentos
TypeRef SemanticAnalyzer::analyzeCall(CallExpr* expr) {
    // ── Função declarada pelo usuário ─────────────────────────────────────
    std::string full_name;
    const Symbol* sym = lookup(expr->function_name, expr->token, full_name);

    // ── Chamada implícita a método da própria classe ──────────────────────
    // Dentro de um método, nome() sem objeto refere-se a this.nome().
    // O parser gera CallExpr, mas semanticamente é um MethodCall implícito.
    // A10: o método da classe tem precedência sobre uma função global de
    // mesmo nome (antes a global vencia).
    if (!current_class_name.empty()) {
        auto cls_it = class_table.find(current_class_name);
        if (cls_it != class_table.end()) {
            auto meth_it = cls_it->second.methods.find(expr->function_name);
            if (meth_it != cls_it->second.methods.end()) {
                const MethodInfo& mi = meth_it->second;
                if (expr->arguments.size() != mi.param_types.size()) {
                    throwError(
                        "Aridade incorreta em '" + expr->function_name +
                        "': esperados " + std::to_string(mi.param_types.size()) +
                        " argumento(s), mas recebeu " +
                        std::to_string(expr->arguments.size()), expr->token);
                }
                for (size_t i = 0; i < expr->arguments.size(); ++i) {
                    TypeRef at = analyzeExpr(expr->arguments[i].get(), mi.param_types[i]);
                    if (!TypeChecker::isAssignable(mi.param_types[i], at)) {
                        throwError(
                            "Argumento " + std::to_string(i + 1) + " de '" +
                            expr->function_name + "': esperado '" +
                            mi.param_types[i]->str() + "', mas recebeu '" + at->str() + "'",
                            expr->token);
                    }
                    coerceInPlace(expr->arguments[i], mi.param_types[i]);
                }
                expr->target          = mi.decl;
                expr->implicit_method = true;
                return expr->resolved_type = mi.return_type;
            }
        }
    }

    if (sym) expr->function_name = full_name;   // C5: função/erro de módulo

    // Fase 7: função de módulo nativo (Math.sqrt, st.upper)
    if (sym) {
        if (auto nit = native_fns.find(expr->function_name); nit != native_fns.end()) {
            expr->native = nit->second;
            return analyzeNativeCall(expr, *nit->second);
        }
    }

    // C4: Tipo("mensagem") cria um erro: ValueError("x"), SaldoInsuficiente("y")
    if (sym && sym->kind == Symbol::Kind::CLASS && types.isError(expr->function_name)) {
        if (expr->arguments.size() > 1) {
            throwError("'" + expr->function_name + "(...)' recebe no máximo 1 argumento: a "
                       "mensagem (string)", expr->token);
        }
        if (!expr->arguments.empty()) {
            TypeRef at = analyzeExpr(expr->arguments[0].get());
            if (!at->is(TK::String))
                throwError("A mensagem de '" + expr->function_name + "(...)' deve ser 'string', "
                           "mas recebeu '" + at->str() + "'", expr->token);
        }
        return expr->resolved_type = types.errorType(expr->function_name);
    }

    // op<...>: type(x) devolve o tipo do valor (o atual, se x for um op); uma
    // função do usuário chamada type tem precedência
    if (!sym && expr->function_name == "type") {
        if (expr->arguments.size() != 1)
            throwError("'type' recebe exatamente 1 argumento, ex.: type(x)", expr->token);
        expr->type_of = analyzeExpr(expr->arguments[0].get());
        if (expr->type_of->is(TK::Void))
            throwError("'type' precisa de um valor, mas a expressão é 'void'", expr->token);
        if (TypeChecker::containsOp(expr->type_of) && !expr->type_of->is(TK::Op))
            throwError("'type' não sabe o tipo travado de '" + expr->type_of->str() + "': em "
                       "runtime a coleção não guarda o tipo dos elementos. Use type() num "
                       "elemento.", expr->token);
        return expr->resolved_type = types.typeType();
    }

    // printf/format: texto com {expr}; uma função do usuário tem precedência
    if (!sym && (expr->function_name == "printf" || expr->function_name == "format"))
        return analyzeInterpolation(expr);

    // C6: nativa do prelude (print, range); uma função do usuário tem precedência
    if (!sym) {
        if (const NativeFn* native = findPrelude(expr->function_name)) {
            expr->native = native;
            return analyzeNativeCall(expr, *native);
        }
    }

    if (!sym) {
        throwError("Função '" + expr->function_name + "' não declarada",
                   expr->token);
    }
    if (sym->kind != Symbol::Kind::FUNCTION) {
        throwError("'" + expr->function_name + "' não é uma função", expr->token);
    }
    // Fase 2.5: main é o ponto de entrada, chamado só pelo interpretador
    if (expr->function_name == "main") {
        throwError("'main' é o ponto de entrada do programa e não pode ser chamada "
                   "pelo próprio programa.", expr->token);
    }

    // Verificação de aridade
    if (expr->arguments.size() != sym->param_types.size()) {
        throwError(
            "Aridade incorreta em '" + expr->function_name +
            "': esperados " + std::to_string(sym->param_types.size()) +
            " argumento(s), mas recebeu " +
            std::to_string(expr->arguments.size()),
            expr->token);
    }

    expr->target = sym->fn_decl;

    for (size_t i = 0; i < expr->arguments.size(); ++i) {
        TypeRef at = analyzeExpr(expr->arguments[i].get(), sym->param_types[i]);
        if (!TypeChecker::isAssignable(sym->param_types[i], at)) {
            throwError(
                "Argumento " + std::to_string(i + 1) + " de '" +
                expr->function_name + "': esperado '" + sym->param_types[i]->str() +
                "', mas recebeu '" + at->str() + "'",
                expr->token);
        }
        coerceInPlace(expr->arguments[i], sym->param_types[i]);
    }

    return expr->resolved_type = sym->return_type;
}

// C6: unificação simples de uma assinatura nativa com o tipo de um argumento.
// T (variável) é fixada na primeira ocorrência; as seguintes precisam caber no
// tipo fixado. "any" aceita qualquer tipo sem se fixar. Genéricos (list<T>)
// unificam parâmetro a parâmetro.
static bool unify(TypeRef param, TypeRef arg, std::map<std::string, TypeRef>& binds) {
    if (param->is(TK::TypeVar)) {
        if (param->name == "any") return true;
        auto it = binds.find(param->name);
        if (it == binds.end()) {
            // Fase 7: variáveis com restrição
            if (param->name == "número" && !arg->is(TK::Int) && !arg->is(TK::Decimal))
                return false;
            if (param->name == "comparável" && !arg->is(TK::Int) && !arg->is(TK::Decimal) &&
                !arg->is(TK::String))
                return false;
            binds[param->name] = arg;
            return true;
        }
        return TypeChecker::isAssignable(it->second, arg);
    }
    if (param->isGeneric() && arg->kind == param->kind &&
        arg->params.size() == param->params.size()) {
        for (size_t i = 0; i < param->params.size(); ++i)
            if (!unify(param->params[i], arg->params[i], binds)) return false;
        return true;
    }
    return TypeChecker::isAssignable(param, arg);
}

// Tipo da assinatura com as variáveis já fixadas (nullptr se sobrar alguma livre)
static TypeRef substitute(TypeRef t, const std::map<std::string, TypeRef>& binds) {
    auto& types = TypeContext::instance();
    if (t->is(TK::TypeVar)) {
        auto it = binds.find(t->name);
        return it != binds.end() ? it->second : nullptr;
    }
    if (!t->isGeneric()) return t;
    std::vector<TypeRef> ps;
    for (TypeRef p : t->params) {
        TypeRef sp = substitute(p, binds);
        if (!sp) return nullptr;
        ps.push_back(sp);
    }
    if (t->is(TK::List)) return types.list(ps[0]);
    if (t->is(TK::Dict)) return types.dict(ps[0], ps[1]);
    return types.pair(ps[0], ps[1]);
}

TypeRef SemanticAnalyzer::analyzeNativeCall(CallExpr* expr, const NativeFn& fn) {
    // Nome como o usuário o vê: "print", "Lists.sort"
    std::string nome = expr->function_name;
    if (auto p = nome.find("::"); p != std::string::npos) nome.replace(p, 2, ".");
    const size_t n     = expr->arguments.size();
    const size_t max_n = fn.params.size();
    const size_t min_n = fn.min_params == NativeFn::SEM_OBRIGATORIOS ? 0
                       : fn.min_params ? fn.min_params : (fn.variadic ? 0 : max_n);
    if (n < min_n || (!fn.variadic && n > max_n)) {
        const std::string esperados = (min_n == max_n) ? std::to_string(max_n)
                                    : std::to_string(min_n) + " a " + std::to_string(max_n);
        throwError("Aridade incorreta em '" + nome + "': esperados " + esperados +
                   " argumento(s), mas recebeu " + std::to_string(n), expr->token);
    }

    std::map<std::string, TypeRef> binds;
    for (size_t i = 0; i < n; ++i) {
        TypeRef param    = (fn.variadic && i >= max_n - 1) ? fn.params.back() : fn.params[i];
        TypeRef expected = substitute(param, binds);   // literais usam o tipo, se já conhecido
        TypeRef at       = analyzeExpr(expr->arguments[i].get(), expected);
        if (at->is(TK::Void))   // Revisão: void não é valor
            throwError("Argumento " + std::to_string(i + 1) + " de '" + nome + "' não tem valor: "
                       "a expressão é 'void'.", expr->token);
        if (!unify(param, at, binds)) {
            TypeRef mostrado = expected ? expected : param;
            throwError("Argumento " + std::to_string(i + 1) + " de '" + nome +
                       "': esperado '" + mostrado->str() + "', mas recebeu '" + at->str() + "'",
                       expr->token);
        }
        if (TypeRef concreto = substitute(param, binds))
            coerceInPlace(expr->arguments[i], concreto);
    }

    // Fase 7: nativa que altera um argumento (Lists.sort, Lists.sort_by) não aceita
    // const nem coleção de op com tipo travado desconhecido
    for (size_t i = 0; i < n && i < 32; ++i) {
        if (!(fn.mutates & (1u << i))) continue;
        if (TypeChecker::containsOp(expr->arguments[i]->resolved_type))
            throwError(readOnlyMessage(expr->arguments[i]->resolved_type), expr->token);
        if (const Symbol* root = constRoot(expr->arguments[i].get()))
            throwError("'" + nome + "' altera a lista, mas '" + root->name + "' é const. "
                       "Listas const não podem ser modificadas.", expr->token);
    }

    TypeRef ret = substitute(fn.ret, binds);
    if (!ret) throwError("Erro interno: retorno de '" + nome + "' ficou sem tipo", expr->token);
    return expr->resolved_type = ret;
}

// Chamada de método:  objeto.metodo(args)
//   - Suporta built-ins de list, dict, string
//   - Suporta métodos de classes definidas pelo usuário
TypeRef SemanticAnalyzer::analyzeMethodCall(MethodCallExpr* expr) {
    TypeRef obj_type = analyzeExpr(expr->object.get());

    if (obj_type->is(TK::Op))
        throwError(opNeedsNarrowing("O método '" + expr->method_name + "'", obj_type), expr->token);

    // interface: só os métodos que ela declara; qual código roda depende da
    // classe do objeto (escolhido em runtime pela tabela montada em checkInterfaces)
    if (obj_type->is(TK::Interface)) {
        const InterfaceInfo& ii = interface_table.at(obj_type->name);
        const int i = ii.indexOf(expr->method_name);
        if (i < 0)
            throwError("A interface '" + obj_type->str() + "' não tem o método '" +
                       expr->method_name + "'", expr->token);
        const MethodInfo& mi = ii.metodos[static_cast<size_t>(i)];
        if (expr->arguments.size() != mi.param_types.size())
            throwError("Aridade incorreta em '" + obj_type->str() + "." + expr->method_name +
                       "': esperados " + std::to_string(mi.param_types.size()) +
                       " argumento(s), mas recebeu " + std::to_string(expr->arguments.size()),
                       expr->token);
        for (size_t a = 0; a < expr->arguments.size(); ++a) {
            TypeRef at = analyzeExpr(expr->arguments[a].get(), mi.param_types[a]);
            if (!TypeChecker::isAssignable(mi.param_types[a], at))
                throwError("Argumento " + std::to_string(a + 1) + " de '" + obj_type->str() + "." +
                           expr->method_name + "': esperado '" + mi.param_types[a]->str() +
                           "', mas recebeu '" + at->str() + "'", expr->token);
            coerceInPlace(expr->arguments[a], mi.param_types[a]);
        }
        expr->iface        = ii.decl;
        expr->iface_method = i;
        return expr->resolved_type = mi.return_type;
    }
    // op<...>: coleção de op com tipo travado desconhecido é somente leitura
    if (TypeChecker::containsOp(obj_type) &&
        (expr->method_name == "add" || expr->method_name == "remove"))
        throwError(readOnlyMessage(obj_type), expr->token);

    // Helper: o receptor vem de um const? Usado para bloquear métodos mutantes
    // (.add, .remove). C3/B5: vale em qualquer profundidade (m[0].add(x) com m
    // const), e para ao passar por um objeto de classe.
    auto isConstObject = [&]() -> bool {
        return constRoot(expr->object.get()) != nullptr;
    };

    // ── list<T> built-ins ─────────────────────────────────────────────────
    if (obj_type->is(TK::List)) {
        TypeRef T = obj_type->elem();

        const std::string& m = expr->method_name;

        if (m == "size") {
            if (!expr->arguments.empty())
                throwError("'list.size()' não aceita argumentos", expr->token);
            return expr->resolved_type = types.intType();
        }
        if (m == "has") {
            if (expr->arguments.size() != 1)
                throwError("'list.has(idx)' requer exatamente 1 argumento", expr->token);
            TypeRef idx_type = analyzeExpr(expr->arguments[0].get());
            if (!idx_type->is(TK::Int))
                throwError("'list.has(idx)' requer índice 'int', recebeu '" + idx_type->str() + "'",
                           expr->token);
            return expr->resolved_type = types.boolType();
        }
        if (m == "add") {
            if (isConstObject())
                throwError("Não é possível chamar '.add()' em lista const. "
                           "Listas const não podem ser modificadas.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'list.add(elem)' requer exatamente 1 argumento", expr->token);
            TypeRef elem_type = analyzeExpr(expr->arguments[0].get(), T);
            if (!TypeChecker::isAssignable(T, elem_type))
                throwError("'list.add': tipo '" + elem_type->str() +
                           "' incompatível com elemento '" + T->str() + "'",
                           expr->token);
            coerceInPlace(expr->arguments[0], T);
            return expr->resolved_type = types.voidType();
        }
        if (m == "remove") {
            if (isConstObject())
                throwError("Não é possível chamar '.remove()' em lista const. "
                           "Listas const não podem ser modificadas.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'list.remove(idx)' requer exatamente 1 argumento", expr->token);
            TypeRef idx_type = analyzeExpr(expr->arguments[0].get());
            if (!idx_type->is(TK::Int))
                throwError("'list.remove(idx)' requer índice 'int', recebeu '" + idx_type->str() + "'",
                           expr->token);
            return expr->resolved_type = types.voidType();
        }
        throwError("Método '" + m + "' não existe em 'list<T>'", expr->token);
    }

    // ── dict<K,V> built-ins ───────────────────────────────────────────────
    if (obj_type->is(TK::Dict)) {
        TypeRef K = obj_type->key();
        TypeRef V = obj_type->value();

        const std::string& m = expr->method_name;

        if (m == "size") {
            if (!expr->arguments.empty())
                throwError("'dict.size()' não aceita argumentos", expr->token);
            return expr->resolved_type = types.intType();
        }
        if (m == "has") {
            if (expr->arguments.size() != 1)
                throwError("'dict.has(key)' requer exatamente 1 argumento", expr->token);
            TypeRef key_type = analyzeExpr(expr->arguments[0].get());
            if (!TypeChecker::isAssignable(K, key_type))
                throwError("'dict.has': chave '" + key_type->str() +
                           "' incompatível com K='" + K->str() + "'",
                           expr->token);
            coerceInPlace(expr->arguments[0], K);
            return expr->resolved_type = types.boolType();
        }
        if (m == "add") {
            if (isConstObject())
                throwError("Não é possível chamar '.add()' em dict const. "
                           "Dicionários const não podem ser modificados.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'dict.add(pair)' requer exatamente 1 argumento", expr->token);
            TypeRef expected_pair = types.pair(K, V);
            TypeRef pair_type = analyzeExpr(expr->arguments[0].get(), expected_pair);
            if (!TypeChecker::isAssignable(expected_pair, pair_type))
                throwError("'dict.add': par '" + pair_type->str() +
                           "' incompatível com '" + expected_pair->str() + "'",
                           expr->token);
            coerceInPlace(expr->arguments[0], expected_pair);
            return expr->resolved_type = types.voidType();
        }
        if (m == "remove") {
            if (isConstObject())
                throwError("Não é possível chamar '.remove()' em dict const. "
                           "Dicionários const não podem ser modificados.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'dict.remove(key)' requer exatamente 1 argumento", expr->token);
            TypeRef key_type = analyzeExpr(expr->arguments[0].get());
            if (!TypeChecker::isAssignable(K, key_type))
                throwError("'dict.remove': chave '" + key_type->str() +
                           "' incompatível com K='" + K->str() + "'",
                           expr->token);
            coerceInPlace(expr->arguments[0], K);
            return expr->resolved_type = types.voidType();
        }
        if (m == "keys") {
            if (!expr->arguments.empty())
                throwError("'dict.keys()' não aceita argumentos", expr->token);
            return expr->resolved_type = types.list(K);
        }
        if (m == "values") {
            if (!expr->arguments.empty())
                throwError("'dict.values()' não aceita argumentos", expr->token);
            return expr->resolved_type = types.list(V);
        }
        throwError("Método '" + expr->method_name + "' não existe em 'dict<K,V>'",
                   expr->token);
    }

    // ── string built-ins ──────────────────────────────────────────────────
    if (obj_type->is(TK::String)) {
        if (expr->method_name == "size") {
            if (!expr->arguments.empty())
                throwError("'string.size()' não aceita argumentos", expr->token);
            return expr->resolved_type = types.intType();
        }
        throwError("Método '" + expr->method_name + "' não existe em 'string'",
                   expr->token);
    }

    // ── C4: erro não tem métodos ──────────────────────────────────────────
    if (obj_type->is(TK::Error)) {
        throwError("Erro não tem métodos ('" + expr->method_name + "' não existe); use os "
                   "campos kind, message, line e column.", expr->token);
    }

    // ── C3: struct não tem métodos ────────────────────────────────────────
    if (obj_type->is(TK::Struct)) {
        throwError("struct '" + obj_type->str() + "' não tem métodos ('" + expr->method_name +
                   "' não existe). Use 'class' se precisar de métodos.", expr->token);
    }

    // ── Método de classe definida pelo usuário ────────────────────────────
    auto it = obj_type->is(TK::Class) ? class_table.find(obj_type->name) : class_table.end();
    if (it == class_table.end()) {
        throwError("Tipo '" + obj_type->str() + "' não possui métodos (não é uma classe conhecida)",
                   expr->token);
    }

    const ClassInfo& cls_info = it->second;
    auto meth_it = cls_info.methods.find(expr->method_name);
    if (meth_it == cls_info.methods.end()) {
        throwError("Método '" + expr->method_name + "' não encontrado na classe '" +
                   obj_type->str() + "'", expr->token);
    }

    const MethodInfo& mi = meth_it->second;
    expr->target = mi.decl;

    // Acesso a método privado só dentro da própria classe
    if (!mi.is_public && current_class_name != obj_type->name) {
        throwError("Método '" + expr->method_name + "' é privado na classe '" +
                   obj_type->str() + "' e não pode ser chamado externamente", expr->token);
    }

    // Aridade
    if (expr->arguments.size() != mi.param_types.size()) {
        throwError(
            "Aridade incorreta em '" + obj_type->str() + "." + expr->method_name +
            "': esperados " + std::to_string(mi.param_types.size()) +
            " argumento(s), mas recebeu " +
            std::to_string(expr->arguments.size()),
            expr->token);
    }

    // Tipos dos argumentos
    for (size_t i = 0; i < expr->arguments.size(); ++i) {
        TypeRef arg_type = analyzeExpr(expr->arguments[i].get(), mi.param_types[i]);
        if (!TypeChecker::isAssignable(mi.param_types[i], arg_type)) {
            throwError(
                "Argumento " + std::to_string(i + 1) + " de '" +
                obj_type->str() + "." + expr->method_name + "': esperado '" +
                mi.param_types[i]->str() + "', mas recebeu '" + arg_type->str() + "'",
                expr->token);
        }
        coerceInPlace(expr->arguments[i], mi.param_types[i]);
    }

    return expr->resolved_type = mi.return_type;
}

// Acesso a membro:  objeto.campo
//   - pair<A,B>: .first → A, .second → B
//   - classe definida pelo usuário: verifica se o campo existe e é público
TypeRef SemanticAnalyzer::analyzeMemberAccess(MemberAccessExpr* expr) {
    TypeRef obj_type = analyzeExpr(expr->object.get());

    // enum: Cor.Verde (o objeto é o nome do enum, que chega como tipo)
    if (expr->object->node_kind == NodeKind::Identifier) {
        auto* id = static_cast<IdentifierExpr*>(expr->object.get());
        if (id->res.kind == Resolution::Kind::Type && id->type_value->is(TK::Enum)) {
            const EnumDecl* en = enum_table.at(id->type_value->name);
            const int i = en->indexOf(expr->member_name);
            if (i < 0) {
                std::string valores;
                for (const auto& m : en->members) valores += (valores.empty() ? "" : ", ") + m;
                throwError("O enum '" + en->name + "' não tem o valor '" + expr->member_name +
                           "' (valores: " + valores + ")", expr->token);
            }
            expr->enum_decl   = en;
            expr->field_index = i;
            return expr->resolved_type = id->type_value;
        }
    }

    if (obj_type->is(TK::Op))
        throwError(opNeedsNarrowing("O campo '" + expr->member_name + "'", obj_type), expr->token);
    if (obj_type->is(TK::Interface))
        throwError("A interface '" + obj_type->str() + "' só tem métodos: '" + expr->member_name +
                   "' não pode ser acessado por ela.", expr->token);

    // ── pair<A,B> ─────────────────────────────────────────────────────────
    if (obj_type->is(TK::Pair)) {
        if (expr->member_name == "first")  return expr->resolved_type = obj_type->first();
        if (expr->member_name == "second") return expr->resolved_type = obj_type->second();
        throwError("'pair<A,B>' possui apenas os campos 'first' e 'second', "
                   "não '" + expr->member_name + "'", expr->token);
    }

    // ── C4: erro — kind, message, line, column (somente leitura) ──────────
    if (obj_type->is(TK::Error)) {
        const std::string& m = expr->member_name;
        if (m == "kind" || m == "message") return expr->resolved_type = types.stringType();
        if (m == "line" || m == "column")  return expr->resolved_type = types.intType();
        throwError("Erro não possui o campo '" + m + "' (campos: kind, message, line, column)",
                   expr->token);
    }

    // ── C3: struct — todos os campos são públicos ─────────────────────────
    if (obj_type->is(TK::Struct)) {
        const auto& info = struct_table.at(obj_type->name);
        for (size_t i = 0; i < info.fields.size(); ++i)
            if (info.fields[i].first == expr->member_name) {
                expr->field_index = static_cast<int>(i);   // B2
                return expr->resolved_type = info.fields[i].second;
            }
        throwError("Campo '" + expr->member_name + "' não existe no struct '" +
                   obj_type->str() + "'", expr->token);
    }

    // ── Classe definida pelo usuário ──────────────────────────────────────
    auto it = obj_type->is(TK::Class) ? class_table.find(obj_type->name) : class_table.end();
    if (it == class_table.end()) {
        throwError("Tipo '" + obj_type->str() + "' não possui campos (não é uma classe conhecida)",
                   expr->token);
    }

    const ClassInfo& cls_info = it->second;
    auto field_it = cls_info.fields.find(expr->member_name);
    if (field_it == cls_info.fields.end()) {
        throwError("Campo '" + expr->member_name + "' não encontrado na classe '" +
                   obj_type->str() + "'", expr->token);
    }

    const FieldInfo& fi = field_it->second;

    // Acesso a campo privado só dentro da própria classe
    if (!fi.is_public && current_class_name != obj_type->name) {
        throwError("Campo '" + expr->member_name + "' é privado na classe '" +
                   obj_type->str() + "' e não pode ser acessado externamente", expr->token);
    }

    expr->field_index = fi.index;   // B2
    return expr->resolved_type = fi.type;
}

// Acesso por índice:  objeto[idx]
//   - list<T>[int]  → T
//   - dict<K,V>[K]  → V
TypeRef SemanticAnalyzer::analyzeIndexAccess(IndexAccessExpr* expr) {
    TypeRef obj_type = analyzeExpr(expr->object.get());
    TypeRef idx_type = analyzeExpr(expr->index.get());

    if (obj_type->is(TK::Op))
        throwError(opNeedsNarrowing("O operador '[]'", obj_type), expr->token);

    if (obj_type->is(TK::List)) {
        if (!idx_type->is(TK::Int)) {
            throwError("Índice de lista deve ser 'int', mas recebeu '" + idx_type->str() + "'",
                       expr->token);
        }
        // Índice negativo literal é erro semântico
        if (isNegativeIntLiteral(expr->index.get()))
            throwError("Índice negativo em lista não é permitido. "
                       "Índices de lista devem ser inteiros não-negativos.", expr->token);
        return expr->resolved_type = obj_type->elem();
    }

    if (obj_type->is(TK::Dict)) {
        TypeRef K = obj_type->key();
        if (!TypeChecker::isAssignable(K, idx_type)) {
            throwError("Chave do dicionário deve ser '" + K->str() +
                       "', mas recebeu '" + idx_type->str() + "'", expr->token);
        }
        coerceInPlace(expr->index, K);   // op<...>: chave conferida em runtime
        return expr->resolved_type = obj_type->value();
    }

    // s[i]: o caractere (string); índice negativo conta do fim
    if (obj_type->is(TK::String)) {
        if (!idx_type->is(TK::Int))
            throwError("Índice de string deve ser 'int', mas recebeu '" + idx_type->str() + "'",
                       expr->token);
        return expr->resolved_type = types.stringType();
    }

    throwError("Operador '[]' não é suportado no tipo '" + obj_type->str() + "'",
               expr->token);
}

// Formato depois de ':' num {expr:formato}: [largura][.casas f] — "8", ".2f", "8.2f"
static bool lerFormato(const std::string& f, int& largura, int& casas) {
    size_t i = 0;
    auto numero = [&](int& n, int maximo) {
        const size_t ini = i;
        n = 0;
        while (i < f.size() && f[i] >= '0' && f[i] <= '9') {
            n = n * 10 + (f[i++] - '0');
            if (n > maximo) return false;
        }
        return i > ini;
    };
    // largura começando com 0 ({n:05}) seria "zeros à esquerda" no Python: recusada
    if (i < f.size() && f[i] == '0') return false;
    if (i < f.size() && f[i] != '.' && !numero(largura, 1000)) return false;
    if (i < f.size() && f[i] == '.') {
        ++i;
        if (!numero(casas, 100) || i >= f.size() || f[i] != 'f') return false;
        ++i;
    }
    return i == f.size() && !f.empty();
}

// printf("Olá, {nome}!") / format(...): o texto literal vira trechos fixos e
// expressões {expr[:formato]}, cada uma analisada como qualquer expressão.
// {{ e }} escrevem chaves. printf imprime (como print, pulando linha);
// format devolve o texto.
TypeRef SemanticAnalyzer::analyzeInterpolation(CallExpr* expr) {
    const std::string nome = expr->function_name;
    const auto* lit = expr->arguments.size() == 1 && expr->arguments[0]->node_kind == NodeKind::Literal
                          ? static_cast<const LiteralExpr*>(expr->arguments[0].get()) : nullptr;
    if (!lit || !std::holds_alternative<std::string>(lit->value))
        throwError("'" + nome + "' recebe um único texto literal entre aspas, ex.: " + nome +
                   "(\"Olá, {nome}!\")", expr->token);
    const std::string& s = std::get<std::string>(lit->value);
    const Token& origem = lit->token;
    // os imports deste arquivo: {Math.abs(x)} precisa reconhecer 'Math' como módulo
    std::set<std::string> apelidos;
    if (current_module >= 0)
        for (const auto& [apelido, idx] : modules[static_cast<size_t>(current_module)].imports)
            apelidos.insert(apelido);

    std::string texto;
    auto fechaTexto = [&]() {
        if (texto.empty()) return;
        CallExpr::InterpPart p;
        p.text = std::move(texto);
        expr->interp.push_back(std::move(p));
        texto.clear();
    };
    for (size_t i = 0; i < s.size();) {
        const char c = s[i];
        if ((c == '{' || c == '}') && i + 1 < s.size() && s[i + 1] == c) { texto += c; i += 2; continue; }
        if (c == '}')
            throwError("'}' sem '{' no texto de '" + nome + "' (para escrever uma chave, use '}}')", origem);
        if (c != '{') { texto += c; ++i; continue; }

        // a '}' que fecha esta expressão: pula strings e o que está entre (), []
        // e {} internos — o ':' do formato é só o de fora deles (s[1:3] é fatia)
        size_t j = i + 1, dois_pontos = std::string::npos;
        for (int prof = 0; j < s.size(); ++j) {
            const char d = s[j];
            if (d == '"') {
                for (++j; j < s.size() && s[j] != '"'; ++j)
                    if (s[j] == '\\') ++j;
                continue;
            }
            if (d == '{' || d == '(' || d == '[') ++prof;
            else if (d == ')' || d == ']') { if (prof > 0) --prof; }
            else if (d == '}') { if (prof == 0) break; --prof; }
            else if (d == ':' && prof == 0) dois_pontos = j;
        }
        if (j >= s.size())
            throwError("'{' sem '}' no texto de '" + nome + "' (para escrever uma chave, use '{{')", origem);
        const size_t fim = dois_pontos == std::string::npos ? j : dois_pontos;
        const std::string codigo = s.substr(i + 1, fim - i - 1);
        if (codigo.find_first_not_of(" \t") == std::string::npos)
            throwError("'{}' vazio no texto de '" + nome + "': coloque uma expressão, ex.: {nome}", origem);

        CallExpr::InterpPart parte;
        try {
            parte.expr = Parser::parseEmbedded(codigo, origem, static_cast<int>(i + 2), apelidos);
        } catch (const ParseError& e) {
            throwError("Expressão inválida em '{" + codigo + "}' no texto de '" + nome + "': " + e.what(),
                       e.error_token);
        }
        if (dois_pontos != std::string::npos) {
            const std::string formato = s.substr(dois_pontos + 1, j - dois_pontos - 1);
            if (!lerFormato(formato, parte.width, parte.precision))
                throwError("Formato inválido '" + formato + "' em '{" + codigo + ":" + formato + "}'. Use a "
                           "largura ({x:8}), as casas decimais ({x:.2f}) ou as duas ({x:8.2f})", origem);
        }
        TypeRef t = analyzeExpr(parte.expr.get());
        if (t->is(TK::Void))
            throwError("'{" + codigo + "}' não tem valor (a expressão é 'void')", parte.expr->token);
        if (parte.precision >= 0 && !t->is(TK::Int) && !t->is(TK::Decimal))
            throwError("O formato de casas decimais só vale para int e decimal, mas '" + codigo + "' é '" +
                       t->str() + "'", parte.expr->token);
        fechaTexto();
        expr->interp.push_back(std::move(parte));
        i = j + 1;
    }
    fechaTexto();
    expr->interpolated = true;
    if (nome == "printf") {
        expr->native = findPrelude("print");
        return expr->resolved_type = types.voidType();
    }
    return expr->resolved_type = types.stringType();
}

// s[ini:fim:passo]: fatia de string; as partes presentes são int
TypeRef SemanticAnalyzer::analyzeSlice(SliceExpr* expr) {
    TypeRef obj_type = analyzeExpr(expr->object.get());
    if (obj_type->is(TK::Op))
        throwError(opNeedsNarrowing("A fatia '[:]'", obj_type), expr->token);
    if (!obj_type->is(TK::String)) {
        if (obj_type->is(TK::List))
            throwError("Fatia '[:]' só vale para string; para listas use Lists.slice(l, ini, fim)",
                       expr->token);
        throwError("Fatia '[:]' não é suportada no tipo '" + obj_type->str() + "'", expr->token);
    }
    const std::pair<const char*, Expr*> partes[] = {
        {"início", expr->start.get()}, {"fim", expr->end.get()}, {"passo", expr->step.get()}};
    for (const auto& [nome, e] : partes) {
        if (!e) continue;
        TypeRef t = analyzeExpr(e);
        if (!t->is(TK::Int))
            throwError(std::string("O ") + nome + " da fatia deve ser 'int', mas recebeu '" + t->str() + "'",
                       expr->token);
    }
    return expr->resolved_type = types.stringType();
}

// Instanciação de objeto:  new NomeClasse(args)
//   - Classe deve existir
//   - Aridade e tipos do construtor
TypeRef SemanticAnalyzer::analyzeNew(NewExpr* expr) {
    expr->class_name = qualifyName(expr->class_name, expr->token);   // C5
    if (interface_table.count(expr->class_name))
        throwError("A interface '" + stripModulePrefixes(expr->class_name) + "' não pode ser "
                   "instanciada: crie o objeto de uma classe que a cumpra.", expr->token);
    // C3: construtor automático de struct — nenhum argumento (valores padrão)
    // ou todos os campos, em ordem; número parcial é erro
    if (auto sit = struct_table.find(expr->class_name); sit != struct_table.end()) {
        const StructInfo& info = sit->second;
        const size_t n = expr->arguments.size();
        if (n != 0 && n != info.fields.size()) {
            throwError(
                "Construtor de '" + info.name + "': informe todos os " +
                std::to_string(info.fields.size()) + " campos, em ordem, ou nenhum (valores "
                "padrão), mas recebeu " + std::to_string(n) + " argumento(s)",
                expr->token);
        }
        for (size_t i = 0; i < n; ++i) {
            const auto& [nome, tipo] = info.fields[i];
            TypeRef at = analyzeExpr(expr->arguments[i].get(), tipo);
            if (!TypeChecker::isAssignable(tipo, at)) {
                throwError(
                    "Argumento " + std::to_string(i + 1) + " do construtor de '" + info.name +
                    "' (campo '" + nome + "'): esperado '" + tipo->str() +
                    "', mas recebeu '" + at->str() + "'",
                    expr->token);
            }
            coerceInPlace(expr->arguments[i], tipo);
        }
        return expr->resolved_type = types.structType(info.name);
    }

    auto it = class_table.find(expr->class_name);

    // checando se a classe existe
    if (it == class_table.end()) {
        throwError("Classe '" + expr->class_name + "' não declarada", expr->token);
    }

    const ClassInfo& cls = it->second;

    // checando aridade
    if (expr->arguments.size() != cls.ctor_param_types.size()) {
        throwError(
            "Construtor de '" + expr->class_name +
            "': esperados " + std::to_string(cls.ctor_param_types.size()) +
            " argumento(s), mas recebeu " +
            std::to_string(expr->arguments.size()),
            expr->token);
    }

    // checando a compatibilidade dos argumentos
    for (size_t i = 0; i < expr->arguments.size(); ++i) {
        TypeRef arg_type = analyzeExpr(expr->arguments[i].get(), cls.ctor_param_types[i]);
        if (!TypeChecker::isAssignable(cls.ctor_param_types[i], arg_type)) {
            throwError(
                "Argumento " + std::to_string(i + 1) + " do construtor de '" +
                expr->class_name + "': esperado '" + cls.ctor_param_types[i]->str() +
                "', mas recebeu '" + arg_type->str() + "'",
                expr->token);
        }
        coerceInPlace(expr->arguments[i], cls.ctor_param_types[i]);
    }

    // o tipo da expressão NewExpr (instanciação) é a própria classe
    return expr->resolved_type = types.classType(expr->class_name);
}

// Literal de lista:  [e1, e2, e3]
// v2.00 #20: Tipo inferido pelo conjunto inteiro, não só pelo primeiro elemento
// v2.00 #19: Promoção numérica centralizada
// A3: com tipo esperado list<T>, cada elemento é checado contra T e o literal
//     assume o tipo esperado (`list<decimal> l = [1, 2];` continua válido).
//     Sem tipo esperado, ou se algum elemento não couber, o tipo é inferido
//     pelo conteúdo e o chamador reporta a incompatibilidade.
TypeRef SemanticAnalyzer::analyzeListLiteral(ListLiteralExpr* expr, TypeRef expected) {
    TypeRef T = (expected && expected->is(TK::List)) ? expected->elem() : nullptr;

    if (expr->elements.empty()) {
        return expr->resolved_type = T ? expected : types.list(types.varType());
    }

    std::vector<TypeRef> elem_types;
    elem_types.reserve(expr->elements.size());
    for (auto& elem : expr->elements)
        elem_types.push_back(analyzeExpr(elem.get(), T));

    if (T) {
        bool fits = std::all_of(elem_types.begin(), elem_types.end(),
                                [&](TypeRef t) { return TypeChecker::isAssignable(T, t); });
        if (fits) {
            for (auto& elem : expr->elements) coerceInPlace(elem, T);
            return expr->resolved_type = expected;
        }
    }

    TypeRef unified = elem_types[0];

    for (size_t i = 1; i < elem_types.size(); ++i) {
        TypeRef t = elem_types[i];

        // Promoção numérica
        if (unified->isNumeric() && t->isNumeric()) {
            unified = TypeChecker::promoteNumeric(unified, t);
        }
        // Mesmo tipo, ou parâmetro ainda não resolvido de um lado (ex.: [[1], []])
        else if (TypeChecker::isAssignable(unified, t)) {
            // nada
        }
        else if (TypeChecker::isAssignable(t, unified)) {
            unified = t;
        }
        else {
            throwError(
                "Literal de lista com tipos incompatíveis: elemento " +
                std::to_string(i + 1) + " tem tipo '" + t->str() +
                "', incompatível com '" + unified->str() + "'",
                expr->token);
        }
    }

    // A2: [1, 2.5] vira list<decimal> com o 1 convertido de fato
    for (auto& elem : expr->elements) coerceInPlace(elem, unified);

    return expr->resolved_type = types.list(unified);
}

// Literal de dicionário:  {{k1,v1}, {k2,v2}}
// v2.00 #20: Tipo inferido pelo conjunto inteiro
// v2.00 #14: Chaves duplicadas → erro semântico
// v2.00 #13: Chaves restritas a primitivos comparáveis
// A3: com tipo esperado dict<K,V>, chaves e valores são checados contra K e V
//     e o literal assume o tipo esperado (mesma regra do literal de lista).
TypeRef SemanticAnalyzer::analyzeDictLiteral(DictLiteralExpr* expr, TypeRef expected) {
    TypeRef K = nullptr, V = nullptr;
    if (expected && expected->is(TK::Dict)) { K = expected->key(); V = expected->value(); }

    if (expr->pairs.empty()) {
        return expr->resolved_type =
            K ? expected : types.dict(types.varType(), types.varType());
    }

    // Texto de uma chave literal, para detectar duplicatas
    auto literalKey = [](const LiteralExpr* lit) -> std::string {
        if (std::holds_alternative<std::int64_t>(lit->value))
            return std::to_string(std::get<std::int64_t>(lit->value));
        if (std::holds_alternative<std::string>(lit->value))
            return std::get<std::string>(lit->value);
        if (std::holds_alternative<bool>(lit->value))
            return std::get<bool>(lit->value) ? "true" : "false";
        return std::to_string(std::get<double>(lit->value));
    };

    std::vector<TypeRef> key_types, val_types;
    std::map<std::string, size_t> seen_keys;

    for (size_t i = 0; i < expr->pairs.size(); ++i) {
        TypeRef kt = analyzeExpr(expr->pairs[i].first.get(),  K);
        TypeRef vt = analyzeExpr(expr->pairs[i].second.get(), V);

        // v2.00 #13 / A10: chaves só int, string, bool ou enum
        if (!kt->is(TK::Int) && !kt->is(TK::String) && !kt->is(TK::Bool) && !kt->is(TK::Enum)) {
            throwError("Chave " + std::to_string(i + 1) + " do literal: " + dictKeyMessage(kt),
                       expr->token);
        }

        // v2.00 #14: chaves duplicadas
        if (expr->pairs[i].first->node_kind == NodeKind::Literal) {
            std::string k = literalKey(static_cast<const LiteralExpr*>(expr->pairs[i].first.get()));
            if (seen_keys.count(k)) {
                throwError(
                    "Chave duplicada '" + k + "' no literal de dicionário. "
                    "Cada chave deve aparecer exatamente uma vez.",
                    expr->pairs[i].first->token);
            }
            seen_keys[k] = i;
        }
        // enum: Cor.Verde repetido também é chave duplicada
        if (expr->pairs[i].first->node_kind == NodeKind::MemberAccess) {
            auto* m = static_cast<const MemberAccessExpr*>(expr->pairs[i].first.get());
            if (m->enum_decl) {
                std::string k = displayName(m->enum_decl->name) + "." + m->member_name;
                if (seen_keys.count(k))
                    throwError("Chave duplicada '" + k + "' no literal de dicionário. "
                               "Cada chave deve aparecer exatamente uma vez.", m->token);
                seen_keys[k] = i;
            }
        }

        key_types.push_back(kt);
        val_types.push_back(vt);
    }

    if (K) {
        bool fits = true;
        for (size_t i = 0; i < expr->pairs.size() && fits; ++i)
            fits = TypeChecker::isAssignable(K, key_types[i]) &&
                   TypeChecker::isAssignable(V, val_types[i]);
        if (fits) {
            for (auto& [key, val] : expr->pairs) {
                coerceInPlace(key, K);
                coerceInPlace(val, V);
            }
            return expr->resolved_type = expected;
        }
    }

    TypeRef key_type = key_types[0];
    TypeRef val_type = val_types[0];

    for (size_t i = 1; i < expr->pairs.size(); ++i) {
        TypeRef kt = key_types[i];
        TypeRef vt = val_types[i];

        // v2.00 #20: unificação do tipo da chave
        if (key_type->isNumeric() && kt->isNumeric()) {
            key_type = TypeChecker::promoteNumeric(key_type, kt);
        } else if (key_type != kt && !TypeChecker::isAssignable(key_type, kt)) {
            throwError("Chave " + std::to_string(i + 1) + " do dict tem tipo '" + kt->str() +
                       "', mas esperava-se '" + key_type->str() + "'", expr->token);
        }

        // v2.00 #20: unificação do tipo do valor
        if (val_type->isNumeric() && vt->isNumeric()) {
            val_type = TypeChecker::promoteNumeric(val_type, vt);
        } else if (val_type != vt && !TypeChecker::isAssignable(val_type, vt)) {
            throwError("Valor " + std::to_string(i + 1) + " do dict tem tipo '" + vt->str() +
                       "', mas esperava-se '" + val_type->str() + "'", expr->token);
        }
    }

    // A2: chaves e valores int convertidos quando o tipo unificado é decimal
    for (auto& [key, val] : expr->pairs) {
        coerceInPlace(key, key_type);
        coerceInPlace(val, val_type);
    }

    return expr->resolved_type = types.dict(key_type, val_type);
}

// Literal de par:  {first, second}
//   - Retorna pair<A, B>
//   - A3: com tipo esperado pair<A,B>, assume o tipo esperado se couber
TypeRef SemanticAnalyzer::analyzePairLiteral(PairLiteralExpr* expr, TypeRef expected) {
    TypeRef A = nullptr, B = nullptr;
    if (expected && expected->is(TK::Pair)) { A = expected->first(); B = expected->second(); }

    TypeRef first_type  = analyzeExpr(expr->first.get(),  A);
    TypeRef second_type = analyzeExpr(expr->second.get(), B);

    if (A &&
        TypeChecker::isAssignable(A, first_type) &&
        TypeChecker::isAssignable(B, second_type)) {
        coerceInPlace(expr->first,  A);
        coerceInPlace(expr->second, B);
        return expr->resolved_type = expected;
    }

    return expr->resolved_type = types.pair(first_type, second_type);
}

// ============================================================================
// COERÇÃO IMPLÍCITA (A2)
//
// isAssignable() só diz que int cabe em decimal; sem materializar a
// conversão o valor chega ao executor como INT (ex.: `decimal d = 5;
// d / 2` virava divisão inteira). Aqui a conversão vira um nó CastExpr.
// ============================================================================

void SemanticAnalyzer::coerceInPlace(ExprPtr& slot, TypeRef target) {
    if (!slot || !target || slot->resolved_type == target) return;

    // op<...>: travar. Valor de tipo conhecido: fica num dos tipos concretos do
    // destino (o próprio, ou int → decimal). Valor de tipo travado desconhecido:
    // se todos os seus tipos já estão no destino, nada a fazer; senão a
    // conversão int → decimal acontece em runtime (CastExpr, sem erro possível:
    // o semântico já garantiu que todos cabem)
    TypeRef origem = slot->resolved_type;
    if (TypeChecker::containsOp(origem) || TypeChecker::containsOp(target)) {
        const auto possiveis = TypeChecker::expand(target);
        auto esta = [&](TypeRef t) {
            return std::find(possiveis.begin(), possiveis.end(), t) != possiveis.end();
        };
        if (!TypeChecker::containsOp(origem)) {
            if (esta(origem)) return;
            for (TypeRef c : possiveis)
                if (TypeChecker::isAssignable(c, origem)) { coerceInPlace(slot, c); return; }
            return;
        }
        const auto origens = TypeChecker::expand(origem);
        if (std::all_of(origens.begin(), origens.end(), esta)) return;
        slot = std::make_unique<CastExpr>(std::move(slot), target);
        return;
    }

    if (target->is(TK::Decimal) && slot->resolved_type->is(TK::Int)) {
        slot = std::make_unique<CastExpr>(std::move(slot), target);
        return;
    }

    switch (slot->node_kind) {
        case NodeKind::ListLiteral: {
            if (!target->is(TK::List)) return;
            auto* list = static_cast<ListLiteralExpr*>(slot.get());
            for (auto& elem : list->elements) coerceInPlace(elem, target->elem());
            list->resolved_type = target;
            return;
        }
        case NodeKind::DictLiteral: {
            if (!target->is(TK::Dict)) return;
            auto* dict = static_cast<DictLiteralExpr*>(slot.get());
            for (auto& [key, val] : dict->pairs) {
                coerceInPlace(key, target->key());
                coerceInPlace(val, target->value());
            }
            dict->resolved_type = target;
            return;
        }
        case NodeKind::PairLiteral: {
            if (!target->is(TK::Pair)) return;
            auto* pair = static_cast<PairLiteralExpr*>(slot.get());
            coerceInPlace(pair->first,  target->first());
            coerceInPlace(pair->second, target->second());
            pair->resolved_type = target;
            return;
        }
        default:
            // Demais casos (ex.: variável list<int> em list<decimal>) não têm
            // conversão possível sem copiar a coleção; A3 passa a rejeitá-los.
            return;
    }
}

// ============================================================================
// VERIFICAÇÃO DE FLUXO DE RETORNO
// ============================================================================

// Retorna true se TODOS os caminhos do stmt terminam com ReturnStmt.
// Usado para verificar funções não-void.
// Revisão: há um break que sai deste laço? (os de laços internos não contam)
static bool hasBreak(const Stmt* s) {
    if (!s) return false;
    switch (s->node_kind) {
        case NodeKind::Break: return true;
        case NodeKind::While: case NodeKind::For: return false;   // break de outro laço
        case NodeKind::Block:
            for (const auto& x : static_cast<const BlockStmt*>(s)->statements)
                if (hasBreak(x.get())) return true;
            return false;
        case NodeKind::If: {
            auto* i = static_cast<const IfStmt*>(s);
            return hasBreak(i->then_branch.get()) || hasBreak(i->else_branch.get());
        }
        case NodeKind::Try: {
            auto* t = static_cast<const TryStmt*>(s);
            if (hasBreak(t->body.get())) return true;
            for (const auto& h : t->handlers) if (hasBreak(h.body.get())) return true;
            return false;   // break no finally é proibido
        }
        default: return false;
    }
}

bool SemanticAnalyzer::allPathsReturn(const Stmt* stmt) const {
    if (!stmt) return false;
    if (isExitStmt(stmt)) return true;   // exit(...) encerra o programa

    switch (stmt->node_kind) {
        // return expr; → garante retorno neste caminho
        case NodeKind::Return:
            return true;

        // C4: throw também encerra o caminho
        case NodeKind::Throw:
            return true;

        // C4: try garante retorno se o bloco e todos os except garantem
        case NodeKind::Try: {
            auto* t = static_cast<const TryStmt*>(stmt);
            if (!allPathsReturn(t->body.get())) return false;
            for (const auto& h : t->handlers)
                if (!allPathsReturn(h.body.get())) return false;
            return true;
        }

        // Block → basta que qualquer statement no bloco garanta retorno
        // (tudo após o return é código morto, não verificamos)
        case NodeKind::Block:
            for (const auto& s : static_cast<const BlockStmt*>(stmt)->statements)
                if (allPathsReturn(s.get())) return true;
            return false;

        // if-else → ambos os ramos devem garantir retorno (e else deve existir)
        case NodeKind::If: {
            auto* if_stmt = static_cast<const IfStmt*>(stmt);
            if (!if_stmt->else_branch) return false;
            return allPathsReturn(if_stmt->then_branch.get()) &&
                   allPathsReturn(if_stmt->else_branch.get());
        }

        // Revisão: while (true) sem break que o encerre nunca termina normalmente
        // (antes `while (true) { return 1; }` dava "não retorna em todos os caminhos")
        case NodeKind::While: {
            auto* w = static_cast<const WhileStmt*>(stmt);
            if (w->condition->node_kind != NodeKind::Literal) return false;
            auto* lit = static_cast<const LiteralExpr*>(w->condition.get());
            if (!std::holds_alternative<bool>(lit->value) || !std::get<bool>(lit->value)) return false;
            return !hasBreak(w->body.get());
        }

        // for: conservativamente não garante retorno (pode não executar)
        default:
            return false;
    }
}

} // namespace cinza
