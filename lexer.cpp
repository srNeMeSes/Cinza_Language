#include "lexer.h"
#include <sstream>
#include <charconv>
#include <limits>

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
    {"struct", TokenType::KW_STRUCT},  // C3: tipo por valor, só campos
    {"enum",   TokenType::KW_ENUM},    // enum Cor { Vermelho, Verde }
    {"interface", TokenType::KW_INTERFACE},   // interface Forma { fn area() -> f64; }
    {"class", TokenType::KW_CLASS},   // declaração de classe
    {"pub",   TokenType::KW_PUB},     // bloco público dentro de class
    {"new",   TokenType::KW_NEW},     // instanciação de objeto
    {"const", TokenType::KW_CONST},   // variável/parâmetro imutável
    {"self",  TokenType::KW_SELF},    // objeto atual dentro de métodos
    {"break",    TokenType::KW_BREAK},     // C2
    {"continue", TokenType::KW_CONTINUE},  // C2
    {"try",      TokenType::KW_TRY},       // C4
    {"except",   TokenType::KW_EXCEPT},
    {"finally",  TokenType::KW_FINALLY},
    {"throw",    TokenType::KW_THROW},
    {"error",    TokenType::KW_ERROR},
    {"import",   TokenType::KW_IMPORT},    // C5
    {"as",       TokenType::KW_AS},
    {"cast",     TokenType::KW_CAST},      // cast<T>(valor): conversão explícita (seção K)
    {"null",     TokenType::KW_NULL},      // a ausência de um valor, só num T? (Q1b)
    {"yield",    TokenType::KW_YIELD},     // o valor de um bloco de else (Q1b)
    {"module",   TokenType::KW_MODULE},    // module a.b; — o nome do módulo (Q1f)
    // C1: and/or/not são sinônimos de &&, || e ! (mesmo token)
    {"and", TokenType::OP_AND},
    {"or",  TokenType::OP_OR},
    {"not", TokenType::OP_NOT},

    // Tipos
    {"i32", TokenType::TYPE_I32},
    {"i64", TokenType::TYPE_I64},
    {"f32", TokenType::TYPE_F32},
    {"f64", TokenType::TYPE_F64},
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
Lexer::Lexer(const std::string& source_code, int file)
    : source(source_code), current(0), start(0), line(1), column(1), start_column(1),
      file_id(file) {
    // A9: ignora o BOM UTF-8 (EF BB BF) que editores do Windows gravam no início
    if (source.compare(0, 3, "\xEF\xBB\xBF") == 0) current = 3;
}

// A9: classificação só em ASCII. std::isalpha & cia. com char negativo
// (bytes de acentos em UTF-8) é UB; e identificadores são só ASCII.
static bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }
static bool isAsciiAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool isAsciiAlnum(char c) { return isAsciiAlpha(c) || isAsciiDigit(c); }
static bool isNonAscii(char c)   { return static_cast<unsigned char>(c) >= 0x80; }

// Tamanho em bytes do caractere UTF-8 que começa com o byte `lead`
static size_t utf8Length(char lead) {
    const auto b = static_cast<unsigned char>(lead);
    if (b >= 0xF0) return 4;
    if (b >= 0xE0) return 3;
    if (b >= 0xC0) return 2;
    return 1;
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

// Consome e retorna o caractere atual. Q7 (decidido em 2026-11-25): a coluna conta caracteres,
// não bytes — o byte de continuação do UTF-8 (10xxxxxx) não a avança
inline char Lexer::advance() {
    if ((static_cast<unsigned char>(source[current]) & 0xC0) != 0x80) column++;
    return source[current++];
}

// Consome se o caractere atual for o esperado
inline bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source[current] != expected) return false;
    current++;
    column++;   // `expected` é sempre um caractere ASCII
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

