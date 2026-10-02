#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "semantic.h"
#include "executor.h"
#include "module_loader.h"
#include "cvm/compiler.h"
#include "cvm/vm.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
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

    // A12: lê o arquivo inteiro de uma vez (antes: linha a linha, com uma
    // concatenação por linha e um '\n' a mais no fim)
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
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
        case TokenType::KW_CLASS: return "KW_CLASS";
        case TokenType::KW_STRUCT: return "KW_STRUCT";
        case TokenType::KW_ENUM:   return "KW_ENUM";
        case TokenType::KW_INTERFACE: return "KW_INTERFACE";
        case TokenType::KW_PUB:   return "KW_PUB";
        case TokenType::KW_NEW:   return "KW_NEW";
        case TokenType::KW_CONST: return "KW_CONST";
        case TokenType::KW_SELF:  return "KW_SELF";
        
        case TokenType::TYPE_INT: return "TYPE_INT";
        case TokenType::TYPE_DECIMAL: return "TYPE_DECIMAL";
        case TokenType::TYPE_STRING: return "TYPE_STRING";
        case TokenType::TYPE_BOOL: return "TYPE_BOOL";
        case TokenType::TYPE_VOID: return "TYPE_VOID";
        case TokenType::TYPE_DICT: return "TYPE_DICT";
        case TokenType::TYPE_LIST: return "TYPE_LIST";
        case TokenType::TYPE_VAR: return "TYPE_VAR";
        case TokenType::TYPE_PAIR: return "TYPE_PAIR";
        case TokenType::TYPE_OP:   return "TYPE_OP";
        
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
        case TokenType::OP_PLUS_ASSIGN:     return "OP_PLUS_ASSIGN";
        case TokenType::OP_MINUS_ASSIGN:    return "OP_MINUS_ASSIGN";
        case TokenType::OP_MULTIPLY_ASSIGN: return "OP_MULTIPLY_ASSIGN";
        case TokenType::OP_DIVIDE_ASSIGN:   return "OP_DIVIDE_ASSIGN";
        case TokenType::OP_MODULO_ASSIGN:   return "OP_MODULO_ASSIGN";
        case TokenType::KW_BREAK:           return "KW_BREAK";
        case TokenType::KW_CONTINUE:        return "KW_CONTINUE";
        case TokenType::KW_TRY:             return "KW_TRY";
        case TokenType::KW_EXCEPT:          return "KW_EXCEPT";
        case TokenType::KW_FINALLY:         return "KW_FINALLY";
        case TokenType::KW_THROW:           return "KW_THROW";
        case TokenType::KW_ERROR:           return "KW_ERROR";
        case TokenType::KW_IMPORT:          return "KW_IMPORT";
        case TokenType::KW_AS:              return "KW_AS";
        
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

// A10: exceção C++ inesperada nunca chega crua ao usuário
void reportInternalError(const std::string& detalhe) {
    std::cerr << "\n✗ Erro interno do compilador: " << detalhe << "\n"
              << "  Isso é um bug do Cinza, não do seu programa. Por favor, reporte-o\n"
              << "  junto com o arquivo .cinza que causou o erro.\n";
}

void printHelp() {
    std::cout << "Compilador Cinza - Linguagem de Programação Interpretada\n\n";
    std::cout << "Uso:\n";
    std::cout << "  cinza [opções] <arquivo.cinza> [argumentos para main...]\n\n";
    std::cout << "Opções:\n";
    std::cout << "  --tokens, -t     Exibe tokens detalhados\n";
    std::cout << "  --ast, -a        Exibe a árvore sintática (AST)\n";
    std::cout << "  --cvm            Executa na máquina virtual (CVM, em construção)\n";
    std::cout << "  --bytecode       Mostra o bytecode da CVM e executa nela\n";
    std::cout << "  --help, -h       Exibe esta ajuda\n\n";
    std::cout << "Exemplo:\n";
    std::cout << "  cinza --ast exemplo.cinza\n";
    std::cout << "  cinza prog.cinza a b     (main(list<string> args) recebe [\"a\", \"b\"])\n\n";
}

