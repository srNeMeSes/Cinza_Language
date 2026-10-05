#ifndef CINZA_PARSER_H
#define CINZA_PARSER_H

#include "ast.h"
#include "lexer.h"
#include <vector>
#include <set>
#include <string>
#include <stdexcept>

namespace cinza {

// Exceção para erros de parsing
class ParseError : public std::runtime_error {
public:
    Token error_token;
    
    ParseError(const std::string& message, const Token& token)
        : std::runtime_error(message), error_token(token) {}
};

// ============================================================================
// PARSER
// ============================================================================

class Parser {
private:
    std::vector<Token> tokens;
    size_t current;
    bool has_errors;
    bool silencioso = false;   // não relata erros (expressão dentro de um texto)
    // Profundidade da árvore em construção: parênteses, unários, cada operador de
    // uma cadeia (a + b + c... cresce para a esquerda) e instruções aninhadas.
    // Acima do limite é erro de sintaxe, antes que as etapas recursivas (parser,
    // semântico, compilador, interpretador) estourem a pilha do C++.
    int profundidade = 0;
    bool aninhamento_excedido = false;   // relatado uma vez; a análise para
    friend struct Niveis;   // parser.cpp: conta os níveis e relata o excesso

    // A8: profundidade de blocos { } em análise; 0 = nível superior.
    // 'fn' e 'class' só são aceitos com block_depth == 0 (métodos são
    // lidos direto no corpo da classe, sem passar por parseStatement).
    int block_depth = 0;

    // A9: chaves abertas (blocos, corpo de classe, pub { }). Com brace_depth > 0,
    // synchronize() para em '}' sem consumi-lo, e o laço que abriu a chave a
    // fecha normalmente (evita erros em cascata).
    int brace_depth = 0;

    // C5: apelidos dos imports deste arquivo. `apelido.nome` vira um nome
    // qualificado (chamada, identificador, tipo, new), resolvido no semântico.
    std::set<std::string> module_aliases;
    bool        checkQualified() const;          // IDENT(apelido) '.' IDENT
    static bool isWord(const Token& tok);        // identificador ou palavra-chave
    std::string parseQualifiedName();            // consome e devolve "apelido.nome"
    ImportDecl  parseImport();
    StmtPtr     parseEnumDeclaration();
    StmtPtr     parseInterfaceDeclaration();
    
    // ========================================================================
    // HELPERS  
    // ========================================================================
    
    // Verifica se chegou ao fim
    bool isAtEnd() const;
    
    // Retorna o token atual
    const Token& peek() const;
    
    // Retorna o token seguinte ao atual (ou o último, END_OF_FILE)
    const Token& peekNext() const;

    // Retorna o token anterior
    const Token& previous() const;
    
    // Avança e retorna o token atual
    const Token& advance();
    
    // Verifica se o token atual é do tipo especificado
    bool check(TokenType type) const;
    
    // Se o token atual for do tipo especificado, avança
    bool match(TokenType type);
    
    // Se o token atual for um dos tipos, avança
    bool match(const std::vector<TokenType>& types);
    
    // Consome um token do tipo especificado ou lança erro
    const Token& consume(TokenType type, const std::string& message);
    
    // Reporta erro sem lançar exceção (modo panic)
    void error(const std::string& message);
    void error(const std::string& message, const Token& token);
    
    // Sincronização após erro
    void synchronize();
    
    // ========================================================================
    // PARSING DE TIPOS
    // ========================================================================
    
    TypePtr parseType();
    TypePtr parseTypeInner();   // C5: parseType sem o token
    
    // ========================================================================
    // PARSING DE EXPRESSÕES (com precedência)
    // ========================================================================
    
    // Precedência crescente:
    // assignment       : =
    // logical_or       : ||
    // logical_and      : &&
    // equality         : == !=
    // comparison       : < <= > >=
    // term             : + -
    // factor           : * / %
    // unary            : ! -
    // primary          : literals, identifiers, calls, member access
    
    ExprPtr parseExpression();
    ExprPtr parseLogicalOrExpr();
    ExprPtr parseLogicalAndExpr();
    ExprPtr parseEqualityExpr();
    ExprPtr parseComparisonExpr();
    ExprPtr parseTermExpr();
    ExprPtr parseFactorExpr();
    ExprPtr parseUnaryExpr();
    ExprPtr parsePrimaryExpr();
    ExprPtr parseCallExpr();
    ExprPtr parsePostfixExpr(ExprPtr expr);
    ExprPtr parseNewExpr();            // new NomeClasse(args)
    
    // Literais compostos
    ExprPtr parseListLiteral();
    ExprPtr parseDictLiteral();
    ExprPtr parsePairLiteral();
    
    // ========================================================================
    // PARSING DE STATEMENTS
    // ========================================================================
    
    StmtPtr parseStatement();
    StmtPtr parseVarDeclStatement();
    StmtPtr parseAssignmentOrExprStatement();
    StmtPtr parseIfStatement();
    StmtPtr parseWhileStatement();
    StmtPtr parseForStatement();
    StmtPtr parseReturnStatement();
    StmtPtr parseBlockStatement();
    StmtPtr parseFunctionDeclaration();
    StmtPtr parseClassDeclaration();   // class Name { fields... pub { methods... } }
    
    // ========================================================================
    // PARSING DE DECLARAÇÕES DE FUNÇÃO
    // ========================================================================
    
    std::vector<Parameter> parseParameterList();

    // C3: campos (class e struct) e declaração de struct
    bool             checkFieldStart() const;
    ClassDecl::Field parseFieldDeclaration();
    StmtPtr          parseStructDeclaration();

    // C4: exceções
    StmtPtr          parseErrorDeclaration();
    StmtPtr          parseTryStatement();
    
public:
    explicit Parser(std::vector<Token> token_list);
    
    // Método principal: parseia o programa inteiro
    Program parse();

    // Uma expressão sozinha, escrita dentro de um texto ({expr} do printf):
    // os tokens recebem a posição de `origem` deslocada em `coluna` caracteres.
    // `apelidos` são os módulos importados pelo arquivo (Math.abs(x) dentro do texto).
    // Lança ParseError se o código não for exatamente uma expressão.
    static ExprPtr parseEmbedded(const std::string& codigo, const Token& origem, int coluna,
                                 const std::set<std::string>& apelidos);
    
    // Verifica se houve erros
    bool hasErrors() const { return has_errors; }
    
    // Retorna as mensagens de erro
    
    // Imprime erros
};

} // namespace cinza

#endif // CINZA_PARSER_H
