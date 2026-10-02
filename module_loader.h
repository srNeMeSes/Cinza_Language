#ifndef CINZA_MODULE_LOADER_H
#define CINZA_MODULE_LOADER_H

#include "ast.h"
#include "lexer.h"
#include "natives.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cinza {

// ============================================================================
// CARREGADOR DE MÓDULOS (C5)
//
// `import util.texto;` procura "util/texto.cinza" primeiro na pasta do
// arquivo que importa e depois em cada pasta de CINZA_PATH (separadas por
// ';' no Windows e ':' nos demais). Cada arquivo é lido e analisado uma só
// vez (cache pelo caminho canônico), mesmo que vários módulos o importem.
// Import circular é erro, com a cadeia completa ("a → b → a").
//
// A ordem final põe as dependências antes de quem as importa e o arquivo
// principal por último: é a ordem da análise semântica e da avaliação dos
// const de cada módulo.
// ============================================================================

struct Module {
    std::string              canonical;   // caminho canônico (chave do cache)
    std::string              display;     // caminho mostrado nas mensagens
    std::string              prefix;      // "" no principal; "util.texto" nos demais
    int                      file_id = 0;
    std::unique_ptr<Program> program;             // vazio num módulo nativo
    const NativeModule*      native = nullptr;    // Fase 7: Strings, Math, ...
    // apelido → índice (em ordered()) do módulo importado
    std::vector<std::pair<std::string, size_t>> imports;
};

class ModuleLoader {
public:
    // Carrega o principal e tudo o que ele importa. Devolve false se houve
    // erro léxico, de sintaxe ou de import (já impresso).
    bool load(const std::string& main_path, const std::string& main_code);

    // Chamado com os tokens do principal logo após a análise léxica (--tokens)
    std::function<void(const std::vector<Token>&)> on_main_tokens;

    // Módulos em ordem topológica, principal por último
    std::vector<Module>& ordered() { return modules; }

private:
    enum class Estado { Visitando, Pronto };

    std::vector<Module>                       modules;    // em ordem topológica
    std::unordered_map<std::string, Estado>   estados;    // canônico → estado
    std::unordered_map<std::string, size_t>   indices;    // canônico → índice em modules
    std::vector<std::pair<std::string, std::string>> pilha;   // (canônico, display) em visita
    std::unordered_map<std::string, std::string> prefixos_usados;   // prefixo → canônico

    // Carrega um arquivo e (recursivamente) seus imports; devolve o índice
    // em modules, ou -1 em caso de erro. `code` só vem pronto no principal;
    // `import_tok` é nulo no principal.
    long visit(const std::string& path, const std::string& display,
               const std::string* code, const std::string& prefix,
               const Token* import_tok);

    // Caminho do arquivo do import, ou "" se não existir; `tentados` recebe
    // os locais procurados
    std::string resolve(const ImportDecl& imp, const std::string& importer_dir,
                        std::vector<std::string>& tentados) const;

    static void importError(const std::string& msg, const Token& tok);
    // Índice do módulo nativo em modules (criado na primeira vez)
    size_t nativeIndex(const NativeModule& nm);
};

} // namespace cinza

#endif // CINZA_MODULE_LOADER_H