// Reconhece números (seção K do REVISAO_V2.md):
//   123      inteiro: i32 se couber, senão i64 (o semântico decide)
//   1.5f     f32       1.f   f32 (sem dígitos depois do ponto)
//   1.5ff    f64       1.ff  f64
// Literal com ponto e sem sufixo (1.5) é erro: a precisão é sempre escrita.
Token Lexer::number() {
    auto at = [&](size_t k) -> char { return current + k < source.size() ? source[current + k] : '\0'; };
    auto identChar = [](char c) { return isAsciiAlnum(c) || c == '_' || isNonAscii(c); };

    // Consome dígitos antes do ponto
    while (isAsciiDigit(peek())) {
        advance();
    }

    // Ponto seguido de dígito, ou de sufixo f/ff que termina ali (1.f, 1.ff): é
    // ponto flutuante. Qualquer outra coisa (1.size) deixa o '.' para o parser.
    const bool sufixo_direto = at(1) == 'f' && (!identChar(at(2)) || (at(2) == 'f' && !identChar(at(3))));
    if (peek() == '.' && (isAsciiDigit(peekNext()) || sufixo_direto)) {
        advance();   // '.'
        while (isAsciiDigit(peek())) advance();
        const size_t fim_numero = current;
        int largura = 0;   // 0 = sem sufixo, 32 = f, 64 = ff
        if (peek() == 'f') {
            if (at(1) == 'f' && !identChar(at(2)))      { advance(); advance(); largura = 64; }
            else if (!identChar(at(1)))                 { advance(); largura = 32; }
        }
        if (largura == 0) {
            // consome o resto do "sufixo" (1.5x, 1.5fff) para o erro mostrar tudo
            while (identChar(peek())) advance();
            const std::string texto = source.substr(start, current - start);
            const std::string numero = source.substr(start, fim_numero - start);
            if (texto == numero)
                return errorToken("Literal '" + texto + "' sem a precisão: escreva " + numero +
                                  "f (f32) ou " + numero + "ff (f64)");
            return errorToken("Literal '" + texto + "' inválido: o sufixo de ponto flutuante é f (f32) "
                              "ou ff (f64), como em " + numero + "f e " + numero + "ff");
        }
        Token token = makeToken(largura == 32 ? TokenType::F32_LITERAL : TokenType::F64_LITERAL);
        const char* first = source.data() + start;
        const char* last  = source.data() + fim_numero;
        // A5: from_chars não lança exceção. f32 é lido direto como float: ler
        // como double e depois arredondar para float erraria em casos de borda
        if (largura == 32) {
            float v = 0.0f;
            auto [ptr, ec] = std::from_chars(first, last, v);
            if (ec != std::errc() || ptr != last)
                return errorToken("Literal f32 '" + token.lexeme + "' fora da faixa de f32 "
                                  "(o maior valor finito é cerca de 3.4e38)");
            token.value.double_value = v;   // exato: todo float cabe num double
        } else {
            double v = 0.0;
            auto [ptr, ec] = std::from_chars(first, last, v);
            if (ec != std::errc() || ptr != last)
                return errorToken("Literal f64 '" + token.lexeme + "' fora da faixa de f64 "
                                  "(o maior valor finito é cerca de 1.8e308)");
            token.value.double_value = v;
        }
        return token;
    }

    // 1f, 10ff: o sufixo só vale depois do ponto
    if (peek() == 'f' && (!identChar(at(1)) || (at(1) == 'f' && !identChar(at(2))))) {
        const std::string numero = source.substr(start, current - start);
        while (identChar(peek())) advance();
        return errorToken("Literal '" + source.substr(start, current - start) + "' inválido: o sufixo de "
                          "ponto flutuante vem depois do ponto: " + numero + ".f (f32) ou " + numero +
                          ".ff (f64)");
    }

    // 0x10, 1e10, 5abc: o número colado em letras é um literal só, e inválido (não há
    // prefixo de base nem expoente; seção 1.5 da spec)
    if (identChar(peek())) {
        while (identChar(peek())) advance();
        return errorToken("Literal '" + source.substr(start, current - start) + "' inválido: um número "
                          "não pode vir colado em letras (não há prefixo de base, como 0x, nem expoente, "
                          "como 1e10)");
    }

    Token token = makeToken(TokenType::INTEGER_LITERAL);
    const char* first = token.lexeme.data();
    const char* last  = first + token.lexeme.size();

    // Inteiro: lido sem sinal, porque o '-' é um token separado
    constexpr std::uint64_t max_int = std::numeric_limits<std::int64_t>::max();
    std::uint64_t v = 0;
    auto [ptr, ec] = std::from_chars(first, last, v);
    if (ec != std::errc() || ptr != last || v > max_int + 1)
        return errorToken("Literal inteiro '" + token.lexeme + "' fora do intervalo de i64 "
                          "(-9223372036854775808 a 9223372036854775807)");

    if (v == max_int + 1) {
        token.int_needs_minus = true;
        token.value.int_value = std::numeric_limits<std::int64_t>::min();
    } else {
        token.value.int_value = static_cast<std::int64_t>(v);
    }
    return token;
}

