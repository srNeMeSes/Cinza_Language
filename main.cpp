#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "semantic.h"
#include "executor.h"
#include <iostream>
#include <fstream>
#include <iomanip>

#ifdef _WIN32
extern "C" {
    __declspec(dllimport) int __stdcall SetConsoleOutputCP(unsigned int);
    __declspec(dllimport) int __stdcall SetConsoleCP(unsigned int);
}
#endif

using namespace cinza;

// Função para ler arquivo
std::string readFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Erro ao abrir arquivo: " + filename);
    }
    
    std::string content;
    std::string line;
    while (std::getline(file, line)) {
        content += line + "\n";
    }
    file.close();
    
    return content;
}

// Função auxiliar para converter TokenType em string legível
std::string tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::INTEGER_LITERAL: return "INTEGER_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::DECIMAL_LITERAL: return "DECIMAL_LITERAL";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        
        case TokenType::KW_FN: return "KW_FN";
        case TokenType::KW_RETURN: return "KW_RETURN";
        case TokenType::KW_IF: return "KW_IF";
        case TokenType::KW_ELSE: return "KW_ELSE";
        case TokenType::KW_WHILE: return "KW_WHILE";
        case TokenType::KW_FOR: return "KW_FOR";
        case TokenType::KW_IN: return "KW_IN";
        case TokenType::KW_TRUE: return "KW_TRUE";
        case TokenType::KW_FALSE: return "KW_FALSE";
        case TokenType::KW_PRINT: return "KW_PRINT";
        case TokenType::KW_CLASS: return "KW_CLASS";
        case TokenType::KW_PUB:   return "KW_PUB";
        case TokenType::KW_NEW:   return "KW_NEW";
        case TokenType::KW_CONST: return "KW_CONST";
        
        case TokenType::TYPE_INT: return "TYPE_INT";
        case TokenType::TYPE_DECIMAL: return "TYPE_DECIMAL";
        case TokenType::TYPE_STRING: return "TYPE_STRING";
        case TokenType::TYPE_BOOL: return "TYPE_BOOL";
        case TokenType::TYPE_VOID: return "TYPE_VOID";
        case TokenType::TYPE_DICT: return "TYPE_DICT";
        case TokenType::TYPE_LIST: return "TYPE_LIST";
        case TokenType::TYPE_VAR: return "TYPE_VAR";
        case TokenType::TYPE_PAIR: return "TYPE_PAIR";
        
        case TokenType::OP_PLUS: return "OP_PLUS";
        case TokenType::OP_MINUS: return "OP_MINUS";
        case TokenType::OP_MULTIPLY: return "OP_MULTIPLY";
        case TokenType::OP_DIVIDE: return "OP_DIVIDE";
        case TokenType::OP_MODULO: return "OP_MODULO";
        case TokenType::OP_ASSIGN: return "OP_ASSIGN";
        case TokenType::OP_EQUAL: return "OP_EQUAL";
        case TokenType::OP_NOT_EQUAL: return "OP_NOT_EQUAL";
        case TokenType::OP_LESS: return "OP_LESS";
        case TokenType::OP_LESS_EQUAL: return "OP_LESS_EQUAL";
        case TokenType::OP_GREATER: return "OP_GREATER";
        case TokenType::OP_GREATER_EQUAL: return "OP_GREATER_EQUAL";
        case TokenType::OP_AND: return "OP_AND";
        case TokenType::OP_OR: return "OP_OR";
        case TokenType::OP_NOT: return "OP_NOT";
        case TokenType::OP_ARROW: return "OP_ARROW";
        
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LBRACKET: return "LBRACKET";
        case TokenType::RBRACKET: return "RBRACKET";
        case TokenType::COMMA: return "COMMA";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::DOT: return "DOT";
        case TokenType::COLON: return "COLON";
        
        case TokenType::END_OF_FILE: return "END_OF_FILE";
        case TokenType::UNKNOWN: return "UNKNOWN";
        
        default: return "UNKNOWN_TYPE";
    }
}

// Função para imprimir tokens formatados
void printTokens(const std::vector<Token>& tokens, bool verbose = false) {
    if (!verbose) {
        //std::cout << "✓ Análise léxica concluída: " << tokens.size() - 1 << " tokens gerados\n";
        return;
    }
    
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "ANÁLISE LÉXICA - LINGUAGEM CINZA\n";
    std::cout << std::string(80, '=') << "\n\n";
    
    std::cout << std::left 
              << std::setw(5) << "Ln" 
              << std::setw(5) << "Col" 
              << std::setw(25) << "Tipo"
              << std::setw(30) << "Lexema"
              << "Valor"
              << "\n";
    std::cout << std::string(80, '-') << "\n";
    
    for (const auto& token : tokens) {
        if (token.type == TokenType::END_OF_FILE) {
            //std::cout << "\n" << std::string(80, '-') << "\n";
            //td::cout << "FIM DA ANÁLISE - Total de tokens: " << tokens.size() - 1 << "\n";
            //std::cout << std::string(80, '=') << "\n";
            break;
        }
        
        std::cout << std::left 
                  << std::setw(5) << token.line
                  << std::setw(5) << token.column;
        
        std::cout << std::setw(25) << tokenTypeToString(token.type);
        
        std::string displayLexeme = token.lexeme;
        if (displayLexeme.length() > 28) {
            displayLexeme = displayLexeme.substr(0, 25) + "...";
        }
        std::cout << std::setw(30) << ("'" + displayLexeme + "'");
        
    // Remove BOOL_LITERAL dos casos (v2.00 #16 - não existe mais)
    switch (token.type) {
            case TokenType::INTEGER_LITERAL:
                std::cout << token.value.int_value;
                break;
            case TokenType::DECIMAL_LITERAL:
                std::cout << token.value.double_value;
                break;
            case TokenType::KW_TRUE:
            case TokenType::KW_FALSE:
                std::cout << (token.value.bool_value ? "true" : "false");
                break;
            default:
                break;
        }
        
        std::cout << "\n";
    }
}

