#ifndef CINZA_EXECUTOR_H
#define CINZA_EXECUTOR_H

#include "ast.h"
#include "value.h"
#include "environment.h"
#include "runtime_error.h"
#include <unordered_map>
#include <string>

namespace cinza {

// ============================================================================
// EXECUTOR
//
// Tree-walk interpreter da linguagem Cinza.
//
// Recebe um Program cuja AST já foi anotada pelo SemanticAnalyzer e executa
// cada nó diretamente, confiando que todas as validações de tipo e escopo
// já foram feitas. Não re-verifica tipos — apenas executa.
//
// Erros que o semântico não detecta (IndexError, KeyError, DivisionByZero)
// são lançados como RuntimeError.
//
// ── Fluxo de execução ────────────────────────────────────────────────────
//
//   Program
//     → preRegisterGlobals()   (registra funções e classes no env global)
//     → executeStmt*           (executa cada statement global)
//
//   Chamada de função:
//     → pushScope()
//     → define() para cada parâmetro
//     → executeStmt* no corpo
//     → captura ReturnSignal para valor de retorno
//     → popScope()
//
//   Chamada de método:
//     → cria instância de Environment para o método
//     → define() campos da instância no escopo
//     → define() parâmetros
//     → executeStmt* no corpo
//     → sincroniza campos modificados de volta para a instância
//     → captura ReturnSignal
//
// ── Sobre current_instance ───────────────────────────────────────────────
//
// Dentro de um método, `nome` pode se referir a um campo da classe.
// O executor mantém `current_instance` apontando para o objeto sendo
// executado. Na resolução de identificadores e atribuições, o executor
// verifica o Environment primeiro e, se não encontrar, verifica os
// campos de current_instance.
//
// ── Invariante de inicialização ──────────────────────────────────────────
//
// O executor não trabalha com estados UNBOUND para variáveis ou campos.
// Ele recebe a AST já validada pelo semântico e assume que:
//   - toda variável não-coleção possui inicializador (ou foi rejeitada antes)
//   - list<T> e dict<K,V> sem inicializador nascem como coleções vazias
//   - VOID_VAL (Value()) é um valor interno do runtime, não um estado de
//     variável do usuário — aparece apenas em retornos de funções void
//
// ============================================================================

class Executor {
private:
    Environment env;

    // Registros globais: nome → ponteiro para nó da AST (não owning)
    std::unordered_map<std::string, const FunctionDecl*> function_registry;
    std::unordered_map<std::string, const ClassDecl*>    class_registry;

    // Contexto do método em execução
    // nullptr quando estamos fora de qualquer método de classe
    ClassInstance* current_instance = nullptr;

    // ── Pré-registro ─────────────────────────────────────────────────────
    void preRegisterGlobals(const Program& program);

    // ── Execução de statements ────────────────────────────────────────────
    void executeStmt       (const Stmt* stmt);
    void executeBlock      (const BlockStmt* block);
    void executeVarDecl    (const VarDeclStmt* stmt);
    void executeAssignment (const AssignmentStmt* stmt);
    void executeIndexAssignment(const IndexAssignmentStmt* stmt);  // v2.00 #8
    void executeExprStmt   (const ExprStmt* stmt);
    void executeIf         (const IfStmt* stmt);
    void executeWhile      (const WhileStmt* stmt);
    void executeFor        (const ForStmt* stmt);
    void executeReturn     (const ReturnStmt* stmt);
    void executePrint      (const CallExpr* call);

    // ── Execução de expressões ────────────────────────────────────────────
    // Cada método avalia o nó e retorna um Value.
    Value evalExpr         (const Expr* expr);
    Value evalLiteral      (const LiteralExpr* expr);
    Value evalIdentifier   (const IdentifierExpr* expr);
    Value evalBinary       (const BinaryExpr* expr);
    Value evalUnary        (const UnaryExpr* expr);
    Value evalCall         (const CallExpr* expr);
    Value evalMethodCall   (const MethodCallExpr* expr);
    Value evalMemberAccess (const MemberAccessExpr* expr);
    Value evalIndexAccess  (const IndexAccessExpr* expr);
    Value evalNew          (const NewExpr* expr);
    Value evalListLiteral  (const ListLiteralExpr* expr);
    Value evalDictLiteral  (const DictLiteralExpr* expr);
    Value evalPairLiteral  (const PairLiteralExpr* expr);

    // ── Chamada de função / construtor ────────────────────────────────────
    // Configura escopo, executa corpo, captura ReturnSignal.
    Value executeFunction(const FunctionDecl* fn,
                          const std::vector<Value>& args);

    // Executa o construtor de uma instância.
    // Modifica o objeto `instance` diretamente (sem retorno).
    void  executeConstructor(ClassInstance& instance,
                             const ClassDecl::Constructor& ctor,
                             const std::vector<Value>& args);

    // Executa um método em uma instância.
    Value executeMethod(ClassInstance& instance,
                        const FunctionDecl* method,
                        const std::vector<Value>& args);

    // ── Operações built-in em coleções ────────────────────────────────────
    Value callListMethod  (Value& obj, const std::string& method,
                           const std::vector<Value>& args, const Token& tok);
    Value callDictMethod  (Value& obj, const std::string& method,
                           const std::vector<Value>& args, const Token& tok);
    Value callStringMethod(Value& obj, const std::string& method,
                           const std::vector<Value>& args, const Token& tok);

    // ── Atribuição com target resolvido ───────────────────────────────────
    // Tenta atribuir no Environment; se não encontrar, tenta nos campos
    // de current_instance. Lança RuntimeError se nenhum dos dois tiver
    // a variável (nunca deve ocorrer após análise semântica).
    void assignVariable(const std::string& name, Value val, const Token& tok);

    // ── Utilitário de erro ────────────────────────────────────────────────
    [[noreturn]] void throwRuntimeError(const std::string& msg,
                                        const Token& tok) const;

public:
    Executor() = default;

    // Ponto de entrada.
    // Executa o programa inteiro.
    // Lança RuntimeError em caso de erro em tempo de execução.
    void execute(const Program& program);
};

} // namespace cinza

#endif // CINZA_EXECUTOR_H
