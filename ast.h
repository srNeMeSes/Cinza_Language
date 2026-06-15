#ifndef CINZA_AST_H
#define CINZA_AST_H

#include <memory>
#include <vector>
#include <string>
#include <variant>
#include "lexer.h"


//  AQUI NÓS TEMOS BASICAMENTE A ESTRUTURA DA NOSSA AST


namespace cinza {

// Forward declarations
class Expr;
class Stmt;
class Type;
class Program; 

// Smart pointers para gerenciamento automático de memória (RAII)
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using TypePtr = std::unique_ptr<Type>;


// ============================================================================
// TIPOS  |  Estrutura de tipos da nossa linguagem
// ============================================================================

class Type {
public:
    enum class Kind {
        INT,       // 123
        DECIMAL,   // sinônimo de DOUBLE
        STRING,    // "amigo de cu é rola"
        BOOL,      // true / false
        VOID,      // sem retorno
        LIST,      // list<T>
        DICT,      // dict<K, V> 
        PAIR,      // par genérico pair<F, S>
        VAR,       // tipo inferido
        CUSTOM     // tipos customizados (class / struct)
    };
    
    Kind kind;
    std::string name;
    
    // Para tipos genéricos (list<T>, dict<K,V>, pair<F,S)
    std::vector<TypePtr> type_params;
    
    explicit Type(Kind k, const std::string& n = "") : kind(k), name(n) {}
    
    std::string toString() const;
    bool isNumeric() const;
    bool isComparable() const;
};


// ============================================================================
// EXPRESSÕES  |  Estrutura de expressões da nossa linguagem
// ============================================================================

// classe base para espressões
class Expr {
public:
    Token       token;         // Token associado para rastreamento de posição
    std::string resolved_type; // Anotado pelo SemanticAnalyzer ("int", "string", …)

    virtual ~Expr() = default;

    // virtual pura "toString", será sobrescrita nas classes derivadas
    virtual std::string toString(int indent = 0) const = 0;
    
protected:
    explicit Expr(const Token& tok) : token(tok) {}
};

// Literais (int, decimal, str, bool)
class LiteralExpr : public Expr {
public:
    // Variante dos tipos literais suportados pela linguagem
    std::variant<int, double, std::string, bool> value;
    
    LiteralExpr(const Token& tok, int val)              : Expr(tok), value(val) {}
    LiteralExpr(const Token& tok, double val)           : Expr(tok), value(val) {}
    LiteralExpr(const Token& tok, const std::string& val) : Expr(tok), value(val) {}
    LiteralExpr(const Token& tok, bool val)             : Expr(tok), value(val) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Identificador (variável)
class IdentifierExpr : public Expr {
public:
    std::string name;
    
    IdentifierExpr(const Token& tok, const std::string& n) : Expr(tok), name(n) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Operação binária (a + b, a == b, etc)
class BinaryExpr : public Expr {
public:
    ExprPtr left;
    TokenType op;
    ExprPtr right;
    