int main(int argc, char* argv[]) {
    // A12: iostreams sem sincronizar com stdio (o programa não usa printf).
    // cerr continua "tied" a cout, então a saída do programa sai antes de
    // qualquer mensagem de erro.
    std::ios::sync_with_stdio(false);

    #ifdef _WIN32
    SetConsoleOutputCP(65001);  // força UTF-8 no terminal Windows
    SetConsoleCP(65001);
    #endif

    if (argc < 2) {
        printHelp();
        return 1;
    }
    
    // Parse argumentos
    // Fase 2.5: opções do interpretador vêm ANTES do arquivo; tudo o que vem
    // depois dele vai para main(list<string> args)
    std::string filename;
    std::vector<std::string> program_args;
    bool show_tokens = false;
    bool show_ast = false; // por padrão NÃO mostra AST
    // CVM (em construção): --cvm executa na máquina virtual; --bytecode mostra o
    // bytecode gerado e executa na CVM. Enquanto a CVM não estiver completa, o
    // padrão continua sendo o interpretador.
    bool use_cvm = false;
    bool show_bytecode = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (!filename.empty()) {
            program_args.push_back(arg);
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        } else if (arg == "--tokens" || arg == "-t") {
            show_tokens = true;
        } else if (arg == "--ast" || arg == "-a") {
            show_ast = true;
        } else if (arg == "--cvm") {
            use_cvm = true;
        } else if (arg == "--bytecode") {
            use_cvm = show_bytecode = true;
        } else if (!arg.empty() && arg[0] != '-') {
            filename = arg;
        }
    }
    
    if (filename.empty()) {
        std::cerr << "Erro: Nenhum arquivo fornecido.\n";
        printHelp();
        return 1;
    }
    
    // B6: o arquivo principal aparece nos diagnósticos como foi informado,
    // com '/' como separador (igual aos módulos)
    filename = std::filesystem::path(filename).generic_string();
    setMainFile(filename);

    // Arquivo inexistente é erro do usuário, não erro interno (A10)
    std::string code;
    try {
        code = readFile(filename);
    } catch (const std::exception&) {
        diagnostics().report("IOError", "não foi possível abrir o arquivo", SourceLocation{0, 0, 0});
        diagnostics().flush(std::cerr);
        return 1;
    }

    try {
        // ====================================================================
        // FASES 1 E 2: ANÁLISE LÉXICA E SINTÁTICA (principal e módulos, C5)
        // ====================================================================
        ModuleLoader loader;
        loader.on_main_tokens = [&](const std::vector<Token>& tokens) {
            printTokens(tokens, show_tokens);
        };
        if (!loader.load(filename, code)) {
            diagnostics().flush(std::cerr);   // B6: léxico, sintaxe e import
            return 1;
        }

        std::vector<Module>& modules = loader.ordered();
        Program& program = *modules.back().program;   // o principal vem por último

        // ====================================================================
        // FASE 3: ANÁLISE SEMÂNTICA
        // ====================================================================
        std::vector<ModuleUnit> units;
        std::vector<const Program*> programs;
        for (auto& m : modules) {
            units.push_back(ModuleUnit{m.program.get(), m.prefix, m.imports, m.native});
            programs.push_back(m.program.get());
        }

        SemanticAnalyzer analyzer;
        try {
            analyzer.analyze(units);
        } catch (const SemanticError& e) {
            diagnostics().report("SemanticError", e.message, e.loc);   // B6
            diagnostics().flush(std::cerr);
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

        if (use_cvm) {
            cvm::Image img;
            try {
                img = cvm::compile(programs, analyzer.globalLayout());
            } catch (const cvm::Unsupported& e) {
                diagnostics().report("CVMError", e.what(), e.loc);
                diagnostics().flush(std::cerr);
                return 1;
            }
            if (show_bytecode) std::cout << cvm::disassemble(img);
            try {
                cvm::VM vm(img);
                vm.run(program_args);
            } catch (const RuntimeError& e) {
                diagnostics().report(e.kind, e.message, e.location(), e.trace);
                diagnostics().flush(std::cerr);
                return 1;
            }
            return 0;
        }

        Executor executor;
        try {
            executor.execute(programs, analyzer.globalLayout(), program_args);
        } catch (const RuntimeError& e) {
            // C4/B6: erro não tratado — tipo, mensagem e stack trace
            diagnostics().report(e.kind, e.message, e.location(), e.trace);
            diagnostics().flush(std::cerr);
            return 1;
        }

        //std::cout << std::string(80, '-') << "\n";
        //std::cout << "✓ Execução concluída.\n\n";
        
    } catch (const std::exception& e) {
        // A10: erros da linguagem (léxico, sintaxe, semântica, runtime) já foram
        // tratados acima; qualquer outra exceção C++ (bad_variant_access,
        // out_of_range, ...) é bug do próprio compilador
        reportInternalError(e.what());
        return 1;
    } catch (...) {
        reportInternalError("exceção desconhecida");
        return 1;
    }
    
    return 0;
}
