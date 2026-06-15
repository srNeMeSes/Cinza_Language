#ifndef CINZA_SEMANTIC_H
#define CINZA_SEMANTIC_H

#include "ast.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <stdexcept>
#include <map>

namespace cinza {

// ============================================================================
// SEMANTIC ERROR
// Para ao primeiro erro encontrado (stop-at-first philosophy da Cinza)
// ============================================================================

class SemanticError : public std::runtime_error {
public:
    int line;
    int column;

    SemanticError(const std::string& msg, int ln, int col)
        : std::runtime_error(buildMessage(msg, ln, col)), line(ln), column(col) {}

    SemanticError(const std::string& msg, const Token& tok)
        : SemanticError(msg, tok.line, tok.column) {}

private:
    static std::string buildMessage(const std::string& msg, int ln, int col) {
        return "SemanticError [linha " + std::to_string(ln) +
               ", col " + std::to_string(col) + "]: " + msg;
    }
};

// ============================================================================
// SYMBOL
// Representa qualquer nome declarado no programa.
// ============================================================================

struct Symbol {
    enum class Kind  { VAR, FUNCTION, CLASS, PARAMETER };

    std::string name;
    Kind        kind     = Kind::VAR;
    bool        is_const = false;  // não pode ser reatribuído após inicialização

    // Tipo resolvido em string canônica:
    //   "int", "decimal", "string", "bool", "void", "var",
    //   "list<int>", "dict<string, int>", "pair<int, string>",
    //   "Pessoa" (classe customizada), …
    std::string resolved_type;

    int         line   = 0;
    int         column = 0;

    // Apenas para Kind::FUNCTION — usados na verificação de chamadas
    std::vector<std::string> param_types;   // tipos dos parâmetros na ordem
    std::string              return_type;   // "void", "int", …

    // ponteiro para o nó AST da função (para re-análise com var params)
    const FunctionDecl*      decl_ptr = nullptr;  // não-owning

    Symbol() = default;

    Symbol(const std::string& n, Kind k,
           const std::string& t, int ln, int col)
        : name(n), kind(k), resolved_type(t), line(ln), column(col) {}
};

// ============================================================================
// SYMBOL TABLE — pilha de escopos
// Cada `pushScope()` cria um novo nível de escopo.
// `resolve()` percorre do topo até a base (escopo global).
// ============================================================================

class SymbolTable {
public:
    using Scope = std::unordered_map<std::string, Symbol>;

private:
    std::vector<Scope> scopes;

public:
    SymbolTable() { scopes.emplace_back(); }  // escopo global inicializado

    void pushScope();   // cria um novo nivel de escopo
    void popScope();    // deleta o escopo

    // Declara símbolo no escopo atual.
    // Lança SemanticError se o nome já existir no mesmo escopo.
    void declare(const Symbol& sym, const Token& tok);

    // Resolve percorrendo do topo para a base.
    // Retorna ponteiro mutável ao símbolo, ou nullptr se não encontrado.
    Symbol*       resolve(const std::string& name);
    const Symbol* resolve(const std::string& name) const;

    // Verifica existência apenas no escopo atual (sem subir a pilha).
    bool existsInCurrentScope(const std::string& name) const;

    // retorna a quantidade de escopos atuais
    int depth() const { return static_cast<int>(scopes.size()); }
};


// ============================================================================
// CLASS INFO — informações sobre uma classe declarada
// Populado por `preRegisterDeclarations` antes da análise dos corpos.
// ============================================================================

struct FieldInfo {  // informações de campos de classes
    std::string type_str;
    bool        is_public = false;
    int         line      = 0;
};

struct MethodInfo { // informações de metodos de classes
    std::vector<std::string> param_types;
    std::string              return_type;
    bool                     is_public = false;
    int                      line      = 0;
};

struct ClassInfo {  // informações de classes
    std::string                        name;
    std::map<std::string, FieldInfo>   fields;
    std::map<std::string, MethodInfo>  methods;
    std::vector<std::string>           ctor_param_types;  // vazio = sem construtor
    int                                line = 0;
};

// ============================================================================
// TYPE CHECKER — verificações e consultas de tipos
// Todos os métodos são estáticos (sem estado interno).
// ============================================================================

class TypeChecker {
public:
    // Converte Type* → string canônica.
    // Usa o mesmo formato de Type::toString() mas centralizado aqui.
    static std::string typeToString(const Type* type);

    // Retorna o tipo resultante de uma operação binária.
    // Lança SemanticError se a operação for inválida para os tipos fornecidos.
    static std::string checkBinaryOp(const std::string& left_type,
                                     TokenType          op,
                                     const std::string& right_type,
                                     const Token&       op_token);

    // Retorna o tipo resultante de uma operação unária.
    // Lança SemanticError se a operação for inválida.
    static std::string checkUnaryOp(TokenType          op,
                                    const std::string& operand_type,
                                    const Token&       op_token);

    // Retorna true se `value_type` pode ser atribuído a `declared_type`.
    // Regras de atribuição da Cinza:
    //   - mesmo tipo → sempre compatível
    //   - int → decimal   (promoção numérica)
    //   - list<int> → list<decimal>  (promoção em listas)
    static bool isAssignable(const std::string& declared_type,
                             const std::string& value_type);

