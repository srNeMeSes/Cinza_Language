#include "semantic.h"
#include "parser.h"          // printf/format: as expressões dentro do texto
#include "runtime_error.h"   // C4: builtinErrorKinds
#include "operacoes.h"       // Q1f: as contas dos const, as mesmas da execução
#include "utf8.h"            // Q7: as colunas contam caracteres
#include <sstream>
#include <limits>
#include <algorithm>
#include <functional>
#include <set>
#include <unordered_set>

namespace cinza {

using TK = TypeInfo::Kind;

namespace {
// N2 (definida na seção das funções genéricas, no fim do arquivo)
void trocaParametros(const std::vector<Token>& params, const std::vector<TypeRef>& tipos, bool marca,
                     const std::vector<Token>& in, size_t inicio, std::vector<Token>& out);
}  // namespace

// ============================================================================
// TYPE CHECKER
// B1: opera sobre TypeRef (tipos únicos do TypeContext); igualdade de tipos
// é comparação de ponteiros e os parâmetros são lidos direto de `params`.
// ============================================================================

// Seção K: regra numérica central — única fonte de verdade.
// Duas famílias: inteiros (i32, i64) e ponto flutuante (f32, f64). Na mesma
// família o resultado é o tipo mais largo (o mais estreito é promovido antes
// da conta); famílias diferentes não se misturam (nullptr): cast<T> explícito.
TypeRef TypeChecker::numericResult(TypeRef l, TypeRef r) {
    auto& types = TypeContext::instance();
    if (l->isInteger() && r->isInteger())
        return (l->is(TK::I64) || r->is(TK::I64)) ? types.i64Type() : types.i32Type();
    if (l->isFloat() && r->isFloat())
        return (l->is(TK::F64) || r->is(TK::F64)) ? types.f64Type() : types.f32Type();
    return nullptr;
}

// Promoção implícita permitida: só alargar dentro da família (i32 → i64, f32 → f64)
bool TypeChecker::widens(TypeRef to, TypeRef from) {
    return (to->is(TK::I64) && from->is(TK::I32)) || (to->is(TK::F64) && from->is(TK::F32));
}

// Dica para um valor que o cast<T> converteria (i32 onde se espera f64, i64
// onde se espera i32, número onde se espera string e vice-versa): a conversão
// é explícita — a mesma sugestão das operações entre famílias
// Q1b: a dica quando um T? foi usado como T (método, campo, índice, for, condição)
static std::string dicaOpcional(TypeRef t) {
    return t->is(TK::Optional) ? ": o valor é opcional; resolva a ausência antes, com else (ex.: valor "
                                 "else padrão) ou com if"
                               : "";
}

static std::string dicaCast(TypeRef esperado, TypeRef recebido) {
    // Q1b: um T? onde se espera T, ou null onde não cabe
    if (recebido->is(TK::Optional) && !esperado->is(TK::Optional))
        return ". O valor é opcional ('" + recebido->str() + "'): resolva a ausência antes, com else "
               "(ex.: valor else padrão) ou com if";
    if (recebido->is(TK::Null) && !esperado->is(TK::Optional))
        return ". null só cabe num tipo opcional: declare '" + esperado->str() + "?'";
    // Q1e
    if (recebido->is(TK::Void))
        return ". A expressão é 'void' e não tem valor (a função não retorna nada)";
    if (esperado->is(TK::Interface) && recebido->is(TK::Class))
        return ". A classe '" + recebido->str() + "' não declara cumprir '" + esperado->str() +
               "' (class " + recebido->str() + " : " + esperado->str() + ")";
    if (esperado->is(TK::Class) && recebido->is(TK::Interface))
        return ". Um valor do tipo interface não volta a ser a classe: use os métodos de '" +
               recebido->str() + "'";
    auto conversivel = [](TypeRef t) { return t->isNumeric() || t->is(TK::String); };
    if (!conversivel(esperado) || !conversivel(recebido) || esperado == recebido) return "";
    return ". Converta com cast<" + esperado->str() + ">(valor)";
}

// Q1e: uma expressão void não é um valor (spec 3.1): não vira elemento de um literal
static void exigeValor(TypeRef t, const std::string& onde, const Token& tok) {
    if (t->is(TK::Void))
        throw SemanticError(onde + ": a expressão é 'void' e não tem valor (a função não retorna nada)", tok);
}

// Mensagem de operação entre famílias diferentes (i32 + f64)
static std::string familiasMessage(const std::string& op_text, TypeRef l, TypeRef r) {
    TypeRef f = l->isFloat() ? l : r;
    return "Operador '" + op_text + "' entre '" + l->str() + "' e '" + r->str() + "': inteiro e "
           "ponto flutuante não se misturam. Converta um dos lados com cast<T>(valor), ex.: cast<" +
           f->str() + ">(n)";
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
        case Type::Kind::I32:     return types.i32Type();
        case Type::Kind::I64:     return types.i64Type();
        case Type::Kind::F32:     return types.f32Type();
        case Type::Kind::F64:     return types.f64Type();
        case Type::Kind::STRING:  return types.stringType();
        case Type::Kind::BOOL:    return types.boolType();
        case Type::Kind::VOID:    return types.voidType();
        case Type::Kind::VAR:     return types.varType();
        case Type::Kind::LIST:    return types.list(param(0));
        case Type::Kind::DICT:    return types.dict(param(0), param(1));
        case Type::Kind::PAIR:    return types.pair(param(0), param(1));
        case Type::Kind::CUSTOM:  return types.named(type->name);   // class ou struct (C3)
        case Type::Kind::OPTIONAL: {   // Q1b
            TypeRef t = param(0);
            if (t->is(TK::Optional))   // numa cópia de genérico: T? com T = i32?
                throw SemanticError("'" + t->str() + "?' não existe: '" + t->str() + "' já é opcional, e "
                                    "não há opcional de opcional", type->token);
            if (t->is(TK::Void) || t->is(TK::Var))
                throw SemanticError("'" + t->str() + "?' não existe: só um tipo de valor pode ser "
                                    "opcional", type->token);
            return types.optional(t);
        }
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
//   - "var" declarado → aceita qualquer PRIMITIVO (números, string, bool)
//   - i32 → i64 e f32 → f64 (alargar na família; seção K)
//   - A3: list/dict/pair são invariantes (list<i32> NÃO cabe em list<i64>);
//     literais se adaptam ao tipo esperado em analyzeExpr(expr, expected)
bool TypeChecker::isAssignable(TypeRef declared, TypeRef value) {
    if (declared == value) return true;

    // Q1b: T? aceita null, um T (que vira T?) e outro opcional do mesmo tipo (ou de uma classe
    // que cumpre a interface); um T? ou null nunca cabe num T
    if (declared->is(TK::Optional)) {
        if (value->is(TK::Null)) return true;
        if (value->is(TK::Optional))
            return declared->elem()->is(TK::Interface) && value->elem()->is(TK::Class) &&
                   isAssignable(declared->elem(), value->elem());
        return isAssignable(declared->elem(), value);
    }
    if (value->is(TK::Optional) || value->is(TK::Null)) return false;

    // interface: aceita objeto de classe que a cumpre (nunca o contrário)
    if (declared->is(TK::Interface) && value->is(TK::Class))
        return TypeContext::instance().implements(value->name, declared->name);

    // v2.00 #2: var só aceita primitivos — não aceita list/dict/pair/class
    if (declared->is(TK::Var)) return value->isPrimitive();
    if (value->is(TK::Var))    return true;   // valor var primitivo ainda não resolvido

    // Seção K: só alargar dentro da família
    if (widens(declared, value)) return true;

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

    // Q1e: uma expressão void não é um valor (spec 3.1) — nem para comparar (f() == f())
    if (left->is(TK::Void) || right->is(TK::Void))
        throw SemanticError("Operador '" + op_text + "': o lado " +
                            (left->is(TK::Void) ? "esquerdo" : "direito") +
                            " é 'void' e não tem valor (a função não retorna nada)", tok);

    // Q1b: num opcional só valem == e != (com null, com outro do mesmo tipo ou com um valor do
    // tipo dele); o resto pede a ausência resolvida antes
    auto ausencia = [](TypeRef t) { return t->is(TK::Optional) || t->is(TK::Null); };
    if (ausencia(left) || ausencia(right)) {
        if (op != TokenType::OP_EQUAL && op != TokenType::OP_NOT_EQUAL) {
            TypeRef o = ausencia(left) ? left : right;
            throw SemanticError("Operador '" + op_text + "' com " +
                                (o->is(TK::Null) ? std::string("null") : "um valor opcional ('" + o->str() + "')") +
                                ": resolva a ausência antes, com else (ex.: x else 0) ou com if", tok);
        }
        const std::string cmp = tokenTypeToOperatorString(op);
        if (left->is(TK::Null) && right->is(TK::Null)) return types.boolType();
        if (left->is(TK::Null) || right->is(TK::Null)) {
            TypeRef outro = left->is(TK::Null) ? right : left;
            if (outro->is(TK::Optional)) return types.boolType();
            throw SemanticError("Comparação '" + cmp + "' de '" + outro->str() + "' com null: um '" +
                                outro->str() + "' nunca é null (só um opcional, '" + outro->str() + "?', é)",
                                tok);
        }
        TypeRef a = left->is(TK::Optional) ? left->elem() : left;
        TypeRef b = right->is(TK::Optional) ? right->elem() : right;
        // dois opcionais: o mesmo tipo; um opcional e um valor: o valor cabe no tipo dele
        const bool ok = (left->is(TK::Optional) && right->is(TK::Optional))
                            ? a == b
                            : (left->is(TK::Optional) ? isAssignable(a, b) : isAssignable(b, a));
        if (ok) return types.boolType();
        throw SemanticError("Comparação '" + cmp + "' não é suportada entre '" + l + "' e '" + r + "'", tok);
    }

    switch (op) {

        // ── Operadores aritméticos ───────────────────────────────────────
        case TokenType::OP_PLUS:
            if (left->isNumeric() && right->isNumeric()) {
                if (TypeRef t = numericResult(left, right)) return t;
                throw SemanticError(familiasMessage(op_text, left, right), tok);
            }
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
            if (left->isNumeric() && right->isNumeric()) {
                TypeRef t = numericResult(left, right);
                if (!t) throw SemanticError(familiasMessage(op_text, left, right), tok);
                // resto só existe entre inteiros (seção K)
                if (op == TokenType::OP_MODULO && !t->isInteger())
                    throw SemanticError(
                        "Operador '" + op_text + "' só existe entre inteiros (i32, i64), mas recebeu '" +
                        l + "' e '" + r + "'", tok);
                return t;
            }
            throw SemanticError(
                "Operador '" + op_text +
                "' requer tipos numéricos, mas recebeu '" + l + "' e '" + r + "'", tok);

        // ── Comparadores de ordem ────────────────────────────────────────
        case TokenType::OP_LESS:
        case TokenType::OP_LESS_EQUAL:
        case TokenType::OP_GREATER:
        case TokenType::OP_GREATER_EQUAL:
            if (left->isNumeric() && right->isNumeric()) {
                if (numericResult(left, right)) return types.boolType();
                throw SemanticError(familiasMessage(tokenTypeToOperatorString(op), left, right), tok);
            }
            if (left->is(TK::String) && right->is(TK::String)) return types.boolType();
            if (left->is(TK::Enum) && right->is(TK::Enum))   // Q1e
                throw SemanticError("Comparação '" + tokenTypeToOperatorString(op) + "' entre valores de "
                                    "enum: um enum só tem == e != (os valores não têm ordem nem número)", tok);
            throw SemanticError(
                "Comparação '" + tokenTypeToOperatorString(op) +
                "' não é suportada entre '" + l + "' e '" + r + "'", tok);

        // ── Igualdade ────────────────────────────────────────────────────
        case TokenType::OP_EQUAL:
        case TokenType::OP_NOT_EQUAL:
            if (left == right)                           return types.boolType();
            if (left->isNumeric() && right->isNumeric()) {
                if (numericResult(left, right)) return types.boolType();
                throw SemanticError(familiasMessage(tokenTypeToOperatorString(op), left, right), tok);
            }
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
    if (operand->is(TK::Optional) || operand->is(TK::Null))   // Q1b
        throw SemanticError("Operador '" + tok.lexeme + "' com " +
                            (operand->is(TK::Null) ? std::string("null")
                                                   : "um valor opcional ('" + operand->str() + "')") +
                            ": resolva a ausência antes, com else (ex.: x else 0) ou com if", tok);
    switch (op) {
        case TokenType::OP_MINUS:
            if (operand->isNumeric()) return operand;
            throw SemanticError(
                "Operador '-' unário requer tipo numérico, mas recebeu '" + operand->str() + "'", tok);

        case TokenType::OP_NOT:
            if (operand->is(TK::Bool)) return operand;
            // Q7: not a == 1 é (not a) == 1 — o not pega só o que vem logo depois
            throw SemanticError(
                "Operador '" + tok.lexeme + "' requer 'bool', mas recebeu '" + operand->str() + "'. O '" +
                tok.lexeme + "' se aplica só ao que vem logo depois: '" + tok.lexeme + " a == 1' é '(" +
                tok.lexeme + " a) == 1'; para negar a comparação, use parênteses ('" + tok.lexeme +
                " (a == 1)') ou '!='", tok);

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
    if (auto it = current.find(sym.name); it != current.end()) {   // já existe neste escopo
        // Q1e: onde está o primeiro; no nível superior, a lembrança de que não há sobrecarga
        const Symbol& antes = it->second;
        std::string onde;
        if (antes.line > 0)
            onde = " (na linha " + std::to_string(antes.line) + ")";
        else if (const auto& k = builtinErrorKinds(); std::find(k.begin(), k.end(), sym.name) != k.end())
            onde = ": é um tipo de erro embutido (capítulo 7 da spec)";
        if (scopes.size() == 1 && antes.line > 0)
            onde += ": não há sobrecarga, e dois itens do nível superior não têm o mesmo nome";
        throw SemanticError("Declaração duplicada: '" + sym.name + "' já foi declarado neste escopo" + onde,
                            tok);
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
    // C3: nomes de struct antes de tudo, para que `Ponto p` vire tipo struct
    // (e não class) em qualquer assinatura registrada a seguir
    // N5: um struct genérico não é um tipo sozinho (só Par[i32, string] é): fica guardado
    // para as cópias, já aqui, porque uma assinatura registrada a seguir pode usá-lo
    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::StructDecl) {
            auto* st = static_cast<const StructDecl*>(stmt.get());
            if (st->type_params.empty()) types.declareStruct(st->name);
            else structs_genericos[st->name] = StructGenerico{st, current_module};
        } else if (stmt->node_kind == NodeKind::ClassDecl) {   // Q1a: class Pilha[T]
            auto* c = static_cast<const ClassDecl*>(stmt.get());
            if (!c->type_params.empty()) classes_genericas[c->class_name] = ClasseGenerica{c, current_module};
        }

    // enum: também antes de tudo, para `Cor c` virar tipo enum em qualquer assinatura
    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::EnumDecl)
            registerEnum(*static_cast<const EnumDecl*>(stmt.get()));

    // interface: os nomes antes de tudo (uma assinatura pode citar outra
    // interface declarada mais abaixo); as assinaturas depois
    for (const auto& stmt : program.statements)
        if (stmt->node_kind == NodeKind::InterfaceDecl) {
            auto* in = static_cast<const InterfaceDecl*>(stmt.get());
            if (in->type_params.empty()) types.declareInterface(in->name);
            else interfaces_genericas[in->name] = InterfaceGenerica{in, current_module};   // Q1a
        }

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
void SemanticAnalyzer::registerStruct(const StructDecl& st, bool simbolo) {
    if (!st.type_params.empty()) {   // N5: genérico — só o nome; os campos vêm em cada cópia
        Symbol sym;
        sym.name          = st.name;
        sym.kind          = Symbol::Kind::CLASS;
        sym.resolved_type = types.structType(st.name);
        sym.line          = st.token.line;
        sym.column        = st.token.column;
        sym.module        = current_module;
        symbol_table.declare(sym, st.token);
        return;
    }
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
    if (!simbolo) return;

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
void SemanticAnalyzer::registerInterface(const InterfaceDecl& in, bool simbolo) {
    if (!in.type_params.empty()) {   // Q1a: genérica — só o nome; as assinaturas vêm em cada cópia
        Symbol sym;
        sym.name          = in.name;
        sym.kind          = Symbol::Kind::CLASS;
        sym.resolved_type = types.interfaceType(in.name);
        sym.line          = in.token.line;
        sym.column        = in.token.column;
        sym.module        = current_module;
        symbol_table.declare(sym, in.token);
        return;
    }
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
    if (!simbolo) return;   // Q1a: a cópia de uma genérica

    Symbol sym;
    sym.name          = in.name;
    sym.kind          = Symbol::Kind::CLASS;   // nome de tipo
    sym.resolved_type = types.interfaceType(in.name);
    sym.line          = in.token.line;
    sym.column        = in.token.column;
    sym.module        = current_module;
    symbol_table.declare(sym, in.token);
}

// Assinatura de um método para mensagens: area() -> f64
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
    // um conjunto (comparar cada valor com todos os anteriores era quadrático: um enum de
    // 100 mil valores levava 20 s)
    std::set<std::string> vistos;
    for (size_t i = 0; i < en.members.size(); ++i)
        if (!vistos.insert(en.members[i]).second)
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
           interface_table.count(name) || structs_genericos.count(name) ||
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
        if (t->is(TK::Pair)) {   // pair guarda os valores dentro
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
    // N2: função genérica — só o nome; a assinatura concreta nasce em cada cópia
    if (!fn.type_params.empty()) {
        Symbol sym;
        sym.name          = fn.name;
        sym.kind          = Symbol::Kind::FUNCTION;
        sym.return_type   = types.voidType();
        sym.resolved_type = sym.return_type;
        sym.line          = fn.token.line;
        sym.column        = fn.token.column;
        sym.module        = current_module;
        sym.fn_decl       = &fn;
        symbol_table.declare(sym, fn.token);
        return;
    }
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
void SemanticAnalyzer::registerClass(const ClassDecl& cls, bool simbolo) {
    if (!cls.type_params.empty()) {   // Q1a: genérica — só o nome; o resto vem em cada cópia
        Symbol sym;
        sym.name          = cls.class_name;
        sym.kind          = Symbol::Kind::CLASS;
        sym.resolved_type = types.classType(cls.class_name);
        sym.line          = cls.token.line;
        sym.column        = cls.token.column;
        sym.module        = current_module;
        symbol_table.declare(sym, cls.token);
        return;
    }
    ClassInfo info;
    info.name = cls.class_name;
    info.line = cls.token.line;

    // class X : Forma — a relação vale já no pré-registro (para qualquer função
    // aceitar X onde se pede Forma); a conferência dos métodos vem na análise
    for (size_t k = 0; k < cls.interfaces.size(); ++k) {
        std::string nome = qualifyName(cls.interfaces[k], cls.interface_tokens[k]);
        // Q1a: Comparavel[Nota] vira a cópia
        const bool com_tipos = k < cls.interface_args.size() && !cls.interface_args[k].empty();
        if (auto g = interfaces_genericas.find(nome); g != interfaces_genericas.end()) {
            static const std::vector<TypePtr> sem_tipos;
            nome = usoInterfaceGenerica(g->second, com_tipos ? cls.interface_args[k] : sem_tipos,
                                        cls.interface_tokens[k])->name;
            for (const auto& ja : info.interfaces)
                if (origem_da_iface.count(ja) && origem_da_iface[ja] == g->second.decl)
                    throwError("A classe '" + stripModulePrefixes(cls.class_name) + "' já cumpre '" +
                               stripModulePrefixes(ja) + "': no máximo uma cópia de cada interface genérica (as duas "
                               "exigiriam métodos com o mesmo nome, e a Cinza não tem sobrecarga)",
                               cls.interface_tokens[k]);
        } else if (com_tipos) {
            throwError("'" + cls.interfaces[k] + "' não é uma interface genérica e não aceita tipos entre colchetes",
                       cls.interface_tokens[k]);
        }
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
    if (!simbolo) return;   // Q1a: a cópia de uma genérica não entra no escopo global

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
    programas.clear();   // N2: onde guardar as cópias das genéricas de cada módulo
    for (const auto& u : units) programas.push_back(u.native ? nullptr : u.program);

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

        // Passo 2: analisar cada statement na ordem. Q1e: os const globais vêm antes, na ordem
        // deles (spec 4.1): uma função, um método ou um campo enxerga um const declarado mais
        // abaixo; o inicializador de um const continua só com os const de cima.
        auto eh_const = [](const StmtPtr& s) { return s->node_kind == NodeKind::VarDecl; };
        for (const auto& stmt : program.statements)
            if (eh_const(stmt)) analyzeStmt(stmt.get());
        for (const auto& stmt : program.statements) {
            if (eh_const(stmt)) continue;
            analyzeStmt(stmt.get());
            analisaInstancias();   // N2: cópias genéricas criadas por esta declaração
        }

        // Passo 3 (Fase 2.5): o programa começa em main (só no principal)
        if (modules[i].is_main) checkMain(program);
    }

    // Q1b: com todos os módulos analisados (os new resolvidos, as cópias criadas)
    checkConstructionCycles(units);
    checkRequiredFieldCycles(units);   // Q1a
    marcaAciclicos(units);             // Q11
    valor_const.clear();   // Q1f: os valores já estão nos literais (as strings não ficam vivas até o fim)
}

// Q11: um objeto só pode estar num ciclo de referências se o tipo dele alcança a si mesmo pelo
// grafo dos tipos: os campos (classe, struct) e os parâmetros (list, dict, pair, T?). Uma
// interface, ou um tipo não resolvido, pode ser qualquer classe: conta como alcançar tudo. Os
// tipos que não alcançam a si mesmos (struct Ponto { f64 x; f64 y; }, uma classe só com números
// e textos, ou com uma list<i32>) ficam fora do registro da coleta de ciclos: a contagem de
// referências basta para eles, e a coleta não os percorre.
bool SemanticAnalyzer::tipoAciclico(TypeRef tipo) const {
    using K = TypeInfo::Kind;
    // os tipos que um valor de `t` referencia diretamente; false se `t` é coringa
    auto vizinhos = [&](TypeRef t, std::vector<TypeRef>& out) -> bool {
        switch (t->kind) {
            case K::I32: case K::I64: case K::F32: case K::F64: case K::Bool: case K::String:
            case K::Void: case K::Null: case K::Type: case K::Enum: case K::Error:
                return true;
            case K::List: case K::Dict: case K::Pair: case K::Optional:
                for (TypeRef p : t->params) out.push_back(p);
                return true;
            case K::Struct: {
                auto it = struct_table.find(t->name);
                if (it == struct_table.end()) return false;
                for (const auto& [nome, ft] : it->second.fields) out.push_back(ft);
                return true;
            }
            case K::Class: {
                auto it = class_table.find(t->name);
                if (it == class_table.end()) return false;
                for (const auto& [nome, f] : it->second.fields) out.push_back(f.type);
                return true;
            }
            default:
                return false;   // interface, variável de tipo, var: pode ser qualquer coisa
        }
    };
    std::vector<TypeRef> pilha;
    if (!vizinhos(tipo, pilha)) return false;
    std::unordered_set<TypeRef> vistos;
    while (!pilha.empty()) {
        TypeRef t = pilha.back();
        pilha.pop_back();
        if (t == tipo) return false;
        if (!vistos.insert(t).second) continue;
        if (!vizinhos(t, pilha)) return false;
    }
    return true;
}

void SemanticAnalyzer::marcaAciclicos(const std::vector<ModuleUnit>& units) {
    auto marca = [&](Stmt* s) {
        TypeContext& tc = TypeContext::instance();
        if (s->node_kind == NodeKind::ClassDecl) {
            auto* c = static_cast<ClassDecl*>(s);
            if (c->type_params.empty()) c->aciclico = tipoAciclico(tc.classType(c->class_name));
        } else if (s->node_kind == NodeKind::StructDecl) {
            auto* st = static_cast<StructDecl*>(s);
            if (st->type_params.empty()) st->aciclico = tipoAciclico(tc.structType(st->name));
        }
    };
    for (auto& u : units) {
        if (u.native || !u.program) continue;
        for (auto& s : u.program->statements) marca(s.get());
        for (auto& s : u.program->instancias) marca(s.get());
    }
}

// Q1b: os `new` escritos numa expressão (sem entrar em funções chamadas: essas contam no limite
// de chamadas)
static void coletaNews(const Expr* e, std::vector<const NewExpr*>& out) {
    if (!e) return;
    switch (e->node_kind) {
        case NodeKind::New: {
            auto* n = static_cast<const NewExpr*>(e);
            out.push_back(n);
            for (const auto& a : n->arguments) coletaNews(a.get(), out);
            break;
        }
        case NodeKind::Call: {
            auto* c = static_cast<const CallExpr*>(e);
            for (const auto& a : c->arguments) coletaNews(a.get(), out);
            for (const auto& p : c->interp) coletaNews(p.expr.get(), out);
            break;
        }
        case NodeKind::MethodCall: {
            auto* m = static_cast<const MethodCallExpr*>(e);
            coletaNews(m->object.get(), out);
            for (const auto& a : m->arguments) coletaNews(a.get(), out);
            break;
        }
        case NodeKind::MemberAccess: coletaNews(static_cast<const MemberAccessExpr*>(e)->object.get(), out); break;
        case NodeKind::Binary: {
            auto* b = static_cast<const BinaryExpr*>(e);
            coletaNews(b->left.get(), out);
            coletaNews(b->right.get(), out);
            break;
        }
        case NodeKind::Unary: coletaNews(static_cast<const UnaryExpr*>(e)->operand.get(), out); break;
        case NodeKind::ListLiteral:
            for (const auto& x : static_cast<const ListLiteralExpr*>(e)->elements) coletaNews(x.get(), out);
            break;
        case NodeKind::DictLiteral:
            for (const auto& [k, v] : static_cast<const DictLiteralExpr*>(e)->pairs) {
                coletaNews(k.get(), out);
                coletaNews(v.get(), out);
            }
            break;
        case NodeKind::PairLiteral: {
            auto* p = static_cast<const PairLiteralExpr*>(e);
            coletaNews(p->first.get(), out);
            coletaNews(p->second.get(), out);
            break;
        }
        case NodeKind::IndexAccess: {
            auto* i = static_cast<const IndexAccessExpr*>(e);
            coletaNews(i->object.get(), out);
            coletaNews(i->index.get(), out);
            break;
        }
        case NodeKind::Slice: {
            auto* s = static_cast<const SliceExpr*>(e);
            coletaNews(s->object.get(), out);
            coletaNews(s->start.get(), out);
            coletaNews(s->end.get(), out);
            coletaNews(s->step.get(), out);
            break;
        }
        case NodeKind::Cast: coletaNews(static_cast<const CastExpr*>(e)->operand.get(), out); break;
        case NodeKind::Else: {   // Q1b: a alternativa só roda na ausência, mas pode rodar
            auto* el = static_cast<const ElseExpr*>(e);
            coletaNews(el->value.get(), out);
            coletaNews(el->alternative.get(), out);
            if (el->block) {   // os new das instruções do bloco, em qualquer profundidade
                std::function<void(const Stmt&)> instr = [&](const Stmt& s) {
                    expressoesDe(s, [&](const Expr& x) { coletaNews(&x, out); });
                    instrucoesDe(s, instr);
                };
                instr(*el->block);
            }
            break;
        }
        default: break;
    }
}

// Q1b: os valores iniciais dos campos de uma classe (e os padrões de um struct) rodam a cada
// criação, fora do limite de chamadas. Um `new` escrito ali que volta ao próprio tipo, direto
// (No prox = new No()) ou por outros (A cria B, B cria A; ou por um struct), seria uma criação
// sem fim — antes, a CVM saía sem memória e o executável nativo caía com a pilha do sistema.
// Pelos outros caminhos (um construtor, um método ou uma função que cria o objeto) a recursão
// passa por chamadas e para no StackOverflowError.
void SemanticAnalyzer::checkConstructionCycles(const std::vector<ModuleUnit>& units) {
    struct Aresta { std::string alvo; Token tok; };
    std::map<std::string, std::vector<Aresta>> grafo;
    auto campos = [&](const std::string& dono, const std::vector<ClassDecl::Field>& fs) {
        auto& arestas = grafo[dono];
        for (const auto& f : fs) {
            std::vector<const NewExpr*> news;
            coletaNews(f.initializer.get(), news);
            for (const NewExpr* n : news) {
                TypeRef t = n->resolved_type;
                if (t && (t->is(TK::Class) || t->is(TK::Struct))) arestas.push_back({t->name, n->token});
            }
        }
    };
    for (const auto& u : units) {
        if (u.native || !u.program) continue;
        for (const auto* lista : {&u.program->statements, &u.program->instancias})
            for (const auto& s : *lista) {
                if (s->node_kind == NodeKind::ClassDecl) {
                    auto* c = static_cast<const ClassDecl*>(s.get());
                    campos(c->class_name, c->fields);
                } else if (s->node_kind == NodeKind::StructDecl) {
                    auto* st = static_cast<const StructDecl*>(s.get());
                    if (st->type_params.empty()) campos(st->name, st->fields);   // N5: só as cópias
                }
            }
    }

    std::map<std::string, int> estado;   // 1: no caminho; 2: sem ciclo a partir dele
    std::vector<std::string> caminho;
    std::function<void(const std::string&)> visita = [&](const std::string& no) {
        estado[no] = 1;
        caminho.push_back(no);
        auto it = grafo.find(no);
        if (it != grafo.end())
            for (const Aresta& a : it->second) {
                if (estado[a.alvo] == 1) {
                    std::string texto;
                    for (auto c = std::find(caminho.begin(), caminho.end(), a.alvo); c != caminho.end(); ++c)
                        texto += displayName(*c) + " → ";
                    texto += displayName(a.alvo);
                    throwError("'" + displayName(a.alvo) + "' cria a si mesmo nos valores iniciais dos campos (" +
                                   texto + "): a criação nunca terminaria. Para 'nenhum ou alguns', use list<" +
                                   displayName(a.alvo) + "> (vazia: nenhum).", a.tok);
                }
                if (estado[a.alvo] == 0) visita(a.alvo);
            }
        caminho.pop_back();
        estado[no] = 2;
    };
    for (const auto& [no, arestas] : grafo)
        if (estado[no] == 0) visita(no);
}

// Q1a: um campo de classe sem valor inicial que não é list, dict nem T? (que nascem vazios)
static bool campoObrigatorio(const ClassDecl::Field& f) {
    return !f.initializer && f.type->kind != Type::Kind::LIST && f.type->kind != Type::Kind::DICT &&
           f.type->kind != Type::Kind::OPTIONAL;
}

// Q1a: o primeiro uso do objeto ainda incompleto numa expressão (ler um campo, chamar um
// método, usar self), para a mensagem; nulo se não há
static const Expr* usoDoObjeto(const Expr& e) {
    switch (e.node_kind) {
        case NodeKind::Identifier: {
            auto& id = static_cast<const IdentifierExpr&>(e);
            if (id.res.kind == Resolution::Kind::Field || id.res.kind == Resolution::Kind::Self) return &e;
            return nullptr;
        }
        case NodeKind::Call:
            if (static_cast<const CallExpr&>(e).implicit_method) return &e;
            break;
        default: break;
    }
    const Expr* achou = nullptr;
    filhosDe(e, [&](const Expr& x) { if (!achou) achou = usoDoObjeto(x); });
    if (!achou)   // o bloco de um else dentro da expressão
        blocosDeElse(e, [&](const BlockStmt& b) {
            std::function<void(const Stmt&)> instr = [&](const Stmt& s) {
                expressoesDe(s, [&](const Expr& x) { if (!achou) achou = usoDoObjeto(x); });
                instrucoesDe(s, instr);
            };
            if (!achou) instr(b);
        });
    return achou;
}

// Q1a: o primeiro uso do objeto incompleto numa instrução (e nas de dentro dela); um return
// também conta. O alvo direto de `campo = valor` não é um uso (é a atribuição)
static const Token* usoNaInstrucao(const Stmt& s, std::string& o_que) {
    if (s.node_kind == NodeKind::Return) { o_que = "retornar"; return &s.token; }
    const Token* t = nullptr;
    auto expr = [&](const Expr& e) {
        if (t) return;
        if (const Expr* u = usoDoObjeto(e)) {
            t = &u->token;
            o_que = u->node_kind == NodeKind::Call ? "chamar um método"
                  : static_cast<const IdentifierExpr*>(u)->res.kind == Resolution::Kind::Self ? "usar self"
                  : "ler o campo '" + static_cast<const IdentifierExpr*>(u)->name + "'";
        }
    };
    if (s.node_kind == NodeKind::Assign) {
        auto& a = static_cast<const AssignStmt&>(s);
        const bool campo_direto = a.op == TokenType::OP_ASSIGN && a.target->node_kind == NodeKind::Identifier &&
            static_cast<const IdentifierExpr&>(*a.target).res.kind == Resolution::Kind::Field;
        if (!campo_direto) expr(*a.target);
        expr(*a.value);
    } else {
        expressoesDe(s, expr);
    }
    if (t) return t;
    instrucoesDe(s, [&](const Stmt& x) { if (!t) t = usoNaInstrucao(x, o_que); });
    return t;
}

void SemanticAnalyzer::confereCamposObrigatorios(ClassDecl* stmt, bool com_construtor) {
    const std::string nome = stripModulePrefixes(stmt->class_name);
    std::vector<std::size_t> obrigatorios;
    for (std::size_t i = 0; i < stmt->fields.size(); ++i)
        if (campoObrigatorio(stmt->fields[i])) obrigatorios.push_back(i);
    if (obrigatorios.empty()) return;
    const auto& primeiro = stmt->fields[obrigatorios[0]];

    // os valores iniciais dos outros campos rodam antes do construtor: sem o objeto
    for (const auto& f : stmt->fields) {
        if (!f.initializer) continue;
        if (const Expr* u = usoDoObjeto(*f.initializer))
            throwError("O valor inicial de '" + f.name + "' não pode usar o objeto (self, os campos ou os métodos): "
                       "a classe '" + nome + "' tem o campo obrigatório '" + primeiro.name +
                       "', que só recebe valor no construtor", u->token);
    }

    if (!stmt->constructor)
        throwError("O campo '" + primeiro.name + "' da classe '" + nome + "' não tem valor inicial: declare um "
                   "construtor que o atribua (" + nome.substr(0, nome.find('[')) + "(...) { " + primeiro.name +
                   " = ...; }), dê a ele um "
                   "valor inicial, ou declare-o opcional ('" + primeiro.type->toString() + "?', que nasce null)",
                   primeiro.token);

    if (!com_construtor) return;   // Q1a: numa cópia, o construtor só é conferido quando usado
    // o construtor: cada obrigatório atribuído no nível de cima, antes de qualquer uso do objeto
    auto& ctor = *stmt->constructor;
    std::set<std::size_t> faltam(obrigatorios.begin(), obrigatorios.end());
    std::vector<const Stmt*> corpo;
    if (ctor.body->node_kind == NodeKind::Block)
        for (const auto& s : static_cast<BlockStmt*>(ctor.body.get())->statements) corpo.push_back(s.get());
    else
        corpo.push_back(ctor.body.get());
    for (const Stmt* s : corpo) {
        if (faltam.empty()) break;
        std::string o_que;
        if (const Token* t = usoNaInstrucao(*s, o_que)) {
            std::string lista;
            for (std::size_t k : faltam) lista += (lista.empty() ? "'" : ", '") + stmt->fields[k].name + "'";
            throwError("O construtor de '" + nome + "' não pode " + o_que + " antes de atribuir " +
                       (faltam.size() == 1 ? "o campo obrigatório " : "os campos obrigatórios ") + lista +
                       " (no nível de cima do corpo, fora de if, laço e try)", *t);
        }
        if (s->node_kind == NodeKind::Assign) {
            auto& a = static_cast<const AssignStmt&>(*s);
            if (a.op == TokenType::OP_ASSIGN && a.target->node_kind == NodeKind::Identifier) {
                auto& id = static_cast<const IdentifierExpr&>(*a.target);
                if (id.res.kind == Resolution::Kind::Field) faltam.erase(id.res.slot);
            }
        }
    }
    if (!faltam.empty()) {
        const auto& f = stmt->fields[*faltam.begin()];
        throwError("O construtor de '" + nome + "' não atribui o campo obrigatório '" + f.name + "' no nível de "
                   "cima do corpo (fora de if, laço e try): todo objeto precisa sair do construtor com ele",
                   ctor.token);
    }
}

// Q1a: um campo obrigatório de um tipo de classe exige um objeto desse tipo já pronto ao criar
// este; um ciclo deles (No { No prox; }, A exige B e B exige A) não deixa criar o primeiro
void SemanticAnalyzer::checkRequiredFieldCycles(const std::vector<ModuleUnit>& units) {
    struct Aresta { std::string alvo; Token tok; };
    std::map<std::string, std::vector<Aresta>> grafo;
    for (const auto& u : units) {
        if (u.native || !u.program) continue;
        for (const auto* lista : {&u.program->statements, &u.program->instancias})
            for (const auto& s : *lista) {
                if (s->node_kind != NodeKind::ClassDecl) continue;
                auto* c = static_cast<const ClassDecl*>(s.get());
                if (!c->type_params.empty()) continue;   // Q1a: só as cópias
                auto& arestas = grafo[c->class_name];
                for (const auto& f : c->fields) {
                    if (!campoObrigatorio(f)) continue;
                    TypeRef t = TypeChecker::fromAst(f.type.get());
                    if (t->is(TK::Class)) arestas.push_back({t->name, f.token});
                }
            }
    }
    std::map<std::string, int> estado;
    std::vector<std::string> caminho;
    std::function<void(const std::string&)> visita = [&](const std::string& no) {
        estado[no] = 1;
        caminho.push_back(no);
        if (auto it = grafo.find(no); it != grafo.end())
            for (const Aresta& a : it->second) {
                if (estado[a.alvo] == 1) {
                    std::string texto;
                    for (auto c = std::find(caminho.begin(), caminho.end(), a.alvo); c != caminho.end(); ++c)
                        texto += displayName(*c) + " → ";
                    texto += displayName(a.alvo);
                    throwError("O campo obrigatório de '" + displayName(no) + "' do tipo '" + displayName(a.alvo) +
                               "' fecha um ciclo (" + texto + "): o primeiro objeto nunca poderia ser criado. "
                               "Declare-o opcional ('" + displayName(a.alvo) + "?', que nasce null)", a.tok);
                }
                if (estado[a.alvo] == 0) visita(a.alvo);
            }
        caminho.pop_back();
        estado[no] = 2;
    };
    for (const auto& [no, arestas] : grafo)
        if (estado[no] == 0) visita(no);
}

// ============================================================================
// C5 — RESOLUÇÃO DE NOMES ENTRE MÓDULOS
// ============================================================================

std::string SemanticAnalyzer::qualifyName(const std::string& name, const Token& tok) const {
    // N2: "::Ponto" é um tipo do principal escrito numa cópia genérica de outro
    // módulo: já é o nome completo (o principal não tem prefixo)
    if (name.rfind("::", 0) == 0) return name.substr(2);
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
        if (g == dep.globals.end()) {
            throwError("'" + resto + "' não existe no módulo '" + alias + "'", tok);
        }
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
    if (name.rfind("::", 0) == 0) return symbol_table.resolveGlobal(name.substr(2));   // N2
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
            // N5: Par[i32, string] vira o nome da cópia, um struct comum
            if (auto g = structs_genericos.find(t->name); g != structs_genericos.end()) {
                t->name = usoStructGenerico(g->second, t->type_params, t->token)->name;
                t->type_params.clear();
                return;
            }
            // Q1a: Comparavel[i32] vira o nome da cópia, uma interface comum
            if (auto g = interfaces_genericas.find(t->name); g != interfaces_genericas.end()) {
                t->name = usoInterfaceGenerica(g->second, t->type_params, t->token)->name;
                t->type_params.clear();
                return;
            }
            // Q1a: Pilha[i32] vira o nome da cópia, uma classe comum
            if (auto g = classes_genericas.find(t->name); g != classes_genericas.end()) {
                t->name = usoClasseGenerica(g->second, t->type_params, t->token)->name;
                t->type_params.clear();
                return;
            }
            if (!t->type_params.empty()) {
                const std::string n = stripModulePrefixes(t->name);
                if (!isKnownType(t->name))   // l[i32] com l uma lista: índice com um tipo
                    throwError("'" + n + "' não é um tipo genérico: depois de um nome, [tipos] só "
                               "vale para struct, classe ou interface genérica (Caixa[i32]); um "
                               "índice é um valor, como " + n + "[0]", t->token);
                throwError("'" + n + "' não é um tipo genérico e não aceita tipos entre "
                           "colchetes", t->token);
            }
        }
        for (auto& p : t->type_params) qualifica(p.get());
    };
    qualifica(type);
    return TypeChecker::fromAst(type);
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
        case NodeKind::Null:           // Q1b: const string? X = null;
            return true;
        case NodeKind::Else: {         // Q1b: a else b, com os dois constantes (sem bloco)
            auto* el = static_cast<const ElseExpr*>(expr);
            return !el->block && isConstantExpr(el->value.get()) && isConstantExpr(el->alternative.get());
        }
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
        case NodeKind::Yield:           analyzeYield          (static_cast<YieldStmt*>(stmt));           return;   // Q1b
        case NodeKind::ErrorDecl:       return;   // C4: registrado no pré-registro
        case NodeKind::EnumDecl:        return;   // registrado no pré-registro
        case NodeKind::InterfaceDecl: {           // tipos das assinaturas existem?
            if (!static_cast<InterfaceDecl*>(stmt)->type_params.empty()) {   // Q1a: genérica, nas cópias
                confereParametrosTipo(static_cast<InterfaceDecl*>(stmt)->type_params,
                                      stripModulePrefixes(static_cast<InterfaceDecl*>(stmt)->name));
                return;
            }
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
        case NodeKind::FunctionDecl: {
            auto* fn = static_cast<FunctionDecl*>(stmt);
            if (!fn->type_params.empty()) checkGenericDecl(fn);   // N2: o corpo não é analisado
            else analyzeFunctionDecl(fn);
            return;
        }
        case NodeKind::ClassDecl:       analyzeClassDecl      (static_cast<ClassDecl*>(stmt));           return;
        case NodeKind::StructDecl: {
            auto* st = static_cast<StructDecl*>(stmt);
            if (!st->type_params.empty()) checkGenericStructDecl(st);   // N5: só as cópias
            else analyzeStructDecl(st);
            return;
        }
        default:                        break;
    }
    throwError("Tipo de statement não reconhecido pelo analisador semântico",
               stmt->token);
}

// ============================================================================
// SEMANTIC ANALYZER — STATEMENTS
// ============================================================================

// Bloco { stmt* } — empurra e desempilha seu próprio escopo
// Q1b: yield valor; — só no bloco de um else, fora de um try dele
void SemanticAnalyzer::analyzeYield(YieldStmt* stmt) {
    if (blocos_else.empty())
        throwError("'yield' só existe no bloco de um else, onde dá o valor da expressão: "
                   "x else { ...; yield valor; }", stmt->token);
    const BlocoElse b = blocos_else.back();
    if (b.tries > 0)
        throwError("'yield' não pode sair de dentro de um try: guarde o valor numa variável e use "
                   "yield depois do try", stmt->token);
    TypeRef vt = analyzeExpr(stmt->value.get(), b.tipo);
    if (vt->is(TK::Null))
        throwError("'yield null' não resolve a ausência: o bloco do else precisa dar um '" +
                   b.tipo->str() + "'", stmt->token);
    if (!TypeChecker::isAssignable(b.tipo, vt))
        throwError("O yield precisa dar um '" + b.tipo->str() + "' (o tipo do valor antes do else), "
                   "mas dá '" + vt->str() + "'" + dicaCast(b.tipo, vt), stmt->token);
    coerceInPlace(stmt->value, b.tipo);
}

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
        case NodeKind::Yield:   // Q1b: sai do bloco do else
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
                       "sempre sai do bloco (return, break, continue, throw, exit ou yield).",
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
// inteiros/string/bool/enum). Ponto flutuante é proibido: comparação exata não é confiável.
static TypeRef invalidDictKey(TypeRef type) {
    if (type->is(TK::Dict)) {
        TypeRef k = type->key();
        if (!k->isInteger() && !k->is(TK::String) && !k->is(TK::Bool) && !k->is(TK::Enum) &&
            !k->is(TK::Var))   // var em tipo composto tem erro próprio
            return k;
    }
    for (TypeRef param : type->params)
        if (TypeRef bad = invalidDictKey(param)) return bad;
    return nullptr;
}

static std::string dictKeyMessage(TypeRef key_type) {
    if (key_type->isFloat())
        return "Chave de dict não pode ser '" + key_type->str() + "': comparação exata de ponto "
               "flutuante não é confiável. Use i32, i64, string, bool ou enum.";
    if (key_type->is(TK::Optional))   // Q1e
        return "Chave de dict não pode ser opcional ('" + key_type->str() + "'): uma chave sempre "
               "existe. Use '" + key_type->elem()->str() + "'";
    return "Chave de dict deve ser i32, i64, string, bool ou enum, mas recebeu '" + key_type->str() + "'. "
           "Tipos compostos como list<>, pair<> e classes não podem ser chaves.";
}

// C3: const protege valores e coleções, mas não se aplica diretamente a um
// objeto de classe (a linguagem não sabe se um método altera o objeto).
// Em `const list<Pessoa>` a lista é const; os objetos dentro dela não.
// C3: tipo de classe ou interface (const não se aplica)
static bool isClassLike(TypeRef t) {
    return t->is(TK::Class) || t->is(TK::Interface);
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
            "Use um tipo primitivo explícito: list<i64>, list<string>, etc.",
            stmt->token);
    }
    if (declared_type->is(TK::Dict) &&
        (declared_type->key()->is(TK::Var) || declared_type->value()->is(TK::Var))) {
        throwError(
            "Tipo 'var' não é permitido dentro de 'dict<>'. "
            "Use tipos primitivos explícitos: dict<string, i64>, etc.",
            stmt->token);
    }
    if (declared_type->is(TK::Pair) &&
        (declared_type->first()->is(TK::Var) || declared_type->second()->is(TK::Var))) {
        throwError(
            "Tipo 'var' não é permitido dentro de 'pair<>'. "
            "Use tipos explícitos: pair<string, i64>, etc.",
            stmt->token);
    }

    // A10: chaves de dict (inclusive aninhados) só inteiros, string, bool ou enum
    if (TypeRef bad = invalidDictKey(declared_type))
        throwError(dictKeyMessage(bad), stmt->token);

    // Tipo CUSTOM deve ser uma classe declarada
    if (stmt->type->kind == Type::Kind::CUSTOM) {
        if (!isKnownType(stmt->type->name)) {
            if (stmt->type->name == "type")   // Q1e
                throwError("Não existe variável do tipo 'type': o valor de type(x) só serve para "
                           "comparar com um tipo (type(x) == i32)", stmt->token);
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
            if (init_type->is(TK::Void))   // Q1e
                throwError("'" + stmt->name + "' não tem valor inicial: a expressão é 'void' (a função "
                           "não retorna nada)", stmt->token);
            if (init_type->is(TK::Null))   // Q1b
                throwError("'var' não infere o tipo de null: declare o tipo opcional, ex.: string? " +
                           stmt->name + " = null;", stmt->token);
            // var infere o tipo do inicializador — apenas primitivos (Q1b: e o opcional deles)
            if (!init_type->isPrimitive() &&
                !(init_type->is(TK::Optional) && init_type->elem()->isPrimitive())) {
                throwError(
                    "Inferência 'var' só é permitida para tipos primitivos "
                    "(números, string, bool), mas recebeu '" + init_type->str() + "'. "
                    "Declare o tipo explicitamente.",
                    stmt->token);
            }
            sym.resolved_type = init_type;
        } else {
            if (!TypeChecker::isAssignable(declared_type, init_type)) {
                throwError(
                    "Tipo incompatível na declaração de '" + stmt->name +
                    "': declarado '" + declared_type->str() +
                    "', mas atribuído '" + init_type->str() + "'" + dicaCast(declared_type, init_type),
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
        if (!declared_type->is(TK::List) && !declared_type->is(TK::Dict) &&
            !declared_type->is(TK::Optional)) {   // Q1b: T? nasce null
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

    // B2: o slot vem depois do inicializador (`i64 x = x;` não enxerga o novo x)
    sym.res   = (symbol_table.depth() == 1) ? newGlobal() : newLocal();
    stmt->res = sym.res;
    if (symbol_table.depth() == 1 && stmt->initializer) dobraConst(stmt, sym.resolved_type);   // Q1f
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
        case NodeKind::Cast:   // conversão implícita: o recipiente é o mesmo
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
    // (i32 x = 1; x += g; com g i64 é erro: i32 + i64 dá i64)
    if (stmt->op != TokenType::OP_ASSIGN) {
        TypeRef val_type = analyzeExpr(stmt->value.get());
        if (target_type->isFloat() && val_type->isInteger())
            val_type = intLiteralAsFloat(stmt->value.get(), target_type);
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
        if (result->isNumeric()) coerceInPlace(stmt->value, result);   // mesmo tipo do alvo
        return;
    }

    TypeRef val_type    = analyzeExpr(stmt->value.get(), target_type);   // A3

    if (!TypeChecker::isAssignable(target_type, val_type)) {
        const Expr* t = stmt->target.get();
        if (t->node_kind == NodeKind::IndexAccess) {
            TypeRef container = static_cast<const IndexAccessExpr*>(t)->object->resolved_type;
            if (container->is(TK::List))
                throwError("Valor de tipo '" + val_type->str() +
                           "' incompatível com elemento da lista '" + target_type->str() + "'" +
                           dicaCast(target_type, val_type), stmt->token);
            throwError("Valor de tipo '" + val_type->str() +
                       "' incompatível com V='" + target_type->str() + "' do dicionário" +
                       dicaCast(target_type, val_type), stmt->token);
        }
        throwError(
            "Tipo incompatível na atribuição de '" + lvalueName(t) +
            "': esperado '" + target_type->str() +
            "', mas recebeu '" + val_type->str() + "'" + dicaCast(target_type, val_type),
            stmt->token);
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
void SemanticAnalyzer::declaraDaCondicao(const char* onde, Expr* cond, Type* tipo, const std::string& nome,
                                         const Token& tok, std::uint32_t& slot, TypeRef& resolvido) {
    TypeRef ct = analyzeExpr(cond);
    if (ct->is(TK::Null))
        throwError(std::string("O valor de '") + onde + " (" + nome + " = ...)' é null: o bloco nunca rodaria",
                   cond->token);
    if (!ct->is(TK::Optional))
        throwError(std::string("O valor de '") + onde + " (" + nome + " = ...)' precisa ser opcional (T?), mas é '" +
                   ct->str() + "': ele sempre existe; declare a variável antes do " + onde, cond->token);
    TypeRef inner = ct->elem();
    TypeRef declarado = tipo->kind == Type::Kind::VAR ? inner : typeOf(tipo);
    if (tipo->kind == Type::Kind::CUSTOM && !isKnownType(tipo->name))
        throwError("Tipo desconhecido '" + tipo->name + "'", tok);
    if (declarado->is(TK::Optional))
        throwError("'" + nome + "' recebe o valor já resolvido: declare '" + declarado->elem()->str() +
                   "', sem '?'", tok);
    // o tipo exato de dentro (ou a interface que a classe cumpre): sem conversão no caminho
    const bool cabe = declarado == inner || (declarado->is(TK::Interface) && inner->is(TK::Class) &&
                                             TypeChecker::isAssignable(declarado, inner));
    if (!cabe)
        throwError("'" + nome + "' é '" + declarado->str() + "', mas o valor é '" + ct->str() + "': declare '" +
                   inner->str() + "' (o tipo de dentro do opcional)", tok);
    checkNotAlias(nome, tok);   // C5
    Symbol sym;
    sym.name          = nome;
    sym.kind          = Symbol::Kind::VAR;
    sym.line          = tok.line;
    sym.column        = tok.column;
    sym.module        = current_module;
    sym.resolved_type = declarado;
    sym.res           = newLocal();
    slot              = sym.res.slot;
    resolvido         = declarado;
    symbol_table.declare(sym, tok);
}

void SemanticAnalyzer::analyzeIf(IfStmt* stmt) {
    if (stmt->decl_type) {   // Q1b: if (T nome = opcional): nome só existe no primeiro bloco
        symbol_table.pushScope();
        declaraDaCondicao("if", stmt->condition.get(), stmt->decl_type.get(), stmt->decl_name, stmt->decl_token,
                          stmt->decl_slot, stmt->decl_resolved);
        analyzeStmt(stmt->then_branch.get());
        symbol_table.popScope();
        if (stmt->else_branch) analyzeStmt(stmt->else_branch.get());
        return;
    }
    TypeRef cond_type = analyzeExpr(stmt->condition.get());
    if (!cond_type->is(TK::Bool)) {
        throwError(
            "Condição do 'if' deve ser 'bool', mas recebeu '" + cond_type->str() + "'" + dicaOpcional(cond_type),
            stmt->token);
    }
    analyzeStmt(stmt->then_branch.get());
    if (stmt->else_branch) analyzeStmt(stmt->else_branch.get());
}

// While:  while (cond) body
//   - Condição deve ser bool
void SemanticAnalyzer::analyzeWhile(WhileStmt* stmt) {
    if (stmt->decl_type) {   // Q1b: while (T nome = opcional): a cada volta, enquanto não é null
        symbol_table.pushScope();
        declaraDaCondicao("while", stmt->condition.get(), stmt->decl_type.get(), stmt->decl_name,
                          stmt->decl_token, stmt->decl_slot, stmt->decl_resolved);
        ++loop_depth;
        analyzeStmt(stmt->body.get());
        --loop_depth;
        symbol_table.popScope();
        return;
    }
    TypeRef cond_type = analyzeExpr(stmt->condition.get());
    if (!cond_type->is(TK::Bool)) {
        throwError(
            "Condição do 'while' deve ser 'bool', mas recebeu '" + cond_type->str() + "'" + dicaOpcional(cond_type),
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

    // C2: list<T> → T; dict<K,V> → pair<K,V> (em ordem de chave); string → string
    //     de um caractere (code point UTF-8)
    TypeRef elem_type = nullptr;
    if (iter_type->is(TK::List))        elem_type = iter_type->elem();
    else if (iter_type->is(TK::Dict))   elem_type = types.pair(iter_type->key(), iter_type->value());
    else if (iter_type->is(TK::String)) elem_type = types.stringType();
    else {
        throwError(
            "'for' requer 'list<T>', 'dict<K,V>' ou 'string' como iterável, mas recebeu '" +
            iter_type->str() + "'" + dicaOpcional(iter_type),
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
    }
    // for (i64 x in list<i32>): cada elemento é alargado ao entrar no iterador
    stmt->widen_to = TypeChecker::widens(declared_iter, elem_type) ? declared_iter : nullptr;

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
            current_function_return_type->str() + "', mas retornando '" + ret_type->str() + "'" +
            dicaCast(current_function_return_type, ret_type), stmt->token);
    }
    coerceInPlace(stmt->value, current_function_return_type);
}

// C4: try { } except (Tipo e) { } ... finally { }
void SemanticAnalyzer::analyzeTry(TryStmt* stmt) {
    // Q1b: dentro do bloco de um else, um yield não sai deste try
    struct ContaTry {
        std::vector<BlocoElse>& b;
        explicit ContaTry(std::vector<BlocoElse>& x) : b(x) { if (!b.empty()) ++b.back().tries; }
        ~ContaTry() { if (!b.empty()) --b.back().tries; }
    } conta_try{blocos_else};
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
        }
        // N5: sem valor inicial, list e dict nascem vazios e os demais são obrigatórios
        // na criação (analyzeNew)
    }
}

// Declaração de classe:  class Nome { campos... pub { métodos... } }
//   - Não re-registra (já feito em registerClass)
//   - Analisa campos, construtor e métodos com acesso à classe
namespace {
int indiceParametro(const std::vector<Token>& params, const std::string& nome);   // mais abaixo
}

// Q1f: o valor de uma expressão constante, com as contas da execução (operacoes.cpp): um erro
// (estouro, divisão por zero, um cast que não cabe) sai como o RuntimeError dela
std::optional<Value> SemanticAnalyzer::avaliaConst(const Expr* e) {
    switch (e->node_kind) {
        case NodeKind::Literal:
            return literalValue(static_cast<const LiteralExpr*>(e)->value, e->resolved_type);
        case NodeKind::Null:
            return Value::nulo();
        case NodeKind::Cast: {
            auto* c = static_cast<const CastExpr*>(e);
            auto v = avaliaConst(c->operand.get());
            if (!v) return std::nullopt;
            if (c->resolved_type->is(TK::Optional) || c->resolved_type == c->operand->resolved_type) return v;
            return convertValue(*v, c->resolved_type);
        }
        case NodeKind::Unary: {
            auto* u = static_cast<const UnaryExpr*>(e);
            auto v = avaliaConst(u->operand.get());
            if (!v) return std::nullopt;
            if (u->op == TokenType::OP_NOT) return Value(!v->asBool());
            return negateOp(*v);
        }
        case NodeKind::Binary: {
            auto* b = static_cast<const BinaryExpr*>(e);
            auto l = avaliaConst(b->left.get());
            if (!l) return std::nullopt;
            if (b->op == TokenType::OP_AND || b->op == TokenType::OP_OR) {   // curto-circuito
                if (b->op == TokenType::OP_AND ? !l->asBool() : l->asBool()) return Value(b->op == TokenType::OP_OR);
                auto r = avaliaConst(b->right.get());
                if (!r) return std::nullopt;
                return Value(r->asBool());
            }
            auto r = avaliaConst(b->right.get());
            if (!r) return std::nullopt;
            return applyBinaryOp(b->op, *l, *r);
        }
        case NodeKind::Else: {
            auto* el = static_cast<const ElseExpr*>(e);
            auto v = avaliaConst(el->value.get());
            if (!v) return std::nullopt;
            return v->isNull() ? avaliaConst(el->alternative.get()) : v;
        }
        case NodeKind::Identifier: {
            auto* id = static_cast<const IdentifierExpr*>(e);
            if (id->res.kind != Resolution::Kind::Global) return std::nullopt;
            if (auto it = valor_const.find(id->res.slot); it != valor_const.end()) return it->second;
            for (const auto& [slot, nc] : global_layout.natives)   // Math.pi
                if (slot == id->res.slot) return nc->value;
            return std::nullopt;
        }
        default:
            return std::nullopt;   // enum (Cor.Verde): sem conta
    }
}

// Q1f: calcula o const e troca o valor inicial pelo literal do resultado (número, texto, bool
// ou null): na execução, nenhum const depende de outro, e não há ordem de inicialização
void SemanticAnalyzer::dobraConst(VarDeclStmt* stmt, TypeRef tipo) {
    std::optional<Value> v;
    try {
        v = avaliaConst(stmt->initializer.get());
    } catch (const RuntimeError& e) {
        throwError("O valor do const '" + stmt->name + "' não pode ser calculado: " + e.kind + ": " + e.message,
                   stmt->token);
    }
    if (!v) return;
    valor_const[stmt->res.slot] = *v;
    const Token& tok = stmt->initializer->token;
    TypeRef t = tipo->is(TK::Optional) ? tipo->elem() : tipo;
    ExprPtr lit;
    if (tipo->is(TK::Optional) && v->isNull()) {
        lit = std::make_unique<NullExpr>(tok);
        lit->resolved_type = types.nullType();
        stmt->initializer = std::move(lit);
        return;
    }
    switch (v->kind()) {
        case Value::Kind::I32: case Value::Kind::I64:
            lit = std::make_unique<LiteralExpr>(tok, v->asInt());
            break;
        case Value::Kind::F32: case Value::Kind::F64: {
            auto f = std::make_unique<LiteralExpr>(tok, v->asFloat());
            f->f32 = v->kind() == Value::Kind::F32;
            lit = std::move(f);
            break;
        }
        case Value::Kind::STRING: lit = std::make_unique<LiteralExpr>(tok, v->asString()); break;
        case Value::Kind::BOOL:   lit = std::make_unique<LiteralExpr>(tok, v->asBool()); break;
        default: return;   // enum, tipo: o valor inicial já não depende de conta
    }
    lit->resolved_type = t;
    if (tipo->is(TK::Optional)) lit = std::make_unique<CastExpr>(std::move(lit), tipo);
    stmt->initializer = std::move(lit);
}

// Q1a: os campos da classe no escopo atual (visíveis nos métodos e no construtor); com
// analisa_valores, também confere os valores iniciais
void SemanticAnalyzer::declaraCamposNoEscopo(ClassDecl* stmt, bool analisa_valores) {
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
        if (field.initializer && analisa_valores) {
            TypeRef init_type = analyzeExpr(field.initializer.get(), fsym.resolved_type);
            if (!TypeChecker::isAssignable(fsym.resolved_type, init_type)) {
                throwError(
                    "Campo '" + field.name + "': tipo '" + init_type->str() +
                    "' é incompatível com '" + fsym.resolved_type->str() + "'",
                    field.token);
            }
            coerceInPlace(field.initializer, fsym.resolved_type);
        }
        // Q1a: sem inicializador, list e dict nascem vazios, o T? nasce null e os demais são
        // obrigatórios (confereCamposObrigatorios)

        symbol_table.declare(fsym, field.token);
    }
}

// O construtor (no escopo dos campos)
void SemanticAnalyzer::analisaConstrutor(ClassDecl* stmt) {
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

void SemanticAnalyzer::analyzeClassDecl(ClassDecl* stmt) {
    if (!stmt->type_params.empty()) {   // Q1a: genérica — conferida em cada cópia
        const std::string nome = stripModulePrefixes(stmt->class_name);
        confereParametrosTipo(stmt->type_params, nome);
        for (const auto& f : stmt->fields)
            if (indiceParametro(stmt->type_params, f.name) >= 0)
                throwError("Em '" + nome + "', o campo '" + f.name + "' tem o nome de um parâmetro de tipo; "
                           "escolha outro nome", f.token);
        return;
    }
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
    declaraCamposNoEscopo(stmt, true);
    if (stmt->constructor) analisaConstrutor(stmt);
    confereCamposObrigatorios(stmt, true);   // Q1a

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
    // Q1b: onde se espera um T?, um literal se adapta ao T (i64? x = 5; f64? y = 2.ff)
    if (expected && expected->is(TK::Optional) && expr->node_kind != NodeKind::Else)
        expected = expected->elem();

    // B3: switch no NodeKind em vez de cadeia de dynamic_cast
    switch (expr->node_kind) {
        case NodeKind::Literal:      return analyzeLiteral     (static_cast<LiteralExpr*>(expr), expected);
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
        case NodeKind::Null:         return expr->resolved_type = types.nullType();   // Q1b
        case NodeKind::Else:         return analyzeElse        (static_cast<ElseExpr*>(expr), expected);
        case NodeKind::TypeLiteral: {   // tipo escrito como valor: type(x) == i64
            auto* e  = static_cast<TypeLiteralExpr*>(expr);
            e->value = typeOf(e->type.get());
            return e->resolved_type = types.typeType();
        }
        case NodeKind::Cast: {
            auto* e = static_cast<CastExpr*>(expr);
            if (e->explicit_cast) return analyzeCast(e);
            // Defensivo: a promoção é inserida depois da análise (A2) e, sem a
            // reanálise por chamada (removida em A7), não deveria voltar aqui.
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
// Seção K: inteiro é i32 se couber, senão i64; 1.5f é f32 e 1.5ff é f64
TypeRef SemanticAnalyzer::analyzeLiteral(LiteralExpr* expr, TypeRef expected) {
    TypeRef type = nullptr;

    if (auto* i = std::get_if<std::int64_t>(&expr->value)) {
        const std::int64_t v = *i;
        const bool cabe32 = v >= std::numeric_limits<std::int32_t>::min() &&
                            v <= std::numeric_limits<std::int32_t>::max();
        type = cabe32 ? types.i32Type() : types.i64Type();
        expr->resolved_type = type;
        if (expected && expected->isFloat()) intLiteralAsFloat(expr, expected);   // lança
        if (!cabe32 && expected && expected->is(TK::I32))
            throwError("O literal " + std::to_string(v) + " não cabe em i32 (-2147483648 a "
                       "2147483647): use i64", expr->token);
    }
    else if (std::holds_alternative<double>      (expr->value)) type = expr->f32 ? types.f32Type()
                                                                                 : types.f64Type();
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
        // N5: numa cópia de função genérica, T trocado por uma cópia de struct
        // (type(x) == T com T = Caixa[i32]) é o nome da cópia, que não é um símbolo
        const std::string copia = expr->name.rfind("::", 0) == 0 ? expr->name.substr(2)
                                                                 : expr->name;
        if (copia_struct.count(copia)) {
            expr->name       = copia;
            expr->res        = {Resolution::Kind::Type, 0};
            expr->type_value = types.structType(copia);
            return expr->resolved_type = types.typeType();
        }
        // Q1e: um valor de enum escrito sem o enum (Verde em vez de Cor.Verde)
        for (const auto& [curto, completo] : modules[current_module].globals)
            if (auto en = enum_table.find(completo); en != enum_table.end())
                for (const std::string& valor : en->second->members)
                    if (valor == expr->name)
                        throwError("Variável '" + expr->name + "' não declarada: '" + expr->name +
                                   "' é um valor do enum '" + curto + "'; escreva " + curto + "." +
                                   expr->name + " (o valor de um enum é sempre qualificado)", expr->token);
        throwError("Variável '" + expr->name + "' não declarada", expr->token);
    }
    expr->name = full;       // C5: global de módulo vira nome completo
    expr->res  = sym->res;   // B2
    // Revisão: função não é valor (antes passava e falhava em runtime)
    if (sym->kind == Symbol::Kind::FUNCTION)
        throwError("'" + expr->name + "' é uma função: chame-a com " + expr->name + "(...). Na "
                   "Cinza funções não são valores.", expr->token);
    // nome de class/struct/erro usado como valor é um tipo (type(x) == Pessoa)
    if (sym->kind == Symbol::Kind::CLASS) {
        expr->res        = {Resolution::Kind::Type, 0};
        expr->type_value = sym->resolved_type;
        return expr->resolved_type = types.typeType();
    }
    return expr->resolved_type = sym->resolved_type;
}

// Operação binária:  left OP right
// Seção K: na mesma família, os dois lados são promovidos ao tipo comum ANTES
// da operação (i32 + i64: o i32 vira i64), então a CVM sempre
// recebem operandos do mesmo tipo
TypeRef SemanticAnalyzer::analyzeBinary(BinaryExpr* expr) {
    TypeRef left_type  = analyzeExpr(expr->left.get());
    TypeRef right_type = analyzeExpr(expr->right.get());

    // Q7: 1 < x < 10 não encadeia (como no Python): compara o bool de 1 < x com 10
    auto ordem = [](TokenType t) {
        return t == TokenType::OP_LESS || t == TokenType::OP_LESS_EQUAL ||
               t == TokenType::OP_GREATER || t == TokenType::OP_GREATER_EQUAL;
    };
    if (ordem(expr->op) && expr->left->node_kind == NodeKind::Binary &&
        ordem(static_cast<BinaryExpr*>(expr->left.get())->op) && !right_type->is(TK::Bool))
        throwError("A Cinza não encadeia comparações: 'a < b < c' compara o resultado de 'a < b' (um "
                   "bool) com c. Escreva 'a < b and b < c'", expr->token);

    // x * 2 com x de ponto flutuante: o erro aponta o literal e a correção
    if (left_type->isFloat() && right_type->isInteger())
        right_type = intLiteralAsFloat(expr->right.get(), left_type);
    else if (right_type->isFloat() && left_type->isInteger())
        left_type = intLiteralAsFloat(expr->left.get(), right_type);

    expr->resolved_type = TypeChecker::checkBinaryOp(left_type, expr->op, right_type, expr->token);
    // Q1b: opcional == valor: o valor (alargado, se for o caso) vira o opcional
    if (left_type->is(TK::Optional) && !right_type->is(TK::Optional) && !right_type->is(TK::Null))
        coerceInPlace(expr->right, left_type);
    else if (right_type->is(TK::Optional) && !left_type->is(TK::Optional) && !left_type->is(TK::Null))
        coerceInPlace(expr->left, right_type);
    if (left_type->isNumeric() && right_type->isNumeric()) {
        TypeRef comum = TypeChecker::numericResult(left_type, right_type);
        coerceInPlace(expr->left,  comum);
        coerceInPlace(expr->right, comum);
    }
    return expr->resolved_type;
}

// Literal inteiro onde se espera ponto flutuante (f64 x = 2; x * 2): erro com
// a correção (2.ff). Devolve o tipo da expressão (só volta se não for literal).
TypeRef SemanticAnalyzer::intLiteralAsFloat(Expr* e, TypeRef float_type) {
    if (e->node_kind != NodeKind::Literal) return e->resolved_type;
    auto* lit = static_cast<LiteralExpr*>(e);
    auto* i = std::get_if<std::int64_t>(&lit->value);
    if (!i) return e->resolved_type;
    const std::string sufixo = float_type->is(TK::F32) ? ".f" : ".ff";
    throwError("O literal " + std::to_string(*i) + " é inteiro, mas aqui se espera '" + float_type->str() +
               "': inteiro e ponto flutuante não se misturam. Escreva " + std::to_string(*i) + sufixo +
               " (ou converta com cast<" + float_type->str() + ">(...))", lit->token);
}

// cast<T>(valor): T é i32, i64, f32, f64 ou string; o valor, número ou string
// (bool não converte: use uma comparação ou um if)
TypeRef SemanticAnalyzer::analyzeCast(CastExpr* expr) {
    TypeRef alvo = typeOf(expr->written.get());
    if (!alvo->isNumeric() && !alvo->is(TK::String))
        throwError("cast<" + alvo->str() + "> não existe: cast converte só para i32, i64, f32, f64 ou "
                   "string", expr->token);
    TypeRef origem = analyzeExpr(expr->operand.get());
    if (!origem->isNumeric() && !origem->is(TK::String)) {
        if (origem->is(TK::Bool))
            throwError("cast<" + alvo->str() + ">(bool) não existe: bool não é número. Use um if ou "
                       "uma comparação", expr->token);
        throwError("cast<" + alvo->str() + "> recebeu '" + origem->str() + "': cast converte só números "
                   "e string", expr->token);
    }
    return expr->resolved_type = alvo;
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

    // N2: f[i32](...) só vale para uma função genérica
    const bool generica = sym && sym->kind == Symbol::Kind::FUNCTION && sym->fn_decl &&
                          !sym->fn_decl->type_params.empty();
    if (!expr->type_args.empty() && !generica) {
        throwError("'" + expr->function_name + "' não é uma função genérica: os colchetes "
                   "antes de '(' informam os tipos de uma função declarada com "
                   "fn nome[T](...)", expr->token);
    }

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
                            mi.param_types[i]->str() + "', mas recebeu '" + at->str() + "'" +
                            dicaCast(mi.param_types[i], at), expr->token);
                    }
                    coerceInPlace(expr->arguments[i], mi.param_types[i]);
                }
                expr->target          = mi.decl;
                expr->implicit_method = true;
                usaCorpo(mi.decl);   // Q1a
                return expr->resolved_type = mi.return_type;
            }
        }
    }

    if (sym) expr->function_name = full_name;   // C5: função/erro de módulo
    if (generica) return analyzeGenericCall(expr, *sym);   // N2

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

    // type(x) devolve o tipo real do valor (a classe atrás de uma interface, o
    // tipo de um erro); uma função do usuário chamada type tem precedência
    if (!sym && expr->function_name == "type") {
        if (expr->arguments.size() != 1)
            throwError("'type' recebe exatamente 1 argumento, ex.: type(x)", expr->token);
        expr->type_of = analyzeExpr(expr->arguments[0].get());
        if (expr->type_of->is(TK::Void))
            throwError("'type' precisa de um valor, mas a expressão é 'void'", expr->token);
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
        // Q1e: o nome existe num módulo importado (pub): falta o apelido
        for (const auto& [apelido, idx] : modules[current_module].imports)
            if (modules[idx].pub.count(expr->function_name))
                throwError("Função '" + expr->function_name + "' não declarada: o módulo '" + apelido +
                           "' tem uma; os nomes de um módulo se usam pelo apelido: " + apelido + "." +
                           expr->function_name + "(...)", expr->token);
        throwError("Função '" + expr->function_name + "' não declarada",
                   expr->token);
    }
    if (sym->kind != Symbol::Kind::FUNCTION) {
        throwError("'" + expr->function_name + "' não é uma função", expr->token);
    }
    // Fase 2.5: main é o ponto de entrada, chamado só pela CVM
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
                "', mas recebeu '" + at->str() + "'" + dicaCast(sym->param_types[i], at),
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
            if (arg->is(TK::Null)) return false;   // Q1b: null não diz o tipo
            // Fase 7: variáveis com restrição
            if (param->name == "número" && !arg->isNumeric())
                return false;
            if (param->name == "inteiro" && !arg->isInteger())
                return false;
            if (param->name == "flutuante" && !arg->isFloat())
                return false;
            if (param->name == "comparável" && !arg->isNumeric() && !arg->is(TK::String))
                return false;
            binds[param->name] = arg;
            return true;
        }
        // Math.max(a_i32, b_i64): a variável alarga na família (fica i64)
        if (TypeChecker::widens(arg, it->second)) { it->second = arg; return true; }
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

// Tipo esperado numa mensagem: as variáveis com restrição viram os tipos que
// aceitam ('f32' ou 'f64'); o resto, o nome entre aspas
static std::string tiposAceitos(TypeRef t) {
    if (t->is(TK::TypeVar)) {
        if (t->name == "número")     return "'i32', 'i64', 'f32' ou 'f64'";
        if (t->name == "inteiro")    return "'i32' ou 'i64'";
        if (t->name == "flutuante")  return "'f32' ou 'f64'";
        if (t->name == "comparável") return "um número ou 'string'";
    }
    return "'" + t->str() + "'";
}

// Tipo da assinatura com as variáveis já fixadas (nullptr se sobrar alguma livre)
static TypeRef substitute(TypeRef t, const std::map<std::string, TypeRef>& binds) {
    auto& types = TypeContext::instance();
    if (t->is(TK::TypeVar)) {
        auto it = binds.find(t->name);
        return it != binds.end() ? it->second : nullptr;
    }
    if (t->is(TK::Optional)) {   // Q1b: V? de uma nativa (Dicts.get)
        TypeRef sp = substitute(t->elem(), binds);
        if (!sp) return nullptr;
        return sp->is(TK::Optional) ? sp : types.optional(sp);   // dict<K, V?>: o V já é opcional
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
                       "': esperado " + tiposAceitos(mostrado) + ", mas recebeu '" + at->str() + "'" +
                       (mostrado->is(TK::TypeVar)
                            ? (mostrado->name == "flutuante" && at->isInteger()   // Q1e
                                   ? std::string(": inteiro não entra onde se espera ponto flutuante; "
                                                 "escreva o literal com .ff (2.ff) ou converta com "
                                                 "cast<f64>(n)")
                                   : std::string())
                            : dicaCast(mostrado, at)),
                       expr->token);
        }
    }
    // depois de todos: uma variável de tipo pode ter alargado num argumento posterior
    for (size_t i = 0; i < n; ++i) {
        TypeRef param = (fn.variadic && i >= max_n - 1) ? fn.params.back() : fn.params[i];
        if (TypeRef concreto = substitute(param, binds))
            coerceInPlace(expr->arguments[i], concreto);
    }

    // Fase 7: nativa que altera um argumento (Lists.sort, Lists.sort_by) não aceita const
    for (size_t i = 0; i < n && i < 32; ++i) {
        if (!(fn.mutates & (1u << i))) continue;
        if (const Symbol* root = constRoot(expr->arguments[i].get())) {
            const bool dict = expr->arguments[i]->resolved_type &&
                              expr->arguments[i]->resolved_type->is(TK::Dict);
            throwError("'" + nome + "' altera " + (dict ? "o dict" : "a lista") + ", mas '" + root->name +
                       "' é const. " + (dict ? "Dicts const não podem ser modificados."
                                                         : "Listas const não podem ser modificadas."),
                       expr->token);
        }
    }

    TypeRef ret = substitute(fn.ret, binds);
    if (!ret) throwError("Erro interno: retorno de '" + nome + "' ficou sem tipo", expr->token);
    // Q3: o dict devolvido precisa de uma chave válida (Dicts.invert de um dict<string, f64>)
    if (TypeRef bad = invalidDictKey(ret))
        throwError("'" + nome + "' devolveria um dict com chave '" + bad->str() + "'. " + dictKeyMessage(bad),
                   expr->token);
    return expr->resolved_type = ret;
}

// Chamada de método:  objeto.metodo(args)
//   - Suporta built-ins de list, dict, string
//   - Suporta métodos de classes definidas pelo usuário
TypeRef SemanticAnalyzer::analyzeMethodCall(MethodCallExpr* expr) {
    TypeRef obj_type = analyzeExpr(expr->object.get());

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
                           "', mas recebeu '" + at->str() + "'" + dicaCast(mi.param_types[a], at), expr->token);
            coerceInPlace(expr->arguments[a], mi.param_types[a]);
        }
        expr->iface        = ii.decl;
        expr->iface_method = i;
        return expr->resolved_type = mi.return_type;
    }
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
            return expr->resolved_type = types.i32Type();   // seção K: tamanhos são i32
        }
        if (m == "has") {
            if (expr->arguments.size() != 1)
                throwError("'list.has(idx)' requer exatamente 1 argumento", expr->token);
            TypeRef idx_type = analyzeExpr(expr->arguments[0].get());
            if (!idx_type->isInteger())
                throwError("'list.has(idx)' requer índice inteiro (i32 ou i64), recebeu '" + idx_type->str() + "'",
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
                           "' incompatível com elemento '" + T->str() + "'" + dicaCast(T, elem_type),
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
            if (!idx_type->isInteger())
                throwError("'list.remove(idx)' requer índice inteiro (i32 ou i64), recebeu '" + idx_type->str() + "'",
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
            return expr->resolved_type = types.i32Type();
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
            return expr->resolved_type = types.i32Type();
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
        throwError("Tipo '" + obj_type->str() + "' não possui métodos" +
                   (obj_type->is(TK::Optional) ? dicaOpcional(obj_type) : " (não é uma classe conhecida)"),
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
    usaCorpo(mi.decl);   // Q1a: numa cópia de classe genérica, o corpo passa a ser conferido

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
                mi.param_types[i]->str() + "', mas recebeu '" + arg_type->str() + "'" +
                dicaCast(mi.param_types[i], arg_type), expr->token);
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
        if (m == "line" || m == "column")  return expr->resolved_type = types.i32Type();
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
        throwError("Tipo '" + obj_type->str() + "' não possui campos" +
                   (obj_type->is(TK::Optional) ? dicaOpcional(obj_type)
                    : obj_type->isPrimitive() ? std::string() : " (não é uma classe conhecida)"), expr->token);
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
//   - list<T>[inteiro]  → T
//   - dict<K,V>[K]  → V
TypeRef SemanticAnalyzer::analyzeIndexAccess(IndexAccessExpr* expr) {
    // N5: Caixa[Ponto] com Caixa struct genérico (e não uma variável local com esse nome)
    // é o tipo escrito como valor
    if (expr->como_tipo && expr->object->node_kind == NodeKind::Identifier) {
        const std::string& nome = static_cast<IdentifierExpr*>(expr->object.get())->name;
        const std::string completo = symbol_table.resolveLocal(nome) ? std::string() : qualifyName(nome, expr->token);
        if (!completo.empty() && (structs_genericos.count(completo) || classes_genericas.count(completo) ||
                                  interfaces_genericas.count(completo))) {
            expr->eh_tipo = true;
            analyzeExpr(expr->como_tipo.get());
            return expr->resolved_type = types.typeType();
        }
    }
    TypeRef obj_type = analyzeExpr(expr->object.get());
    TypeRef idx_type = analyzeExpr(expr->index.get());

    if (obj_type->is(TK::List)) {
        if (!idx_type->isInteger()) {
            throwError("Índice de lista deve ser inteiro (i32 ou i64), mas recebeu '" + idx_type->str() + "'",
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
        coerceInPlace(expr->index, K);
        return expr->resolved_type = obj_type->value();
    }

    // s[i]: o caractere (string); índice negativo conta do fim
    if (obj_type->is(TK::String)) {
        if (!idx_type->isInteger())
            throwError("Índice de string deve ser inteiro (i32 ou i64), mas recebeu '" + idx_type->str() + "'",
                       expr->token);
        return expr->resolved_type = types.stringType();
    }

    throwError("Operador '[]' não é suportado no tipo '" + obj_type->str() + "'" + dicaOpcional(obj_type),
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

// Q1e: por que lerFormato recusou o formato (a regra que faltou, para a mensagem)
static std::string motivoFormato(const std::string& f) {
    if (!f.empty() && f[0] == '0')
        return ": a largura não começa com 0 (não há preenchimento com zeros; use "
               "Strings.pad_left(cast<string>(n), 5, \"0\"))";
    size_t i = 0;
    auto numero = [&]() -> long long {
        long long n = 0;
        while (i < f.size() && f[i] >= '0' && f[i] <= '9' && n <= 100000) n = n * 10 + (f[i++] - '0');
        while (i < f.size() && f[i] >= '0' && f[i] <= '9') ++i;
        return n;
    };
    if (numero() > 1000) return ": a largura vai até 1000";
    if (i < f.size() && f[i] == '.') {
        ++i;
        if (numero() > 100) return ": as casas decimais vão de 0 a 100";
    }
    return "";
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

        // N4: no corpo de uma cópia genérica, o T do texto também vira o tipo concreto
        std::function<void(std::vector<Token>&)> troca;
        if (instancia_atual) {
            const Instancia& inst = *instancia_atual;
            const bool marca = !modules[static_cast<size_t>(inst.modulo)].prefix.empty();
            troca = [&inst, marca](std::vector<Token>& toks) {
                std::vector<Token> novos;
                trocaParametros(inst.origem->type_params, inst.tipos, marca, toks, 0, novos);
                toks = std::move(novos);
            };
        }
        CallExpr::InterpPart parte;
        try {
            parte.expr = Parser::parseEmbedded(codigo, origem,   // Q7: a coluna em caracteres
                                               static_cast<int>(utf8::tamanho(s.substr(0, i + 1))) + 1, apelidos,
                                               troca);
        } catch (const ParseError& e) {
            throwError("Expressão inválida em '{" + codigo + "}' no texto de '" + nome + "': " + e.what(),
                       e.error_token);
        }
        if (dois_pontos != std::string::npos) {
            const std::string formato = s.substr(dois_pontos + 1, j - dois_pontos - 1);
            if (!lerFormato(formato, parte.width, parte.precision))
                throwError("Formato inválido '" + formato + "' em '{" + codigo + ":" + formato + "}'" +
                           motivoFormato(formato) + ". Use a largura ({x:8}), as casas decimais ({x:.2f}) "
                           "ou as duas ({x:8.2f})", origem);
        }
        TypeRef t = analyzeExpr(parte.expr.get());
        if (t->is(TK::Void))
            throwError("'{" + codigo + "}' não tem valor (a expressão é 'void')", parte.expr->token);
        if (parte.precision >= 0 && !t->isNumeric())
            throwError("O formato de casas decimais só vale para números, mas '" + codigo + "' é '" +
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

// s[ini:fim:passo]: fatia de string; as partes presentes são inteiros
TypeRef SemanticAnalyzer::analyzeSlice(SliceExpr* expr) {
    TypeRef obj_type = analyzeExpr(expr->object.get());
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
        if (!t->isInteger())
            throwError(std::string("O ") + nome + " da fatia deve ser inteiro (i32 ou i64), mas recebeu '" + t->str() + "'",
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
    // N5: new Par[i32, string](...) cria a cópia; o nome dela fica no lugar do genérico
    if (!expr->tipos_resolvidos) {
        auto g = structs_genericos.find(expr->class_name);
        auto gc = classes_genericas.find(expr->class_name);   // Q1a
        if (!expr->type_args.empty()) {
            if (g != structs_genericos.end())
                expr->class_name = usoStructGenerico(g->second, expr->type_args, expr->token)->name;
            else if (gc != classes_genericas.end())
                expr->class_name = usoClasseGenerica(gc->second, expr->type_args, expr->token)->name;
            else
                throwError("'" + stripModulePrefixes(expr->class_name) + "' não é um struct nem uma "
                           "classe genérica e não aceita tipos entre colchetes no 'new'", expr->token);
        } else if (g != structs_genericos.end() || gc != classes_genericas.end()) {
            const std::string nome = stripModulePrefixes(expr->class_name);
            throwError(std::string(g != structs_genericos.end() ? "o struct genérico '" : "a classe genérica '") +
                       nome + "' precisa dos tipos entre colchetes, ex.: new " + nome + "[i32](...)", expr->token);
        }
        expr->tipos_resolvidos = true;
    }
    // C3: construtor automático de struct — nenhum argumento (valores padrão), todos os
    // campos em ordem ou (N5) os campos pelo nome; número parcial é erro
    if (auto sit = struct_table.find(expr->class_name); sit != struct_table.end()) {
        const StructInfo& info = sit->second;
        const StructDecl& decl = *info.decl;
        const size_t n = expr->arguments.size();
        std::vector<std::string> obrigatorios;   // N5: campos sem valor inicial
        for (const auto& f : decl.fields)
            if (StructDecl::obrigatorio(f)) obrigatorios.push_back(f.name);

        if (!expr->arg_names.empty()) {   // N5: new Ponto(y: 4)
            expr->arg_fields.assign(n, -1);
            std::vector<bool> dado(info.fields.size(), false);
            for (size_t i = 0; i < n; ++i) {
                const std::string& campo = expr->arg_names[i];
                const Token& tok = expr->arg_name_tokens[i];
                if (campo.empty())
                    throwError("Na criação de '" + info.name + "', não misture argumentos pelo "
                               "nome (campo: valor) com argumentos sem nome: use só um dos dois "
                               "jeitos", tok);
                int idx = -1;
                for (size_t j = 0; j < info.fields.size(); ++j)
                    if (info.fields[j].first == campo) idx = static_cast<int>(j);
                if (idx < 0)
                    throwError("'" + campo + "' não é campo do struct '" + info.name + "'", tok);
                if (dado[static_cast<size_t>(idx)])
                    throwError("O campo '" + campo + "' foi informado mais de uma vez na criação "
                               "de '" + info.name + "'", tok);
                dado[static_cast<size_t>(idx)] = true;
                expr->arg_fields[i] = idx;
                TypeRef tipo = info.fields[static_cast<size_t>(idx)].second;
                TypeRef at   = analyzeExpr(expr->arguments[i].get(), tipo);
                if (!TypeChecker::isAssignable(tipo, at))
                    throwError("Campo '" + campo + "' na criação de '" + info.name + "': esperado '" +
                               tipo->str() + "', mas recebeu '" + at->str() + "'" + dicaCast(tipo, at),
                               tok);
                coerceInPlace(expr->arguments[i], tipo);
            }
            for (size_t j = 0; j < info.fields.size(); ++j)
                if (!dado[j] && StructDecl::obrigatorio(decl.fields[j])) {
                    const std::string& campo = info.fields[j].first;
                    throwError("Na criação de '" + info.name + "', falta o campo obrigatório '" +
                               campo + "' (ele não tem valor inicial): informe-o, ex.: new " +
                               info.name + "(" + campo + ": ...)", expr->token);
                }
            return expr->resolved_type = types.structType(info.name);
        }

        if (n == 0 && !obrigatorios.empty()) {
            std::string lista;
            for (size_t k = 0; k < obrigatorios.size(); ++k)
                lista += (k ? ", '" : "'") + obrigatorios[k] + "'";
            throwError("Na criação de '" + info.name + "': " +
                       (obrigatorios.size() == 1 ? "o campo " + lista + " não tem valor inicial"
                                                 : "os campos " + lista + " não têm valor inicial") +
                       (obrigatorios.size() == 1 ? "; informe-o" : "; informe-os") +
                       " pelo nome, ex.: new " + info.name + "(" + obrigatorios[0] +
                       ": ...), ou todos os campos, em ordem", expr->token);
        }
        if (n != 0 && n != info.fields.size()) {
            throwError(
                "Na criação de '" + info.name + "': informe todos os " +
                std::to_string(info.fields.size()) + " campos, em ordem, " +
                (obrigatorios.empty() ? "ou nenhum (valores padrão), " : "") +
                "ou os campos pelo nome (campo: valor), mas recebeu " + std::to_string(n) +
                " argumento(s)",
                expr->token);
        }
        for (size_t i = 0; i < n; ++i) {
            const auto& [nome, tipo] = info.fields[i];
            TypeRef at = analyzeExpr(expr->arguments[i].get(), tipo);
            if (!TypeChecker::isAssignable(tipo, at)) {
                throwError(
                    "Argumento " + std::to_string(i + 1) + " do construtor de '" + info.name +
                    "' (campo '" + nome + "'): esperado '" + tipo->str() +
                    "', mas recebeu '" + at->str() + "'" + dicaCast(tipo, at),
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

    if (!expr->arg_names.empty())   // N5
        throwError("Argumentos pelo nome (campo: valor) só existem na criação de struct; '" +
                   expr->class_name + "' é uma classe, e os argumentos vão para o construtor, "
                   "em ordem", expr->token);

    // checando aridade
    if (auto cc = copia_classe.find(expr->class_name); cc != copia_classe.end() && cc->second->decl->constructor)
        usaCorpo(cc->second->decl->constructor.get());   // Q1a
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
                "', mas recebeu '" + arg_type->str() + "'" + dicaCast(cls.ctor_param_types[i], arg_type),
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
//     assume o tipo esperado (`list<i64> l = [1, 2];` alarga os elementos).
//     Sem tipo esperado, ou se algum elemento não couber, o tipo é inferido
//     pelo conteúdo e o chamador reporta a incompatibilidade.
TypeRef SemanticAnalyzer::analyzeListLiteral(ListLiteralExpr* expr, TypeRef expected) {
    TypeRef T = (expected && expected->is(TK::List)) ? expected->elem() : nullptr;

    if (expr->elements.empty()) {
        return expr->resolved_type = T ? expected : types.list(types.varType());
    }

    std::vector<TypeRef> elem_types;
    elem_types.reserve(expr->elements.size());
    for (size_t i = 0; i < expr->elements.size(); ++i) {
        elem_types.push_back(analyzeExpr(expr->elements[i].get(), T));
        exigeValor(elem_types.back(), "O elemento " + std::to_string(i + 1) + " da lista",
                   expr->elements[i]->token);
    }

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
        if (TypeRef comum = TypeChecker::numericResult(unified, t)) {
            unified = comum;
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

    // A2: [1.f, 2.5ff] vira list<f64> com o 1.f alargado de fato
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
        exigeValor(kt, "A chave da entrada " + std::to_string(i + 1) + " do dict", expr->pairs[i].first->token);
        exigeValor(vt, "O valor da entrada " + std::to_string(i + 1) + " do dict", expr->pairs[i].second->token);

        // v2.00 #13 / A10: chaves só inteiros, string, bool ou enum
        if (!kt->isInteger() && !kt->is(TK::String) && !kt->is(TK::Bool) && !kt->is(TK::Enum)) {
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
        if (TypeRef comum = TypeChecker::numericResult(key_type, kt)) {
            key_type = comum;
        } else if (key_type != kt && !TypeChecker::isAssignable(key_type, kt)) {
            throwError("Chave " + std::to_string(i + 1) + " do dict tem tipo '" + kt->str() +
                       "', mas esperava-se '" + key_type->str() + "'", expr->token);
        }

        // v2.00 #20: unificação do tipo do valor
        if (TypeRef comum = TypeChecker::numericResult(val_type, vt)) {
            val_type = comum;
        } else if (val_type != vt && !TypeChecker::isAssignable(val_type, vt)) {
            throwError("Valor " + std::to_string(i + 1) + " do dict tem tipo '" + vt->str() +
                       "', mas esperava-se '" + val_type->str() + "'", expr->token);
        }
    }

    // A2: chaves e valores alargados (i32 → i64, f32 → f64) até o tipo unificado
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
    exigeValor(first_type, "O primeiro elemento do par", expr->first->token);
    exigeValor(second_type, "O segundo elemento do par", expr->second->token);

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
// isAssignable() só diz que i32 cabe em i64 (e f32 em f64); sem materializar
// a promoção o valor chegaria à CVM com a largura menor. Literal muda
// só o tipo (o valor é exato); o resto ganha um nó CastExpr.
// ============================================================================

// Q1b: valor else alternativa. O valor precisa ser opcional (T?); a alternativa é um T (o
// resultado é T) ou outro T? (o resultado continua T?: a else b else c)
TypeRef SemanticAnalyzer::analyzeElse(ElseExpr* expr, TypeRef expected) {
    TypeRef vt = analyzeExpr(expr->value.get(), expected ? types.optional(expected->is(TK::Optional)
                                                                              ? expected->elem()
                                                                              : expected)
                                                         : nullptr);
    if (vt->is(TK::Null))
        throwError("O valor antes de 'else' é null: não há o que resolver; escreva só a "
                   "alternativa", expr->token);
    if (!vt->is(TK::Optional))
        throwError("O valor antes de 'else' não é opcional ('" + vt->str() + "'): ele sempre existe, "
                   "e não há ausência a resolver", expr->token);
    TypeRef inner = vt->elem();
    if (expr->block) {   // valor else { ...; yield v; }: o bloco dá um T, ou sai
        blocos_else.push_back({inner});
        analyzeBlock(static_cast<BlockStmt*>(expr->block.get()));
        blocos_else.pop_back();
        if (!alwaysLeaves(expr->block.get()))
            throwError("O bloco do 'else' precisa terminar em todos os caminhos: com yield valor (o "
                       "valor da expressão) ou saindo (return, throw, break, continue)", expr->token);
        return expr->resolved_type = inner;
    }
    TypeRef at = analyzeExpr(expr->alternative.get(), inner);
    if (at->is(TK::Null))
        throwError("'else null' não resolve a ausência: o resultado continuaria podendo ser null",
                   expr->alternative->token);
    TypeRef resultado = at->is(TK::Optional) ? vt : inner;
    if (!TypeChecker::isAssignable(resultado, at))
        throwError("A alternativa do 'else' precisa ser '" + inner->str() + "' (o tipo de '" +
                   vt->str() + "'), mas é '" + at->str() + "'" + dicaCast(inner, at),
                   expr->alternative->token);
    coerceInPlace(expr->alternative, resultado);
    return expr->resolved_type = resultado;
}

void SemanticAnalyzer::coerceInPlace(ExprPtr& slot, TypeRef target) {
    if (!slot || !target || slot->resolved_type == target) return;

    // Q1b: um T onde se espera T?: alargado ao T de dentro e embrulhado (um cast implícito, que a
    // CVM não precisa e o cinzac embrulha no CzValor); null e outro opcional ficam como estão
    if (target->is(TK::Optional)) {
        if (!slot->resolved_type || slot->resolved_type->is(TK::Null) ||
            slot->resolved_type->is(TK::Optional))
            return;
        coerceInPlace(slot, target->elem());
        slot = std::make_unique<CastExpr>(std::move(slot), target);
        return;
    }

    if (slot->resolved_type && TypeChecker::widens(target, slot->resolved_type)) {
        if (slot->node_kind == NodeKind::Literal) slot->resolved_type = target;
        else slot = std::make_unique<CastExpr>(std::move(slot), target);
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
            // Demais casos (ex.: variável list<i32> em list<i64>) não têm
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
    // Q1b: um break no bloco de um else das expressões desta instrução (também na condição de um
    // while ou no iterável de um for: elas rodam fora do laço deles, e o break é do laço de fora)
    bool no_else = false;
    expressoesDe(*s, [&](const Expr& e) {
        blocosDeElse(e, [&](const BlockStmt& b) { no_else = no_else || hasBreak(&b); });
    });
    if (no_else) return true;
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

// ============================================================================
// N2 — FUNÇÕES GENÉRICAS (spec, capítulo 8)
// Cada combinação de tipos usada vira uma cópia comum da função (monomorfização).
// A cópia nasce dos tokens da declaração (FunctionDecl::fonte), sem a lista
// [T, U] e com cada parâmetro de tipo trocado pelos tokens do tipo concreto,
// reparseados: cada cópia tem a própria AST, analisada e compilada como uma
// função escrita à mão. O corpo da genérica em si nunca é analisado — genérico
// não usado não é conferido.
// ============================================================================

namespace {

constexpr int MAX_COPIAS_ENCADEADAS = 64;

// maior[i32], junta[f64, A, bool] (nomes completos; as mensagens tiram os prefixos)
std::string nomeCopia(const std::string& base, const std::vector<TypeRef>& tipos) {
    std::string s = base + "[";
    for (size_t i = 0; i < tipos.size(); ++i) s += (i ? ", " : "") + tipos[i]->str();
    return s + "]";
}

// Nome de cópia para mensagens: sem prefixos de módulo e encurtado quando os tipos
// crescem demais (cresce[list<list<...>>]); identificadores são ASCII (1.3)
std::string nomeParaMensagem(const std::string& nome) {
    std::string s = stripModulePrefixes(nome);
    if (s.size() > 60) s = s.substr(0, 56) + "...]";
    return s;
}

int indiceParametro(const std::vector<Token>& params, const std::string& nome) {
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].lexeme == nome) return static_cast<int>(i);
    return -1;
}

// O parâmetro de tipo T aparece no tipo escrito t?
bool menciona(const Type* t, const std::string& T) {
    if (!t) return false;
    if (t->kind == Type::Kind::CUSTOM && t->name == T) return true;
    for (const auto& q : t->type_params)
        if (menciona(q.get(), T)) return true;
    return false;
}

// Algum parâmetro de tipo aparece no tipo escrito t (sozinho ou dentro de list/dict/pair)?
bool mencionaParametro(const Type* t, const FunctionDecl& g) {
    if (!t) return false;
    if (t->kind == Type::Kind::CUSTOM && indiceParametro(g.type_params, t->name) >= 0) return true;
    for (const auto& q : t->type_params)
        if (mencionaParametro(q.get(), g)) return true;
    return false;
}

// Os tokens que escrevem o tipo t, na posição do parâmetro de tipo trocado.
// `marca_principal`: a genérica é de um módulo importado, e um tipo do principal
// (sem prefixo) vai como "::Nome", para não ser qualificado no módulo dela.
void tokensDoTipo(TypeRef t, const Token& pos, bool marca_principal, std::vector<Token>& out) {
    auto tk = [&](TokenType ty, const std::string& lx) {
        Token x = pos;
        x.type   = ty;
        x.lexeme = lx;
        out.push_back(x);
    };
    auto params = [&](TokenType ty, const std::string& lx) {
        tk(ty, lx);
        tk(TokenType::OP_LESS, "<");
        for (size_t i = 0; i < t->params.size(); ++i) {
            if (i) tk(TokenType::COMMA, ",");
            tokensDoTipo(t->params[i], pos, marca_principal, out);
        }
        tk(TokenType::OP_GREATER, ">");
    };
    switch (t->kind) {
        case TK::I32:    tk(TokenType::TYPE_I32, "i32");       return;
        case TK::I64:    tk(TokenType::TYPE_I64, "i64");       return;
        case TK::F32:    tk(TokenType::TYPE_F32, "f32");       return;
        case TK::F64:    tk(TokenType::TYPE_F64, "f64");       return;
        case TK::String: tk(TokenType::TYPE_STRING, "string"); return;
        case TK::Bool:   tk(TokenType::TYPE_BOOL, "bool");     return;
        case TK::List:   params(TokenType::TYPE_LIST, "list"); return;
        case TK::Dict:   params(TokenType::TYPE_DICT, "dict"); return;
        case TK::Pair:   params(TokenType::TYPE_PAIR, "pair"); return;
        case TK::Optional:   // Q1b: T? — com T já opcional, a cópia fica 'T??' e é recusada
            tokensDoTipo(t->elem(), pos, marca_principal, out);
            tk(TokenType::QUESTION, "?");
            return;
        default: {   // class, struct, enum, interface, erro
            const bool completo = t->name.find("::") != std::string::npos;
            tk(TokenType::IDENTIFIER, !completo && marca_principal ? "::" + t->name : t->name);
            return;
        }
    }
}

// Copia in[inicio..] para out, trocando cada parâmetro de tipo de g (que não seja
// nome de membro, depois de '.') pelos tokens do tipo concreto. Serve à cópia
// inteira e às expressões {...} de printf/format no corpo dela, que só viram
// tokens na análise.
void trocaParametros(const std::vector<Token>& params, const std::vector<TypeRef>& tipos, bool marca,
                     const std::vector<Token>& in, size_t inicio, std::vector<Token>& out) {
    for (size_t k = inicio; k < in.size(); ++k) {
        const Token& t = in[k];
        const int idx = t.type == TokenType::IDENTIFIER &&
                        (k == 0 || in[k - 1].type != TokenType::DOT)
                        ? indiceParametro(params, t.lexeme) : -1;
        if (idx >= 0) tokensDoTipo(tipos[static_cast<size_t>(idx)], t, marca, out);
        else out.push_back(t);
    }
}

}  // namespace

void SemanticAnalyzer::checkGenericDecl(FunctionDecl* fn) {
    const std::string nome = stripModulePrefixes(fn->name);
    if (nome == "main")
        throwError("A função 'main' não pode ser genérica: ela é o ponto de entrada do "
                   "programa, chamada sem tipos.", fn->token);
    confereParametrosTipo(fn->type_params, nome);
    for (const auto& p : fn->parameters)
        if (indiceParametro(fn->type_params, p.name) >= 0)
            throwError("Em '" + nome + "', o parâmetro '" + p.name + "' tem o nome de um "
                       "parâmetro de tipo; escolha outro nome", p.token);
}

void SemanticAnalyzer::confereParametrosTipo(const std::vector<Token>& params,
                                             const std::string& nome) {
    for (size_t i = 0; i < params.size(); ++i) {
        const Token& p = params[i];
        for (size_t j = 0; j < i; ++j)
            if (params[j].lexeme == p.lexeme)
                throwError("Parâmetro de tipo '" + p.lexeme + "' repetido em '" + nome + "'", p);
        if (isKnownType(qualifyName(p.lexeme, p)))
            throwError("Em '" + nome + "', o parâmetro de tipo '" + p.lexeme + "' tem o nome de "
                       "um tipo declarado; escolha outro nome (ex.: T)", p);
        checkNotAlias(p.lexeme, p);
    }
}

// Um argumento de tipo é um tipo de valor conhecido: nem void, nem var, nem o
// tipo 'type' (de type(x)); class/struct/... precisam existir
void SemanticAnalyzer::confereArgumentoDeTipo(TypeRef t, const std::string& param,
                                              const std::string& fn, const Token& tok) {
    std::function<void(TypeRef)> visita = [&](TypeRef x) {
        if (x->is(TK::Void) || x->is(TK::Var) || x->is(TK::Type) || x->is(TK::TypeVar))
            throwError("'" + x->str() + "' não pode ser o tipo de '" + param + "' em '" + fn +
                       "': um argumento de tipo é um tipo de valor (i32, string, list<i32>, "
                       "uma struct...)", tok);
        if (x->is(TK::Class) && !class_table.count(x->name))
            throwError("Tipo desconhecido '" + x->str() + "'", tok);
        for (TypeRef q : x->params) visita(q);
    };
    visita(t);
}

TypeRef SemanticAnalyzer::analyzeGenericCall(CallExpr* expr, const Symbol& sym) {
    const FunctionDecl& g = *sym.fn_decl;
    const std::string nome = stripModulePrefixes(expr->function_name);
    const size_t n = g.type_params.size();

    if (expr->arguments.size() != g.parameters.size()) {
        throwError("Aridade incorreta em '" + nome + "': esperados " +
                   std::to_string(g.parameters.size()) + " argumento(s), mas recebeu " +
                   std::to_string(expr->arguments.size()), expr->token);
    }

    std::vector<TypeRef> tipos(n, nullptr);
    std::vector<TypeRef> analisados(expr->arguments.size(), nullptr);

    if (!expr->type_args.empty()) {
        // f[i32, string](...): todos os parâmetros de tipo, na ordem
        if (expr->type_args.size() != n) {
            std::string lista;
            for (size_t k = 0; k < n; ++k) lista += (k ? ", " : "") + g.type_params[k].lexeme;
            throwError("'" + nome + "' tem " + std::to_string(n) + " parâmetro(s) de tipo (" +
                       lista + "), mas a chamada informa " +
                       std::to_string(expr->type_args.size()), expr->token);
        }
        for (size_t k = 0; k < n; ++k) tipos[k] = typeOf(expr->type_args[k].get());
    } else {
        // 8.3: dedução pelos argumentos em que o parâmetro de tipo aparece. Fora de
        // coleções, números da mesma família combinam no mais largo; dentro, o tipo
        // é exato (invariância) e fixa a ligação.
        struct Ligacao { TypeRef tipo = nullptr; bool fixo = false; };
        std::vector<Ligacao> lig(n);
        auto liga = [&](int k, TypeRef a, bool dentro) {
            Ligacao& l = lig[static_cast<size_t>(k)];
            if (!l.tipo) { l.tipo = a; l.fixo = dentro; return; }
            if (l.tipo == a) { l.fixo = l.fixo || dentro; return; }
            if (!dentro && !l.fixo) {
                if (TypeRef w = TypeChecker::numericResult(l.tipo, a)) { l.tipo = w; return; }
            } else if (!dentro) {
                if (TypeChecker::widens(l.tipo, a)) return;
            } else if (!l.fixo && TypeChecker::widens(a, l.tipo)) {
                l.tipo = a; l.fixo = true; return;
            }
            throwError("tipos conflitantes para '" + g.type_params[static_cast<size_t>(k)].lexeme +
                       "' em '" + nome + "': '" + l.tipo->str() + "' e '" + a->str() + "'" +
                       (dentro || l.fixo
                            ? std::string(": dentro de list, dict, pair ou de um tipo genérico, o tipo "
                                          "precisa ser o mesmo (invariância)")   // Q1e
                            : dicaCast(l.tipo, a)),
                       expr->token);
        };
        std::function<void(const Type*, TypeRef, bool)> deduz =
            [&](const Type* p, TypeRef a, bool dentro) {
                if (!p || !a || a->is(TK::Var) || a->is(TK::Null)) return;   // Q1b: null não diz o tipo
                if (p->kind == Type::Kind::OPTIONAL) {   // T? x: o T sai de um T? ou de um T
                    deduz(p->type_params.empty() ? nullptr : p->type_params[0].get(),
                          a->is(TK::Optional) ? a->elem() : a, dentro);
                    return;
                }
                if (p->kind == Type::Kind::CUSTOM) {
                    if (int k = indiceParametro(g.type_params, p->name); k >= 0) {
                        liga(k, a, dentro);
                        return;
                    }
                    // N5: Par[A, B] com uma cópia do mesmo struct: os tipos dela, exatos
                    if (p->type_params.empty() || !a->is(TK::Struct)) return;
                    auto c = copia_struct.find(a->name);
                    if (c == copia_struct.end()) return;
                    const int salvo = current_module;
                    current_module = sym.module;   // o nome escrito vale no módulo da genérica
                    std::string escrito;
                    try { escrito = qualifyName(p->name, p->token); } catch (const SemanticError&) {}
                    current_module = salvo;
                    if (escrito != c->second->origem->name) return;
                    for (size_t i = 0; i < p->type_params.size() && i < c->second->tipos.size(); ++i)
                        deduz(p->type_params[i].get(), c->second->tipos[i], true);
                    return;
                }
                const bool mesma_forma = (p->kind == Type::Kind::LIST && a->is(TK::List)) ||
                                         (p->kind == Type::Kind::DICT && a->is(TK::Dict)) ||
                                         (p->kind == Type::Kind::PAIR && a->is(TK::Pair));
                if (!mesma_forma) return;
                for (size_t i = 0; i < p->type_params.size() && i < a->params.size(); ++i)
                    deduz(p->type_params[i].get(), a->params[i], true);
            };
        for (size_t i = 0; i < expr->arguments.size(); ++i) {
            const Type* p = g.parameters[i].type.get();
            if (!mencionaParametro(p, g)) continue;
            analisados[i] = analyzeExpr(expr->arguments[i].get());
            deduz(p, analisados[i], false);
        }
        for (size_t k = 0; k < n; ++k) {
            if (lig[k].tipo) { tipos[k] = lig[k].tipo; continue; }
            const std::string& T = g.type_params[k].lexeme;
            // T aparece num parâmetro, mas o argumento não tem a forma dele (list<T> com i32)
            for (size_t i = 0; i < expr->arguments.size(); ++i) {
                const Type* p = g.parameters[i].type.get();
                if (analisados[i] && menciona(p, T))
                    throwError("Argumento " + std::to_string(i + 1) + " de '" + nome +
                               "': esperado '" + p->toString() + "', mas recebeu '" +
                               analisados[i]->str() + "'", expr->token);
            }
            std::string exemplo;
            for (size_t j = 0; j < n; ++j) exemplo += j ? ", i32" : "i32";
            throwError("Não é possível deduzir o tipo '" + T + "' de '" + nome + "' pelos "
                       "argumentos (ele não aparece nos parâmetros): informe os tipos na chamada, "
                       "ex.: " + nome + "[" + exemplo + "](...)", expr->token);
        }
    }
    for (size_t k = 0; k < n; ++k)
        confereArgumentoDeTipo(tipos[k], g.type_params[k].lexeme, nome, expr->token);

    Instancia* inst = instancia(g, sym.module, tipos, expr->token);
    const std::string copia = stripModulePrefixes(inst->fn->name);
    for (size_t i = 0; i < expr->arguments.size(); ++i) {
        TypeRef pt = inst->param_types[i];
        TypeRef at = analisados[i] ? analisados[i] : analyzeExpr(expr->arguments[i].get(), pt);
        if (!TypeChecker::isAssignable(pt, at)) {
            throwError("Argumento " + std::to_string(i + 1) + " de '" + copia + "': esperado '" +
                       pt->str() + "', mas recebeu '" + at->str() + "'" + dicaCast(pt, at),
                       expr->token);
        }
        coerceInPlace(expr->arguments[i], pt);
    }
    expr->target = inst->fn;
    return expr->resolved_type = inst->return_type;
}

SemanticAnalyzer::Instancia* SemanticAnalyzer::instancia(const FunctionDecl& g, int modulo,
                                                         const std::vector<TypeRef>& tipos,
                                                         const Token& uso) {
    auto chave = std::make_pair(&g, tipos);
    if (auto it = instancias.find(chave); it != instancias.end()) return it->second.get();

    const std::string nome = nomeCopia(g.name, tipos);
    const int profundidade = instancia_atual ? instancia_atual->profundidade + 1 : 1;
    if (profundidade > MAX_COPIAS_ENCADEADAS) {
        throwError("Instanciações genéricas encadeadas demais (mais de " +
                   std::to_string(MAX_COPIAS_ENCADEADAS) + "): '" + stripModulePrefixes(g.name) +
                   "' gera uma cópia nova a cada chamada, com tipos que só crescem (como T → "
                   "list<T>); isso nunca termina na compilação, qualquer que seja o valor na "
                   "execução.", uso);
    }

    // Tokens da cópia: "fn nome" + o resto depois do ']' dos parâmetros de tipo,
    // com cada T (que não seja nome de membro, x.T) trocado pelo tipo concreto
    const std::vector<Token>& F = g.fonte;
    std::vector<Token> toks{F[0], F[1]};
    size_t k = 2;
    while (k < F.size() && F[k].type != TokenType::RBRACKET) ++k;
    const bool marca = !modules[static_cast<size_t>(modulo)].prefix.empty();
    trocaParametros(g.type_params, tipos, marca, F, k + 1, toks);
    Token fim = F.back();
    fim.type   = TokenType::END_OF_FILE;
    fim.lexeme = "";
    toks.push_back(fim);

    std::set<std::string> apelidos;
    for (const auto& [apelido, _] : modules[static_cast<size_t>(modulo)].imports)
        apelidos.insert(apelido);
    std::string erro;
    Token       erro_tok;
    StmtPtr copia = Parser::parseGenericCopy(std::move(toks), apelidos, erro, erro_tok);
    if (!copia) {
        // ex.: new T() com T = i32: o corpo deixa de ser sintaxe válida
        std::string ligacoes;
        for (size_t j = 0; j < tipos.size(); ++j)
            ligacoes += (j ? ", " : "") + g.type_params[j].lexeme + " = " + tipos[j]->str();
        SemanticError e("Com " + ligacoes + ", o corpo de '" + stripModulePrefixes(g.name) +
                        "' não é válido: " + erro, erro_tok);
        e.notes = rastro(nome, uso, instancia_atual);
        throw e;
    }

    auto* fn = static_cast<FunctionDecl*>(copia.get());
    fn->name = nome;
    auto inst = std::make_unique<Instancia>();
    inst->origem       = &g;
    inst->tipos        = tipos;
    inst->fn           = fn;
    inst->modulo       = modulo;
    inst->uso          = uso;
    inst->pai          = instancia_atual;
    inst->profundidade = profundidade;
    programas[static_cast<size_t>(modulo)]->instancias.push_back(std::move(copia));

    // Assinatura concreta, resolvida no módulo da declaração
    const int salvo = current_module;
    current_module = modulo;
    for (const auto& p : fn->parameters) inst->param_types.push_back(typeOf(p.type.get()));
    inst->return_type = typeOf(fn->return_type.get());
    current_module = salvo;

    Instancia* r = inst.get();
    instancias.emplace(std::move(chave), std::move(inst));
    fila_instancias.push_back(r);
    return r;
}

void SemanticAnalyzer::analisaInstancias() {
    while (!fila_instancias.empty() || !fila_structs.empty() || !fila_classes.empty() || !fila_corpos.empty()) {
        if (!fila_classes.empty()) {   // Q1a: os campos e as interfaces de uma cópia de classe
            InstanciaClasse* c = fila_classes.front();
            fila_classes.pop_front();
            analisaCopiaDeClasse(c);
            continue;
        }
        if (!fila_corpos.empty() && fila_structs.empty()) {   // Q1a: um método (ou o construtor) usado
            CorpoPendente p = fila_corpos.front();
            fila_corpos.pop_front();
            analisaCorpoDaCopia(p);
            continue;
        }
        // N5: os valores iniciais das cópias de struct, no escopo global (como os de um
        // struct escrito à mão), e a conferência dos tipos usados
        if (!fila_structs.empty()) {
            InstanciaStruct* s = fila_structs.front();
            fila_structs.pop_front();
            const int        salvo_modulo = current_module;
            const Instancia* salva_atual  = instancia_atual;
            current_module  = s->modulo;
            instancia_atual = nullptr;
            try {
                for (size_t k = 0; k < s->tipos.size(); ++k)
                    confereArgumentoDeTipo(s->tipos[k], s->origem->type_params[k].lexeme,
                                           stripModulePrefixes(s->origem->name), s->uso);
                analyzeStructDecl(s->decl);
                checkStructCycles();
            } catch (SemanticError& e) {
                if (e.notes.empty()) e.notes = rastro(s->decl->name, s->uso, s->pai);
                throw;
            }
            current_module  = salvo_modulo;
            instancia_atual = salva_atual;
            continue;
        }
        Instancia* inst = fila_instancias.front();
        fila_instancias.pop_front();
        const int        salvo_modulo = current_module;
        const Instancia* salva_atual  = instancia_atual;
        current_module  = inst->modulo;
        instancia_atual = inst;
        try {
            analyzeFunctionDecl(inst->fn);
        } catch (SemanticError& e) {
            if (e.notes.empty()) e.notes = rastro(inst->fn->name, inst->uso, inst->pai);
            throw;
        }
        current_module  = salvo_modulo;
        instancia_atual = salva_atual;
    }
}

std::vector<std::string> SemanticAnalyzer::rastro(const std::string& nome, const Token& uso,
                                                  const Instancia* pai) const {
    std::vector<std::string> linhas;
    auto linha = [&](const std::string& n, const Token& u) {
        linhas.push_back("em " + nomeParaMensagem(n) + ", usada em " + fileName(u.file_id) +
                         ":" + std::to_string(u.line));
    };
    linha(nome, uso);
    for (const Instancia* p = pai; p; p = p->pai) linha(p->fn->name, p->uso);
    // como o stack trace (7.3): as 10 mais internas, quantas faltam e a mais externa
    if (linhas.size() > 12) {
        std::vector<std::string> curto(linhas.begin(), linhas.begin() + 10);
        curto.push_back("... (" + std::to_string(linhas.size() - 11) + " usos omitidos)");
        curto.push_back(linhas.back());
        return curto;
    }
    return linhas;
}

// ============================================================================
// N5 — STRUCTS GENÉRICOS (spec 8.6)
// Como as funções: os tokens da declaração com cada parâmetro de tipo trocado pelo
// tipo concreto, reparseados. A cópia é um struct comum ("Par[i32, string]"): os
// campos são registrados na hora (o tipo dela é preciso já, numa assinatura ou numa
// variável); os valores iniciais são analisados depois, no escopo global.
// ============================================================================

// Q1a: Comparavel[i32] — a cópia da interface (criada no primeiro uso): só as assinaturas
TypeRef SemanticAnalyzer::usoInterfaceGenerica(const InterfaceGenerica& g, const std::vector<TypePtr>& args,
                                               const Token& tok) {
    const std::string nome = stripModulePrefixes(g.decl->name);
    const size_t n = g.decl->type_params.size();
    if (args.empty())
        throwError("a interface genérica '" + nome + "' precisa dos tipos entre colchetes, ex.: " + nome + "[i32]", tok);
    if (args.size() != n)
        throwError("'" + nome + "' tem " + std::to_string(n) + " parâmetro(s) de tipo, mas recebeu " +
                   std::to_string(args.size()), tok);
    std::vector<TypeRef> tipos;
    for (const auto& a : args) {
        TypeRef t = typeOf(a.get());
        if (t->is(TK::Void) || t->is(TK::Var))
            throwError("'" + t->str() + "' não pode ser o tipo de '" + g.decl->type_params[tipos.size()].lexeme +
                       "' em '" + nome + "'", tok);
        tipos.push_back(t);
    }
    auto chave = std::make_pair(g.decl, tipos);
    if (auto it = instancias_iface.find(chave); it != instancias_iface.end()) return types.interfaceType(it->second);

    const std::string copia_nome = nomeCopia(g.decl->name, tipos);
    const std::vector<Token>& F = g.decl->fonte;
    std::vector<Token> toks{F[0], F[1]};
    size_t k = 2;
    while (k < F.size() && F[k].type != TokenType::RBRACKET) ++k;
    const bool marca = !modules[static_cast<size_t>(g.modulo)].prefix.empty();
    trocaParametros(g.decl->type_params, tipos, marca, F, k + 1, toks);
    Token fim = F.back();
    fim.type   = TokenType::END_OF_FILE;
    fim.lexeme = "";
    toks.push_back(fim);
    std::set<std::string> apelidos;
    for (const auto& [apelido, _] : modules[static_cast<size_t>(g.modulo)].imports) apelidos.insert(apelido);
    std::string erro;
    Token       erro_tok;
    StmtPtr copia = Parser::parseGenericCopy(std::move(toks), apelidos, erro, erro_tok);
    if (!copia) {
        std::string ligacoes;
        for (size_t j = 0; j < tipos.size(); ++j)
            ligacoes += (j ? ", " : "") + g.decl->type_params[j].lexeme + " = " + tipos[j]->str();
        SemanticError e("Com " + ligacoes + ", a interface '" + nome + "' não é válida: " + erro, erro_tok);
        e.notes = rastro(copia_nome, tok, instancia_atual);
        throw e;
    }
    auto* in = static_cast<InterfaceDecl*>(copia.get());
    in->name = copia_nome;
    instancias_iface.emplace(std::move(chave), copia_nome);
    origem_da_iface[copia_nome] = g.decl;
    types.declareInterface(copia_nome);
    programas[static_cast<size_t>(g.modulo)]->instancias.push_back(std::move(copia));
    const int salvo = current_module;
    current_module = g.modulo;
    try {
        registerInterface(*in, false);
    } catch (SemanticError& e) {
        current_module = salvo;
        if (e.notes.empty()) e.notes = rastro(copia_nome, tok, instancia_atual);
        throw;
    }
    current_module = salvo;
    return types.interfaceType(copia_nome);
}

// Q1a: Pilha[i32] — confere os tipos e devolve a cópia (criada no primeiro uso)
TypeRef SemanticAnalyzer::usoClasseGenerica(const ClasseGenerica& g, const std::vector<TypePtr>& args,
                                            const Token& tok) {
    const std::string nome = stripModulePrefixes(g.decl->class_name);
    const size_t n = g.decl->type_params.size();
    if (args.empty())
        throwError("a classe genérica '" + nome + "' precisa dos tipos entre colchetes, ex.: " + nome + "[i32]", tok);
    if (args.size() != n) {
        std::string lista;
        for (size_t k = 0; k < n; ++k) lista += (k ? ", " : "") + g.decl->type_params[k].lexeme;
        throwError("'" + nome + "' tem " + std::to_string(n) + " parâmetro(s) de tipo (" + lista + "), mas recebeu " +
                   std::to_string(args.size()), tok);
    }
    std::vector<TypeRef> tipos;
    for (const auto& a : args) {
        TypeRef t = typeOf(a.get());
        if (t->is(TK::Void) || t->is(TK::Var))
            throwError("'" + t->str() + "' não pode ser o tipo de '" + g.decl->type_params[tipos.size()].lexeme +
                       "' em '" + nome + "': um argumento de tipo é um tipo de valor (i32, string, list<i32>, "
                       "uma classe...)", tok);
        tipos.push_back(t);
    }
    return instanciaClasse(g, tipos, tok);
}

TypeRef SemanticAnalyzer::instanciaClasse(const ClasseGenerica& g, const std::vector<TypeRef>& tipos,
                                          const Token& uso) {
    auto chave = std::make_pair(g.decl, tipos);
    if (auto it = instancias_classe.find(chave); it != instancias_classe.end())
        return types.classType(it->second->decl->class_name);

    const std::string nome = nomeCopia(g.decl->class_name, tipos);
    if (instancias_classe.size() >= 4096 || structs_abertos >= MAX_COPIAS_ENCADEADAS) {
        SemanticError e("Instanciações genéricas encadeadas demais: '" + stripModulePrefixes(g.decl->class_name) +
                        "' usa a si mesma com tipos que só crescem (como T → list<T>); isso nunca termina na "
                        "compilação.", uso);
        e.notes = rastro(nome, uso, instancia_atual);
        throw e;
    }

    // os tokens da cópia: "class nome" + o resto depois do ']' dos parâmetros de tipo
    const std::vector<Token>& F = g.decl->fonte;
    std::vector<Token> toks{F[0], F[1]};
    size_t k = 2;
    while (k < F.size() && F[k].type != TokenType::RBRACKET) ++k;
    const bool marca = !modules[static_cast<size_t>(g.modulo)].prefix.empty();
    trocaParametros(g.decl->type_params, tipos, marca, F, k + 1, toks);
    Token fim = F.back();
    fim.type   = TokenType::END_OF_FILE;
    fim.lexeme = "";
    toks.push_back(fim);

    std::set<std::string> apelidos;
    for (const auto& [apelido, _] : modules[static_cast<size_t>(g.modulo)].imports) apelidos.insert(apelido);
    std::string erro;
    Token       erro_tok;
    StmtPtr copia = Parser::parseGenericCopy(std::move(toks), apelidos, erro, erro_tok);
    if (!copia) {   // ex.: new T() com T = i32
        std::string ligacoes;
        for (size_t j = 0; j < tipos.size(); ++j)
            ligacoes += (j ? ", " : "") + g.decl->type_params[j].lexeme + " = " + tipos[j]->str();
        SemanticError e("Com " + ligacoes + ", a classe '" + stripModulePrefixes(g.decl->class_name) +
                        "' não é válida: " + erro, erro_tok);
        e.notes = rastro(nome, uso, instancia_atual);
        throw e;
    }

    auto* cls = static_cast<ClassDecl*>(copia.get());
    cls->class_name = nome;
    auto inst = std::make_unique<InstanciaClasse>();
    inst->origem = g.decl;
    inst->tipos  = tipos;
    inst->decl   = cls;
    inst->modulo = g.modulo;
    inst->uso    = uso;
    inst->pai    = instancia_atual;
    InstanciaClasse* r = inst.get();
    // os corpos esperam o uso
    for (auto* lista : {&cls->priv_methods, &cls->pub_methods})
        for (auto& m : *lista)
            if (m->node_kind == NodeKind::FunctionDecl) {
                auto* fn = static_cast<FunctionDecl*>(m.get());
                fn->corpo_analisado = false;
                corpo_da_copia[fn] = {r, fn};
            }
    if (cls->constructor) {
        cls->constructor->corpo_analisado = false;
        corpo_da_copia[cls->constructor.get()] = {r, nullptr};
    }
    programas[static_cast<size_t>(g.modulo)]->instancias.push_back(std::move(copia));
    instancias_classe.emplace(std::move(chave), std::move(inst));
    copia_classe[nome] = r;

    // as assinaturas, no módulo da declaração (podem criar outras cópias: No[T] dentro de No[T]
    // acha esta, já registrada no mapa)
    const int salvo = current_module;
    current_module = g.modulo;
    ++structs_abertos;
    try {
        registerClass(*cls, false);
    } catch (SemanticError& e) {
        --structs_abertos;
        current_module = salvo;
        if (e.notes.empty()) e.notes = rastro(nome, uso, instancia_atual);
        throw;
    }
    --structs_abertos;
    current_module = salvo;
    fila_classes.push_back(r);
    return types.classType(nome);
}

void SemanticAnalyzer::usaCorpo(const void* corpo) {
    auto it = corpo_da_copia.find(corpo);
    if (it == corpo_da_copia.end() || !corpos_pedidos.insert(corpo).second) return;
    fila_corpos.push_back(it->second);
}

// Q1a: a cópia, no escopo global e no módulo da declaração: os tipos usados, as interfaces (os
// métodos exigidos por elas passam a ser usados), os campos e os valores iniciais
void SemanticAnalyzer::analisaCopiaDeClasse(InstanciaClasse* c) {
    const int        salvo_modulo = current_module;
    const Instancia* salva_atual  = instancia_atual;
    const std::string salva_classe = current_class_name;
    current_module  = c->modulo;
    instancia_atual = nullptr;
    try {
        for (size_t k = 0; k < c->tipos.size(); ++k)
            confereArgumentoDeTipo(c->tipos[k], c->origem->type_params[k].lexeme,
                                   stripModulePrefixes(c->origem->class_name), c->uso);
        checkInterfaces(c->decl);
        for (const auto& [iface, tabela] : c->decl->itables)
            for (const FunctionDecl* fn : tabela) usaCorpo(fn);
        current_class_name = c->decl->class_name;
        symbol_table.pushScope();
        declaraCamposNoEscopo(c->decl, true);
        symbol_table.popScope();
        confereCamposObrigatorios(c->decl, false);
    } catch (SemanticError& e) {
        if (e.notes.empty()) e.notes = rastro(c->decl->class_name, c->uso, c->pai);
        current_class_name = salva_classe;
        throw;
    }
    current_class_name = salva_classe;
    current_module  = salvo_modulo;
    instancia_atual = salva_atual;
}

// Q1a: o corpo de um método (ou o construtor) usado de uma cópia, no escopo dos campos dela
void SemanticAnalyzer::analisaCorpoDaCopia(const CorpoPendente& p) {
    const int        salvo_modulo = current_module;
    const Instancia* salva_atual  = instancia_atual;
    const std::string salva_classe = current_class_name;
    ClassDecl* cls = p.classe->decl;
    current_module     = p.classe->modulo;
    instancia_atual    = nullptr;
    current_class_name = cls->class_name;
    symbol_table.pushScope();
    try {
        declaraCamposNoEscopo(cls, false);
        if (p.metodo) {
            auto* fn = const_cast<FunctionDecl*>(p.metodo);
            fn->corpo_analisado = true;
            analyzeFunctionDecl(fn);
        } else {
            cls->constructor->corpo_analisado = true;
            analisaConstrutor(cls);
            confereCamposObrigatorios(cls, true);
        }
    } catch (SemanticError& e) {
        if (e.notes.empty()) e.notes = rastro(cls->class_name, p.classe->uso, p.classe->pai);
        symbol_table.popScope();
        current_class_name = salva_classe;
        throw;
    }
    symbol_table.popScope();
    current_class_name = salva_classe;
    current_module     = salvo_modulo;
    instancia_atual    = salva_atual;
}

void SemanticAnalyzer::checkGenericStructDecl(StructDecl* st) {
    const std::string nome = stripModulePrefixes(st->name);
    confereParametrosTipo(st->type_params, nome);
    for (const auto& f : st->fields) {
        const Type* t = f.type.get();
        if (indiceParametro(st->type_params, f.name) >= 0)
            throwError("Em '" + nome + "', o campo '" + f.name + "' tem o nome de um parâmetro "
                       "de tipo; escolha outro nome", f.token);
        if (f.initializer && t->kind == Type::Kind::CUSTOM && t->type_params.empty() &&
            indiceParametro(st->type_params, t->name) >= 0)
            throwError("Em '" + nome + "', o campo '" + f.name + "' é do tipo '" + t->name +
                       "' e não pode ter valor inicial: não há valor que sirva a todo " +
                       t->name + ". Deixe-o sem valor; quem cria o struct o informa.", f.token);
    }
}

TypeRef SemanticAnalyzer::usoStructGenerico(const StructGenerico& g,
                                            const std::vector<TypePtr>& args, const Token& tok) {
    const std::string nome = stripModulePrefixes(g.decl->name);
    const size_t n = g.decl->type_params.size();
    if (args.empty())
        throwError("o struct genérico '" + nome + "' precisa dos tipos entre colchetes, ex.: " +
                   nome + "[i32]", tok);
    if (args.size() != n) {
        std::string lista;
        for (size_t k = 0; k < n; ++k) lista += (k ? ", " : "") + g.decl->type_params[k].lexeme;
        throwError("'" + nome + "' tem " + std::to_string(n) + " parâmetro(s) de tipo (" + lista +
                   "), mas recebeu " + std::to_string(args.size()), tok);
    }
    std::vector<TypeRef> tipos;
    for (const auto& a : args) {
        TypeRef t = typeOf(a.get());
        if (t->is(TK::Void) || t->is(TK::Var))
            throwError("'" + t->str() + "' não pode ser o tipo de '" +
                       g.decl->type_params[tipos.size()].lexeme + "' em '" + nome + "': um "
                       "argumento de tipo é um tipo de valor (i32, string, list<i32>, uma "
                       "struct...)", tok);
        tipos.push_back(t);
    }
    return instanciaStruct(g, tipos, tok);
}

TypeRef SemanticAnalyzer::instanciaStruct(const StructGenerico& g, const std::vector<TypeRef>& tipos,
                                          const Token& uso) {
    auto chave = std::make_pair(g.decl, tipos);
    if (auto it = instancias_struct.find(chave); it != instancias_struct.end())
        return types.structType(it->second->decl->name);

    const std::string nome = nomeCopia(g.decl->name, tipos);
    if (structs_abertos >= MAX_COPIAS_ENCADEADAS) {
        SemanticError e("Instanciações genéricas encadeadas demais (mais de " +
                        std::to_string(MAX_COPIAS_ENCADEADAS) + "): '" +
                        stripModulePrefixes(g.decl->name) + "' usa a si mesmo com tipos que só "
                        "crescem (como T → list<T>); isso nunca termina na compilação.", uso);
        e.notes = rastro(nome, uso, instancia_atual);
        throw e;
    }

    // Tokens da cópia: "struct nome" + o resto depois do ']' dos parâmetros de tipo
    const std::vector<Token>& F = g.decl->fonte;
    std::vector<Token> toks{F[0], F[1]};
    size_t k = 2;
    while (k < F.size() && F[k].type != TokenType::RBRACKET) ++k;
    const bool marca = !modules[static_cast<size_t>(g.modulo)].prefix.empty();
    trocaParametros(g.decl->type_params, tipos, marca, F, k + 1, toks);
    Token fim = F.back();
    fim.type   = TokenType::END_OF_FILE;
    fim.lexeme = "";
    toks.push_back(fim);

    std::set<std::string> apelidos;
    for (const auto& [apelido, _] : modules[static_cast<size_t>(g.modulo)].imports)
        apelidos.insert(apelido);
    std::string erro;
    Token       erro_tok;
    StmtPtr copia = Parser::parseGenericCopy(std::move(toks), apelidos, erro, erro_tok);
    if (!copia) {
        std::string ligacoes;
        for (size_t j = 0; j < tipos.size(); ++j)
            ligacoes += (j ? ", " : "") + g.decl->type_params[j].lexeme + " = " + tipos[j]->str();
        SemanticError e("Com " + ligacoes + ", o struct '" + stripModulePrefixes(g.decl->name) +
                        "' não é válido: " + erro, erro_tok);
        e.notes = rastro(nome, uso, instancia_atual);
        throw e;
    }

    auto* st = static_cast<StructDecl*>(copia.get());
    st->name = nome;
    auto inst = std::make_unique<InstanciaStruct>();
    inst->origem = g.decl;
    inst->tipos  = tipos;
    inst->decl   = st;
    inst->modulo = g.modulo;
    inst->uso    = uso;
    inst->pai    = instancia_atual;
    InstanciaStruct* r = inst.get();
    programas[static_cast<size_t>(g.modulo)]->instancias.push_back(std::move(copia));
    instancias_struct.emplace(std::move(chave), std::move(inst));
    copia_struct[nome] = r;
    types.declareStruct(nome);   // antes dos campos: list<No[T]> dentro de No[T] acha a cópia

    // Os campos, no módulo da declaração (podem criar outras cópias)
    const int salvo = current_module;
    current_module = g.modulo;
    ++structs_abertos;
    try {
        registerStruct(*st, false);
    } catch (SemanticError& e) {
        if (e.notes.empty()) e.notes = rastro(nome, uso, instancia_atual);
        throw;
    }
    --structs_abertos;
    current_module = salvo;
    fila_structs.push_back(r);
    // Com as cópias que esta criou já registradas: struct que contém a si mesmo por valor
    // (A[T] { B[T] b; } e B[T] { A[T] a; }) é erro já, antes de algum uso da cópia
    if (structs_abertos == 0) {
        try {
            checkStructCycles();
        } catch (SemanticError& e) {
            if (e.notes.empty()) e.notes = rastro(nome, uso, instancia_atual);
            throw;
        }
    }
    return types.structType(nome);
}

} // namespace cinza
