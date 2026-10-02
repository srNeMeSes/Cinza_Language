#ifndef CINZA_AST_H
#define CINZA_AST_H

#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <variant>
#include "lexer.h"
#include "types.h"


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
        OP,        // op<T1, T2, ...>
        VAR,       // tipo inferido
        CUSTOM     // tipos customizados (class / struct)
    };
    
    Kind kind;
    std::string name;
    // C5: nome escrito como apelido.Nome já foi trocado pelo nome completo
    bool qualified = false;
    Token token;   // C5: onde o tipo foi escrito (mensagens de erro)
    
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

// B3: tipo concreto do nó, preenchido pelo construtor de cada classe.
// Permite despachar com switch + static_cast em vez de cadeias de dynamic_cast.
enum class NodeKind {
    // expressões
    Literal, Identifier, Binary, Unary, Call, MemberAccess, ListLiteral,
    DictLiteral, PairLiteral, IndexAccess, MethodCall, New, Cast, TypeLiteral,
    // statements
    VarDecl, Assign, ExprStmt, Block, If, While, For,
    Return, Break, Continue, Try, Throw, FunctionDecl, ClassDecl, StructDecl, ErrorDecl, EnumDecl,
    InterfaceDecl
};

// classe base para espressões
class Expr {
public:
    const NodeKind node_kind;  // B3: classe concreta do nó
    Token       token;         // Token associado para rastreamento de posição
    TypeRef     resolved_type = nullptr; // B1: anotado pelo SemanticAnalyzer

    virtual ~Expr() = default;

    // virtual pura "toString", será sobrescrita nas classes derivadas
    virtual std::string toString(int indent = 0) const = 0;
    
protected:
    Expr(NodeKind k, const Token& tok) : node_kind(k), token(tok) {}
};

// Literais (int, decimal, str, bool)
class LiteralExpr : public Expr {
public:
    // Variante dos tipos literais suportados pela linguagem
    std::variant<std::int64_t, double, std::string, bool> value;   // A5: int de 64 bits
    
    LiteralExpr(const Token& tok, std::int64_t val)     : Expr(NodeKind::Literal, tok), value(val) {}
    LiteralExpr(const Token& tok, double val)           : Expr(NodeKind::Literal, tok), value(val) {}
    LiteralExpr(const Token& tok, const std::string& val) : Expr(NodeKind::Literal, tok), value(val) {}
    LiteralExpr(const Token& tok, bool val)             : Expr(NodeKind::Literal, tok), value(val) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Identificador (variável)
// B2: onde uma variável vive, decidido pelo semântico. O executor acessa por
// índice, sem procurar o nome:
//   Local  → slot no frame da função em execução
//   Field  → índice do campo no objeto atual (current_instance)
//   Global → slot nos const globais (de todos os módulos, e Math.pi etc.)
//   Self   → o objeto atual
struct Resolution {
    enum class Kind : std::uint8_t { None, Local, Field, Global, Self,
                                     Type /* nome de class/struct/erro usado como tipo */ };
    Kind          kind = Kind::None;
    std::uint32_t slot = 0;
};

struct NativeConst;
class EnumDecl;
class InterfaceDecl;

// B2: const globais em slots. O semântico conta os slots e diz quais são
// constantes de módulos nativos (valor já pronto); os demais recebem o valor
// quando o executor avalia os const de cada módulo.
struct GlobalLayout {
    std::uint32_t count = 0;
    std::vector<std::pair<std::uint32_t, const NativeConst*>> natives;
};

class IdentifierExpr : public Expr {
public:
    std::string name;
    Resolution  res;   // B2
    TypeRef     type_value = nullptr;   // res.kind == Type: o tipo (Pessoa, ValueError)
    
