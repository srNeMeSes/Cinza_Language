#include "module_loader.h"
#include "parser.h"
#include "source_files.h"
#include "plataforma.h"   // caminhos em UTF-8
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace cinza {

namespace {

bool lerArquivo(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream content;
    content << file.rdbuf();
    out = content.str();
    return true;
}

std::string canonico(const fs::path& path) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(path, ec);
    return textoCaminho(ec ? fs::absolute(path) : c);
}

// Pastas de CINZA_PATH, na ordem
std::vector<fs::path> pastasCinzaPath() {
    std::vector<fs::path> pastas;
    std::string env;
    if (!ambienteUtf8("CINZA_PATH", env)) return pastas;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string item;
    std::istringstream in(env);
    while (std::getline(in, item, sep))
        if (!item.empty()) pastas.push_back(caminhoUtf8(item));
    return pastas;
}

} // namespace

void ModuleLoader::importError(const std::string& msg, const Token& tok) {
    diagnostics().report("ImportError", msg, tok.loc());   // B6
}

size_t ModuleLoader::nativeIndex(const NativeModule& nm) {
    const std::string chave = "<nativo>:" + nm.name;
    if (auto it = indices.find(chave); it != indices.end()) return it->second;
    Module mod;
    mod.canonical = chave;
    mod.display   = nm.name;
    mod.prefix    = nm.name;
    mod.program   = std::make_unique<Program>();
    mod.native    = &nm;
    indices[chave] = modules.size();
    modules.push_back(std::move(mod));
    return modules.size() - 1;
}

bool ModuleLoader::load(const std::string& main_path, const std::string& main_code) {
    raiz = projectRoot(main_path);
    return visit(main_path, main_path, &main_code, "", nullptr) >= 0;
}

// Sobe a partir da pasta do arquivo principal até achar um czm.mod (o marcador
// do projeto, por enquanto vazio). Sem marcador, a raiz é a pasta do principal.
// Caminho principal relativo dá raiz relativa à pasta atual, para as mensagens
// mostrarem "tests/modulos/geom.cinza" como antes.
std::string ModuleLoader::projectRoot(const std::string& main_path) {
    std::error_code ec;
    const fs::path principal = caminhoUtf8(main_path);
    fs::path pasta = fs::absolute(principal, ec).parent_path();
    fs::path achada;
    for (fs::path p = pasta; !p.empty(); p = p.parent_path()) {
        if (fs::is_regular_file(p / "czm.mod", ec)) { achada = p; break; }
        if (p == p.parent_path()) break;   // chegou à raiz do disco
    }
    if (achada.empty()) achada = pasta;
    if (principal.is_relative()) {
        fs::path rel = achada.lexically_relative(fs::current_path(ec));
        if (!rel.empty()) return textoCaminho(rel);
    }
    return textoCaminho(achada);
}

std::vector<std::string> ModuleLoader::resolve(const ImportDecl& imp,
                                               std::vector<std::string>& tentados) const {
    fs::path rel;
    for (const auto& parte : imp.path) rel /= parte;
    rel += ".cinza";

    std::vector<fs::path> bases{caminhoUtf8(raiz.empty() ? "." : raiz)};
    for (auto& p : pastasCinzaPath()) bases.push_back(std::move(p));

    std::vector<std::string> achados, canonicos;
    for (const auto& base : bases) {
        fs::path cand = (base / rel).lexically_normal();
        const std::string texto = textoCaminho(cand);
        if (std::find(tentados.begin(), tentados.end(), texto) != tentados.end())
            continue;   // a raiz repetida no CINZA_PATH
        tentados.push_back(texto);
        std::error_code ec;
        if (!fs::is_regular_file(cand, ec)) continue;
        const std::string c = canonico(cand);   // a raiz também pode estar no CINZA_PATH
        if (std::find(canonicos.begin(), canonicos.end(), c) != canonicos.end()) continue;
        canonicos.push_back(c);
        achados.push_back(texto);
    }
    return achados;
}

