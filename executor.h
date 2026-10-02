#ifndef CINZA_EXECUTOR_H
#define CINZA_EXECUTOR_H

#include "ast.h"
#include "value.h"
#include "runtime_error.h"
#include "natives.h"
#include <unordered_map>
#include <set>
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
//     → preRegisterGlobals()   (registra funções e classes)
//     → executeStmt*           (avalia os const globais — Fase 2.5: o nível
//                               superior só tem declarações)
//     → executeFunction(main)  (com args, se main tiver list<string>)
//
//   Chamada de função, método ou construtor:
//     → FrameGuard: frame novo com os slots da função (B2); o do chamador
//       fica invisível
//     → parâmetros nos slots 0..n-1
//     → executeStmt* no corpo, até um Flow::Return (B4)
//     → valor de retorno lido de return_value
//     → o destrutor do guard fecha o frame
//   Métodos e construtores também ajustam current_instance (InstanceGuard).
//
// ── Sobre current_instance ───────────────────────────────────────────────
//
// Dentro de um método, `nome` pode se referir a um campo da classe.
// O executor mantém `current_instance` apontando para o objeto sendo
// executado. B2: o semântico já decidiu se cada nome é slot local, campo de
// current_instance ou slot global; slotOf() só segue essa resolução.
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

// B4: resultado da execução de um statement. Controle de fluxo não usa mais
// exceções: Return/Break/Continue sobem pelos blocos até quem os consome
// (função para Return; laço para Break/Continue, usados a partir da Fase 4).
enum class Flow { Normal, Return, Break, Continue };

class Executor {
private:
    // B2: variáveis em slots, sem busca por nome. Cada chamada (função,
    // método, construtor, inicializadores de campo) empilha um frame com o
    // número de slots que o semântico contou; `locals` aponta para os slots do
    // frame atual. O buffer de cada frame não muda de lugar enquanto ele vive
    // (mover o vector interno preserva o buffer), então `locals` segue válido
    // quando frames de baixo são empilhados.
    std::vector<std::vector<Value>> frames;
    Value*                          locals = nullptr;
    std::vector<Value>              globals;   // const de todos os módulos

    // B4: valor do último `return` executado; consumido (movido) pela
    // chamada de função/método que recebeu Flow::Return
    Value return_value;

    // Registros globais: nome → ponteiro para nó da AST (não owning)
    std::unordered_map<std::string, const FunctionDecl*> function_registry;
    std::unordered_map<std::string, const ClassDecl*>    class_registry;
    std::unordered_map<std::string, const StructDecl*>   struct_registry;   // C3
    std::set<std::string>                                error_kinds;       // C4

    // Contexto do método em execução
    // nullptr quando estamos fora de qualquer método de classe
    ClassInstance* current_instance = nullptr;
    // A10/B5: o mesmo objeto como Value (shared_ptr), para `self` poder ser
    // lido, passado e retornado; VOID fora de métodos
    Value          current_self;

    // A6: profundidade de chamadas (função, método, construtor) e seu limite.
    // Acima do limite: RuntimeError "StackOverflowError", em vez de a
    // recursão estourar a pilha nativa (segmentation fault).
    int max_call_depth = 2000;

    // C4/B6: pilha de chamadas para o stack trace. Cada chamada guarda o nome
    // e a linha, em quem chamou, de onde ela foi feita. A profundidade (A6) é
    // o tamanho desta pilha.
    struct CallFrame {
        const std::string* name;   // trace_name da declaração (vive na AST)
        int         call_line;
        int         file_id;     // C5: arquivo onde a função está
    };
    std::vector<CallFrame> call_stack;
    friend struct CallGuard;
    friend struct FrameGuard;

    // Lança o erro anexando o stack trace do momento (se ainda não tiver)
    [[noreturn]] void raise(RuntimeError err) const;
    std::vector<std::string> snapshotTrace(int error_line) const;

    // ── Pré-registro ─────────────────────────────────────────────────────
    void preRegisterGlobals(const Program& program);

    // ── Execução de statements ────────────────────────────────────────────
    // B4: cada statement devolve Flow; blocos e laços propagam o que não é Normal
    Flow executeStmt       (const Stmt* stmt);
    Flow executeBlock      (const BlockStmt* block);
    void executeVarDecl    (const VarDeclStmt* stmt);
    void executeAssign     (const AssignStmt* stmt);         // B5
    void executeExprStmt   (const ExprStmt* stmt);
    Flow executeIf         (const IfStmt* stmt);
    Flow executeWhile      (const WhileStmt* stmt);
    Flow executeFor        (const ForStmt* stmt);
    Flow executeReturn     (const ReturnStmt* stmt);
    Flow executeTry        (const TryStmt* stmt);           // C4
    void executeThrow      (const ThrowStmt* stmt);         // C4

    // Executa o corpo de função/método/construtor no frame já aberto e
    // devolve o valor de retorno (VOID se o corpo terminar sem return)
    Value executeBody      (const Stmt* body);

