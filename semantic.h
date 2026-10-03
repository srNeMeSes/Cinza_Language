#ifndef CINZA_SEMANTIC_H
#define CINZA_SEMANTIC_H

#include "ast.h"
#include "types.h"
#include "natives.h"
#include "source_files.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <stdexcept>
#include <map>
#include <set>
#include <functional>

namespace cinza {

// ============================================================================
// SEMANTIC ERROR
// Para ao primeiro erro encontrado (stop-at-first philosophy da Cinza)
// ============================================================================

class SemanticError : public std::runtime_error {
public:
    std::string    message;   // sem os prefixos internos de módulo (C5)
    SourceLocation loc;

    SemanticError(const std::string& msg, int ln, int col, int file_id = 0)
        : SemanticError(stripModulePrefixes(msg), SourceLocation{file_id, ln, col}) {}

    SemanticError(const std::string& msg, const Token& tok)
        : SemanticError(msg, tok.line, tok.column, tok.file_id) {}

private:
    SemanticError(std::string msg, SourceLocation l)
        : std::runtime_error(formatDiagnostic("SemanticError", msg, l)),   // B6
          message(std::move(msg)), loc(l) {}
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

    // B1: tipo resolvido como TypeRef (antes: string canônica)
    TypeRef     resolved_type = nullptr;

    int         line   = 0;
    int         column = 0;

    // Apenas para Kind::FUNCTION — usados na verificação de chamadas
    std::vector<TypeRef> param_types;   // tipos dos parâmetros na ordem
    TypeRef              return_type = nullptr;

    // C5: módulo dono de um símbolo global (-1: embutido, visível em todos)
    int                  module = -1;

    // B2: onde a variável vive em runtime (VAR e PARAMETER)
    Resolution           res;

    // Apenas para Kind::FUNCTION do usuário: a declaração (alvo da chamada)
    const FunctionDecl*  fn_decl = nullptr;
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
    // C5: só nos escopos locais (tudo menos o global) / só no global
    const Symbol* resolveLocal (const std::string& name) const;
    const Symbol* resolveGlobal(const std::string& name) const;

    // retorna a quantidade de escopos atuais
    int depth() const { return static_cast<int>(scopes.size()); }
};


// ============================================================================
// CLASS INFO — informações sobre uma classe declarada
// Populado por `preRegisterDeclarations` antes da análise dos corpos.
// ============================================================================

struct FieldInfo {  // informações de campos de classes
    TypeRef type      = nullptr;
    bool    is_public = false;
    int     line      = 0;
    int     index     = 0;   // B2: posição do campo no objeto
};

struct MethodInfo { // informações de metodos de classes
    std::vector<TypeRef> param_types;
    TypeRef              return_type = nullptr;
    bool                 is_public   = false;
    int                  line        = 0;
    const FunctionDecl*  decl        = nullptr;   // alvo das chamadas
};

// C3: struct — campos na ordem da declaração (a ordem define o construtor)
struct StructInfo {
    std::string                                   name;
    std::vector<std::pair<std::string, TypeRef>>  fields;
    const StructDecl*                             decl = nullptr;
};

struct ClassInfo {  // informações de classes
    std::string                        name;
    std::vector<std::string>           interfaces;   // nomes completos (class X : Forma)
    std::map<std::string, FieldInfo>   fields;
    std::map<std::string, MethodInfo>  methods;
    std::vector<TypeRef>               ctor_param_types;  // vazio = sem construtor
    int                                line = 0;
};

// ============================================================================
// TYPE CHECKER — verificações e consultas de tipos
// Todos os métodos são estáticos (sem estado interno).
// ============================================================================

class TypeChecker {
public:
    // B1: converte o tipo escrito na AST (Type*) no TypeRef único do contexto
    static TypeRef fromAst(const Type* type);

