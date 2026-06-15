#ifndef CINZA_LEXER_H
#define CINZA_LEXER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cctype>
#include <stdexcept>

namespace cinza {

// Enumeração de todos os tipos de tokens da linguagem Cinza
enum class TokenType {
    // Literais
    INTEGER_LITERAL,
    DECIMAL_LITERAL,
    STRING_LITERAL,
    
    // Identificadores
    IDENTIFIER,
    
    // Palavras-chave
    KW_FN,
    KW_RETURN,
    KW_IF,
    KW_ELSE,
    KW_WHILE,
    KW_FOR,
    KW_IN,
    KW_TRUE,
    KW_FALSE,
    KW_PRINT,
    KW_CLASS,          // class
    KW_PUB,            // pub  (bloco público dentro de class)
    KW_NEW,            // new  (instanciação de objeto)
    KW_CONST,          // const — variável/parâmetro imutável

    // Tipos
    TYPE_INT,
    TYPE_DECIMAL,      // Sinônimo de double
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_DICT,
    TYPE_LIST,
    TYPE_VAR,
    TYPE_PAIR,         // par genérico pair<K, V>
    
    // Operadores
    OP_PLUS,           // +
    OP_MINUS,          // -
    OP_MULTIPLY,       // *
    OP_DIVIDE,         // /
    OP_MODULO,         // %
    OP_ASSIGN,         // =
    OP_EQUAL,          // ==
    OP_NOT_EQUAL,      // !=
    OP_LESS,           // <
    OP_LESS_EQUAL,     // <=
    OP_GREATER,        // >
    OP_GREATER_EQUAL,  // >=
    OP_AND,            // &&
    OP_OR,             // ||
    OP_NOT,            // !
    OP_ARROW,          // ->
    
    // Delimitadores
    LPAREN,            // (
    RPAREN,            // )
    LBRACE,            // {
    RBRACE,            // }
    LBRACKET,          // [
    RBRACKET,          // ]
    COMMA,             // ,
    SEMICOLON,         // ;
    DOT,               // .
    COLON,             // :
    
    // Especiais
    END_OF_FILE,
    UNKNOWN
};

// Estrutura que representa um token
struct Token {
    TokenType type;
    std::string lexeme;
    int line;
    int column;
    
    // Valor associado (para literais)
    union {
        int int_value;
        double double_value;
        bool bool_value;
    } value;
    
    Token() : type(TokenType::UNKNOWN), line(0), column(0) {
        value.int_value = 0;
    }
    
    Token(TokenType t, const std::string& lex, int ln, int col) 
        : type(t), lexeme(lex), line(ln), column(col) {
        value.int_value = 0;
    }
    
    std::string toString() const;
};

// Classe do Analisador Léxico
class Lexer {
private:
    std::string source;
    size_t current;
    size_t start;
    int line;
    int column;
    int start_column;
    
    // Mapa de palavras-chave para otimização de busca
    static const std::unordered_map<std::string, TokenType> keywords;
    
    // Métodos auxiliares
    bool isAtEnd() const;
    char peek() const;
    char peekNext() const;
    char advance();
    bool match(char expected);
    void skipWhitespace();
    void skipComment();
    
    // Métodos de reconhecimento de tokens
    Token makeToken(TokenType type);
    Token makeToken(TokenType type, const std::string& lexeme);
    Token errorToken(const std::string& message);
    
    Token number();         // Reconhece números (int, decimal)
    Token string();         // Reconhece strings
    Token identifier();     // Reconhece identificadores e palavras-chave
    
public:
    explicit Lexer(const std::string& source_code);
    
    // Método principal para obter o próximo token
    Token nextToken();
    
    // Método para tokenizar todo o código de uma vez
    std::vector<Token> tokenize();
    
    // Getters para informação de posição
    int getCurrentLine() const { return line; }
    int getCurrentColumn() const { return column; }
};

} // namespace cinza

#endif     // CINZA_LEXER_H
