#include "lexer.h"
#include <sstream>

namespace cinza {

// Inicialização do mapa de palavras-chave (otimização com hash map)
const std::unordered_map<std::string, TokenType> Lexer::keywords = {
    {"fn", TokenType::KW_FN},
    {"return", TokenType::KW_RETURN},
    {"if", TokenType::KW_IF},
    {"else", TokenType::KW_ELSE},
    {"while", TokenType::KW_WHILE},
    {"for", TokenType::KW_FOR},
    {"in", TokenType::KW_IN},
    {"true", TokenType::KW_TRUE},
    {"false", TokenType::KW_FALSE},
    {"print", TokenType::KW_PRINT},
    {"class", TokenType::KW_CLASS},   // declaração de classe
    {"pub",   TokenType::KW_PUB},     // bloco público dentro de class
    {"new",   TokenType::KW_NEW},     // instanciação de objeto
    {"const", TokenType::KW_CONST},   // variável/parâmetro imutável

    // Tipos
    {"int", TokenType::TYPE_INT},
    {"decimal", TokenType::TYPE_DECIMAL},
    {"string", TokenType::TYPE_STRING},
    {"str", TokenType::TYPE_STRING},
    {"bool", TokenType::TYPE_BOOL},
    {"void", TokenType::TYPE_VOID},
    {"dict", TokenType::TYPE_DICT},     // dict<K, V>
    {"list", TokenType::TYPE_LIST},     // list<T>
    {"pair", TokenType::TYPE_PAIR},     // pair<F, S>
    {"var", TokenType::TYPE_VAR}
};

// Implementação do Token::toString
std::string Token::toString() const {
    std::ostringstream oss;  // stream para montar strings em memória
    oss << "Token(";
    oss << "type=" << static_cast<int>(type);
    oss << ", lexeme='" << lexeme << "'";
    oss << ", line=" << line;
    oss << ", column=" << column;
    oss << ")";
    return oss.str();       // Ex: "Token(type=KW_FN, lexeme='fn', line=1, column=1)"
}

// Construtor do Lexer
Lexer::Lexer(const std::string& source_code) 
    : source(source_code), current(0), start(0), line(1), column(1), start_column(1) {
}

// Verifica se chegou ao final do código
inline bool Lexer::isAtEnd() const {
    return current >= source.length();
}

// Olha o caractere atual sem consumi-lo
inline char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source[current];
}

// Olha o próximo caractere sem consumi-lo
inline char Lexer::peekNext() const {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

// Consome e retorna o caractere atual
inline char Lexer::advance() {
    column++;
    return source[current++];
}

// Consome se o caractere atual for o esperado
inline bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source[current] != expected) return false;
    current++;
    column++;
    return true;
}

// Pula espaços em branco
void Lexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                line++;
                column = 0;
                advance();
                break;
            case '/':
                // Verifica se é comentário
                if (peekNext() == '/') {
                    skipComment();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

// Pula comentários de linha: // isso aqui é um comentario :)
void Lexer::skipComment() {
    // Pula os dois '/'
    advance();
    advance();
    
    // Pula até o final da linha
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

// Cria um token
Token Lexer::makeToken(TokenType type) {
    std::string lexeme = source.substr(start, current - start);
    return Token(type, lexeme, line, start_column);
}

Token Lexer::makeToken(TokenType type, const std::string& lexeme) {
    return Token(type, lexeme, line, start_column);
}

// Cria um token de erro
Token Lexer::errorToken(const std::string& message) {
    return Token(TokenType::UNKNOWN, message, line, start_column);
}

// Reconhece números (int, decimal)
Token Lexer::number() {
    bool is_float = false;
    
    // Consome dígitos antes do ponto
    while (std::isdigit(peek())) {
        advance();
    }
    
    // Verifica se tem parte decimal
    if (peek() == '.' && std::isdigit(peekNext())) {
        is_float = true;
        advance(); // consome o '.'
        
        while (std::isdigit(peek())) {
            advance();
        }
    }
    
    Token token = makeToken(is_float ? TokenType::DECIMAL_LITERAL : TokenType::INTEGER_LITERAL);
    
    // Converte o valor
    if (is_float) {
        token.value.double_value = std::stod(token.lexeme);
    } else {
        token.value.int_value = std::stoi(token.lexeme);  // int 32-bit (v2.00 #17)
    }
    
    return token;
}

// Reconhece strings
Token Lexer::string() {
    start_column = column;
    advance(); // consome a aspas de abertura
    
    std::string str_value;
    
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') {
            line++;
            column = 0;
        }
        
        // Suporte a caracteres de escape
        if (peek() == '\\') {
            advance();
            if (!isAtEnd()) {
                char escaped = advance();
                switch (escaped) {
                    case 'n': str_value += '\n'; break;
                    case 't': str_value += '\t'; break;
                    case 'r': str_value += '\r'; break;
                    case '\\': str_value += '\\'; break;
                    case '"': str_value += '"'; break;
                    default: str_value += escaped; break;
                }
            }
        } else {
            str_value += advance();     // armazenando a string
        }
    }
    
    if (isAtEnd()) {
        return errorToken("String não terminada");
    }
    
    advance(); // consome a aspas de fechamento
    
    return makeToken(TokenType::STRING_LITERAL, str_value);
}