    // ── Execução de expressões ────────────────────────────────────────────
    // Cada método avalia o nó e retorna um Value.
    Value evalExpr         (const Expr* expr);
    Value evalLiteral      (const LiteralExpr* expr);
    Value evalIdentifier   (const IdentifierExpr* expr);
    Value evalBinary       (const BinaryExpr* expr);
    Value applyBinary      (TokenType op, const Value& left, const Value& right,
                            const Token& tok);   // C1
    Value evalUnary        (const UnaryExpr* expr);
    Value evalCall         (const CallExpr* expr);
    Value evalMethodCall   (const MethodCallExpr* expr);
    Value evalMemberAccess (const MemberAccessExpr* expr);
    Value evalIndexAccess  (const IndexAccessExpr* expr);
    Value evalNew          (const NewExpr* expr);
    Value evalListLiteral  (const ListLiteralExpr* expr);
    Value evalDictLiteral  (const DictLiteralExpr* expr);
    Value evalPairLiteral  (const PairLiteralExpr* expr);
    Value evalCast         (const CastExpr* expr);
    Value callNative       (const NativeFn& fn, const CallExpr* expr);   // C6

    // ── Chamada de função / construtor ────────────────────────────────────
    // Abre frame, define parâmetros e executa o corpo via executeBody (B4).
    // call_line: linha da chamada em quem chamou (0 para main), para o trace
    // `frame`: slots do novo frame, com os argumentos já nos slots 0..n-1
    // (evalArgs), para não copiar os argumentos de um vetor para o frame
    Value executeFunction(const FunctionDecl* fn,
                          std::vector<Value> frame, int call_line);

    // Executa o construtor de uma instância (`self` é o Value do objeto novo).
    // Modifica o objeto diretamente (sem retorno).
    void  executeConstructor(const Value& self,
                             const ClassDecl::Constructor& ctor,
                             std::vector<Value> frame, int call_line);

    // Executa um método em uma instância (`self` é o Value do objeto).
    Value executeMethod(const Value& self,
                        const FunctionDecl* method,
                        std::vector<Value> frame, int call_line);

    // Avalia os argumentos direto nos primeiros slots de um frame novo
    std::vector<Value> evalArgs(const std::vector<ExprPtr>& args, std::uint32_t num_slots);

    // ── B5: lugar de uma atribuição ───────────────────────────────────────
    // Caminho do alvo: variável (ou valor temporário) de base e os passos
    // até o lugar (campo ou índice já avaliado).
    struct PlaceStep {
        bool         is_member;
        std::string  member;    // is_member: nome do campo
        int          index;     // is_member: índice do campo (B2)
        Value        key;       // !is_member: índice (list) ou chave (dict)
        const Token* token;
    };
    struct PlacePath {
        const IdentifierExpr*  base_var = nullptr;  // nullptr: base é base_temp
        Value                  base_temp;
        std::vector<PlaceStep> steps;
    };
    // Etapa 1: avalia, da esquerda para a direita, a base temporária e os índices
    void   collectPlace(const Expr* target, PlacePath& path);
    // Etapa 2: percorre o caminho sem executar código do usuário e devolve o lugar
    Value* walkPlace(PlacePath& path);

    // ── Operações built-in em coleções ────────────────────────────────────
    Value callListMethod  (Value& obj, const std::string& method,
                           const std::vector<Value>& args, const Token& tok);
    Value callDictMethod  (Value& obj, const std::string& method,
                           const std::vector<Value>& args, const Token& tok);
    Value callStringMethod(Value& obj, const std::string& method,
                           const std::vector<Value>& args, const Token& tok);

    // ── Resolução de nomes (A1) ───────────────────────────────────────────
    // Frame atual → campos de current_instance → globais.
    // Retorna nullptr se não encontrar (não deveria ocorrer após o semântico).
    // O ponteiro vale até o próximo define()/pushScope()/pushFrame().
    // B2: o lugar da variável, pela resolução anotada no semântico
    Value* slotOf(const IdentifierExpr* id);

    // ── op<...> ───────────────────────────────────────────────────────────
    // Tipo real de um valor cujo tipo estático é `st` (se `st` for um op, o
    // tipo do op que o valor guarda agora)
    static TypeRef runtimeType(const Value& v, TypeRef st);
    // Confere se o valor (de tipo estático `from`) cabe em `to`; TypeError se
    // não couber. Converte int em decimal quando `to` pede.
    Value narrow(Value v, TypeRef from, TypeRef to, const Token& tok) const;
    // Atribuição a quem já travou (tipo desconhecido na compilação): mantém o tipo
    static Value keepLock(const Value& atual, Value novo);

    // ── Utilitário de erro ────────────────────────────────────────────────
    [[noreturn]] void throwRuntimeError(const std::string& msg,
                                        const Token& tok) const;

public:
    // max_depth: limite de chamadas aninhadas antes de StackOverflowError (A6)
    explicit Executor(int max_depth = 2000) : max_call_depth(max_depth) {}

    // Ponto de entrada.
    // Avalia os const globais e chama main; `args` vira o list<string> de
    // main(list<string> args), se ela tiver esse parâmetro (Fase 2.5).
    // Lança RuntimeError em caso de erro em tempo de execução.
    // C5: módulos em ordem topológica (principal por último); os const de cada
    // módulo são avaliados nessa ordem e depois main roda
    void execute(const std::vector<const Program*>& programs, const GlobalLayout& layout,
                 const std::vector<std::string>& args = {});
};

} // namespace cinza

#endif // CINZA_EXECUTOR_H
