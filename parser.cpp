#include "parser.h"
#include "source_files.h"
#include <iostream>
#include <sstream>




namespace cinza {

// RAII para contadores de profundidade do parser (A8/A9): desconta mesmo
// quando um ParseError atravessa o trecho
struct DepthGuard {
    int& depth;
    explicit DepthGuard(int& d) : depth(d) { ++depth; }
    ~DepthGuard() noexcept { --depth; }

    DepthGuard(const DepthGuard&)            = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
};

// A7: 'var' em qualquer ponto do tipo (inclusive list<var>)
static bool containsVar(const Type* type) {
    if (!type) return false;
    if (type->kind == Type::Kind::VAR) return true;
    for (const auto& param : type->type_params)
        if (containsVar(param.get())) return true;
    return false;
}

// Construtor da classe Parser
Parser::Parser(std::vector<Token> token_list) 
    : tokens(std::move(token_list)), current(0), has_errors(false) {
}

// ============================================================================
// HELPER METHODS
// ============================================================================

bool Parser::isAtEnd() const {
    return peek().type == TokenType::END_OF_FILE;
}

const Token& Parser::peek() const {
    return tokens[current];
}

const Token& Parser::peekNext() const {
    return current + 1 < tokens.size() ? tokens[current + 1] : tokens.back();
}

const Token& Parser::previous() const {
    return tokens[current - 1];
}

const Token& Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::check(TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::match(const std::vector<TokenType>& types) {
    for (TokenType type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    
    error(message, peek());
    throw ParseError(message, peek());
}

void Parser::error(const std::string& message) {
    error(message, peek());
}

void Parser::error(const std::string& message, const Token& token) {
    has_errors = true;
    if (silencioso) return;   // parseEmbedded: quem chamou relata o erro
    // B6: vai para o motor único de diagnósticos (impresso por quem chamou)
    diagnostics().report("SyntaxError", message + " (token: '" + token.lexeme + "')",
                         token.loc());
}

void Parser::synchronize() {
    // A9: '}' fecha a chave em que o erro ocorreu: não consumir, para quem a
    // abriu fechá-la normalmente. No nível superior não há chave aberta, então
    // o '}' avulso é consumido (garante que o parser avança).
    if (check(TokenType::RBRACE) && brace_depth > 0) return;
    advance();

    while (!isAtEnd()) {
        if (previous().type == TokenType::SEMICOLON) return;

        switch (peek().type) {
            case TokenType::RBRACE:
                if (brace_depth > 0) return;
                advance();
                break;
            case TokenType::KW_FN:
            case TokenType::KW_CLASS:
            case TokenType::KW_IF:
            case TokenType::KW_WHILE:
            case TokenType::KW_FOR:
            case TokenType::KW_RETURN:
            case TokenType::TYPE_INT:
            case TokenType::TYPE_DECIMAL:
            case TokenType::TYPE_STRING:
            case TokenType::TYPE_BOOL:
            case TokenType::TYPE_LIST:
            case TokenType::TYPE_DICT:
            case TokenType::TYPE_PAIR:
            case TokenType::TYPE_OP:
            case TokenType::TYPE_VAR:
                return;
            default:
                advance();
        }
    }
}


// ============================================================================
// TYPE PARSING
// ============================================================================

// C5: guarda no Type o token onde ele foi escrito
TypePtr Parser::parseType() {
    Token start = peek();
    TypePtr type = parseTypeInner();
    type->token = start;
    return type;
}

// r: retorna um objeto do tipo 'Type<T>'
TypePtr Parser::parseTypeInner() {
    Token type_token = peek();
    Type::Kind kind;

    // const dentro de parâmetros de tipo é proibido: list<const int>, dict<const str, V>
    if (check(TokenType::KW_CONST)) {
        error("'const' não pode ser usado dentro de parâmetros de tipo como "
              "list<const T>, dict<const K, V>. "
              "Use 'const list<T>' para declarar uma coleção imutável.", peek());
        throw ParseError("'const' inválido em parâmetro de tipo", peek());
    }
    
    if (match(TokenType::TYPE_INT)) {
        kind = Type::Kind::INT;
    } else if (match(TokenType::TYPE_DECIMAL)) {
        kind = Type::Kind::DECIMAL;  // Sinônimo de double
    } else if (match(TokenType::TYPE_STRING)) {
        kind = Type::Kind::STRING;
    } else if (match(TokenType::TYPE_BOOL)) {
        kind = Type::Kind::BOOL;
    } else if (match(TokenType::TYPE_VOID)) {   // POSSIVEL BUG
        kind = Type::Kind::VOID;
    } else if (match(TokenType::TYPE_VAR)) {
        kind = Type::Kind::VAR;
    } else if (match(TokenType::TYPE_LIST)) {
        auto list_type = std::make_unique<Type>(Type::Kind::LIST);
        
        // Parse tipo genérico: list<T> — A9: '<T>' é obrigatório
        consume(TokenType::OP_LESS,
                "Esperado '<' após 'list': informe o tipo dos elementos, ex.: list<int>");
        {
            list_type->type_params.push_back(parseType());
            consume(TokenType::OP_GREATER, "Esperado '>' após tipo do list");
        }
        return list_type;

    } else if (match(TokenType::TYPE_DICT)) {
        auto dict_type = std::make_unique<Type>(Type::Kind::DICT);
        
        // Parse tipo genérico: dict<K, V> — A9: '<K, V>' é obrigatório
        consume(TokenType::OP_LESS,
                "Esperado '<' após 'dict': informe chave e valor, ex.: dict<string, int>");
        {
            dict_type->type_params.push_back(parseType());
            consume(TokenType::COMMA, "Esperado ',' entre tipos do dict");
            dict_type->type_params.push_back(parseType());
            consume(TokenType::OP_GREATER, "Esperado '>' após tipos do dict");
        }
        return dict_type;

    } else if (match(TokenType::TYPE_PAIR)) {
        // Tipo genérico: pair<K, V>
        auto pair_type = std::make_unique<Type>(Type::Kind::PAIR);
        
        // A9: '<A, B>' é obrigatório
        consume(TokenType::OP_LESS,
                "Esperado '<' após 'pair': informe os dois tipos, ex.: pair<string, int>");
        {
            pair_type->type_params.push_back(parseType());
            consume(TokenType::COMMA, "Esperado ',' entre tipos do pair");
            pair_type->type_params.push_back(parseType());
            consume(TokenType::OP_GREATER, "Esperado '>' após tipos do pair");
        }
        return pair_type;

    } else if (match(TokenType::TYPE_OP)) {
        // op<T1, T2, ...>: as regras (quantidade, repetição, op dentro de op...)
        // são checadas no semântico, com a posição de cada tipo
        auto op_type = std::make_unique<Type>(Type::Kind::OP);
        consume(TokenType::OP_LESS,
                "Esperado '<' após 'op': informe os tipos aceitos, ex.: op<int, string>");
        do {
            op_type->type_params.push_back(parseType());
        } while (match(TokenType::COMMA));
        consume(TokenType::OP_GREATER, "Esperado '>' após os tipos do op");
        return op_type;

    } else if (checkQualified()) {
        // C5: tipo de um módulo importado: tx.Pessoa p;
        return std::make_unique<Type>(Type::Kind::CUSTOM, parseQualifiedName());
    } else if (check(TokenType::IDENTIFIER)) {
        // Tipo customizado: nome de uma classe definida pelo usuário
        //   Ex: Pilha p;  →  type = CUSTOM("Pilha")
        Token id_token = advance();
        return std::make_unique<Type>(Type::Kind::CUSTOM, id_token.lexeme);
    } else {
        error("Esperado tipo válido");
        throw ParseError("Tipo inválido", type_token);
    }
    
    return std::make_unique<Type>(kind);
}

// ============================================================================
// EXPRESSION PARSING
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
// ============================================================================

ExprPtr Parser::parseEmbedded(const std::string& codigo, const Token& origem, int coluna) {
    Lexer lexer(codigo, origem.file_id);
    std::vector<Token> toks = lexer.tokenize();
    for (Token& t : toks) {   // o texto do printf fica numa linha só
        t.line   = origem.line;
        t.column = origem.column + coluna + t.column - 1;
    }
    for (const Token& t : toks)
        if (t.type == TokenType::UNKNOWN)
            throw ParseError("Caractere inválido '" + t.lexeme + "'", t);
    Parser p(std::move(toks));
    p.silencioso = true;
    ExprPtr e = p.parseExpression();
    if (!p.isAtEnd())
        throw ParseError("Esperado o fim da expressão (sobrou '" + p.peek().lexeme + "')", p.peek());
    return e;
}

// começa o encadeamento de expressões
ExprPtr Parser::parseExpression() {
    return parseLogicalOrExpr();
}

// r: a || b
ExprPtr Parser::parseLogicalOrExpr() {
    ExprPtr expr = parseLogicalAndExpr();
    
    while (match(TokenType::OP_OR)) {
        Token op_token = previous();
        ExprPtr right = parseLogicalAndExpr();
        expr = std::make_unique<BinaryExpr>(op_token, std::move(expr), 
                                           TokenType::OP_OR, std::move(right));
    }
    
    return expr;
}

// r: a && b
ExprPtr Parser::parseLogicalAndExpr() {
    ExprPtr expr = parseEqualityExpr();
    
    while (match(TokenType::OP_AND)) {
        Token op_token = previous();
        ExprPtr right = parseEqualityExpr();
        expr = std::make_unique<BinaryExpr>(op_token, std::move(expr), 
                                           TokenType::OP_AND, std::move(right));
    }
    
    return expr;
}

// r: (a == b) , (a != b)
ExprPtr Parser::parseEqualityExpr() {
    ExprPtr expr = parseComparisonExpr();
    
    while (match({TokenType::OP_EQUAL, TokenType::OP_NOT_EQUAL})) {
        Token op_token = previous();
        ExprPtr right = parseComparisonExpr();
        expr = std::make_unique<BinaryExpr>(op_token, std::move(expr), 
                                           op_token.type, std::move(right));
    }
    
    return expr;
}

// r: (a < b) , (a <= b) , (a > b) , (a>= b)
ExprPtr Parser::parseComparisonExpr() {
    ExprPtr expr = parseTermExpr();
    
    while (match({TokenType::OP_LESS, TokenType::OP_LESS_EQUAL, 
                  TokenType::OP_GREATER, TokenType::OP_GREATER_EQUAL})) {
        Token op_token = previous();
        ExprPtr right = parseTermExpr();
        expr = std::make_unique<BinaryExpr>(op_token, std::move(expr), 
                                           op_token.type, std::move(right));
    }
    
    return expr;
}

// r: (a + b) , (a - b)
ExprPtr Parser::parseTermExpr() {
    ExprPtr expr = parseFactorExpr();
    
    while (match({TokenType::OP_PLUS, TokenType::OP_MINUS})) {
        Token op_token = previous();
        ExprPtr right = parseFactorExpr();
        expr = std::make_unique<BinaryExpr>(op_token, std::move(expr), 
                                           op_token.type, std::move(right));
    }
    
    return expr;
}

// r: (a * b) , (a / b) , (a % b)
ExprPtr Parser::parseFactorExpr() {
    ExprPtr expr = parseUnaryExpr();
    
    while (match({TokenType::OP_MULTIPLY, TokenType::OP_DIVIDE, TokenType::OP_MODULO})) {
        Token op_token = previous();
        ExprPtr right = parseUnaryExpr();
        expr = std::make_unique<BinaryExpr>(op_token, std::move(expr), 
                                           op_token.type, std::move(right));
    }
    
    return expr;
}

// r: -1, -a, -somar(), !value, !a, !f()
ExprPtr Parser::parseUnaryExpr() {
    // A5: '-' seguido de literal inteiro vira um literal negativo antes da
    // checagem de faixa, para que -9223372036854775808 (INT64_MIN) seja válido
    if (check(TokenType::OP_MINUS) && peekNext().type == TokenType::INTEGER_LITERAL) {
        advance();                       // '-'
        Token tok = advance();           // literal
        std::int64_t v = tok.int_needs_minus ? tok.value.int_value   // já é INT64_MIN
                                             : -tok.value.int_value;
        return parsePostfixExpr(std::make_unique<LiteralExpr>(tok, v));
    }

    if (match({TokenType::OP_NOT, TokenType::OP_MINUS})) {
        Token op_token = previous();
        ExprPtr operand = parseUnaryExpr();
        return std::make_unique<UnaryExpr>(op_token, op_token.type, std::move(operand));
    }
    
    return parsePostfixExpr(parsePrimaryExpr());
}

// r: LiteralExpr(IdentifierExpr(2)), LiteralExpr(true), LiteralExpr(2.3), parseCallExpr()
ExprPtr Parser::parsePrimaryExpr() {
    // Literais booleanos
    if (match(TokenType::KW_TRUE)) {
        return std::make_unique<LiteralExpr>(previous(), true);
    }
    if (match(TokenType::KW_FALSE)) {
        return std::make_unique<LiteralExpr>(previous(), false);
    }
    
    
    // op<...>: tipo usado como valor, para comparar com type(x):
    //   type(n) == int    type(l) == list<int>
    if (check(TokenType::TYPE_INT)  || check(TokenType::TYPE_DECIMAL) ||
        check(TokenType::TYPE_STRING) || check(TokenType::TYPE_BOOL) ||
        check(TokenType::TYPE_LIST) || check(TokenType::TYPE_DICT) ||
        check(TokenType::TYPE_PAIR) || check(TokenType::TYPE_OP)) {
        Token tok = peek();
        return std::make_unique<TypeLiteralExpr>(tok, parseType());
    }

    // Instanciação de objeto:  new Pessoa("Jose", 22, 3.4)
    if (match(TokenType::KW_NEW)) {
        return parseNewExpr();
    }

    // self: o objeto atual dentro de um método (A10/B5). É palavra reservada;
    // vira um IdentifierExpr "self", que o semântico e o executor tratam à parte.
    if (match(TokenType::KW_SELF)) {
        return std::make_unique<IdentifierExpr>(previous(), "self");
    }
    
    // Literais numéricos
    if (match(TokenType::INTEGER_LITERAL)) {
        Token tok = previous();
        if (tok.int_needs_minus) {
            std::string msg = "Literal inteiro '" + tok.lexeme + "' fora do intervalo de int "
                              "(-9223372036854775808 a 9223372036854775807)";
            error(msg, tok);
            throw ParseError(msg, tok);
        }
        return std::make_unique<LiteralExpr>(tok, tok.value.int_value);
    }
    if (match(TokenType::DECIMAL_LITERAL)) {
        Token tok = previous();
        return std::make_unique<LiteralExpr>(tok, tok.value.double_value);
    }
    
    // Literais de string
    if (match(TokenType::STRING_LITERAL)) {
        Token tok = previous();
        return std::make_unique<LiteralExpr>(tok, tok.lexeme);
    }
    
    // Lista
    if (check(TokenType::LBRACKET)) {
        return parseListLiteral();
    }
    
    // '{' pode ser:
    //   - dict literal: {{...}}  -> primeiro token apos '{' e '{'
    //   - pair literal: {a, b}   -> dois valores separados por virgula
    //
    // '{}' vazio e PROIBIDO: para declarar sem valor, omita o inicializador.
    //   CORRETO: dict<str, int> d;
    //   ERRADO:  dict<str, int> d = {};
    if (check(TokenType::LBRACE)) {
        size_t saved_pos = current;
        advance(); // consome '{'

        if (check(TokenType::RBRACE)) {
            Token brace_token = previous();
            error("Inicializador vazio '{}' não é permitido. "
                  "Para declarar sem valor, omita o inicializador: 'dict<K,V> nome;'",
                  brace_token);
            throw ParseError("Inicializador vazio proibido", brace_token);
        }

        if (check(TokenType::LBRACE)) {
            // dict literal: comeca com '{{'
            current = saved_pos;
            return parseDictLiteral();
        }

        // pair literal: {expr, expr}
        current = saved_pos;
        return parsePairLiteral();
    }
    
    // Identificador ou chamada de função
    // v2.00 #10: TYPE_VAR removido daqui — 'var' não é uma expressão primária
    // C5: nome qualificado por um módulo importado: tx.soma(1) ou tx.PI
    if (checkQualified()) {
        Token name_token = peek();
        std::string name = parseQualifiedName();
        name_token.lexeme = name;
        if (match(TokenType::LPAREN)) {
            std::vector<ExprPtr> arguments;
            if (!check(TokenType::RPAREN)) {
                do {
                    arguments.push_back(parseExpression());
                } while (match(TokenType::COMMA));
            }
            consume(TokenType::RPAREN, "Esperado ')' após argumentos");
            return std::make_unique<CallExpr>(name_token, name, std::move(arguments));
        }
        return std::make_unique<IdentifierExpr>(name_token, name);
    }

    if (match(TokenType::IDENTIFIER)) {   // C6: print é nativa, não palavra-chave
        Token name_token = previous();
        
        // Verifica se é chamada de função
        if (check(TokenType::LPAREN)) {
            return parseCallExpr();
        }
        
        return std::make_unique<IdentifierExpr>(name_token, name_token.lexeme);
    }

    // v2.00 #10: captura explícita de 'var' em contexto de expressão
    if (check(TokenType::TYPE_VAR)) {
        error("'var' não pode ser usado como expressão. "
              "'var' é apenas um marcador de inferência em declarações.", peek());
        throw ParseError("'var' inválido em expressão", peek());
    }
    
    // Expressão entre parênteses
    if (match(TokenType::LPAREN)) {
        ExprPtr expr = parseExpression();
        consume(TokenType::RPAREN, "Esperado ')' após expressão");
        return expr;
    }
    
    error("Esperado expressão");
    throw ParseError("Expressão inválida", peek());
}

// r: CallExpr(token, fname, [arguments])   ->   chamada de função
ExprPtr Parser::parseCallExpr() {
    Token name_token = previous();
    std::string func_name = name_token.lexeme;
    
    consume(TokenType::LPAREN, "Esperado '(' após nome da função");
    
    std::vector<ExprPtr> arguments;
    
    if (!check(TokenType::RPAREN)) {
        do {
            arguments.push_back(parseExpression());
        } while (match(TokenType::COMMA));
    }
    
    consume(TokenType::RPAREN, "Esperado ')' após argumentos");
    
    return std::make_unique<CallExpr>(name_token, func_name, std::move(arguments));
}

// r: NewExpr(class: Pessoa, args: ["Jose", 22, 3.4])
//    Sintaxe:  new NomeClasse(arg1, arg2, ...)
//    Só válido no contexto de classes — verificado pelo semântico
ExprPtr Parser::parseNewExpr() {
    Token new_token = previous(); // já consumiu 'new'

    // C5: new apelido.Classe(...)
    std::string class_name;
    if (checkQualified()) {
        class_name = parseQualifiedName();
    } else {
        class_name = consume(TokenType::IDENTIFIER, "Esperado nome da classe após 'new'").lexeme;
    }

    consume(TokenType::LPAREN, "Esperado '(' após nome da classe em 'new'");

    std::vector<ExprPtr> arguments;
    if (!check(TokenType::RPAREN)) {
        do {
            arguments.push_back(parseExpression());
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RPAREN, "Esperado ')' após argumentos do construtor");

    return std::make_unique<NewExpr>(new_token, class_name, std::move(arguments));
}

// Encadeia acesso a membros (.key, .value), índices ([0], ["chave"]) e métodos (.add(...))
//Verifica e classifica a expressão como chamada de método, acesso a membro ou acesso por índice
ExprPtr Parser::parsePostfixExpr(ExprPtr expr) {    
    while (true) {
        if (match(TokenType::DOT)) {
            // Acesso a membro ou chamada de método
            Token dot_token = previous();           
            Token member_token = consume(TokenType::IDENTIFIER, 
                                        "Esperado nome do membro/método após '.'");
            
            if (check(TokenType::LPAREN)) {
                // É chamada de método: objeto.metodo(args)
                advance(); // consome '('
                
                std::vector<ExprPtr> arguments;
                if (!check(TokenType::RPAREN)) {
                    do {
                        arguments.push_back(parseExpression());
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::RPAREN, "Esperado ')' após argumentos do método");
                
                expr = std::make_unique<MethodCallExpr>(dot_token, std::move(expr),
                                                        member_token.lexeme,
                                                        std::move(arguments));
            } else {
                // É acesso a membro: objeto.membro
                expr = std::make_unique<MemberAccessExpr>(dot_token, std::move(expr), 
                                                          member_token.lexeme);
            }
        } else if (match(TokenType::LBRACKET)) {
            // Acesso por índice ou chave: lista[0] ou dict["chave"]; com ':' é
            // fatia de string: s[ini:fim:passo], cada parte opcional
            Token bracket_token = previous();
            ExprPtr index;
            if (!check(TokenType::COLON)) index = parseExpression();
            if (match(TokenType::COLON)) {
                ExprPtr fim, passo;
                if (!check(TokenType::COLON) && !check(TokenType::RBRACKET)) fim = parseExpression();
                if (match(TokenType::COLON) && !check(TokenType::RBRACKET)) passo = parseExpression();
                consume(TokenType::RBRACKET, "Esperado ']' após a fatia");
                expr = std::make_unique<SliceExpr>(bracket_token, std::move(expr), std::move(index),
                                                   std::move(fim), std::move(passo));
                continue;
            }
            consume(TokenType::RBRACKET, "Esperado ']' após índice/chave");

            expr = std::make_unique<IndexAccessExpr>(bracket_token, std::move(expr),
                                                     std::move(index));
        } else {
            break;
        }
    }
    
    return expr;
}

// r: ListLiteralExpr([elements])
ExprPtr Parser::parseListLiteral() {
    Token bracket_token = advance(); // consome '['

    // '[]' vazio é PROIBIDO: para declarar sem valor, omita o inicializador.
    //   CORRETO: list<str> names;
    //   ERRADO:  list<str> names = [];
    if (check(TokenType::RBRACKET)) {
        error("Inicializador vazio '[]' não é permitido. "
              "Para declarar sem valor, omita o inicializador: 'list<T> nome;'",
              bracket_token);
        throw ParseError("Inicializador vazio proibido", bracket_token);
    }

    std::vector<ExprPtr> elements;
    do {
        elements.push_back(parseExpression());
    } while (match(TokenType::COMMA));

    consume(TokenType::RBRACKET, "Esperado ']' apos elementos da lista");

    return std::make_unique<ListLiteralExpr>(bracket_token, std::move(elements));
}

// r: PairLiteralExpr {expr, expr}   →   usado em: casas.add({"casa22", 34});
ExprPtr Parser::parsePairLiteral() {
    Token brace_token = advance(); // consome '{'
    
    ExprPtr first = parseExpression();
    consume(TokenType::COMMA, "Esperado ',' entre os dois valores do pair");
    ExprPtr second = parseExpression();
    consume(TokenType::RBRACE, "Esperado '}' após o par {key, value}");
    
    return std::make_unique<PairLiteralExpr>(brace_token, std::move(first), std::move(second));
}

// r: DictLiteralExpr<str, int>{{...}, {...}}
ExprPtr Parser::parseDictLiteral() {
    Token brace_token = advance(); // consome primeiro '{'
    
    std::vector<std::pair<ExprPtr, ExprPtr>> pairs;
    
    if (!check(TokenType::RBRACE)) {
        do {
            consume(TokenType::LBRACE, "Esperado '{' antes do par chave-valor");
            
            ExprPtr key = parseExpression();
            consume(TokenType::COMMA, "Esperado ',' entre chave e valor");
            ExprPtr value = parseExpression();
            
            consume(TokenType::RBRACE, "Esperado '}' após par chave-valor");
            
            pairs.push_back({std::move(key), std::move(value)});
            
        } while (match(TokenType::COMMA));
    }
    
    consume(TokenType::RBRACE, "Esperado '}' após dicionário");
    
    return std::make_unique<DictLiteralExpr>(brace_token, std::move(pairs));
}

// ============================================================================
// STATEMENT PARSING
// ============================================================================

// começa o encadeamento de instruções (statements)
StmtPtr Parser::parseStatement() {
    try {
        // A8: fn e class só no nível superior (fn também dentro de class).
        // Antes, uma fn aninhada era aceita e nunca registrada.
        if (block_depth > 0 && (check(TokenType::KW_FN) || check(TokenType::KW_CLASS) ||
                                check(TokenType::KW_STRUCT) || check(TokenType::KW_ERROR) ||
                                check(TokenType::KW_ENUM) || check(TokenType::KW_INTERFACE))) {
            Token decl_token = peek();
            const bool is_fn = decl_token.type == TokenType::KW_FN;
            std::string nome = peekNext().type == TokenType::IDENTIFIER
                               ? " '" + peekNext().lexeme + "'" : "";
            const std::string tipo = is_fn ? "Função"
                                   : decl_token.type == TokenType::KW_CLASS  ? "Classe"
                                   : decl_token.type == TokenType::KW_STRUCT ? "Struct"
                                   : decl_token.type == TokenType::KW_ENUM   ? "Enum"
                                   : decl_token.type == TokenType::KW_INTERFACE ? "Interface"
                                                                             : "Declaração de erro";
            error(tipo + nome +
                  " declarada dentro de um bloco: '" + decl_token.lexeme +
                  "' só é permitido no nível superior" +
                  (is_fn ? " (ou como método dentro de 'class')" : "") +
                  ". Mova a declaração para fora.", decl_token);
            // Lê a declaração inteira e a descarta: o erro já foi registrado e
            // o parser continua no ponto certo, sem erros em cascata (A9)
            if (is_fn) parseFunctionDeclaration();
            else if (decl_token.type == TokenType::KW_CLASS)  parseClassDeclaration();
            else if (decl_token.type == TokenType::KW_STRUCT) parseStructDeclaration();
            else if (decl_token.type == TokenType::KW_ENUM)   parseEnumDeclaration();
            else if (decl_token.type == TokenType::KW_INTERFACE) parseInterfaceDeclaration();
            else parseErrorDeclaration();
            return nullptr;
        }

        // Função
        if (check(TokenType::KW_FN)) {
            return parseFunctionDeclaration();
        }

        // Declaração de classe
        if (check(TokenType::KW_CLASS)) {
            return parseClassDeclaration();
        }

        // C3: declaração de struct
        if (check(TokenType::KW_STRUCT)) {
            return parseStructDeclaration();
        }

        if (check(TokenType::KW_ENUM)) return parseEnumDeclaration();
        if (check(TokenType::KW_INTERFACE)) return parseInterfaceDeclaration();

        // C4: error Nome;  /  try ... except ... finally  /  throw expr;
        if (check(TokenType::KW_ERROR)) return parseErrorDeclaration();
        if (check(TokenType::KW_TRY))   return parseTryStatement();
        if (match(TokenType::KW_THROW)) {
            Token throw_token = previous();
            ExprPtr value = parseExpression();
            consume(TokenType::SEMICOLON, "Esperado ';' após 'throw'");
            return std::make_unique<ThrowStmt>(throw_token, std::move(value));
        }

        // const — deve preceder um tipo
        if (check(TokenType::KW_CONST)) {
            return parseVarDeclStatement();
        }

        // Declaração de variável (começa com tipo primitivo ou composto)
        if (check(TokenType::TYPE_INT) || check(TokenType::TYPE_DECIMAL) ||
            check(TokenType::TYPE_STRING) || check(TokenType::TYPE_BOOL) ||
            check(TokenType::TYPE_LIST) || check(TokenType::TYPE_DICT) ||
            check(TokenType::TYPE_PAIR) || check(TokenType::TYPE_OP) ||
            check(TokenType::TYPE_VAR)) {
            return parseVarDeclStatement();
        }

        // C5: declaração com tipo de módulo: tx.Pessoa p = ...;
        if (checkQualified() && current + 3 < tokens.size() &&
            tokens[current + 3].type == TokenType::IDENTIFIER) {
            return parseVarDeclStatement();
        }

        // Declaração de variável de tipo customizado: NomeClasse variavel;
        if (check(TokenType::IDENTIFIER) &&
            current + 1 < tokens.size() &&
            tokens[current + 1].type == TokenType::IDENTIFIER) {
            return parseVarDeclStatement();
        }
        
        // If
        if (check(TokenType::KW_IF)) {
            return parseIfStatement();
        }
        
        // While
        if (check(TokenType::KW_WHILE)) {
            return parseWhileStatement();
        }
        
        // For
        if (check(TokenType::KW_FOR)) {
            return parseForStatement();
        }
        
        // Return
        if (check(TokenType::KW_RETURN)) {
            return parseReturnStatement();
        }

        // C2: break; / continue;
        if (match({TokenType::KW_BREAK, TokenType::KW_CONTINUE})) {
            Token tok = previous();
            consume(TokenType::SEMICOLON, "Esperado ';' após '" + tok.lexeme + "'");
            if (tok.type == TokenType::KW_BREAK) return std::make_unique<BreakStmt>(tok);
            return std::make_unique<ContinueStmt>(tok);
        }
        
        // Bloco
        if (check(TokenType::LBRACE)) {
            return parseBlockStatement();
        }
        
        // Atribuição ou expressão
        return parseAssignmentOrExprStatement();
        
    } catch (const ParseError& e) {
        synchronize();
        return nullptr;
    }
}

// r: [const] tipo nome = expr;
StmtPtr Parser::parseVarDeclStatement() {
    bool is_const = false;
    if (match(TokenType::KW_CONST)) {
        is_const = true;
    }

    TypePtr type = parseType();
    
    Token name_token = consume(TokenType::IDENTIFIER, "Esperado nome da variável");
    std::string var_name = name_token.lexeme;

    // list<T> e dict<K,V> podem ser declarados sem inicializador (nascem vazios)
    bool can_omit_init = (type->kind == Type::Kind::LIST ||
                          type->kind == Type::Kind::DICT);

    ExprPtr initializer = nullptr;
    if (match(TokenType::OP_ASSIGN)) {
        initializer = parseExpression();
    } else if (!can_omit_init) {
        // Todos os outros tipos exigem inicializador.
        // Somente list<T> e dict<K,V> podem omitir o inicializador (nascem vazios).
        std::string suggestion;
        std::string extra_note;
        switch (type->kind) {
            case Type::Kind::INT:     suggestion = "= 0";              break;
            case Type::Kind::DECIMAL: suggestion = "= 0.0";            break;
            case Type::Kind::BOOL:    suggestion = "= false";          break;
            case Type::Kind::STRING:  suggestion = "= \"\"";           break;
            case Type::Kind::VAR:
                suggestion = "= <valor>";
                extra_note = " ('var' exige inicializador: o tipo é inferido a partir do valor inicial)";
                break;
            case Type::Kind::PAIR:    suggestion = "= {<a>, <b>}";     break;
            case Type::Kind::CUSTOM:
                suggestion = "= new " + type->name + "(...)";          break;
            default:                  suggestion = "= <valor>";        break;
        }
        error(
            "'" + var_name + "'" + " deve ser inicializado na declaração" + extra_note + ". "
            "Somente list<T> e dict<K,V> podem omitir o inicializador. "
            "Use: " + (is_const ? "const " : "") + type->toString() +
            " " + var_name + " " + suggestion + ";",
            name_token);
        throw ParseError("Declaração sem inicializador", name_token);
    }
    
    consume(TokenType::SEMICOLON, "Esperado ';' após declaração de variável");
    
    return std::make_unique<VarDeclStmt>(name_token, std::move(type),
                                         var_name, std::move(initializer), is_const);
}

// B5: o que pode ficar à esquerda de '=': variável, campo ou índice
static bool isLValue(const Expr* expr) {
    return expr->node_kind == NodeKind::Identifier   ||
           expr->node_kind == NodeKind::MemberAccess ||
           expr->node_kind == NodeKind::IndexAccess;
}

// r: variavel = (1 + 2 * 3);   somar();   m[i][j] = v;   obj.campo = v;
// B5: lê o lado esquerdo como expressão; se vier '=', ele precisa ser um lvalue
StmtPtr Parser::parseAssignmentOrExprStatement() {
    Token start_token = peek();
    ExprPtr expr = parseExpression();

    // C1: =, +=, -=, *=, /=, %=
    if (match({TokenType::OP_ASSIGN, TokenType::OP_PLUS_ASSIGN, TokenType::OP_MINUS_ASSIGN,
               TokenType::OP_MULTIPLY_ASSIGN, TokenType::OP_DIVIDE_ASSIGN,
               TokenType::OP_MODULO_ASSIGN})) {
        Token op_token = previous();
        if (!isLValue(expr.get())) {
            error("O lado esquerdo de '" + op_token.lexeme + "' não é atribuível: use uma "
                  "variável (x), um campo (obj.campo) ou um índice (lista[i]).", start_token);
            throw ParseError("Alvo de atribuição inválido", op_token);
        }
        ExprPtr value = parseExpression();
        consume(TokenType::SEMICOLON, "Esperado ';' após atribuição");
        return std::make_unique<AssignStmt>(start_token, std::move(expr),
                                            op_token.type, std::move(value));
    }

    // Expressão statement
    consume(TokenType::SEMICOLON, "Esperado ';' após expressão");
    return std::make_unique<ExprStmt>(start_token, std::move(expr));
}

// r: if(condition) {...} else {...}
StmtPtr Parser::parseIfStatement() {
    Token if_token = advance(); // consome 'if'
    
    consume(TokenType::LPAREN, "Esperado '(' após 'if'");
    ExprPtr condition = parseExpression();
    consume(TokenType::RPAREN, "Esperado ')' após condição do if");
    
    // v2.00 #1: bloco obrigatório
    if (!check(TokenType::LBRACE)) {
        error("Bloco '{...}' obrigatório após 'if'. Corpos sem bloco não são permitidos no Cinza.", peek());
        throw ParseError("Bloco obrigatório", peek());
    }
    StmtPtr then_branch = parseBlockStatement();
    
    StmtPtr else_branch = nullptr;
    if (match(TokenType::KW_ELSE)) {
        // v2.00 #1: bloco obrigatório no else também
        if (!check(TokenType::LBRACE) && !check(TokenType::KW_IF)) {
            error("Bloco '{...}' obrigatório após 'else'. Use 'else if' ou 'else {...}'.", peek());
            throw ParseError("Bloco obrigatório", peek());
        }
        if (check(TokenType::KW_IF)) {
            else_branch = parseIfStatement(); // else if encadeado OK
        } else {
            else_branch = parseBlockStatement();
        }
    }
    
    return std::make_unique<IfStmt>(if_token, std::move(condition), 
                                    std::move(then_branch), std::move(else_branch));
}

// r: while(condition) {...}
StmtPtr Parser::parseWhileStatement() {
    Token while_token = advance(); // consome 'while'
    
    consume(TokenType::LPAREN, "Esperado '(' após 'while'");
    ExprPtr condition = parseExpression();
    consume(TokenType::RPAREN, "Esperado ')' após condição do while");
    
    // v2.00 #1: bloco obrigatório
    if (!check(TokenType::LBRACE)) {
        error("Bloco '{...}' obrigatório após 'while'. Corpos sem bloco não são permitidos.", peek());
        throw ParseError("Bloco obrigatório", peek());
    }
    StmtPtr body = parseBlockStatement();
    
    return std::make_unique<WhileStmt>(while_token, std::move(condition), std::move(body));
}

// r: for(type in iterable) {...}
StmtPtr Parser::parseForStatement() {
    Token for_token = advance(); // consome 'for'
    
    consume(TokenType::LPAREN, "Esperado '(' após 'for'");
    
    // Aceita tipo explícito (int, string, etc), 'var' ou identificador direto
    std::string iterator_name;
    TypePtr type;
    
    // Verifica se começa com um tipo (primitivo, composto ou customizado)
    if (check(TokenType::TYPE_INT) || check(TokenType::TYPE_DECIMAL) ||
        check(TokenType::TYPE_STRING) || check(TokenType::TYPE_BOOL) ||
        check(TokenType::TYPE_LIST) || check(TokenType::TYPE_DICT) ||
        check(TokenType::TYPE_PAIR) || check(TokenType::TYPE_OP) ||
        check(TokenType::TYPE_VAR) ||
        // tipo customizado: NomeClasse (IDENTIFIER seguido de IDENTIFIER)
        (check(TokenType::IDENTIFIER) &&
         current + 1 < tokens.size() &&
         tokens[current + 1].type == TokenType::IDENTIFIER) ||
        // C5: tipo de módulo: for (fm.Forma f in l)
        (checkQualified() && current + 3 < tokens.size() &&
         tokens[current + 3].type == TokenType::IDENTIFIER)) {
        
        // Consome o tipo
        type = parseType();
        
        // Agora espera o nome do iterador
        iterator_name = consume(TokenType::IDENTIFIER, "Esperado nome do iterador após tipo").lexeme;
        
    } else {
        error("Esperado tipo do iterador no for");
        throw ParseError("For inválido", peek());
    }
    
    consume(TokenType::KW_IN, "Esperado 'in' no for");
    
    ExprPtr iterable = parseExpression();
    
    consume(TokenType::RPAREN, "Esperado ')' após for");
    
    // v2.00 #1: bloco obrigatório
    if (!check(TokenType::LBRACE)) {
        error("Bloco '{...}' obrigatório após 'for'. Corpos sem bloco não são permitidos.", peek());
        throw ParseError("Bloco obrigatório", peek());
    }
    StmtPtr body = parseBlockStatement();
    
    return std::make_unique<ForStmt>(for_token, std::move(type), iterator_name, 
                                     std::move(iterable), std::move(body));
}

// r: return; returna expr;
StmtPtr Parser::parseReturnStatement() {
    Token return_token = advance(); // consome 'return'
    
    ExprPtr value = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        value = parseExpression();
    }
    
    consume(TokenType::SEMICOLON, "Esperado ';' após return");
    
    return std::make_unique<ReturnStmt>(return_token, std::move(value));
}

// r: {...}
StmtPtr Parser::parseBlockStatement() {
    // A9: consume em vez de advance — antes, 'fn f() -> int return 1;'
    // engolia o 'return' como se fosse '{'
    Token brace_token = consume(TokenType::LBRACE, "Esperado '{' para abrir o bloco");

    DepthGuard block_guard{block_depth};   // A8
    DepthGuard brace_guard{brace_depth};   // A9
    
    std::vector<StmtPtr> statements;
    
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        StmtPtr stmt = parseStatement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        }
    }
    
    consume(TokenType::RBRACE, "Esperado '}' após bloco");
    
    return std::make_unique<BlockStmt>(brace_token, std::move(statements));
}

// r: fn fname([parameters]) -> type : default -> void
StmtPtr Parser::parseFunctionDeclaration() {
    Token fn_token = advance(); // consome 'fn'
    
    Token name_token = consume(TokenType::IDENTIFIER, "Esperado nome da função");
    std::string func_name = name_token.lexeme;
    
    consume(TokenType::LPAREN, "Esperado '(' após nome da função");
    
    std::vector<Parameter> parameters = parseParameterList();
    
    consume(TokenType::RPAREN, "Esperado ')' após parâmetros");
    
    // Tipo de retorno (opcional, default é void)
    TypePtr return_type = std::make_unique<Type>(Type::Kind::VOID);
    if (match(TokenType::OP_ARROW)) {
        // const em tipo de retorno é proibido
        if (check(TokenType::KW_CONST)) {
            error("'const' não é válido como tipo de retorno de função. "
                  "Remova o 'const' do retorno.", peek());
            throw ParseError("'const' inválido em retorno", peek());
        }
        Token type_token = peek();
        return_type = parseType();
        // A7: sem 'var' no retorno
        if (containsVar(return_type.get())) {
            error("'var' não é permitido como tipo de retorno de '" + func_name +
                  "'. Declare o tipo explicitamente, ex.: '-> int'.", type_token);
            throw ParseError("'var' inválido em retorno", type_token);
        }
    }
    
    // Corpo da função
    StmtPtr body = parseBlockStatement();
    
    return std::make_unique<FunctionDecl>(fn_token, func_name, std::move(parameters),
                                          std::move(return_type), std::move(body));
}

// r: [Parameter{type, name, token, is_const}, ...]
std::vector<Parameter> Parser::parseParameterList() {
    std::vector<Parameter> parameters;
    
    if (!check(TokenType::RPAREN)) {
        do {
            // Parâmetro const: const int x
            bool param_const = false;
            if (match(TokenType::KW_CONST)) {
                param_const = true;
            }
            Token   type_token = peek();
            TypePtr param_type = parseType();
            Token param_name_token = consume(TokenType::IDENTIFIER, 
                                             "Esperado nome do parâmetro");
            // A7: sem 'var' em parâmetros (o corpo precisa ser checado uma vez só)
            if (containsVar(param_type.get())) {
                error("'var' não é permitido em parâmetros ('" + param_name_token.lexeme +
                      "'). Use um tipo explícito, ex.: 'fn f(int " +
                      param_name_token.lexeme + ")'.", type_token);
                throw ParseError("'var' inválido em parâmetro", type_token);
            }
            parameters.emplace_back(std::move(param_type), param_name_token.lexeme, 
                                   param_name_token, param_const);
        } while (match(TokenType::COMMA));
    }
    
    return parameters;
}

// ============================================================================
// CLASS DECLARATION PARSING
//
// Gramática:
//   classDecl  = 'class' IDENTIFIER '{' classMember* '}'
//
//   classMember = fieldDecl      →  campo privado
//               | fnDecl         →  método privado
//               | pubBlock        →  bloco público
//
//   fieldDecl  = type IDENTIFIER ( '=' expr )? ';'
//     ↳ sem '=' só é permitido para list<T> e dict<K,V> (nascem vazios implicitamente)
//     ↳ todos os outros tipos exigem '=' com valor explícito
//   pubBlock   = 'pub' '{' fnDecl* '}'
//
// Exemplos:
//   class Pilha {
//       int topo = 0;               ← campo privado (inicializador obrigatório)
//       list<int> dados;            ← campo privado (nasce vazio implicitamente)
//
//       pub {
//           fn push(int x) -> void { ... }
//           fn pop()       -> int  { ... }
//       }
//   }
// ============================================================================

StmtPtr Parser::parseClassDeclaration() {
    Token class_token = advance(); // consome 'class'

    Token name_token = consume(TokenType::IDENTIFIER,
                                "Esperado nome da classe após 'class'");
    std::string class_name = name_token.lexeme;

    // class X : Forma, Desenhavel — interfaces cumpridas
    std::vector<std::string> interfaces;
    std::vector<Token>       interface_tokens;
    if (match(TokenType::COLON)) {
        do {
            Token t = peek();
            if (checkQualified()) {
                t.lexeme = parseQualifiedName();
            } else {
                t = consume(TokenType::IDENTIFIER, "Esperado o nome de uma interface após ':'");
            }
            interfaces.push_back(t.lexeme);
            interface_tokens.push_back(t);
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::LBRACE, "Esperado '{' após nome da classe");
    DepthGuard class_brace_guard{brace_depth};   // A9

    std::vector<ClassDecl::Field>        fields;    
    std::unique_ptr<ClassDecl::Constructor> ctor;   // nullptr → sem construtor
    std::vector<StmtPtr>                 priv_methods;
    std::vector<StmtPtr>                 pub_methods;

    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        try {
            // ── bloco pub { fn ... } ──────────────────────────────────────
            if (check(TokenType::KW_PUB)) {
                advance(); // consome 'pub'
                consume(TokenType::LBRACE, "Esperado '{' após 'pub'");
                DepthGuard pub_brace_guard{brace_depth};   // A9

                while (!check(TokenType::RBRACE) && !isAtEnd()) {
                    if (check(TokenType::KW_FN)) {
                        pub_methods.push_back(parseFunctionDeclaration());

                    // ── construtor: NomeClasse(params) { body } ──────────
                    //    IDENTIFIER seguido de '(' dentro do pub{} onde
                    //    o identificador bate com o nome da classe
                    } else if (check(TokenType::IDENTIFIER)) {
                        Token ctor_tok = peek();

                        if (ctor_tok.lexeme == class_name) {
                            // É um construtor

                            if (ctor) {
                                error("A classe '" + class_name +
                                      "' já possui um construtor", ctor_tok);
                            }

                            advance(); // consome o nome da classe
                            consume(TokenType::LPAREN,
                                    "Esperado '(' após nome do construtor");

                            auto ctor_params = parseParameterList();     // parametros do construtor

                            consume(TokenType::RPAREN,
                                    "Esperado ')' após parâmetros do construtor");

                            StmtPtr ctor_body = parseBlockStatement();   // corpo do construtor

                            ctor = std::make_unique<ClassDecl::Constructor>(
                                       std::move(ctor_params),
                                       std::move(ctor_body),
                                       ctor_tok);
                        } else {
                            error("Identificador '" + ctor_tok.lexeme +
                                  "' inválido dentro de pub{} — "
                                  "esperado 'fn' ou construtor '" +
                                  class_name + "()'", ctor_tok);
                            synchronize();
                        }
                    } else {
                        error("Esperado 'fn' ou construtor dentro do bloco pub");
                        synchronize();
                    }
                }

                consume(TokenType::RBRACE, "Esperado '}' para fechar bloco pub");

            // ── método privado: fn fora do pub{} ─────────────────────────
            } else if (check(TokenType::KW_FN)) {
                priv_methods.push_back(parseFunctionDeclaration());

            // ── campo da classe: tipo nome; ou tipo nome = expr; ──────────
            //    O tipo pode ser primitivo, composto, ou customizado (IDENTIFIER)
            } else if (checkFieldStart()) {

                fields.push_back(parseFieldDeclaration());

            } else {
                error("Membro de classe inválido: esperado campo, 'fn' ou 'pub'");
                synchronize();
            }

        } catch (const ParseError&) {
            synchronize();
        }
    }

    consume(TokenType::RBRACE, "Esperado '}' para fechar a classe");

    auto classe = std::make_unique<ClassDecl>(class_token, class_name,
                                       std::move(fields),
                                       std::move(ctor),
                                       std::move(priv_methods),
                                       std::move(pub_methods));
    classe->interfaces       = std::move(interfaces);
    classe->interface_tokens = std::move(interface_tokens);
    return classe;
}

// Início de um campo: tipo primitivo/composto, ou tipo customizado
// (IDENTIFIER seguido de IDENTIFIER). Usado por class e struct (C3).
bool Parser::checkFieldStart() const {
    return check(TokenType::TYPE_INT)    || check(TokenType::TYPE_DECIMAL) ||
           check(TokenType::TYPE_STRING) || check(TokenType::TYPE_BOOL)    ||
           check(TokenType::TYPE_LIST)   || check(TokenType::TYPE_DICT)    ||
           check(TokenType::TYPE_PAIR)   || check(TokenType::TYPE_VAR)     ||
           check(TokenType::TYPE_OP)     ||
           (check(TokenType::IDENTIFIER) &&
            current + 1 < tokens.size() &&
            tokens[current + 1].type == TokenType::IDENTIFIER) ||
           (checkQualified() && current + 3 < tokens.size() &&            // C5
            tokens[current + 3].type == TokenType::IDENTIFIER);
}

// Campo de class ou struct:  tipo nome;  ou  tipo nome = expr;
// C3: extraído de parseClassDeclaration para ser usado também por struct.
ClassDecl::Field Parser::parseFieldDeclaration() {
    Token   field_tok = peek();
    TypePtr field_type = parseType();
    Token   field_name = consume(TokenType::IDENTIFIER, "Esperado nome do campo");

    // A7: 'var' só em declaração local e no iterador do for
    if (containsVar(field_type.get())) {
        error("'var' não é permitido no campo '" + field_name.lexeme +
              "'. Declare o tipo explicitamente, ex.: 'int " +
              field_name.lexeme + " = 0;'.", field_tok);
        throw ParseError("'var' inválido em campo", field_tok);
    }

    // list<T> e dict<K,V> podem ser declarados sem inicializador
    bool field_can_omit = (field_type->kind == Type::Kind::LIST ||
                           field_type->kind == Type::Kind::DICT);

    ExprPtr initializer = nullptr;
    if (match(TokenType::OP_ASSIGN)) {
        initializer = parseExpression();
    } else if (!field_can_omit) {
        // Todos os outros tipos de campo exigem inicializador.
        // Somente list<T> e dict<K,V> podem omitir o inicializador (nascem vazios).
        std::string suggestion;
        std::string extra_note;
        switch (field_type->kind) {
            case Type::Kind::INT:     suggestion = "= 0";          break;
            case Type::Kind::DECIMAL: suggestion = "= 0.0";        break;
            case Type::Kind::BOOL:    suggestion = "= false";      break;
            case Type::Kind::STRING:  suggestion = "= \"\"";       break;
            case Type::Kind::VAR:
                suggestion = "= <valor>";
                extra_note = " ('var' exige inicializador: o tipo é inferido a partir do valor inicial)";
                break;
            case Type::Kind::PAIR:    suggestion = "= {<a>, <b>}"; break;
            case Type::Kind::CUSTOM:
                suggestion = "= new " + field_type->name + "(...)";break;
            default:                  suggestion = "= <valor>";    break;
        }
        error(
            "Campo '" + field_name.lexeme + "' deve ser inicializado na declaração" + extra_note + ". "
            "Somente list<T> e dict<K,V> podem omitir o inicializador. "
            "Use: " + field_type->toString() + " " +
            field_name.lexeme + " " + suggestion + ";",
            field_name);
        throw ParseError("Campo sem inicializador", field_name);
    }

    consume(TokenType::SEMICOLON,
            "Esperado ';' após declaração de campo");

    return ClassDecl::Field(std::move(field_type), field_name.lexeme,
                            std::move(initializer), field_tok);
}

// interface Forma { fn area() -> decimal; fn escala(decimal f); }
StmtPtr Parser::parseInterfaceDeclaration() {
    Token iface_token = advance();   // consome 'interface'
    Token name_token  = consume(TokenType::IDENTIFIER,
                                "Esperado o nome da interface, ex.: interface Forma { ... }");
    consume(TokenType::LBRACE, "Esperado '{' após o nome da interface");
    DepthGuard guard{brace_depth};
    std::vector<InterfaceDecl::Method> metodos;
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        if (!check(TokenType::KW_FN)) {
            error("Uma interface só tem assinaturas de métodos: fn nome(parâmetros) -> tipo;",
                  peek());
            throw ParseError("interface só tem métodos", peek());
        }
        InterfaceDecl::Method m;
        m.token = advance();   // 'fn'
        m.name  = consume(TokenType::IDENTIFIER, "Esperado o nome do método").lexeme;
        consume(TokenType::LPAREN, "Esperado '(' após o nome do método");
        m.parameters = parseParameterList();
        consume(TokenType::RPAREN, "Esperado ')' após os parâmetros");
        m.return_type = std::make_unique<Type>(Type::Kind::VOID);
        if (match(TokenType::OP_ARROW)) m.return_type = parseType();
        consume(TokenType::SEMICOLON, "Esperado ';' após a assinatura: na interface o método não "
                                      "tem corpo");
        metodos.push_back(std::move(m));
    }
    consume(TokenType::RBRACE, "Esperado '}' ao fim da interface");
    if (metodos.empty()) {
        error("A interface '" + name_token.lexeme + "' precisa de pelo menos um método.", name_token);
        throw ParseError("interface vazia", name_token);
    }
    return std::make_unique<InterfaceDecl>(iface_token, name_token.lexeme, std::move(metodos));
}

// enum Cor { Vermelho, Verde, Azul }
StmtPtr Parser::parseEnumDeclaration() {
    Token enum_token = advance();   // consome 'enum'
    Token name_token = consume(TokenType::IDENTIFIER,
                               "Esperado o nome do enum, ex.: enum Cor { Vermelho, Verde }");
    consume(TokenType::LBRACE, "Esperado '{' após o nome do enum");
    std::vector<std::string> membros;
    std::vector<Token>       tokens_membros;
    if (check(TokenType::RBRACE)) {
        error("O enum '" + name_token.lexeme + "' precisa de pelo menos um valor.", peek());
        throw ParseError("enum vazio", peek());
    }
    do {
        Token m = consume(TokenType::IDENTIFIER, "Esperado o nome de um valor do enum");
        membros.push_back(m.lexeme);
        tokens_membros.push_back(m);
    } while (match(TokenType::COMMA));
    consume(TokenType::RBRACE, "Esperado '}' ao fim do enum (os valores são separados por ',')");
    return std::make_unique<EnumDecl>(enum_token, name_token.lexeme, std::move(membros),
                                      std::move(tokens_membros));
}

// C4: error Nome;
StmtPtr Parser::parseErrorDeclaration() {
    Token error_token = advance();   // consome 'error'
    Token name_token  = consume(TokenType::IDENTIFIER,
                                "Esperado o nome do tipo de erro, ex.: error SaldoInsuficiente;");
    consume(TokenType::SEMICOLON, "Esperado ';' após a declaração de erro");
    return std::make_unique<ErrorDecl>(error_token, name_token.lexeme);
}

// C4: try { ... } except (Tipo nome) { ... } ... [finally { ... }]
StmtPtr Parser::parseTryStatement() {
    Token try_token = advance();   // consome 'try'

    auto bloco = [&](const std::string& depois) -> StmtPtr {
        if (!check(TokenType::LBRACE)) {
            error("Bloco '{...}' obrigatório após '" + depois + "'.", peek());
            throw ParseError("Bloco obrigatório", peek());
        }
        return parseBlockStatement();
    };

    StmtPtr body = bloco("try");

    std::vector<ExceptClause> handlers;
    while (match(TokenType::KW_EXCEPT)) {
        Token except_token = previous();
        consume(TokenType::LPAREN, "Esperado '(' após 'except', ex.: except (ValueError e)");
        Token type_tok = peek();
        if (checkQualified()) type_tok.lexeme = parseQualifiedName();   // C5
        else type_tok = consume(TokenType::IDENTIFIER,
                                "Esperado o tipo do erro, ex.: except (ValueError e)");
        Token var_tok  = consume(TokenType::IDENTIFIER,
                                 "Esperado o nome do erro, ex.: except (ValueError e)");
        consume(TokenType::RPAREN, "Esperado ')' após 'except (Tipo nome'");
        handlers.push_back({type_tok.lexeme, var_tok.lexeme, except_token, bloco("except")});
    }

    StmtPtr finally_block = nullptr;
    if (match(TokenType::KW_FINALLY)) finally_block = bloco("finally");

    if (handlers.empty() && !finally_block) {
        error("'try' precisa de pelo menos um 'except' ou um 'finally'.", try_token);
        throw ParseError("try incompleto", try_token);
    }

    return std::make_unique<TryStmt>(try_token, std::move(body), std::move(handlers),
                                     std::move(finally_block));
}

// C3: struct Nome { campos... }  — só campos, sem métodos e sem bloco pub
StmtPtr Parser::parseStructDeclaration() {
    Token struct_token = advance(); // consome 'struct'

    Token name_token = consume(TokenType::IDENTIFIER,
                               "Esperado nome do struct após 'struct'");

    consume(TokenType::LBRACE, "Esperado '{' após nome do struct");
    DepthGuard struct_brace_guard{brace_depth};   // A9

    std::vector<ClassDecl::Field> fields;

    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        try {
            if (check(TokenType::KW_FN) || check(TokenType::KW_PUB)) {
                error("struct '" + name_token.lexeme + "' só pode ter campos: métodos e o "
                      "bloco 'pub' não são permitidos em struct (todos os campos já são "
                      "públicos). Use 'class' se precisar de métodos.", peek());
                throw ParseError("Membro inválido em struct", peek());
            }
            if (checkFieldStart()) {
                fields.push_back(parseFieldDeclaration());
            } else {
                error("Membro de struct inválido: esperado um campo (tipo nome = valor;)");
                synchronize();
            }
        } catch (const ParseError&) {
            synchronize();
        }
    }

