#include "parser.h"
#include <iostream>
#include <sstream>




namespace cinza {


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
    std::ostringstream oss;
    oss << "[Linha " << token.line << ", Coluna " << token.column << "] Erro: " 
        << message << " (token: '" << token.lexeme << "')";
    error_messages.push_back(oss.str());
}

void Parser::synchronize() {
    advance();
    
    while (!isAtEnd()) {
        if (previous().type == TokenType::SEMICOLON) return;
        
        switch (peek().type) {
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
            case TokenType::TYPE_VAR:
                return;
            default:
                advance();
        }
    }
}

void Parser::printErrors() const {
    for (const auto& msg : error_messages) {
        std::cerr << msg << "\n";
    }
}

// ============================================================================
// TYPE PARSING
// ============================================================================

// r: retorna um objeto do tipo 'Type<T>'
TypePtr Parser::parseType() {
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
        
        // Parse tipo genérico: list<T>
        if (match(TokenType::OP_LESS)) {
            list_type->type_params.push_back(parseType());
            consume(TokenType::OP_GREATER, "Esperado '>' após tipo do list");
        }
        return list_type;

    } else if (match(TokenType::TYPE_DICT)) {
        auto dict_type = std::make_unique<Type>(Type::Kind::DICT);
        
        // Parse tipo genérico: dict<K, V>
        if (match(TokenType::OP_LESS)) {
            dict_type->type_params.push_back(parseType());
            consume(TokenType::COMMA, "Esperado ',' entre tipos do dict");
            dict_type->type_params.push_back(parseType());
            consume(TokenType::OP_GREATER, "Esperado '>' após tipos do dict");
        }
        return dict_type;

    } else if (match(TokenType::TYPE_PAIR)) {
        // Tipo genérico: pair<K, V>
        auto pair_type = std::make_unique<Type>(Type::Kind::PAIR);
        
        if (match(TokenType::OP_LESS)) {
            pair_type->type_params.push_back(parseType());
            consume(TokenType::COMMA, "Esperado ',' entre tipos do pair");
            pair_type->type_params.push_back(parseType());
            consume(TokenType::OP_GREATER, "Esperado '>' após tipos do pair");
        }
        return pair_type;

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
    
    
    // Instanciação de objeto:  new Pessoa("Jose", 22, 3.4)
    if (match(TokenType::KW_NEW)) {
        return parseNewExpr();
    }
    
    // Literais numéricos
    if (match(TokenType::INTEGER_LITERAL)) {
        Token tok = previous();
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
            error("Inicializador vazio '{}' nao é permitido. "
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
    if (match(TokenType::IDENTIFIER) || match(TokenType::KW_PRINT)) {
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

    Token class_token = consume(TokenType::IDENTIFIER,
                                 "Esperado nome da classe após 'new'");

    consume(TokenType::LPAREN, "Esperado '(' após nome da classe em 'new'");

    std::vector<ExprPtr> arguments;
    if (!check(TokenType::RPAREN)) {
        do {
            arguments.push_back(parseExpression());
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RPAREN, "Esperado ')' após argumentos do construtor");

    return std::make_unique<NewExpr>(new_token, class_token.lexeme,
                                     std::move(arguments));
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
            // Acesso por índice ou chave: lista[0] ou dict["chave"]
            Token bracket_token = previous();
            ExprPtr index = parseExpression();
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
        error("Inicializador vazio '[]' nao e permitido. "
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
        // Função
        if (check(TokenType::KW_FN)) {
            return parseFunctionDeclaration();
        }

        // Declaração de classe
        if (check(TokenType::KW_CLASS)) {
            return parseClassDeclaration();
        }

        // const — deve preceder um tipo
        if (check(TokenType::KW_CONST)) {
            return parseVarDeclStatement();
        }

        // Declaração de variável (começa com tipo primitivo ou composto)
        if (check(TokenType::TYPE_INT) || check(TokenType::TYPE_DECIMAL) ||
            check(TokenType::TYPE_STRING) || check(TokenType::TYPE_BOOL) ||
            check(TokenType::TYPE_LIST) || check(TokenType::TYPE_DICT) ||
            check(TokenType::TYPE_PAIR) || check(TokenType::TYPE_VAR)) {
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

// r: variavel = (1 + 2 * 3);   somar();   nome[idx] = valor;
StmtPtr Parser::parseAssignmentOrExprStatement() {
    Token start_token = peek();
    
    if (check(TokenType::IDENTIFIER)) {
        size_t saved = current;           // salva posição antes do IDENTIFIER
        Token name_token = advance();
        
        // v2.00 #8: Atribuição por índice: nome[expr] = expr;
        if (check(TokenType::LBRACKET)) {
            advance(); // consome '['
            ExprPtr index = parseExpression();
            consume(TokenType::RBRACKET, "Esperado ']' após índice");

            if (match(TokenType::OP_ASSIGN)) {
                ExprPtr val = parseExpression();
                consume(TokenType::SEMICOLON, "Esperado ';' após atribuição por índice");
                return std::make_unique<IndexAssignmentStmt>(
                    name_token, name_token.lexeme, std::move(index), std::move(val));
            }
            // Não é atribuição — restaura e parse como expressão
            current = saved;
        }
        // Atribuição simples?
        else if (match(TokenType::OP_ASSIGN)) {
            ExprPtr value = parseExpression();
            consume(TokenType::SEMICOLON, "Esperado ';' após atribuição");
            return std::make_unique<AssignmentStmt>(name_token, name_token.lexeme, 
                                                    std::move(value));
        } else {
            current = saved; // Restaura para parsear como expressão
        }
    }
    
    // Expressão statement
    ExprPtr expr = parseExpression();
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
        check(TokenType::TYPE_PAIR) || check(TokenType::TYPE_VAR) ||
        // tipo customizado: NomeClasse (IDENTIFIER seguido de IDENTIFIER)
        (check(TokenType::IDENTIFIER) &&
         current + 1 < tokens.size() &&
         tokens[current + 1].type == TokenType::IDENTIFIER)) {
        
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
    Token brace_token = advance(); // consome '{'
    
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
        return_type = parseType();
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
            TypePtr param_type = parseType();
            Token param_name_token = consume(TokenType::IDENTIFIER, 
                                             "Esperado nome do parâmetro");
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

    consume(TokenType::LBRACE, "Esperado '{' após nome da classe");

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
            } else if (check(TokenType::TYPE_INT)    || check(TokenType::TYPE_DECIMAL) ||
                       check(TokenType::TYPE_STRING)  || check(TokenType::TYPE_BOOL)   ||
                       check(TokenType::TYPE_LIST)    || check(TokenType::TYPE_DICT)   ||
                       check(TokenType::TYPE_PAIR)    || check(TokenType::TYPE_VAR)    ||
                       // tipo customizado: IDENTIFIER seguido de IDENTIFIER
                       (check(TokenType::IDENTIFIER) &&
                        current + 1 < tokens.size() &&  
                        tokens[current + 1].type == TokenType::IDENTIFIER)) {

                Token   field_tok = peek();
                TypePtr field_type = parseType();
                Token   field_name = consume(TokenType::IDENTIFIER, "Esperado nome do campo");

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

                fields.emplace_back(std::move(field_type), field_name.lexeme,
                                    std::move(initializer), field_tok);

            } else {
                error("Membro de classe inválido: esperado campo, 'fn' ou 'pub'");
                synchronize();
            }

        } catch (const ParseError&) {
            synchronize();
        }
    }

    consume(TokenType::RBRACE, "Esperado '}' para fechar a classe");

    return std::make_unique<ClassDecl>(class_token, class_name,
                                       std::move(fields),
                                       std::move(ctor),
                                       std::move(priv_methods),
                                       std::move(pub_methods));
}

// ============================================================================
// MAIN PARSE METHOD
// ============================================================================

Program Parser::parse() {
    std::vector<StmtPtr> statements;
    
    while (!isAtEnd()) {
        try {
            StmtPtr stmt = parseStatement();
            if (stmt) {
                statements.push_back(std::move(stmt));
            }
        } catch (const ParseError& e) {
            // Erro já foi reportado, continuar
            synchronize();
        }
    }
    
    return Program(std::move(statements));
}

} // namespace cinza
