#include "pacotes.h"
#include "plataforma.h"
#include "versao.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace cinza::pacotes {

namespace {

bool lerArquivo(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return true;
}

// Tira o comentário da linha: // no começo ou depois de um espaço (o de https:// fica)
std::string semComentario(const std::string& linha) {
    for (std::size_t i = linha.find("//"); i != std::string::npos; i = linha.find("//", i + 1))
        if (i == 0 || std::isspace(static_cast<unsigned char>(linha[i - 1]))) return linha.substr(0, i);
    return linha;
}

bool nomeValido(const std::string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    return true;
}

bool lerNumeros(const std::string& s, int v[3], int minimo) {
    v[0] = v[1] = v[2] = 0;
    std::istringstream in(s);
    std::string parte;
    int k = 0;
    while (std::getline(in, parte, '.')) {
        if (k >= 3 || parte.empty() || parte.size() > 9 || parte.find_first_not_of("0123456789") != std::string::npos)
            return false;
        v[k++] = std::stoi(parte);
    }
    return k >= minimo;
}

// uma origem de 'requer' é local se for um caminho (./, ../, /, C:)
bool origemLocal(const std::string& o) {
    return o.rfind("./", 0) == 0 || o.rfind("../", 0) == 0 || o.rfind("/", 0) == 0 || o == "." || o == ".." ||
           o.rfind(".\\", 0) == 0 || o.rfind("..\\", 0) == 0 ||
           (o.size() >= 2 && std::isalpha(static_cast<unsigned char>(o[0])) && o[1] == ':' &&
            (o.size() == 2 || o[2] == '/' || o[2] == '\\'));
}

// ── SHA-256 (FIPS 180-4) ──────────────────────────────────────────────
struct Sha256 {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint64_t total = 0;
    unsigned char bloco[64];
    std::size_t   usado = 0;

