#include "module_loader.h"
#include "biblioteca.h"
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

// Q10: o caminho para as mensagens: relativo à pasta atual quando der
std::string paraMostrar(const fs::path& p) {
    std::error_code ec;
    if (p.is_relative()) return textoCaminho(p.lexically_normal());
    fs::path rel = p.lexically_relative(fs::current_path(ec));
    if (!rel.empty() && rel.native().find(fs::path("..").native()) != 0) return textoCaminho(rel);
    return textoCaminho(p.lexically_normal());
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
    size_t projeto = SEM_PROJETO;
    if (!raiz.empty()) {
        const long p = carregaProjeto(raiz);
        if (p < 0) return false;
        projeto = static_cast<size_t>(p);
    }
    return visit(main_path, main_path, &main_code, "", nullptr, projeto, "") >= 0;
}

// Q10: o czm.mod — uma declaração por linha, com comentários //: projeto nome, cinza 0.6,
// requer ../caminho [as nome], requer url v1.2.0 [as nome] (o leitor é o de pacotes.cpp)
long ModuleLoader::carregaProjeto(const std::string& raiz_projeto, bool do_cache) {
    const fs::path pasta = caminhoUtf8(raiz_projeto.empty() ? "." : raiz_projeto);
    const std::string canon = canonico(pasta);
    if (auto it = projeto_de.find(canon); it != projeto_de.end()) return static_cast<long>(it->second);

    Projeto proj;
    proj.raiz     = raiz_projeto;
    proj.do_cache = do_cache;
    const fs::path arquivo = pasta / "czm.mod";
    const std::string mostrado = paraMostrar(arquivo);
    proj.file_id = registerSourceFile(mostrado);
    auto erro = [&](const std::string& msg, int linha) {
        diagnostics().report("ImportError", msg, {proj.file_id, linha, 1});
        return -1L;
    };
    std::string texto;
    if (!lerArquivo(arquivo, texto)) return erro("não foi possível ler '" + mostrado + "'", 1);
    pacotes::Manifesto m;
    std::string msg;
    int linha = 0;
    if (!pacotes::leManifesto(texto, m, msg, linha)) return erro(msg, linha);
    proj.nome = m.nome;
    for (const auto& r : m.requer) {
        Dependencia d;
        d.origem = r.origem;
        d.versao = r.versao;
        d.nome   = r.nome;
        d.local  = r.local;
        d.linha  = r.linha;
        if (do_cache && d.local)
            return erro("um pacote git não pode ter dependências locais ('" + d.origem +
                        "'): fora da máquina do autor, o caminho não existe", d.linha);
        proj.deps.push_back(std::move(d));
    }

    const size_t idx = projetos.size();
    projetos.push_back(std::move(proj));
    projeto_de[canon] = idx;   // antes das dependências: um pacote que volta a este não lê de novo
    const int file_id = projetos[idx].file_id;
    auto erroDep = [&](const std::string& msg, int linha) {
        diagnostics().report("ImportError", msg, {file_id, linha, 1});
        return -1L;
    };

    for (size_t k = 0; k < projetos[idx].deps.size(); ++k) {
        const Dependencia d = projetos[idx].deps[k];
        fs::path raiz_dep;
        std::error_code ec;
        if (d.local) {
            raiz_dep = (pasta / caminhoUtf8(d.origem)).lexically_normal();
            if (!fs::is_regular_file(raiz_dep / "czm.mod", ec))
                return erroDep("a dependência '" + d.origem + "' não é um projeto: não há '" +
                               paraMostrar(raiz_dep / "czm.mod") + "'", d.linha);
        } else {
            const std::string p = pastaGit(d, file_id);
            if (p.empty()) return -1;
            raiz_dep = caminhoUtf8(p);
        }
        const long sub = carregaProjeto(textoCaminho(raiz_dep), !d.local);
        if (sub < 0) return -1;
        std::string nome = d.nome.empty() ? projetos[static_cast<size_t>(sub)].nome : d.nome;
        if (nome.empty())
            return erroDep("a dependência '" + d.origem + "' não declara o nome no czm.mod dela ('projeto nome'): "
                           "declare-o lá, ou dê um aqui com 'as nome'", d.linha);
        if (findNativeModule(nome) || fonteDaBiblioteca(nome))
            return erroDep("o pacote '" + nome + "' tem o nome de um módulo da biblioteca padrão: dê outro com 'as'",
                           d.linha);
        for (size_t j = 0; j < k; ++j)
            if (projetos[idx].deps[j].nome == nome)
                return erroDep("dois pacotes com o nome '" + nome + "' (linhas " +
                               std::to_string(projetos[idx].deps[j].linha) + " e " + std::to_string(d.linha) +
                               "): dê outro nome a um deles com 'as'", d.linha);
        if (fs::exists(pasta / caminhoUtf8(nome), ec) || fs::exists(pasta / caminhoUtf8(nome + ".cinza"), ec))
            return erroDep("o nome do pacote '" + nome + "' é também uma pasta ou um arquivo do projeto ('" +
                           paraMostrar(pasta / caminhoUtf8(nome)) + "'): um import não saberia qual dos dois; dê "
                           "outro nome ao pacote com 'as'", d.linha);
        projetos[idx].deps[k].nome    = nome;
        projetos[idx].deps[k].projeto = static_cast<size_t>(sub);
    }
    return static_cast<long>(idx);
}