    // Retorna o tipo resultante de uma operação binária.
    // Lança SemanticError se a operação for inválida para os tipos fornecidos.
    static TypeRef checkBinaryOp(TypeRef left, TokenType op, TypeRef right,
                                 const Token& op_token);

    // Retorna o tipo resultante de uma operação unária.
    // Lança SemanticError se a operação for inválida.
    static TypeRef checkUnaryOp(TokenType op, TypeRef operand, const Token& op_token);

    // Retorna true se `value` pode ser atribuído a `declared`.
    // Regras de atribuição da Cinza:
    //   - mesmo tipo → sempre compatível
    //   - int → decimal   (promoção numérica)
    //   - A3: list/dict/pair são invariantes (list<int> NÃO cabe em list<decimal>)
    static bool isAssignable(TypeRef declared, TypeRef value);

    // op<...>: o tipo trava na inicialização. Um tipo com op (op<A, B>,
    // list<op<A, B>>...) representa o conjunto de tipos concretos que ele pode
    // ter travado; expand() os lista (list<op<int, decimal>> → list<int>,
    // list<decimal>). isAssignable acima vale para "travar" (cada tipo possível
    // do valor cabe em algum do destino); reassignable, para trocar o valor de
    // quem já travou num tipo que o compilador não sabe (o valor novo precisa
    // caber em todos).
    static bool                 containsOp(TypeRef t);
    static std::vector<TypeRef> expand(TypeRef t);
    static bool                 reassignable(TypeRef declared, TypeRef value);

    // v2.00 #19: int+int → int; qualquer decimal envolvido → decimal
    static TypeRef promoteNumeric(TypeRef left, TypeRef right);
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

// C5: um arquivo a analisar. `imports` liga cada apelido ao índice (em
// units) do módulo importado, que vem sempre antes de quem o importa.
struct ModuleUnit {
    Program*    program = nullptr;
    std::string prefix;   // "" no principal; "util.texto" nos importados
    std::vector<std::pair<std::string, size_t>> imports;
    const NativeModule* native = nullptr;   // Fase 7: módulo nativo (sem AST)
};

class SemanticAnalyzer {
private:
    // C5: escopo global de cada módulo. Os nomes globais dos módulos
    // importados ganham o prefixo do módulo ("util.texto::soma") no próprio
    // AST, então o executor vê um único espaço de nomes.
    struct ModuleScope {
        std::string                                  prefix;
        bool                                         is_main = false;
        std::unordered_map<std::string, std::string> globals;   // curto → completo
        std::set<std::string>                        pub;       // curtos exportados
        std::unordered_map<std::string, int>         imports;   // apelido → módulo
    };
    std::vector<ModuleScope> modules;
    int                      current_module = -1;
    // Fase 7: nome completo ("Math::sqrt") → função nativa
    std::unordered_map<std::string, const NativeFn*> native_fns;
    // enum: nome completo → declaração
    std::unordered_map<std::string, const EnumDecl*> enum_table;
    void registerEnum(const EnumDecl& en);

    // interface: nome completo → assinaturas dos métodos (na ordem da declaração)
    struct InterfaceInfo {
        const InterfaceDecl*     decl = nullptr;
        std::vector<std::string> nomes;
        std::vector<MethodInfo>  metodos;
        int indexOf(const std::string& n) const {
            for (size_t i = 0; i < nomes.size(); ++i) if (nomes[i] == n) return static_cast<int>(i);
            return -1;
        }
    };
    std::unordered_map<std::string, InterfaceInfo> interface_table;
    void registerInterface(const InterfaceDecl& in);
    // class X : Forma — confere que a classe tem cada método, com a mesma
    // assinatura e em pub{}, e monta a tabela usada nas chamadas pela interface
    void checkInterfaces(ClassDecl* cls);
    // Analisa uma sequência de instruções; código depois de uma que sempre sai
    // do bloco é erro (código morto)
    void analyzeStatements(const std::vector<StmtPtr>& stmts);

