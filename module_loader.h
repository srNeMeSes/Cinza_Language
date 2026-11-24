#ifndef CINZA_MODULE_LOADER_H
#define CINZA_MODULE_LOADER_H

#include "ast.h"
#include "lexer.h"
#include "natives.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "pacotes.h"
#include <utility>
#include <vector>

namespace cinza {

// ============================================================================
// CARREGADOR DE MÓDULOS (C5)
//
// Raiz do projeto: a pasta do czm.mod mais próximo (na pasta do arquivo
// principal ou acima dela); sem czm.mod não há projeto, e só os módulos nativos
// valem (Q1f). `import util.texto;` procura "util/texto.cinza" a partir da raiz
// do projeto de quem importa (não da pasta dele). Q10: o czm.mod declara o nome
// do projeto (projeto nome) e as dependências (requer ...); `import json.leitor;`
// com json uma dependência procura "leitor.cinza" a partir da raiz do pacote, e
// dentro do pacote os caminhos partem da raiz dele. Cada arquivo é lido e
// analisado uma só vez (cache pelo caminho canônico), mesmo que vários módulos o
// importem.
// Import circular é erro, com a cadeia completa ("a → b → a").
//
// A ordem final põe as dependências antes de quem as importa e o arquivo
// principal por último: é a ordem da análise semântica e da avaliação dos
// const de cada módulo.
// ============================================================================

// Q10: uma dependência declarada no czm.mod (requer ...)
struct Dependencia {
    std::string nome;          // o nome pelo qual os imports a chamam (o do pacote, ou o 'as')
    std::string origem;        // o caminho (local) ou a URL do repositório (git)
    std::string versao;        // git: a tag pedida (v1.2.0); local: vazio
    bool        local = true;
    size_t      projeto = 0;   // índice do projeto dela em projects()
    int         linha = 0;     // no czm.mod de quem a declara
};

// Q10: um projeto (o principal ou um pacote): a raiz, o nome e as dependências
struct Projeto {
    std::string              raiz;      // a pasta do czm.mod (como mostrada nas mensagens)
    std::string              nome;      // projeto nome (vazio: não declarado)
    std::vector<Dependencia> deps;
    int                      file_id = -1;   // o czm.mod, para as mensagens
    bool                     do_cache = false;   // um pacote git (no cache do usuário)
};

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

    // Raiz do projeto do arquivo principal (relativa, se o caminho dele for relativo); vazia
    // sem czm.mod (Q1f)
    static std::string projectRoot(const std::string& main_path);
    // Q10: os projetos lidos (o principal primeiro, depois os pacotes)
    const std::vector<Projeto>& projects() const { return projetos; }

private:
    enum class Estado { Visitando, Pronto };

    std::vector<Module>                       modules;    // em ordem topológica
    std::unordered_map<std::string, Estado>   estados;    // canônico → estado
    std::unordered_map<std::string, size_t>   indices;    // canônico → índice em modules
    std::vector<std::pair<std::string, std::string>> pilha;   // (canônico, display) em visita
    std::unordered_map<std::string, std::string> prefixos_usados;   // prefixo → canônico
    std::string raiz;   // raiz do projeto (ver projectRoot)
    // Q10: os projetos (o principal e os pacotes), pela raiz canônica
    std::vector<Projeto>                    projetos;
    std::unordered_map<std::string, size_t> projeto_de;
    static constexpr size_t SEM_PROJETO = static_cast<size_t>(-1);
    // Lê o czm.mod da raiz (e, recursivamente, os das dependências locais); devolve o índice
    // em projetos, ou -1 com o erro já relatado
    long carregaProjeto(const std::string& raiz, bool do_cache = false);
    // O czm.lock do projeto principal (lido na primeira dependência git)
    bool lock_lido = false;
    std::vector<pacotes::Travado> lock;
    // A pasta no cache da dependência git `d` (conferida pelo czm.lock e pelo hash); vazia com o
    // erro já relatado
    std::string pastaGit(const Dependencia& d, int file_id);

    // Carrega um arquivo e (recursivamente) seus imports; devolve o índice
    // em modules, ou -1 em caso de erro. `code` só vem pronto no principal;
    // `import_tok` é nulo no principal.
    // `projeto`: o projeto a que o arquivo pertence (SEM_PROJETO: nenhum); `nome_esperado`: o
    // nome que ele precisa declarar em module (o caminho dentro do projeto dele)
    long visit(const std::string& path, const std::string& display,
               const std::string* code, const std::string& prefix,
               const Token* import_tok, size_t projeto, const std::string& nome_esperado);

    // O arquivo de `partes` (a.b → a/b.cinza) a partir de `raiz_projeto`: vazio se não existir.
    // `tentado` recebe o caminho procurado.
    std::string resolve(const std::vector<std::string>& partes, const std::string& raiz_projeto,
                        std::string& tentado) const;

    static void importError(const std::string& msg, const Token& tok);
    // Índice do módulo nativo em modules (criado na primeira vez)
    size_t nativeIndex(const NativeModule& nm);
};

} // namespace cinza

#endif // CINZA_MODULE_LOADER_H