    consume(TokenType::RBRACE, "Esperado '}' para fechar o struct");

    return std::make_unique<StructDecl>(struct_token, name_token.lexeme, std::move(fields));
}

// ============================================================================
// MAIN PARSE METHOD
// ============================================================================

Program Parser::parse() {
    std::vector<StmtPtr> statements;

    // C5: imports vêm antes de tudo (o parser precisa dos apelidos)
    std::vector<ImportDecl> imports;
    while (check(TokenType::KW_IMPORT)) {
        try {
            imports.push_back(parseImport());
        } catch (const ParseError&) {
            synchronize();
        }
    }
    
    while (!isAtEnd()) {
        try {
            if (check(TokenType::KW_IMPORT)) {
                error("'import' deve vir no início do arquivo, antes das declarações.", peek());
                throw ParseError("import fora do início", peek());
            }

            // C5: pub no nível superior exporta a declaração do módulo
            bool is_pub = false;
            if (match(TokenType::KW_PUB)) {
                is_pub = true;
                if (!check(TokenType::KW_FN) && !check(TokenType::KW_CLASS) &&
                    !check(TokenType::KW_STRUCT) && !check(TokenType::KW_ERROR) &&
                    !check(TokenType::KW_ENUM) && !check(TokenType::KW_INTERFACE) &&
                    !check(TokenType::KW_CONST)) {
                    error("'pub' no nível superior só vale antes de fn, class, struct, enum, "
                          "interface, error ou const.", peek());
                    throw ParseError("pub inválido", peek());
                }
            }

            StmtPtr stmt = parseStatement();
            if (!stmt) continue;
            stmt->is_pub = is_pub;

            // Fase 2.5: o nível superior é declarativo (program = declaration*).
            // Não há variáveis globais mutáveis nem comandos soltos: o programa
            // começa em fn main().
            auto* var = dynamic_cast<VarDeclStmt*>(stmt.get());
            if (var && !var->is_const) {
                error("Variável global '" + var->name + "' precisa ser 'const': não há "
                      "variáveis globais mutáveis. Use 'const' ou mova a declaração "
                      "para dentro de 'fn main()'.", var->token);
            } else if (!var &&
                       stmt->node_kind != NodeKind::FunctionDecl &&
                       stmt->node_kind != NodeKind::ClassDecl &&
                       stmt->node_kind != NodeKind::StructDecl &&
                       stmt->node_kind != NodeKind::EnumDecl &&
                       stmt->node_kind != NodeKind::InterfaceDecl &&
                       stmt->node_kind != NodeKind::ErrorDecl) {
                error("Comando solto no nível superior: aqui só são permitidas "
                      "declarações (fn, class, struct, enum, interface, error, const). Mova-o para dentro de 'fn main()'.",
                      stmt->token);
            } else {
                statements.push_back(std::move(stmt));
            }
        } catch (const ParseError& e) {
            // Erro já foi reportado, continuar
            synchronize();
        }
    }
    
    Program program(std::move(statements));
    program.imports = std::move(imports);
    return program;
}