// Q10: uma dependência git na compilação: a versão vem do czm.lock do projeto principal (escolhida
// pelo cinza get entre todos os pedidos), a pasta do cache do usuário, e o conteúdo precisa ter o
// hash gravado no lock. Nada é baixado aqui: o que faltar, o cinza get resolve.
std::string ModuleLoader::pastaGit(const Dependencia& d, int file_id) {
    auto erro = [&](const std::string& msg) {
        diagnostics().report("ImportError", msg, {file_id, d.linha, 1});
        return std::string();
    };
    if (!lock_lido) {
        lock_lido = true;
        std::string msg;
        int linha = 0;
        if (!pacotes::leLock(projetos.front().raiz, lock, msg, linha)) {
            const fs::path arq =
                caminhoUtf8(projetos.front().raiz.empty() ? "." : projetos.front().raiz) / "czm.lock";
            diagnostics().report("ImportError", msg, {registerSourceFile(paraMostrar(arq)), linha, 1});
            lock.clear();
            return "";
        }
    }
    const pacotes::Travado* t = nullptr;
    for (const auto& x : lock)
        if (x.origem == d.origem) t = &x;
    if (!t) return erro("a dependência '" + d.origem + "' " + d.versao + " não está no czm.lock: rode 'cinza get'");
    int pedida[3], travada[3];
    pacotes::versao(d.versao, pedida);
    if (!pacotes::versao(t->versao, travada) || travada[0] != pedida[0] ||
        std::lexicographical_compare(travada, travada + 3, pedida, pedida + 3))
        return erro("o czm.lock tem '" + d.origem + "' " + t->versao + ", e aqui se pede " + d.versao +
                    ": o czm.lock está desatualizado; rode 'cinza get'");
    const std::string pasta = pacotes::pastaNoCache(t->origem, t->versao);
    std::error_code ec;
    if (!fs::is_regular_file(caminhoUtf8(pasta) / "czm.mod", ec))
        return erro("o pacote '" + d.origem + "' " + t->versao + " não está no cache (" + pasta +
                    "): rode 'cinza get'");
    if (pacotes::hashDaPasta(pasta) != t->hash)
        return erro("o conteúdo de '" + d.origem + "' " + t->versao + " no cache (" + pasta +
                    ") não confere com o hash do czm.lock: o cache foi alterado, ou a tag foi movida no "
                    "repositório; apague a pasta e rode 'cinza get'");
    return pasta;
}

// Sobe a partir da pasta do arquivo principal até achar um czm.mod (o marcador
// do projeto, por enquanto vazio). Q1f: sem marcador, não há projeto (vazio): o
// programa é de um arquivo só e importa só os módulos nativos.
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
    if (achada.empty()) return "";   // Q1f: sem projeto
    if (principal.is_relative()) {
        fs::path rel = achada.lexically_relative(fs::current_path(ec));
        if (!rel.empty()) return textoCaminho(rel);
    }
    return textoCaminho(achada);
}

std::string ModuleLoader::resolve(const std::vector<std::string>& partes, const std::string& raiz_projeto,
                                  std::string& tentado) const {
    fs::path rel;
    for (const auto& parte : partes) rel /= caminhoUtf8(parte);
    rel += ".cinza";
    const fs::path cand = (caminhoUtf8(raiz_projeto.empty() ? "." : raiz_projeto) / rel).lexically_normal();
    tentado = paraMostrar(cand);
    std::error_code ec;
    return fs::is_regular_file(cand, ec) ? tentado : std::string();
}