// Reconhece identificadores e palavras-chave
Token Lexer::identifier() {
    while (std::isalnum(peek()) || peek() == '_') {
        advance();
    }
    
    std::string text = source.substr(start, current - start);
    
    // Verifica se é uma palavra-chave
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        Token token = makeToken(it->second);
        
        // Se for bool literal, define o valor
        if (it->second == TokenType::KW_TRUE) {
            token.value.bool_value = true;
        } else if (it->second == TokenType::KW_FALSE) {
            token.value.bool_value = false;
        }
        
        return token;
    }
    
    return makeToken(TokenType::IDENTIFIER);
}

// Método principal: obtém o próximo token
Token Lexer::nextToken() {
    skipWhitespace();
    
    if (isAtEnd()) {
        return makeToken(TokenType::END_OF_FILE);
    }
    
    start = current;
    start_column = column;
    
    char c = advance(); // Armazenano o primeiro caractere de código e avançando para o proximo
    
    // Identifica letras (identificadores ou palavras-chave)
    if (std::isalpha(c) || c == '_') {
        current--;
        column--;
        return identifier();
    }
    
    // Identifica números (int ou decimal)
    if (std::isdigit(c)) {
        current--;
        column--;
        return number();
    }
    
    // Identifica outros tokens
    switch (c) {
        // Strings
        case '"': 
            current--;
            column--;
            return string();
        
        // Operadores aritméticos
        case '+': return makeToken(TokenType::OP_PLUS);
        case '*': return makeToken(TokenType::OP_MULTIPLY);
        case '%': return makeToken(TokenType::OP_MODULO);
        
        case '-':
            if (match('>')) return makeToken(TokenType::OP_ARROW);
            return makeToken(TokenType::OP_MINUS);
        
        case '/':
            return makeToken(TokenType::OP_DIVIDE);
        
        // Operadores de comparação e atribuição
        case '=':
            if (match('=')) return makeToken(TokenType::OP_EQUAL);
            return makeToken(TokenType::OP_ASSIGN);
        
        case '!':
            if (match('=')) return makeToken(TokenType::OP_NOT_EQUAL);
            return makeToken(TokenType::OP_NOT);
        
        case '<':
            if (match('=')) return makeToken(TokenType::OP_LESS_EQUAL);
            return makeToken(TokenType::OP_LESS);
        
        case '>':
            if (match('=')) return makeToken(TokenType::OP_GREATER_EQUAL);
            return makeToken(TokenType::OP_GREATER);
        
        // Operadores lógicos
        case '&':
            if (match('&')) return makeToken(TokenType::OP_AND);
            return errorToken("Caractere inesperado '&'");
        
        case '|':
            if (match('|')) return makeToken(TokenType::OP_OR);
            return errorToken("Caractere inesperado '|'");
        
        // Delimitadores
        case '(': return makeToken(TokenType::LPAREN);
        case ')': return makeToken(TokenType::RPAREN);
        case '{': return makeToken(TokenType::LBRACE);
        case '}': return makeToken(TokenType::RBRACE);
        case '[': return makeToken(TokenType::LBRACKET);
        case ']': return makeToken(TokenType::RBRACKET);
        case ',': return makeToken(TokenType::COMMA);
        case ';': return makeToken(TokenType::SEMICOLON);
        case '.': return makeToken(TokenType::DOT);
        case ':': return makeToken(TokenType::COLON);
        
        default:
            return errorToken(std::string("Caractere inesperado: '") + c + "'");
    }
}

// A função pika que tokeniza todo o código de uma vez (principal tokenize)
std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    tokens.reserve(256); // Reserva memória para otimização
    
    Token token;
    do {
        token = nextToken();
        tokens.push_back(token);
    } while (token.type != TokenType::END_OF_FILE);
    
    return tokens;      // RETORNA O VETOR DE TOKENS
}

} // namespace cinza -> fim :)
