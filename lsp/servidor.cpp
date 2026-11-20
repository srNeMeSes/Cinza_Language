#include "servidor.h"
#include "json.h"
#include "../diagnostics.h"
#include "../formatador.h"
#include "../module_loader.h"
#include "../plataforma.h"
#include "../semantic.h"
#include "../source_files.h"
#include "../versao.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace cinza::lsp {

namespace {

using json::Valor;

// ── o transporte ────────────────────────────────────────────────────────────

// Uma mensagem: os cabeçalhos até a linha vazia (só o Content-Length importa) e o corpo
bool leMensagem(std::string& corpo) {
    std::size_t tamanho = 0;
    bool tem = false;
    std::string linha;
    while (std::getline(std::cin, linha)) {
        if (!linha.empty() && linha.back() == '\r') linha.pop_back();
        if (linha.empty()) {
            if (tem) break;
            continue;
        }
        if (linha.rfind("Content-Length:", 0) == 0) {
            tamanho = static_cast<std::size_t>(std::stoul(linha.substr(15)));
            tem = true;
        }
    }
    if (!tem) return false;
    corpo.assign(tamanho, '\0');
    std::cin.read(corpo.data(), static_cast<std::streamsize>(tamanho));
    return std::cin.gcount() == static_cast<std::streamsize>(tamanho);
}

void envia(const Valor& v) {
    const std::string c = json::texto(v);
    std::cout << "Content-Length: " << c.size() << "\r\n\r\n" << c;
    std::cout.flush();
}

void responde(const Valor& id, Valor resultado) {
    Valor m = Valor::objeto();
    m.set("jsonrpc", "2.0").set("id", id).set("result", std::move(resultado));
    envia(m);
}

void respondeErro(const Valor& id, int codigo, const std::string& mensagem) {
    Valor e = Valor::objeto();
    e.set("code", codigo).set("message", mensagem);
    Valor m = Valor::objeto();
    m.set("jsonrpc", "2.0").set("id", id).set("error", std::move(e));
    envia(m);
}

void notifica(const std::string& metodo, Valor params) {
    Valor m = Valor::objeto();
    m.set("jsonrpc", "2.0").set("method", metodo).set("params", std::move(params));
    envia(m);
}

// ── URIs e posições ─────────────────────────────────────────────────────────

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// file:///c%3A/x/y.cinza → c:/x/y.cinza (no Windows); file:///home/x → /home/x
std::string deUri(const std::string& uri) {
    std::string s = uri.rfind("file://", 0) == 0 ? uri.substr(7) : uri;
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
#ifdef _WIN32
    if (out.size() > 2 && out[0] == '/' && out[2] == ':') out.erase(0, 1);
#endif
    return out;
}

std::string paraUri(const std::string& caminho) {
    std::error_code ec;
    std::string p = textoCaminho(std::filesystem::absolute(caminhoUtf8(caminho), ec));
    std::string out = "file://";
    if (p.empty() || p[0] != '/') out += '/';
    static const char* h = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~' || c >= 0x80)
            out += static_cast<char>(c);
        else { out += '%'; out += h[c >> 4]; out += h[c & 0xF]; }
    }
    return out;
}