    IdentifierExpr(const Token& tok, const std::string& n) : Expr(NodeKind::Identifier, tok), name(n) {}
    
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
        : Expr(NodeKind::Binary, tok), left(std::move(l)), op(o), right(std::move(r)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Operação unária (!x, -x)
class UnaryExpr : public Expr {
public:
    TokenType op;
    ExprPtr operand;
    
    UnaryExpr(const Token& tok, TokenType o, ExprPtr operand_expr)
        : Expr(NodeKind::Unary, tok), op(o), operand(std::move(operand_expr)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Chamada de função
struct NativeFn;
class FunctionDecl;

class CallExpr : public Expr {
public:
    std::string function_name;
    std::vector<ExprPtr> arguments;
    const NativeFn* native = nullptr;   // C6: nativa resolvida pelo semântico
    // Custo das chamadas: função (ou método da própria classe, se
    // implicit_method) resolvida pelo semântico; o executor não procura o nome
    const FunctionDecl* target = nullptr;
    bool implicit_method = false;
    // op<...>: type(x) embutido; guarda o tipo estático do argumento
    TypeRef type_of = nullptr;
    
    CallExpr(const Token& tok, const std::string& name, std::vector<ExprPtr> args)
        : Expr(NodeKind::Call, tok), function_name(name), arguments(std::move(args)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Acesso a membro (obj.field)
class MemberAccessExpr : public Expr {
public:
    ExprPtr object;
    std::string member_name;
    int field_index = -1;   // B2: índice do campo (objeto de classe ou struct); no enum, do valor
    const EnumDecl* enum_decl = nullptr;   // Cor.Verde: valor de enum (object é o tipo)

    MemberAccessExpr(const Token& tok, ExprPtr obj, const std::string& member)
        : Expr(NodeKind::MemberAccess, tok), object(std::move(obj)), member_name(member) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Literal de lista [1, 2, 3]
class ListLiteralExpr : public Expr {
public:
    std::vector<ExprPtr> elements;
    
    ListLiteralExpr(const Token& tok, std::vector<ExprPtr> elems)
        : Expr(NodeKind::ListLiteral, tok), elements(std::move(elems)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Literal de dicionário {{"key", value}}
class DictLiteralExpr : public Expr {
public:
    std::vector<std::pair<ExprPtr, ExprPtr>> pairs;  // dicionario
    
    DictLiteralExpr(const Token& tok, std::vector<std::pair<ExprPtr, ExprPtr>> p)
        : Expr(NodeKind::DictLiteral, tok), pairs(std::move(p)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Literal de par {key, value}  →  pair<K, V> usado em add/atribuição
class PairLiteralExpr : public Expr {
public:
    ExprPtr first;
    ExprPtr second;
    
    PairLiteralExpr(const Token& tok, ExprPtr f, ExprPtr s)
        : Expr(NodeKind::PairLiteral, tok), first(std::move(f)), second(std::move(s)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Acesso por índice ou chave: lista[0], dict["chave"]
class IndexAccessExpr : public Expr {
public:
    ExprPtr object;
    ExprPtr index;   // pode ser inteiro (lista) ou string (dict)
    
    IndexAccessExpr(const Token& tok, ExprPtr obj, ExprPtr idx)
        : Expr(NodeKind::IndexAccess, tok), object(std::move(obj)), index(std::move(idx)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Chamada de método: objeto.metodo(args)
class MethodCallExpr : public Expr {
public:
    ExprPtr object;
    std::string method_name;
    std::vector<ExprPtr> arguments;
    const FunctionDecl* target = nullptr;   // método de classe resolvido pelo semântico
    // interface: chamada por um valor do tipo interface — o método é escolhido
    // em runtime pela classe do objeto (ClassDecl::itables)
    const InterfaceDecl* iface = nullptr;
    int iface_method = -1;
    
    MethodCallExpr(const Token& tok, ExprPtr obj, const std::string& method,
                   std::vector<ExprPtr> args)
        : Expr(NodeKind::MethodCall, tok), object(std::move(obj)), method_name(method),
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
        : Expr(NodeKind::New, tok), class_name(name), arguments(std::move(args)) {}

    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Conversão implícita inserida pelo SemanticAnalyzer (A2), nunca pelo parser.
// O tipo alvo fica em resolved_type; hoje só existe int → decimal.
// op<...>: tipo escrito como valor, para comparar com type(x):
//   type(n) == int     type(l) == list<int>
// (nomes de class/struct/erro chegam como IdentifierExpr com res.kind == Type)
class TypeLiteralExpr : public Expr {
public:
    TypePtr type;
    TypeRef value = nullptr;   // preenchido pelo semântico

    TypeLiteralExpr(const Token& tok, TypePtr t)
        : Expr(NodeKind::TypeLiteral, tok), type(std::move(t)) {}

    std::string toString(int indent = 0) const override;
};

class CastExpr : public Expr {
public:
    ExprPtr operand;

    CastExpr(ExprPtr op, TypeRef target)
        : Expr(NodeKind::Cast, op->token), operand(std::move(op)) { resolved_type = target; }

    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};


// ============================================================================
// STATEMENTS  |  Estrutura de instruções da nossa linguagem
// ============================================================================

// classe base para instruções
class Stmt {
public:
    const NodeKind node_kind;  // B3: classe concreta do nó
    Token token;
    bool  is_pub = false;      // C5: `pub` no nível superior (exportado pelo módulo)
    
    virtual ~Stmt() = default;

    // A função toString será sobrecrita nas classes derivadas :)
    virtual std::string toString(int indent = 0) const = 0;
    
protected:
    Stmt(NodeKind k, const Token& tok) : node_kind(k), token(tok) {}
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
    Resolution res;        // B2: slot Local (em função) ou Global (const do nível superior)
    
    VarDeclStmt(const Token& tok, TypePtr t, const std::string& n, ExprPtr init,
                bool cnst = false)
        : Stmt(NodeKind::VarDecl, tok), type(std::move(t)), name(n), initializer(std::move(init)),
          is_const(cnst) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// B5: atribuição com lvalue genérico:  alvo op valor;
// O alvo é um Identifier, MemberAccess ou IndexAccess (validado no parser):
//   x = v;   obj.campo = v;   m[i][j] = v;   obj.lista[0] = v;   self.nome = v;
// `op` é OP_ASSIGN; os operadores compostos (+=, -=, ...) entram na C1.
class AssignStmt : public Stmt {
public:
    ExprPtr   target;
    TokenType op;
    ExprPtr   value;
    // op<...>: o alvo tem tipo travado que o compilador não conhece (parâmetro,
    // campo...): o valor novo mantém o tipo travado (int vira decimal se o
    // alvo guarda decimal). O semântico já garantiu que o valor serve para
    // todos os tipos do op.
    bool      keep_lock   = false;

    AssignStmt(const Token& tok, ExprPtr t, TokenType o, ExprPtr v)
        : Stmt(NodeKind::Assign, tok), target(std::move(t)), op(o), value(std::move(v)) {}

    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Expressão statement (chamada de função sozinha, etc)
class ExprStmt : public Stmt {
public:
    ExprPtr expression;
    
    ExprStmt(const Token& tok, ExprPtr expr)
        : Stmt(NodeKind::ExprStmt, tok), expression(std::move(expr)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// Bloco { ... }
class BlockStmt : public Stmt {
public:
    std::vector<StmtPtr> statements;
    
    BlockStmt(const Token& tok, std::vector<StmtPtr> stmts)
        : Stmt(NodeKind::Block, tok), statements(std::move(stmts)) {}
    
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
        : Stmt(NodeKind::If, tok), condition(std::move(cond)), 
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
        : Stmt(NodeKind::While, tok), condition(std::move(cond)), body(std::move(body_stmt)) {}
    
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
    std::uint32_t iter_slot = 0;   // B2: slot local do iterador
    
    ForStmt(const Token& tok, TypePtr type, const std::string& iter, ExprPtr iter_expr, StmtPtr body_stmt)
        : Stmt(NodeKind::For, tok), type_iterator(std::move(type)),  iterator_name(iter), iterable(std::move(iter_expr)), 
          body(std::move(body_stmt)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};

// C4: try { ... } except (TipoDeErro e) { ... } ... finally { ... }
struct ExceptClause {
    std::string type_name;   // "ValueError", "Error", ou um tipo declarado com `error`
    std::string var_name;    // nome do erro dentro do bloco
    Token       token;
    StmtPtr     body;        // BlockStmt
    std::uint32_t var_slot = 0;   // B2: slot local do erro
};

class TryStmt : public Stmt {
public:
    StmtPtr                   body;            // BlockStmt
    std::vector<ExceptClause> handlers;        // na ordem em que aparecem
    StmtPtr                   finally_block;   // nullptr se não houver

    TryStmt(const Token& tok, StmtPtr b, std::vector<ExceptClause> h, StmtPtr f)
        : Stmt(NodeKind::Try, tok), body(std::move(b)), handlers(std::move(h)),
          finally_block(std::move(f)) {}

    std::string toString(int indent = 0) const override;
};

// C4: throw expr;  — expr é um erro: ValueError("msg"), SaldoInsuficiente("msg"), e
class ThrowStmt : public Stmt {
public:
    ExprPtr value;

    ThrowStmt(const Token& tok, ExprPtr v) : Stmt(NodeKind::Throw, tok), value(std::move(v)) {}

    std::string toString(int indent = 0) const override;
};

// C2: break;  — encerra o laço mais interno
class BreakStmt : public Stmt {
public:
    explicit BreakStmt(const Token& tok) : Stmt(NodeKind::Break, tok) {}
    std::string toString(int indent = 0) const override;
};

// C2: continue;  — vai para a próxima iteração do laço mais interno
class ContinueStmt : public Stmt {
public:
    explicit ContinueStmt(const Token& tok) : Stmt(NodeKind::Continue, tok) {}
    std::string toString(int indent = 0) const override;
};

// Return
class ReturnStmt : public Stmt {
public:
    ExprPtr value; 
    
    ReturnStmt(const Token& tok, ExprPtr val)
        : Stmt(NodeKind::Return, tok), value(std::move(val)) {}
    
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
    std::uint32_t num_slots = 0;   // B2: parâmetros (slots 0..n-1) + locais
    std::string trace_name;        // nome no stack trace ("f", "Classe.metodo"), montado uma vez
    
    FunctionDecl(const Token& tok, const std::string& fname, 
                 std::vector<Parameter> params, TypePtr ret_type, StmtPtr body_stmt)
        : Stmt(NodeKind::FunctionDecl, tok), name(fname), parameters(std::move(params)),
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
        std::uint32_t          num_slots = 0;   // B2
        std::string            trace_name;      // "new Classe", para o stack trace

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
    // class X : Forma, Desenhavel — interfaces declaradas (nomes como escritos)
    std::vector<std::string>           interfaces;
    std::vector<Token>                 interface_tokens;
    // Preenchido pelo semântico: para cada interface cumprida, os métodos da
    // classe na ordem dos métodos da interface
    std::vector<std::pair<const InterfaceDecl*, std::vector<const FunctionDecl*>>> itables;

    ClassDecl(const Token& tok,
              const std::string& name,
              std::vector<Field>           flds,
              std::unique_ptr<Constructor> ctor,
              std::vector<StmtPtr>         priv,
              std::vector<StmtPtr>         pub
            )
            : Stmt(NodeKind::ClassDecl, tok), class_name(name),
            fields(std::move(flds)),
            constructor(std::move(ctor)),
            priv_methods(std::move(priv)),
            pub_methods(std::move(pub)) {}
    
    // sobrescreve  a toString
    std::string toString(int indent = 0) const override;
};


// C3: struct — tipo por valor, só com campos públicos (sem métodos, sem pub).
//   struct Ponto { int x = 0; int y = 0; }
//   Ponto p = new Ponto();       // valores padrão
//   Ponto q = new Ponto(1, 2);   // todos os campos, em ordem
// Atribuir ou passar um struct o copia (cópia rasa: list/dict/objetos dentro
// dele continuam compartilhados).
class StructDecl : public Stmt {
public:
    std::string                   name;
    std::vector<ClassDecl::Field> fields;   // na ordem da declaração

    StructDecl(const Token& tok, const std::string& n, std::vector<ClassDecl::Field> flds)
        : Stmt(NodeKind::StructDecl, tok), name(n), fields(std::move(flds)) {}

    std::string toString(int indent = 0) const override;
};


// interface Forma { fn area() -> decimal; }  — no nível superior. Só
// assinaturas de métodos: sem código e sem campos (não é herança).
class InterfaceDecl : public Stmt {
public:
    struct Method {
        std::string            name;
        std::vector<Parameter> parameters;
        TypePtr                return_type;
        Token                  token;
    };
    std::string         name;
    std::vector<Method> methods;

    InterfaceDecl(const Token& tok, const std::string& n, std::vector<Method> m)
        : Stmt(NodeKind::InterfaceDecl, tok), name(n), methods(std::move(m)) {}

    std::string toString(int indent = 0) const override;
};

// enum Cor { Vermelho, Verde, Azul }  — no nível superior. Os valores são só
// nomes (sem int por trás) e se escrevem sempre qualificados: Cor.Verde.
class EnumDecl : public Stmt {
public:
    std::string              name;
    std::vector<std::string> members;
    std::vector<Token>       member_tokens;

    EnumDecl(const Token& tok, const std::string& n, std::vector<std::string> m,
             std::vector<Token> mt)
        : Stmt(NodeKind::EnumDecl, tok), name(n), members(std::move(m)),
          member_tokens(std::move(mt)) {}

    int indexOf(const std::string& membro) const {
        for (size_t i = 0; i < members.size(); ++i)
            if (members[i] == membro) return static_cast<int>(i);
        return -1;
    }

    std::string toString(int indent = 0) const override;
};

// C4: error Nome;  — declara um novo tipo de erro (filho de Error), no nível superior
class ErrorDecl : public Stmt {
public:
    std::string name;

    ErrorDecl(const Token& tok, const std::string& n) : Stmt(NodeKind::ErrorDecl, tok), name(n) {}

    std::string toString(int indent = 0) const override;
};


// ============================================================================
// PROGRAMA (raiz da AST)  |  A bagaça começa aqui
// ============================================================================
// C5: import a.b.c [as x];  — só no início do arquivo
struct ImportDecl {
    std::vector<std::string> path;    // {"util", "texto"}
    std::string              alias;   // nome usado no arquivo (padrão: último trecho)
    Token                    token;
};

class Program {
public:
    std::vector<StmtPtr>    statements; // funções e statements globais
    std::vector<ImportDecl> imports;    // C5
    
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
// C1: operador base de um operador composto (+= → +, -= → -, ...);
// devolve o próprio `op` se ele não for composto
TokenType compoundBaseOp(TokenType op);

} // namespace cinza

#endif // CINZA_AST_H