long ModuleLoader::visit(const std::string& path, const std::string& display,
                         const std::string* code, const std::string& prefix,
                         const Token* import_tok) {
    const std::string canon = canonico(caminhoUtf8(path));

    auto est = estados.find(canon);
    if (est != estados.end()) {
        if (est->second == Estado::Pronto) return static_cast<long>(indices[canon]);
        // Visitando: import circular. Cadeia desde o módulo repetido.
        std::string cadeia;
        bool dentro = false;
        for (const auto& [c, d] : pilha) {
            if (c == canon) dentro = true;
            if (dentro) cadeia += d + " → ";
        }
        cadeia += display;
        importError("import circular: " + cadeia, *import_tok);
        return -1;
    }

    // Dois arquivos diferentes com o mesmo caminho de import (em pastas
    // distintas) não podem dividir o prefixo dos nomes
    std::string pref = prefix;
    if (!pref.empty()) {
        for (int n = 2; prefixos_usados.count(pref) && prefixos_usados[pref] != canon; ++n)
            pref = prefix + "#" + std::to_string(n);
        prefixos_usados[pref] = canon;
    }

    // Fase 7: arquivo do usuário com o nome de um módulo nativo é erro
    const std::string stem = textoCaminho(caminhoUtf8(path).stem());
    if (findNativeModule(stem)) {
        const std::string msg = "o arquivo '" + display + "' tem o nome do módulo nativo '" +
                                stem + "'; renomeie o arquivo";
        if (import_tok) importError(msg, *import_tok);
        else diagnostics().report("ImportError", msg, SourceLocation{0, 0, 0});
        return -1;
    }

    std::string lido;
    if (!code) {
        if (!lerArquivo(caminhoUtf8(path), lido)) {
            importError("não foi possível ler o módulo '" + display + "'", *import_tok);
            return -1;
        }
        code = &lido;
    }

    Module mod;
    mod.canonical = canon;
    mod.display   = display;
    mod.prefix    = pref;
    mod.file_id   = import_tok ? registerSourceFile(display) : 0;

    // Léxico
    Lexer lexer(*code, mod.file_id);
    std::vector<Token> tokens = lexer.tokenize();
    bool has_lex_errors = false;
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::UNKNOWN) {
            diagnostics().report("LexicalError", tok.lexeme, tok.loc());   // B6
            has_lex_errors = true;
        }
    }
    if (has_lex_errors) return -1;
    if (!import_tok && on_main_tokens) on_main_tokens(tokens);

    // Sintaxe
    Parser parser(std::move(tokens));
    mod.program = std::make_unique<Program>(parser.parse());
    if (parser.hasErrors()) return -1;   // B6: já estão em diagnostics()

    // Imports, em profundidade
    estados[canon] = Estado::Visitando;
    pilha.emplace_back(canon, display);
    for (const auto& imp : mod.program->imports) {
        std::string nome;
        for (const auto& parte : imp.path) nome += (nome.empty() ? "" : ".") + parte;

        std::vector<std::string> tentados;
        const std::vector<std::string> achados = resolve(imp, tentados);
        const std::string achado = achados.empty() ? "" : achados.front();

        // Fase 7: módulo nativo (import Math;). Um arquivo do usuário com o
        // mesmo nome no caminho de busca é erro, sem ambiguidade.
        if (imp.path.size() == 1)
            if (const NativeModule* nm = findNativeModule(imp.path[0])) {
                if (!achado.empty()) {
                    importError("o arquivo '" + achado + "' tem o nome do módulo nativo '" +
                                nm->name + "'; renomeie o arquivo", imp.token);
                    return -1;
                }
                mod.imports.emplace_back(imp.alias, nativeIndex(*nm));
                continue;
            }

        if (achado.empty()) {
            std::string lista;
            for (const auto& t : tentados) lista += (lista.empty() ? "" : ", ") + t;
            importError("módulo '" + nome + "' não encontrado (procurado em: " + lista + "). "
                        "O caminho do import parte da raiz do projeto (a pasta do czm.mod, ou "
                        "a do arquivo principal), não da pasta de quem importa", imp.token);
            return -1;
        }
        // O mesmo caminho na raiz e numa pasta de CINZA_PATH (ou em duas delas):
        // nenhum encobre o outro em silêncio
        if (achados.size() > 1) {
            std::string lista;
            for (const auto& a : achados) lista += (lista.empty() ? "" : " e ") + a;
            importError("o módulo '" + nome + "' existe em mais de um lugar: " + lista +
                        "; renomeie ou remova um deles", imp.token);
            return -1;
        }
        const long idx = visit(achado, achado, nullptr, nome, &imp.token);
        if (idx < 0) return -1;
        mod.imports.emplace_back(imp.alias, static_cast<size_t>(idx));
    }
    pilha.pop_back();

    estados[canon] = Estado::Pronto;
    indices[canon] = modules.size();
    modules.push_back(std::move(mod));
    return static_cast<long>(modules.size() - 1);
}

} // namespace cinza