void printHelp() {
    std::cout << "Compilador Cinza - Linguagem de Programação Interpretada\n\n";
    std::cout << "Uso:\n";
    std::cout << "  cinza_compiler <arquivo.cinza> [opções]\n\n";
    std::cout << "Opções:\n";
    std::cout << "  --tokens, -t     Exibe tokens detalhados\n";
    std::cout << "  --ast, -a        Exibe a árvore sintática (AST)\n";
    std::cout << "  --help, -h       Exibe esta ajuda\n\n";
    std::cout << "Exemplo:\n";
    std::cout << "  cinza_compiler exemplo.cinza --ast\n\n";
}

int main(int argc, char* argv[]) {

    #ifdef _WIN32
    SetConsoleOutputCP(65001);  // força UTF-8 no terminal Windows
    SetConsoleCP(65001);
    #endif

    if (argc < 2) {
        printHelp();
        return 1;
    }
    
    // Parse argumentos
    std::string filename;
    bool show_tokens = false;
    bool show_ast = false; // por padrão NÃO mostra AST
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "--tokens" || arg == "-t") {
            show_tokens = true;
        } else if (arg == "--ast" || arg == "-a") {
            show_ast = true;
        } else if (arg[0] != '-') {
            filename = arg;
        }
    }
    
    if (filename.empty()) {
        std::cerr << "Erro: Nenhum arquivo fornecido.\n";
        printHelp();
        return 1;
    }
    
    try {
        // ====================================================================
        // FASE 1: ANÁLISE LÉXICA
        // ====================================================================
        //std::cout << "\n[1] Iniciando análise léxica...\n";
        
        std::string code = readFile(filename);
        Lexer lexer(code);
        std::vector<Token> tokens = lexer.tokenize();

        // v2.00 #9: Erros léxicos param a compilação antes do Parser
        bool has_lex_errors = false;
        for (const auto& tok : tokens) {
            if (tok.type == TokenType::UNKNOWN) {
                std::cerr << "LexicalError [linha " << tok.line
                          << ", col " << tok.column << "]: "
                          << tok.lexeme << "\n";
                has_lex_errors = true;
            }
        }
        if (has_lex_errors) {
            std::cerr << "\n✗ Compilação interrompida: erros léxicos encontrados.\n";
            return 1;
        }

        printTokens(tokens, show_tokens);
        
        // ====================================================================
        // FASE 2: ANÁLISE SINTÁTICA
        // ====================================================================
        //std::cout << "\n[2] Iniciando análise sintática...\n";
        
        Parser parser(tokens);
        Program program = parser.parse();
        
        if (parser.hasErrors()) {
            std::cout << "✗ Erros encontrados durante o parsing:\n\n";
            parser.printErrors();
            return 1;
        }
        
        //std::cout << "✓ Análise sintática concluída com sucesso!\n";
        
        // ====================================================================
        // FASE 3: ANÁLISE SEMÂNTICA
        // ====================================================================
       // std::cout << "\n[3] Iniciando análise semântica...\n";
        
        SemanticAnalyzer analyzer;
        try {
            analyzer.analyze(program);
            //std::cout << "✓ Análise semântica concluída com sucesso! (AST anotada)\n";
        } catch (const SemanticError& e) {
            std::cerr << "\n✗ " << e.what() << "\n";
            return 1;
        }
        
        // ====================================================================
        // EXIBIR AST
        // ====================================================================
        if (show_ast) {
            std::cout << "\n" << std::string(80, '=') << "\n";
            std::cout << "ÁRVORE SINTÁTICA ABSTRATA (AST)\n";
            std::cout << std::string(80, '=') << "\n\n";
            std::cout << program.toString() << "\n";
            std::cout << std::string(80, '=') << "\n";
        }
        
        //std::cout << "\n✓ Compilação concluída com sucesso!\n\n";

        // ====================================================================
        // FASE 4: EXECUÇÃO
        // ====================================================================
        //std::cout << "[4] Executando...\n";
        //std::cout << std::string(80, '-') << "\n";

        Executor executor;
        try {
            executor.execute(program);
        } catch (const RuntimeError& e) {
            std::cerr << "\n✗ " << e.what() << "\n";
            return 1;
        }

        //std::cout << std::string(80, '-') << "\n";
        //std::cout << "✓ Execução concluída.\n\n";
        
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Erro fatal: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