    // B2: próximo slot local da função em análise e layout dos globais
    std::uint32_t next_slot = 0;
    GlobalLayout  global_layout;
    Resolution    newLocal()  { return {Resolution::Kind::Local,  next_slot++}; }
    Resolution    newGlobal() { return {Resolution::Kind::Global, global_layout.count++}; }

    // Nome global como escrito (curto, ou "apelido.nome") → nome completo.
    // Lança erro para nome inexistente ou privado em módulo importado.
    std::string   qualifyName(const std::string& name, const Token& tok) const;
    // Símbolo visível com esse nome (locais, globais do módulo, imports,
    // embutidos); `full` recebe o nome completo quando o símbolo é global.
    const Symbol* lookup(const std::string& name, const Token& tok, std::string& full);
    // Como lookup, para nomes já reescritos no AST, sem lançar erros
    const Symbol* findSymbol(const std::string& name) const;
    // Tipo escrito na AST → TypeRef, qualificando nomes de módulo (tx.Pessoa)
    TypeRef       typeOf(Type* type);
    // op<...>: regras da declaração (quantidade, repetição, op dentro de op...)
    void          checkOpType(const Type* type);
    bool          registering = false;   // durante o pré-registro nem todo tipo é conhecido
    // op<...>: estreita o objeto de um método/campo/índice/for para o único
    // tipo do op que aceita a operação (senão, erro pedindo uma variável)
    // op<...>: literal de coleção travado num dos tipos concretos do destino
    TypeRef       lockCollectionLiteral(Expr* expr, TypeRef natural, TypeRef target);
    // op<...>: `if (type(x) == T)` — o símbolo x estreitado para T, ou nulo
    const Symbol* narrowingOf(const Expr* cond, TypeRef& narrowed);
    // Nome de variável/parâmetro não pode repetir um apelido de import
    void          checkNotAlias(const std::string& name, const Token& tok) const;

    SymbolTable  symbol_table;
    std::unordered_map<std::string, ClassInfo> class_table;
    std::unordered_map<std::string, StructInfo> struct_table;   // C3
    TypeContext& types = TypeContext::instance();   // B1: fábrica única de tipos

    // Contexto de análise atual
    TypeRef     current_function_return_type = nullptr;  // nullptr se fora de função
    std::string current_class_name;                      // "" se fora de classe
    bool        inside_function = false;
    int         loop_depth      = 0;   // C2: laços abertos na função atual
    int         finally_depth   = 0;   // C4: blocos finally abertos

    // ── Pré-registro (forward-reference support) ──────────────────────────
    void preRegisterDeclarations(const Program& program);
    void registerClass   (const ClassDecl&    cls);
    void registerFunction(const FunctionDecl& fn);
    void registerStruct  (const StructDecl&   st);   // C3
    // C3: struct que contém a si mesmo por valor (direta ou indiretamente)
    void checkStructCycles();
    // Nome de tipo definido pelo usuário (class ou struct) existe?
    bool isKnownType(const std::string& name) const;

    // ── Análise de statements ─────────────────────────────────────────────
    void analyzeStmt       (Stmt* stmt);
    void analyzeBlock      (BlockStmt* block);
    void analyzeVarDecl    (VarDeclStmt* stmt);
    void analyzeAssign     (AssignStmt* stmt);                // B5
    void analyzeExprStmt   (ExprStmt* stmt);
    void analyzeIf         (IfStmt* stmt);
    void analyzeWhile      (WhileStmt* stmt);
    void analyzeFor        (ForStmt* stmt);
    void analyzeReturn     (ReturnStmt* stmt);
    void analyzeLoopControl(Stmt* stmt);                     // C2: break/continue
    void analyzeTry        (TryStmt* stmt);                  // C4
    void analyzeThrow      (ThrowStmt* stmt);                // C4
    // C4: registra um tipo de erro (embutido ou `error Nome;`)
    void registerErrorType (const std::string& name, const Token& tok);
    void analyzeFunctionDecl(FunctionDecl* stmt);
    void analyzeClassDecl   (ClassDecl* stmt);
    void analyzeStructDecl  (StructDecl* stmt);      // C3

