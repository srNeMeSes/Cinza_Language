#include "module_loader.h"
#include "parser.h"
#include "source_files.h"
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
    return (ec ? fs::absolute(path) : c).generic_string();
}

// Pastas de CINZA_PATH, na ordem
std::vector<fs::path> pastasCinzaPath() {
    std::vector<fs::path> pastas;
    const char* env = std::getenv("CINZA_PATH");
    if (!env) return pastas;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string item;
    std::istringstream in(env);
    while (std::getline(in, item, sep))
        if (!item.empty()) pastas.emplace_back(item);
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
    return visit(main_path, main_path, &main_code, "", nullptr) >= 0;
}

std::string ModuleLoader::resolve(const ImportDecl& imp, const std::string& importer_dir,
                                  std::vector<std::string>& tentados) const {
    fs::path rel;
    for (const auto& parte : imp.path) rel /= parte;
    rel += ".cinza";

    std::vector<fs::path> bases{fs::path(importer_dir)};
    for (auto& p : pastasCinzaPath()) bases.push_back(std::move(p));

    for (const auto& base : bases) {
        fs::path cand = (base / rel).lexically_normal();
        tentados.push_back(cand.generic_string());
        std::error_code ec;
        if (fs::is_regular_file(cand, ec)) return cand.generic_string();
    }
    return "";
}

long ModuleLoader::visit(const std::string& path, const std::string& display,
                         const std::string* code, const std::string& prefix,
                         const Token* import_tok) {
    const std::string canon = canonico(path);

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
    const std::string stem = fs::path(path).stem().string();
    if (findNativeModule(stem)) {
        const std::string msg = "o arquivo '" + display + "' tem o nome do módulo nativo '" +
                                stem + "'; renomeie o arquivo";
        if (import_tok) importError(msg, *import_tok);
        else diagnostics().report("ImportError", msg, SourceLocation{0, 0, 0});
        return -1;
    }

    std::string lido;
    if (!code) {
        if (!lerArquivo(path, lido)) {
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
    const std::string dir = fs::path(path).parent_path().generic_string();
    for (const auto& imp : mod.program->imports) {
        std::string nome;
        for (const auto& parte : imp.path) nome += (nome.empty() ? "" : ".") + parte;

        std::vector<std::string> tentados;
        const std::string achado = resolve(imp, dir.empty() ? "." : dir, tentados);

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
            importError("módulo '" + nome + "' não encontrado (procurado em: " + lista + ")",
                        imp.token);
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
