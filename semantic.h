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
#include <deque>
#include <memory>

namespace cinza {

// ============================================================================
// SEMANTIC ERROR
// Para ao primeiro erro encontrado (stop-at-first philosophy da Cinza)
// ============================================================================

class SemanticError : public std::runtime_error {
public:
    std::string    message;   // sem os prefixos internos de módulo (C5)
    SourceLocation loc;
    // N2: linhas extras do relato — o rastro dos usos de uma função genérica
    // ("em f[i32], usada em prog.cinza:9")
    std::vector<std::string> notes;

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
    //   - i32 → i64 e f32 → f64 (alargar dentro da família; seção K)
    //   - A3: list/dict/pair são invariantes (list<i32> NÃO cabe em list<i64>)
    static bool isAssignable(TypeRef declared, TypeRef value);

    // Seção K: tipo comum de dois números da mesma família (o mais largo);
    // nullptr se as famílias forem diferentes (inteiro com ponto flutuante)
    static TypeRef numericResult(TypeRef left, TypeRef right);
    // Promoção implícita: i32 → i64 e f32 → f64
    static bool widens(TypeRef to, TypeRef from);
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
//   - Todos os demais tipos (i64, f64, string, bool, var, pair<>, CUSTOM)
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
    // AST, então a CVM vê um único espaço de nomes.
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
    // C3. N5: `simbolo` falso numa cópia de struct genérico (Par[i32, string]), que não é
    // um nome escrito no código e pode nascer dentro de uma função
    void registerStruct  (const StructDecl&   st, bool simbolo = true);
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
    TypeRef analyzeLiteral      (LiteralExpr* expr, TypeRef expected = nullptr);
    TypeRef analyzeCast         (CastExpr* expr);
    TypeRef intLiteralAsFloat   (Expr* e, TypeRef float_type);
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
    // Materializa a promoção i32 → i64 envolvendo o slot num CastExpr.
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

    // ── N2: funções genéricas (spec, capítulo 8) ──────────────────────────
    // Cada combinação de tipos usada vira uma cópia: os tokens da declaração com
    // cada parâmetro de tipo trocado pelo tipo concreto, reparseados (a cópia
    // tem a própria AST, dona: Program::instancias do módulo da declaração).
    struct Instancia {
        const FunctionDecl*  origem = nullptr;   // a declaração genérica
        std::vector<TypeRef> tipos;              // na ordem dos parâmetros de tipo
        FunctionDecl*        fn     = nullptr;   // a cópia (nome "maior[i32]")
        int                  modulo = -1;        // módulo da declaração
        Token                uso;                // a chamada que a criou
        const Instancia*     pai    = nullptr;   // cópia em cujo corpo está o uso
        int                  profundidade = 1;   // cópias encadeadas até aqui
        std::vector<TypeRef> param_types;        // assinatura concreta
        TypeRef              return_type = nullptr;
    };
    std::map<std::pair<const FunctionDecl*, std::vector<TypeRef>>,
             std::unique_ptr<Instancia>> instancias;
    std::deque<Instancia*>  fila_instancias;    // corpos ainda não analisados
    const Instancia*        instancia_atual = nullptr;   // corpo em análise
    std::vector<Program*>   programas;          // por módulo (nullptr no nativo)

    // 8.1: parâmetros de tipo distintos, sem nome de tipo, main não genérica
    void       checkGenericDecl(FunctionDecl* fn);
    // f(args) ou f[tipos](args): deduz os tipos, cria (ou reusa) a cópia e confere
    // os argumentos contra a assinatura concreta
    TypeRef    analyzeGenericCall(CallExpr* expr, const Symbol& sym);
    void       confereArgumentoDeTipo(TypeRef t, const std::string& param,
                                      const std::string& fn, const Token& tok);
    Instancia* instancia(const FunctionDecl& g, int modulo, const std::vector<TypeRef>& tipos,
                         const Token& uso);
    // Analisa os corpos das cópias pendentes (no escopo global, entre declarações)
    void       analisaInstancias();
    // "em f[i32], usada em prog.cinza:9", da cópia mais interna para fora
    std::vector<std::string> rastro(const std::string& nome, const Token& uso,
                                    const Instancia* pai) const;
    // 8.1: parâmetros de tipo distintos e sem nome de tipo (funções e structs)
    void       confereParametrosTipo(const std::vector<Token>& params, const std::string& nome);

    // ── N5: structs genéricos (spec 8.6) ──────────────────────────────────
    // Como as funções: cada combinação de tipos usada vira uma cópia, um struct comum
    // ("Par[i32, string]"), com os campos registrados na hora (o tipo é preciso já) e
    // os valores iniciais analisados depois, no escopo global (analisaInstancias).
    struct StructGenerico {
        const StructDecl* decl   = nullptr;
        int               modulo = -1;
    };
    std::unordered_map<std::string, StructGenerico> structs_genericos;   // nome completo
    struct InstanciaStruct {
        const StructDecl*    origem = nullptr;
        std::vector<TypeRef> tipos;
        StructDecl*          decl   = nullptr;   // a cópia
        int                  modulo = -1;
        Token                uso;
        const Instancia*     pai    = nullptr;   // cópia de função em cujo corpo está o uso
    };
    std::map<std::pair<const StructDecl*, std::vector<TypeRef>>,
             std::unique_ptr<InstanciaStruct>> instancias_struct;
    std::unordered_map<std::string, const InstanciaStruct*> copia_struct;   // nome → cópia
    std::deque<InstanciaStruct*> fila_structs;    // valores iniciais ainda não analisados
    int                          structs_abertos = 0;   // cópias montadas umas dentro das outras

    void    checkGenericStructDecl(StructDecl* st);
    // Par[tipos] escrito num tipo ou num new: confere os tipos e devolve a cópia
    TypeRef usoStructGenerico(const StructGenerico& g, const std::vector<TypePtr>& args,
                              const Token& tok);
    TypeRef instanciaStruct(const StructGenerico& g, const std::vector<TypeRef>& tipos,
                            const Token& uso);

public:
    SemanticAnalyzer() = default;

    // Ponto de entrada.
    // Percorre e anota a AST inteira.
    // Lança SemanticError no primeiro problema encontrado.
    void analyze(Program& program);
    // C5: vários módulos, dependências antes de quem as importa e o principal
    // por último
    void analyze(std::vector<ModuleUnit>& units);

    // B2: slots dos const globais, para a CVM
    const GlobalLayout& globalLayout() const { return global_layout; }
};

} // namespace cinza

#endif // CINZA_SEMANTIC_H