    static std::uint32_t rot(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void processa(const unsigned char* b) {
        static const std::uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(b[4 * i]) << 24) | (std::uint32_t(b[4 * i + 1]) << 16) |
                   (std::uint32_t(b[4 * i + 2]) << 8) | std::uint32_t(b[4 * i + 3]);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rot(w[i - 15], 7) ^ rot(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rot(w[i - 2], 17) ^ rot(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rot(e, 6) ^ rot(e, 11) ^ rot(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rot(a, 2) ^ rot(a, 13) ^ rot(a, 22);
            const std::uint32_t mj = (a & bb) ^ (a & c) ^ (bb & c);
            const std::uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void soma(const void* dados, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(dados);
        total += n;
        while (n > 0) {
            const std::size_t m = std::min(n, 64 - usado);
            std::copy(p, p + m, bloco + usado);
            usado += m; p += m; n -= m;
            if (usado == 64) { processa(bloco); usado = 0; }
        }
    }
    std::string fim() {
        const std::uint64_t bits = total * 8;
        const unsigned char um = 0x80, zero = 0;
        soma(&um, 1);
        while (usado != 56) soma(&zero, 1);
        unsigned char tam[8];
        for (int i = 0; i < 8; ++i) tam[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        soma(tam, 8);
        static const char* hex = "0123456789abcdef";
        std::string r;
        for (std::uint32_t x : h)
            for (int i = 7; i >= 0; --i) r += hex[(x >> (4 * i)) & 0xf];
        return r;
    }
};

std::string urlDe(const std::string& origem) {
    if (origem.find("://") != std::string::npos || origem.rfind("git@", 0) == 0) return origem;
    return "https://" + origem;
}

// A raiz do projeto da pasta atual (o czm.mod dela ou de uma acima); vazio sem projeto
std::string raizAtual() {
    std::error_code ec;
    for (fs::path p = fs::current_path(ec); !p.empty(); p = p.parent_path()) {
        if (fs::is_regular_file(p / "czm.mod", ec)) return textoCaminho(p);
        if (p == p.parent_path()) break;
    }
    return "";
}

bool git(const std::vector<std::string>& args, std::string* saida, std::string& erro) {
    std::vector<std::string> cmd{"git"};
    cmd.insert(cmd.end(), args.begin(), args.end());
    int codigo = 0;
    if (!executa(cmd, codigo, saida)) {
        erro = "o git não foi encontrado: instale o git (os pacotes são baixados por ele)";
        return false;
    }
    if (codigo != 0) {
        erro = "o git saiu com o código " + std::to_string(codigo);
        return false;
    }
    return true;
}

bool menor(const int a[3], const int b[3]) { return std::lexicographical_compare(a, a + 3, b, b + 3); }

} // namespace

bool versao(const std::string& tag, int v[3]) {
    return tag.size() > 1 && tag[0] == 'v' && lerNumeros(tag.substr(1), v, 3);
}

bool leManifesto(const std::string& texto, Manifesto& m, std::string& erro, int& linha_erro) {
    std::istringstream linhas(texto);
    std::string linha;
    int n = 0;
    bool tem_cinza = false;
    auto falha = [&](const std::string& msg) { erro = msg; linha_erro = n; return false; };
    while (std::getline(linhas, linha)) {
        ++n;
        linha = semComentario(linha);
        std::istringstream campos(linha);
        std::vector<std::string> w;
        for (std::string x; campos >> x;) w.push_back(x);
        if (w.empty()) continue;
        if (w[0] == "projeto") {
            if (w.size() != 2 || !nomeValido(w[1])) return falha("'projeto' leva um nome (letras, dígitos e _): projeto meu_pacote");
            if (!m.nome.empty()) return falha("'projeto' repetido no czm.mod");
            m.nome = w[1];
        } else if (w[0] == "cinza") {
            int pedida[3], atual[3];
            if (w.size() != 2 || !lerNumeros(w[1], pedida, 2)) return falha("'cinza' leva a versão mínima da linguagem: cinza 0.6");
            if (tem_cinza) return falha("'cinza' repetido no czm.mod");
            tem_cinza = true;
            lerNumeros(CINZA_VERSAO, atual, 3);
            if (menor(atual, pedida)) return falha(std::string("o projeto pede a Cinza ") + w[1] + ", e esta é a " + CINZA_VERSAO);
        } else if (w[0] == "requer") {
            Requer r;
            r.linha = n;
            if (w.size() < 2) return falha("'requer' leva a origem: requer ../caminho, ou requer url v1.2.0");
            r.origem = w[1];
            r.local  = origemLocal(r.origem);
            std::size_t k = 2;
            if (!r.local) {
                int v[3];
                if (k >= w.size() || !versao(w[k], v))
                    return falha("a dependência '" + r.origem + "' precisa da versão (uma tag): requer " + r.origem +
                                 " v1.0.0 (uma dependência local começa com ./ ou ../)");
                r.versao = w[k++];
            }
            if (k < w.size()) {
                if (w[k] != "as" || k + 2 != w.size() || !nomeValido(w[k + 1]))
                    return falha("depois da origem" + std::string(r.local ? "" : " e da versão") + " só vem 'as nome': requer " +
                                 r.origem + (r.local ? "" : " " + r.versao) + " as outro");
                r.nome = w[k + 1];
            }
            for (const auto& ja : m.requer)
                if (ja.origem == r.origem)
                    return falha("'" + r.origem + "' pedido duas vezes (linhas " + std::to_string(ja.linha) + " e " +
                                 std::to_string(n) + ")");
            m.requer.push_back(std::move(r));
        } else {
            return falha("declaração desconhecida no czm.mod: '" + w[0] + "' (valem projeto, cinza e requer)");
        }
    }
    return true;
}

bool leLock(const std::string& raiz, std::vector<Travado>& out, std::string& erro, int& linha_erro) {
    const fs::path arquivo = caminhoUtf8(raiz.empty() ? "." : raiz) / "czm.lock";
    std::error_code ec;
    if (!fs::exists(arquivo, ec)) return true;
    std::string texto;
    if (!lerArquivo(arquivo, texto)) { erro = "não foi possível ler o czm.lock"; linha_erro = 1; return false; }
    std::istringstream linhas(texto);
    std::string linha;
    int n = 0;
    while (std::getline(linhas, linha)) {
        ++n;
        linha = semComentario(linha);
        std::istringstream campos(linha);
        std::vector<std::string> w;
        for (std::string x; campos >> x;) w.push_back(x);
        if (w.empty()) continue;
        if (w.size() != 5 || w[0] != "pacote") {
            erro = "linha inválida no czm.lock (ele é gerado pelo cinza get: apague-o e rode cinza get)";
            linha_erro = n;
            return false;
        }
        out.push_back({w[1], w[2], w[3], w[4]});
    }
    return true;
}

std::string pastaDoCache() {
    std::string p;
    if (ambienteUtf8("CINZA_PACOTES", p) && !p.empty()) return p;
#ifdef _WIN32
    if (!ambienteUtf8("USERPROFILE", p)) p = ".";
#else
    if (!ambienteUtf8("HOME", p)) p = ".";
#endif
    return textoCaminho(caminhoUtf8(p) / ".cinza" / "pacotes");
}

// A chave de um pacote no cache: o último trecho da origem (legível) e 12 dígitos do hash dela
// (único), para o caminho não passar do limite do Windows: json-3fa2c1d0e9ab@v1.2.0
std::string pastaNoCache(const std::string& origem, const std::string& ver) {
    std::string o = origem;
    while (!o.empty() && (o.back() == '/' || o.back() == '\\')) o.pop_back();
    const std::size_t barra = o.find_last_of("/\\:");
    std::string ultimo = barra == std::string::npos ? o : o.substr(barra + 1);
    if (ultimo.size() > 4 && ultimo.compare(ultimo.size() - 4, 4, ".git") == 0) ultimo.resize(ultimo.size() - 4);
    std::string chave;
    for (char c : ultimo.substr(0, 32))
        chave += (std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_') ? c : '_';
    Sha256 h;
    h.soma(origem.data(), origem.size());
    chave += "-" + h.fim().substr(0, 12) + "@" + ver;
    return textoCaminho(caminhoUtf8(pastaDoCache()) / caminhoUtf8(chave));
}

std::string hashDaPasta(const std::string& pasta) {
    const fs::path base = caminhoUtf8(pasta);
    std::vector<std::string> arquivos;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(base, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const fs::path rel = it->path().lexically_relative(base);
        if (!rel.empty() && *rel.begin() == ".git") {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec)) arquivos.push_back(textoCaminho(rel));
    }
    std::sort(arquivos.begin(), arquivos.end());
    Sha256 h;
    for (const auto& a : arquivos) {
        std::string conteudo;
        lerArquivo(base / caminhoUtf8(a), conteudo);
        const std::string cabeca = a + '\0' + std::to_string(conteudo.size()) + '\0';
        h.soma(cabeca.data(), cabeca.size());
        h.soma(conteudo.data(), conteudo.size());
    }
    return "sha256-" + h.fim();
}

namespace {

// Baixa (se ainda não estiver no cache) o pacote na tag pedida; devolve o commit
bool baixa(const std::string& origem, const std::string& ver, std::string& commit, std::string& erro) {
    const fs::path destino = caminhoUtf8(pastaNoCache(origem, ver));
    const fs::path arquivo_commit = caminhoUtf8(textoCaminho(destino) + ".commit");
    std::error_code ec;
    if (fs::is_directory(destino, ec) && lerArquivo(arquivo_commit, commit)) {
        while (!commit.empty() && std::isspace(static_cast<unsigned char>(commit.back()))) commit.pop_back();
        return true;
    }
    fs::create_directories(destino.parent_path(), ec);
    const fs::path tmp = caminhoUtf8(textoCaminho(destino) + ".baixando");
    fs::remove_all(tmp, ec);
    std::cout << "baixando " << origem << " " << ver << std::endl;
    std::string e;
    // sem conversão de fins de linha: o conteúdo (e o hash) é o mesmo em todo sistema
    if (!git({"-c", "core.autocrlf=false", "-c", "core.eol=lf", "-c", "advice.detachedHead=false", "clone",
              "--quiet", "--depth", "1", "--branch", ver, urlDe(origem), textoCaminho(tmp)}, nullptr, e)) {
        erro = "não foi possível baixar '" + origem + "' " + ver + ": " + e + " (a tag existe? o endereço está certo?)";
        fs::remove_all(tmp, ec);
        return false;
    }
    std::string saida;
    if (!git({"-C", textoCaminho(tmp), "rev-parse", "HEAD"}, &saida, e)) {
        erro = "não foi possível ler o commit de '" + origem + "' " + ver + ": " + e;
        return false;
    }
    commit = saida.substr(0, saida.find_first_of("\r\n"));
    // o .git sai (só o conteúdo fica no cache; os arquivos dele podem ser só leitura)
    for (auto it = fs::recursive_directory_iterator(tmp / ".git", ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec))
        fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(tmp / ".git", ec);
    fs::remove_all(destino, ec);
    fs::rename(tmp, destino, ec);
    if (ec) { erro = "não foi possível gravar no cache '" + textoCaminho(destino) + "': " + ec.message(); return false; }
    std::ofstream(arquivo_commit, std::ios::binary) << commit << "\n";
    return true;
}

bool leManifestoDe(const fs::path& raiz, Manifesto& m, std::string& erro) {
    std::string texto;
    if (!lerArquivo(raiz / "czm.mod", texto)) { erro = "'" + textoCaminho(raiz / "czm.mod") + "' não existe"; return false; }
    int linha = 0;
    std::string e;
    if (!leManifesto(texto, m, e, linha)) {
        erro = textoCaminho(raiz / "czm.mod") + ":" + std::to_string(linha) + ": " + e;
        return false;
    }
    return true;
}

// A seleção das versões (a maior pedida em cada major; majors diferentes são erro) e o czm.lock
int resolveEGrava(const std::string& raiz) {
    struct Escolha {
        std::string versao;
        int         v[3] = {0, 0, 0};
        std::string quem;   // quem pediu esta versão
    };
    std::map<std::string, Escolha> escolha;            // origem → versão escolhida
    std::set<std::pair<std::string, std::string>> lidos;   // (origem, versão) já lidos
    std::set<std::string> locais_lidos;
    struct NaFila {
        fs::path    pasta;
        std::string quem;       // a cadeia de quem pediu, para as mensagens
        bool        do_cache;   // um pacote git
    };
    std::vector<NaFila> fila{{caminhoUtf8(raiz), "o projeto", false}};
    std::map<std::string, std::string> commits;
    std::string erro;
    // o czm.lock anterior: o hash de uma versão já travada não pode mudar
    std::vector<Travado> antigo;
    int linha_erro = 0;
    if (!leLock(raiz, antigo, erro, linha_erro)) {
        std::cerr << "cinza get: czm.lock:" << linha_erro << ": " << erro << "\n";
        return 1;
    }
    while (!fila.empty()) {
        const NaFila item = fila.back();
        fila.pop_back();
        const fs::path& pasta = item.pasta;
        const std::string& quem = item.quem;
        Manifesto m;
        if (!leManifestoDe(pasta, m, erro)) { std::cerr << "cinza get: " << erro << "\n"; return 1; }
        for (const auto& r : m.requer) {
            if (r.local && item.do_cache) {
                std::cerr << "cinza get: " << quem << " tem uma dependência local ('" << r.origem
                          << "'): um pacote git não pode ter (fora da máquina do autor, o caminho não existe)\n";
                return 1;
            }
            if (r.local) {
                std::error_code ec;
                const fs::path dep = (pasta / caminhoUtf8(r.origem)).lexically_normal();
                const std::string c = textoCaminho(fs::weakly_canonical(dep, ec));
                if (locais_lidos.insert(c).second) fila.push_back({dep, quem + " → " + r.origem, false});
                continue;
            }
            int v[3];
            versao(r.versao, v);
            auto it = escolha.find(r.origem);
            const std::string pedido = quem + " pede " + r.versao;
            if (it != escolha.end()) {
                if (it->second.v[0] != v[0]) {
                    std::cerr << "cinza get: conflito de versões de '" << r.origem << "': " << it->second.quem << ", e "
                              << pedido << " (majors diferentes não são compatíveis; escolha uma no czm.mod)\n";
                    return 1;
                }
                if (!menor(it->second.v, v)) continue;   // a escolhida já é maior ou igual
            }
            Escolha e;
            e.versao = r.versao;
            std::copy(v, v + 3, e.v);
            e.quem = pedido;
            escolha[r.origem] = e;
            if (!lidos.insert({r.origem, r.versao}).second) continue;
            std::string commit;
            if (!baixa(r.origem, r.versao, commit, erro)) { std::cerr << "cinza get: " << erro << "\n"; return 1; }
            commits[r.origem + "@" + r.versao] = commit;
            fila.push_back({caminhoUtf8(pastaNoCache(r.origem, r.versao)), quem + " → " + r.origem + " " + r.versao, true});
        }
    }
    std::ostringstream lock;
    lock << "// czm.lock: gerado pelo cinza get (não edite): a versão, o commit e o hash de cada pacote\n";
    for (const auto& [origem, e] : escolha) {
        std::string hash = hashDaPasta(pastaNoCache(origem, e.versao));
        std::string& commit = commits[origem + "@" + e.versao];
        for (const auto& t : antigo) {
            if (t.origem != origem || t.versao != e.versao || t.hash == hash) continue;
            // o cache não confere com o lock: baixa de novo; se ainda não conferir, a tag mudou
            std::error_code ec;
            fs::remove_all(caminhoUtf8(pastaNoCache(origem, e.versao)), ec);
            fs::remove(caminhoUtf8(pastaNoCache(origem, e.versao) + ".commit"), ec);
            if (!baixa(origem, e.versao, commit, erro)) { std::cerr << "cinza get: " << erro << "\n"; return 1; }
            hash = hashDaPasta(pastaNoCache(origem, e.versao));
            if (hash != t.hash) {
                std::cerr << "cinza get: o conteúdo de '" << origem << "' " << e.versao << " mudou no repositório (a tag "
                          << "foi movida): o czm.lock tem o commit " << t.commit << ", e a tag aponta agora para "
                          << commit << ". Se a mudança é esperada, tire a linha dele do czm.lock e rode 'cinza get'\n";
                return 1;
            }
        }
        lock << "pacote " << origem << " " << e.versao << " " << commit << " " << hash << "\n";
    }
    const fs::path arquivo = caminhoUtf8(raiz) / "czm.lock";
    std::error_code ec;
    if (escolha.empty()) {
        fs::remove(arquivo, ec);
    } else {
        std::ofstream f(arquivo, std::ios::binary);
        f << lock.str();
        if (!f) { std::cerr << "cinza get: não foi possível gravar o czm.lock\n"; return 1; }
    }
    std::cout << escolha.size() << " pacote(s) no czm.lock\n";
    return 0;
}

// As linhas do czm.mod (o texto, para editar uma linha 'requer' e gravar de volta)
std::vector<std::string> linhasDe(const std::string& texto) {
    std::vector<std::string> r;
    std::istringstream in(texto);
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        r.push_back(l);
    }
    return r;
}

bool gravaLinhas(const std::string& raiz, const std::vector<std::string>& linhas) {
    std::ofstream f(caminhoUtf8(raiz) / "czm.mod", std::ios::binary);
    for (const auto& l : linhas) f << l << "\n";
    return static_cast<bool>(f);
}

// A última tag semver do repositório
bool ultimaVersao(const std::string& origem, std::string& tag, std::string& erro) {
    std::string saida, e;
    if (!git({"ls-remote", "--tags", "--refs", urlDe(origem)}, &saida, e)) {
        erro = "não foi possível ler as tags de '" + origem + "': " + e;
        return false;
    }
    int melhor[3] = {-1, 0, 0};
    std::istringstream in(saida);
    for (std::string l; std::getline(in, l);) {
        const auto p = l.find("refs/tags/");
        if (p == std::string::npos) continue;
        std::string t = l.substr(p + 10);
        while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
        int v[3];
        if (versao(t, v) && (melhor[0] < 0 || menor(melhor, v))) { std::copy(v, v + 3, melhor); tag = t; }
    }
    if (melhor[0] < 0) { erro = "'" + origem + "' não tem nenhuma tag de versão (v1.2.0)"; return false; }
    return true;
}

std::string raizOuErro(const char* comando) {
    const std::string r = raizAtual();
    if (r.empty()) std::cerr << "cinza " << comando << ": esta pasta não está num projeto (não há czm.mod nela nem acima); crie com 'cinza init'\n";
    return r;
}

} // namespace

int comandoGet(const std::vector<std::string>& args) {
    if (!args.empty()) { std::cerr << "Uso: cinza get\n"; return 2; }
    const std::string raiz = raizOuErro("get");
    return raiz.empty() ? 1 : resolveEGrava(raiz);
}

int comandoAdd(const std::vector<std::string>& args) {
    // cinza add <origem> [versão] [as nome]
    if (args.empty() || args.size() > 4) { std::cerr << "Uso: cinza add <url ou ./caminho> [versão] [as nome]\n"; return 2; }
    const std::string raiz = raizOuErro("add");
    if (raiz.empty()) return 1;
    Requer r;
    r.origem = args[0];
    r.local  = origemLocal(r.origem);
    std::size_t k = 1;
    int v[3];
    if (k < args.size() && args[k] != "as") {
        if (r.local) { std::cerr << "cinza add: uma dependência local não tem versão\n"; return 2; }
        if (!versao(args[k], v)) { std::cerr << "cinza add: '" << args[k] << "' não é uma versão (v1.2.0)\n"; return 2; }
        r.versao = args[k++];
    }
    if (k < args.size()) {
        if (args[k] != "as" || k + 2 != args.size() || !nomeValido(args[k + 1])) {
            std::cerr << "Uso: cinza add <url ou ./caminho> [versão] [as nome]\n";
            return 2;
        }
        r.nome = args[k + 1];
    }
    // um caminho relativo é dado a partir da pasta atual, e o czm.mod o lê a partir da raiz
    if (r.local && !caminhoUtf8(r.origem).is_absolute()) {
        std::error_code ec;
        const fs::path alvo = fs::absolute(caminhoUtf8(r.origem), ec).lexically_normal();
        std::string rel = textoCaminho(alvo.lexically_relative(fs::absolute(caminhoUtf8(raiz), ec).lexically_normal()));
        r.origem = rel.empty() || rel == "." ? "." : rel.rfind("..", 0) == 0 ? rel : "./" + rel;
    }
    std::string erro;
    if (!r.local && r.versao.empty() && !ultimaVersao(r.origem, r.versao, erro)) {
        std::cerr << "cinza add: " << erro << "\n";
        return 1;
    }
    std::string texto;
    lerArquivo(caminhoUtf8(raiz) / "czm.mod", texto);
    std::vector<std::string> linhas = linhasDe(texto);
    const std::string nova = "requer " + r.origem + (r.local ? "" : " " + r.versao) + (r.nome.empty() ? "" : " as " + r.nome);
    bool trocou = false;
    for (auto& l : linhas) {
        std::istringstream campos(semComentario(l));
        std::string a, b;
        campos >> a >> b;
        if (a == "requer" && b == r.origem) { l = nova; trocou = true; }
    }
    if (!trocou) linhas.push_back(nova);
    const std::vector<std::string> antes = linhasDe(texto);
    if (!gravaLinhas(raiz, linhas)) { std::cerr << "cinza add: não foi possível gravar o czm.mod\n"; return 1; }
    const int rc = resolveEGrava(raiz);
    if (rc != 0) gravaLinhas(raiz, antes);   // o czm.mod volta como estava
    else std::cout << nova << "\n";
    return rc;
}

int comandoRemove(const std::vector<std::string>& args) {
    if (args.size() != 1) { std::cerr << "Uso: cinza remove <nome do pacote ou origem>\n"; return 2; }
    const std::string raiz = raizOuErro("remove");
    if (raiz.empty()) return 1;
    std::string texto;
    lerArquivo(caminhoUtf8(raiz) / "czm.mod", texto);
    std::vector<std::string> linhas = linhasDe(texto), novas;
    std::vector<Travado> lock;
    std::string erro;
    int linha_erro = 0;
    leLock(raiz, lock, erro, linha_erro);
    bool achou = false;
    for (const auto& l : linhas) {
        std::istringstream campos(semComentario(l));
        std::vector<std::string> w;
        for (std::string x; campos >> x;) w.push_back(x);
        bool este = false;
        if (w.size() >= 2 && w[0] == "requer") {
            const auto as = std::find(w.begin(), w.end(), "as");
            std::string nome = as != w.end() && as + 1 != w.end() ? *(as + 1) : "";
            if (nome.empty()) {   // o nome declarado no czm.mod do pacote
                Manifesto m;
                fs::path pasta;
                if (origemLocal(w[1])) {
                    pasta = caminhoUtf8(raiz) / caminhoUtf8(w[1]);
                } else {
                    for (const auto& t : lock)
                        if (t.origem == w[1]) pasta = caminhoUtf8(pastaNoCache(t.origem, t.versao));
                }
                std::string e;
                if (!pasta.empty() && leManifestoDe(pasta, m, e)) nome = m.nome;
            }
            este = w[1] == args[0] || nome == args[0];
        }
        if (este) achou = true;
        else novas.push_back(l);
    }
    if (!achou) { std::cerr << "cinza remove: '" << args[0] << "' não é uma dependência do czm.mod\n"; return 1; }
    if (!gravaLinhas(raiz, novas)) { std::cerr << "cinza remove: não foi possível gravar o czm.mod\n"; return 1; }
    return resolveEGrava(raiz);
}

} // namespace cinza::pacotes