// As linhas de um texto (sem o \r do fim)
std::vector<std::string> linhas(const std::string& texto) {
    std::vector<std::string> r;
    std::string l;
    std::istringstream in(texto);
    while (std::getline(in, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        r.push_back(l);
    }
    return r;
}

// Os bytes de [0, n) da linha, em unidades UTF-16 (o que o LSP conta)
int utf16(const std::string& linha, std::size_t n) {
    int u = 0;
    for (std::size_t i = 0; i < n && i < linha.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(linha[i]);
        if ((c & 0xC0) == 0x80) continue;   // continuação
        u += c >= 0xF0 ? 2 : 1;             // fora do plano básico: um par substituto
    }
    return u;
}

Valor posicao(int linha, int caractere) {
    Valor p = Valor::objeto();
    p.set("line", linha).set("character", caractere);
    return p;
}

// O trecho do diagnóstico: da coluna (em bytes, a partir de 1) até o fim da palavra que começa nela
Valor trecho(const std::vector<std::string>& ls, int linha, int coluna) {
    const int l = linha > 0 ? linha - 1 : 0;
    const std::string texto = l < static_cast<int>(ls.size()) ? ls[static_cast<std::size_t>(l)] : std::string();
    std::size_t ini = coluna > 0 ? static_cast<std::size_t>(coluna - 1) : 0;
    if (ini > texto.size()) ini = texto.size();
    std::size_t fim = ini;
    auto palavra = [](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; };
    while (fim < texto.size() && palavra(static_cast<unsigned char>(texto[fim]))) ++fim;
    if (fim == ini && fim < texto.size()) ++fim;   // um símbolo: um caractere
    Valor r = Valor::objeto();
    r.set("start", posicao(l, utf16(texto, ini))).set("end", posicao(l, utf16(texto, fim)));
    return r;
}

// ── o estado ────────────────────────────────────────────────────────────────

std::map<std::string, std::string>            documentos;   // uri → o texto do editor
std::map<std::string, std::set<std::string>>  publicados;   // uri analisado → uris com erros publicados

// O texto de um arquivo: o do editor, se ele estiver aberto; senão o do disco
std::string textoDe(const std::string& uri) {
    if (auto it = documentos.find(uri); it != documentos.end()) return it->second;
    std::ifstream f(caminhoUtf8(deUri(uri)), std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

// O front-end sobre o documento; os erros vão para o editor, por arquivo
void analisa(const std::string& uri) {
    const std::string caminho = deUri(uri);
    const std::string codigo = documentos[uri];
    setMainFile(caminho);
    {
        ModuleLoader carregador;
        if (carregador.load(caminho, codigo)) {
            std::vector<ModuleUnit> unidades;
            for (auto& m : carregador.ordered())
                unidades.push_back(ModuleUnit{m.program.get(), m.prefix, m.imports, m.native});
            SemanticAnalyzer analisador;
            try {
                analisador.analyze(unidades);
            } catch (const SemanticError& e) {
                diagnostics().report("SemanticError", e.message, e.loc, e.notes);
            }
        }
    }
    std::map<std::string, Valor> por_arquivo;
    por_arquivo[uri] = Valor::listaVazia();   // o principal sempre: sem erros, limpa os antigos
    std::map<std::string, std::vector<std::string>> textos;
    for (const auto& d : diagnostics().all()) {
        std::string alvo = uri;
        if (d.loc.file_id != 0) {
            const std::string& nome = fileName(d.loc.file_id);
            if (nome.empty() || nome.rfind("<biblioteca>", 0) == 0) continue;   // fora do disco
            alvo = paraUri(nome);
        }
        if (!textos.count(alvo)) textos[alvo] = linhas(textoDe(alvo));
        Valor diag = Valor::objeto();
        std::string mensagem = displayName(d.kind) + ": " + d.message;
        for (const auto& nota : d.notes) mensagem += "\n" + nota;
        diag.set("range", trecho(textos[alvo], d.loc.line, d.loc.column))
            .set("severity", 1)
            .set("source", "cinza")
            .set("message", mensagem);
        if (!por_arquivo.count(alvo)) por_arquivo[alvo] = Valor::listaVazia();
        por_arquivo[alvo].add(std::move(diag));
    }
    std::ostringstream descarta;
    diagnostics().flush(descarta);   // esvazia para a próxima análise

    std::set<std::string> agora;
    for (auto& [alvo, lista] : por_arquivo) {
        Valor p = Valor::objeto();
        p.set("uri", alvo).set("diagnostics", std::move(lista));
        notifica("textDocument/publishDiagnostics", std::move(p));
        agora.insert(alvo);
    }
    for (const std::string& antigo : publicados[uri])   // os que não têm mais erro
        if (!agora.count(antigo)) {
            Valor p = Valor::objeto();
            p.set("uri", antigo).set("diagnostics", Valor::listaVazia());
            notifica("textDocument/publishDiagnostics", std::move(p));
        }
    publicados[uri] = agora;
}

Valor capacidades() {
    Valor c = Valor::objeto();
    c.set("textDocumentSync", 1);   // o documento inteiro a cada mudança
    c.set("documentFormattingProvider", true);
    Valor info = Valor::objeto();
    info.set("name", "cinza").set("version", CINZA_VERSAO);
    Valor r = Valor::objeto();
    r.set("capabilities", std::move(c)).set("serverInfo", std::move(info));
    return r;
}

// textDocument/formatting: o documento inteiro trocado pelo do cinza fmt (nada, se ele recusar)
Valor formatacao(const std::string& uri) {
    const std::string texto = documentos[uri];
    std::string saida, erro;
    Valor edicoes = Valor::listaVazia();
    if (!formata(texto, saida, erro) || saida == texto) return edicoes;
    const int n = static_cast<int>(linhas(texto).size());
    Valor r = Valor::objeto();
    r.set("start", posicao(0, 0)).set("end", posicao(n + 1, 0));
    Valor e = Valor::objeto();
    e.set("range", std::move(r)).set("newText", saida);
    edicoes.add(std::move(e));
    return edicoes;
}

} // namespace

int rodaServidor() {
    entradaESaidaBinarias();
    std::string corpo;
    bool encerrado = false;
    while (leMensagem(corpo)) {
        Valor m;
        try {
            m = json::Leitor(corpo).documento();
        } catch (const std::exception& e) {
            std::cerr << "cinza --lsp: mensagem inválida: " << e.what() << "\n";
            continue;
        }
        const std::string metodo = m["method"].texto();
        const Valor& id = m["id"];
        const Valor& params = m["params"];
        try {
            if (metodo == "initialize") {
                responde(id, capacidades());
            } else if (metodo == "shutdown") {
                encerrado = true;
                responde(id, Valor());
            } else if (metodo == "exit") {
                return encerrado ? 0 : 1;
            } else if (metodo == "textDocument/didOpen") {
                const Valor& doc = params["textDocument"];
                documentos[doc["uri"].texto()] = doc["text"].texto();
                analisa(doc["uri"].texto());
            } else if (metodo == "textDocument/didChange") {
                const std::string uri = params["textDocument"]["uri"].texto();
                const Valor& mudancas = params["contentChanges"];
                if (!mudancas.lista.empty()) documentos[uri] = mudancas.lista.back()["text"].texto();
                analisa(uri);
            } else if (metodo == "textDocument/didSave") {
                analisa(params["textDocument"]["uri"].texto());
            } else if (metodo == "textDocument/didClose") {
                const std::string uri = params["textDocument"]["uri"].texto();
                documentos.erase(uri);
                for (const std::string& alvo : publicados[uri]) {
                    Valor p = Valor::objeto();
                    p.set("uri", alvo).set("diagnostics", Valor::listaVazia());
                    notifica("textDocument/publishDiagnostics", std::move(p));
                }
                publicados.erase(uri);
            } else if (metodo == "textDocument/formatting") {
                responde(id, formatacao(params["textDocument"]["uri"].texto()));
            } else if (!id.nulo()) {
                respondeErro(id, -32601, "método não suportado: " + metodo);
            }
        } catch (const std::exception& e) {
            std::cerr << "cinza --lsp: " << metodo << ": " << e.what() << "\n";
            if (!id.nulo()) respondeErro(id, -32603, e.what());
        }
    }
    return encerrado ? 0 : 1;
}

} // namespace cinza::lsp