    BinaryExpr(const Token& tok, ExprPtr l, TokenType o, ExprPtr r)
        : Expr(tok), left(std::move(l)), op(o), right(std::move(r)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Operação unária (!x, -x)
class UnaryExpr : public Expr {
public:
    TokenType op;
    ExprPtr operand;
    
    UnaryExpr(const Token& tok, TokenType o, ExprPtr operand_expr)
        : Expr(tok), op(o), operand(std::move(operand_expr)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Chamada de função
class CallExpr : public Expr {
public:
    std::string function_name;
    std::vector<ExprPtr> arguments;
    
    CallExpr(const Token& tok, const std::string& name, std::vector<ExprPtr> args)
        : Expr(tok), function_name(name), arguments(std::move(args)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Acesso a membro (obj.field)
class MemberAccessExpr : public Expr {
public:
    ExprPtr object;
    std::string member_name;
    
    MemberAccessExpr(const Token& tok, ExprPtr obj, const std::string& member)
        : Expr(tok), object(std::move(obj)), member_name(member) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Literal de lista [1, 2, 3]
class ListLiteralExpr : public Expr {
public:
    std::vector<ExprPtr> elements;
    
    ListLiteralExpr(const Token& tok, std::vector<ExprPtr> elems)
        : Expr(tok), elements(std::move(elems)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Literal de dicionário {{"key", value}}
class DictLiteralExpr : public Expr {
public:
    std::vector<std::pair<ExprPtr, ExprPtr>> pairs;  // dicionario
    
    DictLiteralExpr(const Token& tok, std::vector<std::pair<ExprPtr, ExprPtr>> p)
        : Expr(tok), pairs(std::move(p)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Literal de par {key, value}  →  pair<K, V> usado em add/atribuição
class PairLiteralExpr : public Expr {
public:
    ExprPtr first;
    ExprPtr second;
    
    PairLiteralExpr(const Token& tok, ExprPtr f, ExprPtr s)
        : Expr(tok), first(std::move(f)), second(std::move(s)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Acesso por índice ou chave: lista[0], dict["chave"]
class IndexAccessExpr : public Expr {
public:
    ExprPtr object;
    ExprPtr index;   // pode ser inteiro (lista) ou string (dict)
    
    IndexAccessExpr(const Token& tok, ExprPtr obj, ExprPtr idx)
        : Expr(tok), object(std::move(obj)), index(std::move(idx)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Chamada de método: objeto.metodo(args)
class MethodCallExpr : public Expr {
public:
    ExprPtr object;
    std::string method_name;
    std::vector<ExprPtr> arguments;
    
    MethodCallExpr(const Token& tok, ExprPtr obj, const std::string& method,
                   std::vector<ExprPtr> args)
        : Expr(tok), object(std::move(obj)), method_name(method),
          arguments(std::move(args)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Instanciação de objeto:  new Pessoa("Jose", 22, 3.400)
// Sempre aparece como expressão no lado direito de uma declaração ou atribuição
class NewExpr : public Expr {
public:
    std::string          class_name;
    std::vector<ExprPtr> arguments;

    NewExpr(const Token& tok, const std::string& name, std::vector<ExprPtr> args)
        : Expr(tok), class_name(name), arguments(std::move(args)) {}

    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};


// ============================================================================
// STATEMENTS  |  Estrutura de instruções da nossa linguagem
// ============================================================================

// classe base para instruções
class Stmt {
public:
    Token token;
    
    virtual ~Stmt() = default;

    // A função toString será sobrecrita nas classes derivadas :)
    virtual std::string toString(int indent = 0) const = 0;
    
protected:
    explicit Stmt(const Token& tok) : token(tok) {}
};

// Declaração de variável
class VarDeclStmt : public Stmt {
public:
    TypePtr type;
    std::string name;
    // nullptr somente para list<T> e dict<K,V>: recebem inicialização vazia implícita.
    // Todos os outros tipos exigem inicializador — o parser rejeita a declaração antes.
    ExprPtr initializer;
    bool is_const = false; // const: não pode ser reatribuído após inicialização
    
    VarDeclStmt(const Token& tok, TypePtr t, const std::string& n, ExprPtr init,
                bool cnst = false)
        : Stmt(tok), type(std::move(t)), name(n), initializer(std::move(init)),
          is_const(cnst) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Atribuição
class AssignmentStmt : public Stmt {
public:
    std::string variable_name;
    ExprPtr value;
    
    AssignmentStmt(const Token& tok, const std::string& name, ExprPtr val)
        : Stmt(tok), variable_name(name), value(std::move(val)) {}

    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// v2.00 #8: Atribuição por índice/chave: lista[i] = v  ou  dict["k"] = v
class IndexAssignmentStmt : public Stmt {
public:
    std::string object_name;   // nome da coleção (string simples)
    ExprPtr     index;         // índice (int para list, K para dict)
    ExprPtr     value;         // valor a atribuir

    IndexAssignmentStmt(const Token& tok, const std::string& obj,
                        ExprPtr idx, ExprPtr val)
        : Stmt(tok), object_name(obj), index(std::move(idx)), value(std::move(val)) {}

    std::string toString(int indent = 0) const override;
};

// Expressão statement (chamada de função sozinha, etc)
class ExprStmt : public Stmt {
public:
    ExprPtr expression;
    
    ExprStmt(const Token& tok, ExprPtr expr)
        : Stmt(tok), expression(std::move(expr)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Bloco { ... }
class BlockStmt : public Stmt {
public:
    std::vector<StmtPtr> statements;
    
    BlockStmt(const Token& tok, std::vector<StmtPtr> stmts)
        : Stmt(tok), statements(std::move(stmts)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// If-Else
class IfStmt : public Stmt {
public:
    ExprPtr condition;
    StmtPtr then_branch;
    StmtPtr else_branch; // pode ser nullptr
    
    IfStmt(const Token& tok, ExprPtr cond, StmtPtr then_stmt, StmtPtr else_stmt)
        : Stmt(tok), condition(std::move(cond)), 
          then_branch(std::move(then_stmt)), else_branch(std::move(else_stmt)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// While
class WhileStmt : public Stmt {
public:
    ExprPtr condition;
    StmtPtr body;
    
    WhileStmt(const Token& tok, ExprPtr cond, StmtPtr body_stmt)
        : Stmt(tok), condition(std::move(cond)), body(std::move(body_stmt)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// For
class ForStmt : public Stmt {
public:
    TypePtr type_iterator;
    std::string iterator_name;
    ExprPtr iterable;
    StmtPtr body;
    
    ForStmt(const Token& tok, TypePtr type, const std::string& iter, ExprPtr iter_expr, StmtPtr body_stmt)
        : Stmt(tok), type_iterator(std::move(type)),  iterator_name(iter), iterable(std::move(iter_expr)), 
          body(std::move(body_stmt)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Return
class ReturnStmt : public Stmt {
public:
    ExprPtr value; 
    
    ReturnStmt(const Token& tok, ExprPtr val)
        : Stmt(tok), value(std::move(val)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Parâmetro de função
struct Parameter {
    TypePtr type;
    std::string name;
    Token token;
    bool is_const = false; // parâmetro const: não pode ser reatribuído no corpo
    
    Parameter(TypePtr t, const std::string& n, const Token& tok, bool cnst = false)
        : type(std::move(t)), name(n), token(tok), is_const(cnst) {}
};

// Declaração de função
class FunctionDecl : public Stmt {
public:
    std::string name;
    std::vector<Parameter> parameters;
    TypePtr return_type;
    StmtPtr body; // sempre um BlockStmt
    
    FunctionDecl(const Token& tok, const std::string& fname, 
                 std::vector<Parameter> params, TypePtr ret_type, StmtPtr body_stmt)
        : Stmt(tok), name(fname), parameters(std::move(params)),
          return_type(std::move(ret_type)), body(std::move(body_stmt)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// ============================================================================
// CLASSE
//
//  Sintaxe:
//    class Pilha {
//        int topo = 0;          ← campo privado (inicializador obrigatório)
//        list<int> dados;       ← campo privado (list/dict nascem vazios implicitamente)
//
//        pub {                                   ← bloco público
//            Pilha(list<int> d) { dados = d; }   ← construtor
//            fn push(int x) -> void { ... }      ← metodo publico
//            fn pop()       -> int  { ... }      ← metodo publico
//        }
//    }
//
//  Regras (verificadas mais tarde pelo semântico):
//    - Sem herança, sem override, sem polimorfismo de subtipo
//    - Tudo fora de pub{} é privado
//    - list<T> e dict<K,V> podem omitir inicializador e nascem vazios
//    - Todos os demais campos exigem inicializador explícito
// ============================================================================
class ClassDecl : public Stmt {
public:
    // Representa um campo declarado no corpo da classe (fora do pub{})
    struct Field {
        TypePtr     type;
        std::string name;
        // nullptr indica inicialização vazia implícita.
        // Invariante: nullptr é válido SOMENTE para list<T> e dict<K,V>.
        // Campos de qualquer outro tipo devem chegar aqui com inicializador
        // (o parser rejeita a declaração caso contrário).
        ExprPtr     initializer;
        Token       token;

        Field(TypePtr t, const std::string& n, ExprPtr init, const Token& tok)
            : type(std::move(t)), name(n), initializer(std::move(init)), token(tok) {}

        // Não copiável (unique_ptr), mas movível
        Field(Field&&)            = default;
        Field& operator=(Field&&) = default;
    };

    // Construtor da classe (opcional)
    //   Sintaxe dentro de pub{} — sem 'fn', sem '-> void':
    //     Pessoa(str n, int i, decimal s) { nome = n; ... }
    //
    //   Regras (verificadas pelo semântico):
    //     - Mesmo nome da classe
    //     - Sempre void (não retorna valor)
    //     - Máximo um por classe
    struct Constructor {
        std::vector<Parameter> parameters;
        StmtPtr                body;
        Token                  token;

        Constructor(std::vector<Parameter> params, StmtPtr b, const Token& tok)
            : parameters(std::move(params)), body(std::move(b)), token(tok) {}

        // Não copiável, mas movível
        Constructor(Constructor&&)            = default;
        Constructor& operator=(Constructor&&) = default;
    };

    std::string                        class_name;
    std::vector<Field>                 fields;        // campos privados (fora do pub{})
    std::unique_ptr<Constructor>       constructor;   // nullptr → sem construtor
    std::vector<StmtPtr>               priv_methods;  // fn fora do pub{} → privados
    std::vector<StmtPtr>               pub_methods;   // fn dentro do pub{} → públicos

    ClassDecl(const Token& tok,
              const std::string& name,
              std::vector<Field>           flds,
              std::unique_ptr<Constructor> ctor,
              std::vector<StmtPtr>         priv,
              std::vector<StmtPtr>         pub
            )
            : Stmt(tok), class_name(name),
            fields(std::move(flds)),
            constructor(std::move(ctor)),
            priv_methods(std::move(priv)),
            pub_methods(std::move(pub)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};


// ============================================================================
// PROGRAMA (raiz da AST)  |  A bagaça começa aqui
// ============================================================================
class Program {
public:
    std::vector<StmtPtr> statements; // funções e statements globais
    
    Program() = default;
    explicit Program(std::vector<StmtPtr> stmts) : statements(std::move(stmts)) {}
    
    // Program {...}
    std::string toString() const;
};


// ============================================================================
// UTILITÁRIOS  |  Funções auxiliares basicas
// ============================================================================
std::string indent(int level);  // retorna: std::string(level * 2, ' ')
std::string tokenTypeToOperatorString(TokenType type);      // retorna: +, -, /, *, ... ou <?>

} // namespace cinza

#endif // CINZA_AST_H
