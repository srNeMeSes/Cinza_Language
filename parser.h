#ifndef CINZA_PARSER_H
#define CINZA_PARSER_H

#include "ast.h"
#include "lexer.h"
#include <vector>
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
    std::vector<std::string> error_messages;
    
    // ========================================================================
    // HELPERS  
    // ========================================================================
    
    // Verifica se chegou ao fim
    bool isAtEnd() const;
    
    // Retorna o token atual
    const Token& peek() const;
    
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
    
public:
    explicit Parser(std::vector<Token> token_list);
    
    // Método principal: parseia o programa inteiro
    Program parse();
    
    // Verifica se houve erros
    bool hasErrors() const { return has_errors; }
    
    // Retorna as mensagens de erro
    const std::vector<std::string>& getErrors() const { return error_messages; }
    
    // Imprime erros
    void printErrors() const;
};

} // namespace cinza

#endif // CINZA_PARSER_H