    // Helpers de consulta
    static bool isNumeric(const std::string& type_str);   // int ou decimal
    static bool isPrimitive(const std::string& type_str); // int, decimal, string, bool

    // Extrai parâmetros de tipo genérico respeitando aninhamento.
    //   "list<int>"               → {"int"}
    //   "dict<string, int>"       → {"string", "int"}
    //   "dict<string, list<int>>" → {"string", "list<int>"}
    //   "pair<string, int>"       → {"string", "int"}
    static std::vector<std::string> extractTypeParams(const std::string& type_str);
};

// ============================================================================
// SEMANTIC ANALYZER — ponto de entrada da análise semântica
//
// Fluxo:
//   Program → preRegisterDeclarations → analyzeStmt* → AST anotada
//
// Invariantes:
//   - Para ao primeiro SemanticError (stop-at-first)
//   - Não executa código, apenas valida e anota
//   - Runtime recebe a AST anotada e executa sem revalidar tipos
//
// Regra de inicialização (reforçada em conjunto com o parser):
//   - list<T> e dict<K,V> podem omitir inicializador: nascem como coleções
//     vazias implicitamente, tanto em variáveis locais quanto em campos de classe.
//   - Todos os demais tipos (int, decimal, string, bool, var, pair<>, CUSTOM)
//     exigem inicializador explícito — o parser rejeita a declaração antes do
//     semântico ser chamado. O semântico adiciona uma blindagem defensiva para
//     detectar inconsistências na pipeline caso essa invariante seja violada.
// ============================================================================

class SemanticAnalyzer {
private:
    SymbolTable  symbol_table;
    std::unordered_map<std::string, ClassInfo> class_table;

    // Contexto de análise atual
    std::string current_function_return_type;  // "" se fora de função
    std::string current_class_name;            // "" se fora de classe
    bool        inside_function = false;

    // ── Pré-registro (forward-reference support) ──────────────────────────
    void preRegisterDeclarations(const Program& program);
    void registerClass   (const ClassDecl&    cls);
    void registerFunction(const FunctionDecl& fn);

    // ── Análise de statements ─────────────────────────────────────────────
    void analyzeStmt       (Stmt* stmt);
    void analyzeBlock      (BlockStmt* block);
    void analyzeVarDecl    (VarDeclStmt* stmt);
    void analyzeAssignment (AssignmentStmt* stmt);
    void analyzeIndexAssignment(IndexAssignmentStmt* stmt);  // v2.00 #8
    void analyzeExprStmt   (ExprStmt* stmt);
    void analyzeIf         (IfStmt* stmt);
    void analyzeWhile      (WhileStmt* stmt);
    void analyzeFor        (ForStmt* stmt);
    void analyzeReturn     (ReturnStmt* stmt);
    void analyzeFunctionDecl(FunctionDecl* stmt);
    void analyzeClassDecl   (ClassDecl* stmt);

    // ── Análise de expressões ─────────────────────────────────────────────
    // Cada função: analisa o nó, anota `resolved_type` no nó, retorna o tipo.
    std::string analyzeExpr         (Expr* expr);
    std::string analyzeLiteral      (LiteralExpr* expr);
    std::string analyzeIdentifier   (IdentifierExpr* expr);
    std::string analyzeBinary       (BinaryExpr* expr);
    std::string analyzeUnary        (UnaryExpr* expr);
    std::string analyzeCall         (CallExpr* expr);
    std::string analyzeMethodCall   (MethodCallExpr* expr);
    std::string analyzeMemberAccess (MemberAccessExpr* expr);
    std::string analyzeIndexAccess  (IndexAccessExpr* expr);
    std::string analyzeNew          (NewExpr* expr);
    std::string analyzeListLiteral  (ListLiteralExpr* expr);
    std::string analyzeDictLiteral  (DictLiteralExpr* expr);
    std::string analyzePairLiteral  (PairLiteralExpr* expr);

    // ── Análise de fluxo de retorno ───────────────────────────────────────
    // true se TODOS os caminhos do stmt terminam em ReturnStmt
    bool allPathsReturn(const Stmt* stmt) const;
    // true se ALGUM caminho do stmt contém ReturnStmt (para checar funções void)
    bool hasAnyReturn  (const Stmt* stmt) const;

    // ── Lookups em class_table ────────────────────────────────────────────
    const MethodInfo* resolveMethod(const std::string& obj_type,
                                    const std::string& method_name) const;
    const FieldInfo*  resolveField (const std::string& obj_type,
                                    const std::string& field_name,
                                    bool require_public) const;

    // ── Utilidades internas ───────────────────────────────────────────────
    // Lança SemanticError com mensagem formatada (nunca retorna).
    [[noreturn]] void throwError(const std::string& msg, const Token& tok) const;
    [[noreturn]] void throwError(const std::string& msg, int line, int col) const;

public:
    SemanticAnalyzer() = default;

    // Ponto de entrada.
    // Percorre e anota a AST inteira.
    // Lança SemanticError no primeiro problema encontrado.
    void analyze(Program& program);
};

} // namespace cinza

#endif // CINZA_SEMANTIC_H