// Reconhece strings
Token Lexer::string() {
    start_column = column;
    advance(); // consome a aspas de abertura
    
    // A9: string com várias linhas registra a linha de INÍCIO
    const int start_line = line;
    std::string bad_escape;   // primeiro escape desconhecido, se houver
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
                    default:
                        // A9: escape desconhecido é erro (antes virava o próprio caractere)
                        if (bad_escape.empty()) {
                            bad_escape = std::string("\\") + escaped;
                            if (isNonAscii(escaped)) {
                                for (size_t k = 1; k < utf8Length(escaped) && !isAtEnd(); ++k)
                                    bad_escape += advance();
                            }
                        }
                        break;
                }
            }
        } else if (peek() == '\r' && peekNext() == '\n') {
            advance();   // spec 1.1: \r\n é um fim de linha só; no texto vira \n
        } else {
            str_value += advance();     // armazenando a string
        }
    }

    if (isAtEnd()) {
        return Token(TokenType::UNKNOWN, "String não terminada", start_line, start_column);
    }

    advance(); // consome a aspas de fechamento

    if (!bad_escape.empty()) {
        return Token(TokenType::UNKNOWN,
                     "Escape desconhecido '" + bad_escape + "' em string. "
                     "Escapes válidos: \\n, \\t, \\r, \\\\ e \\\"",
                     start_line, start_column);
    }

    return Token(TokenType::STRING_LITERAL, str_value, start_line, start_column);
}

// Reconhece identificadores e palavras-chave
Token Lexer::identifier() {
    // A9: identificadores só em ASCII. Bytes não-ASCII são consumidos junto
    // com a palavra para gerar UM erro claro (ex.: 'ação' → "não podem conter 'ç'").
    std::string first_non_ascii;
    while (isAsciiAlnum(peek()) || peek() == '_' || isNonAscii(peek())) {
        if (isNonAscii(peek())) {
            const size_t len = utf8Length(peek());
            std::string ch;
            for (size_t k = 0; k < len && !isAtEnd(); ++k) ch += advance();
            if (first_non_ascii.empty()) first_non_ascii = ch;
        } else {
            advance();
        }
    }

    std::string text = source.substr(start, current - start);

    if (!first_non_ascii.empty()) {
        return errorToken("Identificadores não podem conter '" + first_non_ascii +
                          "' (em '" + text + "'). Use apenas letras sem acento "
                          "(a-z, A-Z), dígitos e '_'.");
    }
    
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

    // A9: start antes do END_OF_FILE, para o token não levar lexema lixo
    start = current;
    start_column = column;

    if (isAtEnd()) {
        return makeToken(TokenType::END_OF_FILE);
    }

    char c = advance(); // Armazenano o primeiro caractere de código e avançando para o proximo

    // Identifica letras (identificadores ou palavras-chave); bytes não-ASCII
    // também entram aqui para gerar o erro de identificador com acento (A9)
    if (isAsciiAlpha(c) || c == '_' || isNonAscii(c)) {
        current--;
        column--;
        return identifier();
    }
    
    // Identifica números (i64 ou f64)
    if (isAsciiDigit(c)) {
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
        // C1: operadores compostos (+=, -=, *=, /=, %=)
        case '+':
            if (match('=')) return makeToken(TokenType::OP_PLUS_ASSIGN);
            return makeToken(TokenType::OP_PLUS);
        case '*':
            if (match('=')) return makeToken(TokenType::OP_MULTIPLY_ASSIGN);
            return makeToken(TokenType::OP_MULTIPLY);
        case '%':
            if (match('=')) return makeToken(TokenType::OP_MODULO_ASSIGN);
            return makeToken(TokenType::OP_MODULO);
        
        case '-':
            if (match('>')) return makeToken(TokenType::OP_ARROW);
            if (match('=')) return makeToken(TokenType::OP_MINUS_ASSIGN);
            return makeToken(TokenType::OP_MINUS);
        
        case '/':
            if (match('=')) return makeToken(TokenType::OP_DIVIDE_ASSIGN);
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
        case '.': {
            // .5ff: o flutuante começa por dígito (seção 1.5); nenhum outro uso do ponto vem
            // seguido de dígito (não há acesso por número, como t.0)
            if (isAsciiDigit(peek())) {
                while (isAsciiDigit(peek()) || isAsciiAlnum(peek()) || peek() == '_') advance();
                const std::string texto = source.substr(start, current - start);
                return errorToken("Literal '" + texto + "' inválido: o número começa por dígito; "
                                  "escreva 0" + texto);
            }
            return makeToken(TokenType::DOT);
        }
        case ':': return makeToken(TokenType::COLON);
        case '?': return makeToken(TokenType::QUESTION);
        
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
        token.file_id = file_id;   // C5
        token.end_line = line;     // Q7
        token.end_column = column;
        tokens.push_back(token);
    } while (token.type != TokenType::END_OF_FILE);
    
    return tokens;      // RETORNA O VETOR DE TOKENS
}

} // namespace cinza -> fim :)