long ModuleLoader::visit(const std::string& path, const std::string& display,
                         const std::string* code, const std::string& prefix,
                         const Token* import_tok, size_t projeto, const std::string& nome_esperado) {
    // Q3: um módulo da biblioteca em Cinza tem o caminho "<biblioteca>/Nome.cinza", fora do disco
    const bool da_biblioteca = path.rfind("<biblioteca>/", 0) == 0;
    const std::string canon = da_biblioteca ? path : canonico(caminhoUtf8(path));

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

    // Fase 7: arquivo do usuário com o nome de um módulo nativo (ou da biblioteca, Q3) é erro
    const std::string stem = textoCaminho(caminhoUtf8(path).stem());
    if (!da_biblioteca && (findNativeModule(stem) || fonteDaBiblioteca(stem))) {
        const std::string msg = "o arquivo '" + display + "' tem o nome do módulo " +
                                (findNativeModule(stem) ? "nativo" : "da biblioteca") + " '" + stem +
                                "'; renomeie o arquivo";
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

    // Q1f: o nome declarado (module a.b;) é o caminho pelo qual o arquivo é importado; o
    // principal não precisa declarar, mas se declarar, vale o caminho dele a partir da raiz
    {
        std::string declarado;
        for (const auto& parte : mod.program->module_path) declarado += (declarado.empty() ? "" : ".") + parte;
        std::string esperado = nome_esperado;
        if (!import_tok) {
            esperado.clear();
            if (!declarado.empty() && projeto != SEM_PROJETO) {
                std::error_code ec;
                fs::path rel = fs::absolute(caminhoUtf8(path), ec)
                                   .lexically_relative(fs::absolute(caminhoUtf8(projetos[projeto].raiz), ec));
                rel.replace_extension();
                for (const auto& parte : rel) esperado += (esperado.empty() ? "" : ".") + textoCaminho(parte);
            }
        }
        if (import_tok && declarado.empty()) {
            importError("o arquivo '" + display + "' é importado como '" + prefix + "', mas não declara o nome: "
                        "comece-o com 'module " + esperado + ";'", *import_tok);
            return -1;
        }
        if (!declarado.empty() && !esperado.empty() && declarado != esperado) {
            importError("o arquivo '" + display + "' declara 'module " + declarado + ";', mas o nome dele é '" +
                        esperado + "' (o caminho a partir da raiz do projeto dele): troque para 'module " + esperado +
                        ";'", mod.program->module_token);
            return -1;
        }
    }

    // Imports, em profundidade
    estados[canon] = Estado::Visitando;
    pilha.emplace_back(canon, display);
    for (const auto& imp : mod.program->imports) {
        std::string nome;
        for (const auto& parte : imp.path) nome += (nome.empty() ? "" : ".") + parte;

        // Q10: o primeiro trecho é uma dependência do projeto deste arquivo?
        const Dependencia* dep = nullptr;
        if (projeto != SEM_PROJETO)
            for (const auto& d : projetos[projeto].deps)
                if (d.nome == imp.path[0]) dep = &d;
        if (dep) {
            // import json; é o módulo json.cinza na raiz do pacote; import json.leitor;, leitor.cinza
            std::vector<std::string> dentro(imp.path.begin() + 1, imp.path.end());
            if (dentro.empty()) dentro.push_back(imp.path[0]);
            std::string dentro_nome;
            for (const auto& parte : dentro) dentro_nome += (dentro_nome.empty() ? "" : ".") + parte;
            std::string tentado;
            const std::string arq = resolve(dentro, projetos[dep->projeto].raiz, tentado);
            if (arq.empty()) {
                importError("o pacote '" + dep->nome + "' não tem o módulo '" + dentro_nome + "' (procurado em: " +
                            tentado + ")", imp.token);
                return -1;
            }
            const long idx = visit(arq, arq, nullptr, nome, &imp.token, dep->projeto, dentro_nome);
            if (idx < 0) return -1;
            mod.imports.emplace_back(imp.alias, static_cast<size_t>(idx));
            continue;
        }

        std::string tentado;
        const std::string achado =
            projeto == SEM_PROJETO ? std::string() : resolve(imp.path, projetos[projeto].raiz, tentado);

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

        // Q3: módulo da biblioteca escrito em Cinza (import Json;), embutido no executável
        if (imp.path.size() == 1)
            if (const std::string* fonte = fonteDaBiblioteca(imp.path[0])) {
                if (!achado.empty()) {
                    importError("o arquivo '" + achado + "' tem o nome do módulo da biblioteca '" +
                                imp.path[0] + "'; renomeie o arquivo", imp.token);
                    return -1;
                }
                const std::string caminho = caminhoDaBiblioteca(imp.path[0]);
                const long idx = visit(caminho, caminho, fonte, imp.path[0], &imp.token, SEM_PROJETO, imp.path[0]);
                if (idx < 0) return -1;
                mod.imports.emplace_back(imp.alias, static_cast<size_t>(idx));
                continue;
            }

        // Q1f: um módulo de arquivo só existe num projeto (a pasta do czm.mod)
        if (projeto == SEM_PROJETO) {
            importError("'import " + nome + "': este programa não está num projeto (não há um czm.mod na "
                        "pasta dele nem acima). Sem projeto, só se importam os módulos nativos; crie o "
                        "projeto com 'cinza init' na pasta dele", imp.token);
            return -1;
        }
        if (achado.empty()) {
            importError("módulo '" + nome + "' não encontrado (procurado em: " + tentado + "). "
                        "O caminho do import parte da raiz do projeto (a pasta do czm.mod), não "
                        "da pasta de quem importa, e o primeiro trecho não é um pacote do czm.mod", imp.token);
            return -1;
        }
        const long idx = visit(achado, achado, nullptr, nome, &imp.token, projeto, nome);
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