    // ── Análise de expressões ─────────────────────────────────────────────
    // Cada função: analisa o nó, anota `resolved_type` no nó, retorna o tipo.
    // A3: `expected` é o tipo exigido pelo contexto (nullptr se não houver); os
    // literais de list/dict/pair o usam para assumir o tipo esperado.
    TypeRef analyzeExpr         (Expr* expr, TypeRef expected = nullptr);
    TypeRef analyzeLiteral      (LiteralExpr* expr);
    TypeRef analyzeIdentifier   (IdentifierExpr* expr);
    TypeRef analyzeBinary       (BinaryExpr* expr);
    TypeRef analyzeUnary        (UnaryExpr* expr);
    TypeRef analyzeCall         (CallExpr* expr);
    TypeRef analyzeMethodCall   (MethodCallExpr* expr);
    TypeRef analyzeMemberAccess (MemberAccessExpr* expr);
    TypeRef analyzeIndexAccess  (IndexAccessExpr* expr);
    TypeRef analyzeSlice        (SliceExpr* expr);
    TypeRef analyzeInterpolation(CallExpr* expr);   // printf / format
    TypeRef analyzeNew          (NewExpr* expr);
    TypeRef analyzeListLiteral  (ListLiteralExpr* expr, TypeRef expected);
    TypeRef analyzeDictLiteral  (DictLiteralExpr* expr, TypeRef expected);
    TypeRef analyzePairLiteral  (PairLiteralExpr* expr, TypeRef expected);
    // C6: chamada de função nativa, checada pela assinatura (com variáveis de tipo)
    TypeRef analyzeNativeCall   (CallExpr* expr, const NativeFn& fn);

    // ── B5: alvos de atribuição e const ───────────────────────────────────
    // Analisa o lado esquerdo de uma atribuição (Identifier, MemberAccess ou
    // IndexAccess), aplica as regras de const e devolve o tipo do lugar.
    TypeRef analyzeLValue(Expr* target);
    // Variável const de onde vem o recipiente `container`, ou nullptr. Sobe por
    // campos e índices até a variável de origem e para ao passar por um objeto
    // de classe: objetos nunca são const, mesmo dentro de recipiente const (C3).
    const Symbol* constRoot(const Expr* container) const;

    // ── Fase 2.5: main e nível superior ───────────────────────────────────
    // Valida a assinatura de main (void; sem parâmetros ou só list<string>)
    // e exige que ela exista.
    void checkMain(const Program& program);
    // Expressão constante: literais, operadores e outros const globais.
    // Proibidos: chamadas, new, literais de list/dict/pair.
    bool isConstantExpr(const Expr* expr) const;

    // ── Coerção implícita (A2) ────────────────────────────────────────────
    // Chamada depois de isAssignable(target, slot->resolved_type) aprovar.
    // Materializa a promoção int → decimal envolvendo o slot num CastExpr.
    // Em literais de list/dict/pair desce nos elementos e reanota o literal
    // com o tipo alvo. Nos demais casos não altera nada.
    void coerceInPlace(ExprPtr& slot, TypeRef target);

    // ── Análise de fluxo de retorno ───────────────────────────────────────
    // true se TODOS os caminhos do stmt terminam em ReturnStmt
    bool allPathsReturn(const Stmt* stmt) const;

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
    // C5: vários módulos, dependências antes de quem as importa e o principal
    // por último
    void analyze(std::vector<ModuleUnit>& units);

    // B2: slots dos const globais, para o executor
    const GlobalLayout& globalLayout() const { return global_layout; }
};

} // namespace cinza

#endif // CINZA_SEMANTIC_H