// C5: import a.b.c [as x];
ImportDecl Parser::parseImport() {
    Token import_token = advance();   // consome 'import'
    ImportDecl decl;
    decl.token = import_token;
    decl.path.push_back(consume(TokenType::IDENTIFIER,
                                "Esperado o nome do módulo após 'import', ex.: import util.texto;").lexeme);
    while (match(TokenType::DOT))
        decl.path.push_back(consume(TokenType::IDENTIFIER,
                                    "Esperado nome após '.' no caminho do módulo").lexeme);
    decl.alias = decl.path.back();
    if (match(TokenType::KW_AS))
        decl.alias = consume(TokenType::IDENTIFIER, "Esperado o apelido após 'as'").lexeme;
    consume(TokenType::SEMICOLON, "Esperado ';' após o import");

    if (module_aliases.count(decl.alias)) {
        error("Apelido de módulo '" + decl.alias + "' repetido: use 'as' para dar outro nome.",
              import_token);
        throw ParseError("apelido repetido", import_token);
    }
    module_aliases.insert(decl.alias);
    return decl;
}

// C5: `apelido.nome`, com apelido vindo de um import deste arquivo
bool Parser::checkQualified() const {
    return check(TokenType::IDENTIFIER) && module_aliases.count(peek().lexeme) &&
           current + 2 < tokens.size() &&
           tokens[current + 1].type == TokenType::DOT &&
           isWord(tokens[current + 2]);
}

// C6: depois de `apelido.` vale também palavra-chave (Random.int, Random.decimal):
// ali não há ambiguidade, só pode ser um nome do módulo
bool Parser::isWord(const Token& tok) {
    if (tok.type == TokenType::IDENTIFIER) return true;
    const std::string& l = tok.lexeme;
    if (l.empty() || !(std::isalpha(static_cast<unsigned char>(l[0])) || l[0] == '_')) return false;
    for (unsigned char c : l)
        if (!std::isalnum(c) && c != '_') return false;
    return true;
}

std::string Parser::parseQualifiedName() {
    std::string alias = advance().lexeme;
    advance();   // '.'
    return alias + "." + advance().lexeme;
}

} // namespace cinza
