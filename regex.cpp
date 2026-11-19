#include "regex.h"
#include "utf8.h"
#include <algorithm>
#include <utility>

namespace cinza::regex {

namespace {

// Limites: repetição {n,m} até 1000 (como o RE2) e o programa até 100 mil instruções —
// um padrão maior que isso é erro, não um programa que trava a máquina
constexpr int MAX_REPETICAO = 1000;
constexpr std::size_t MAX_PROGRAMA = 100000;

bool palavra(char32_t c) { return utf8::letra(c) || utf8::digito(c) || c == U'_'; }

// A árvore do padrão
struct No {
    enum class T { Vazio, Char, Qualquer, Classe, Concat, Alt, Repete, Grupo, Inicio, Fim, Borda, NaoBorda } t;
    explicit No(T tipo) : t(tipo) {}
    char32_t         c = 0;
    int              classe = -1;   // Classe
    int              grupo = -1;    // Grupo: o número (-1: sem captura)
    int              min = 0, max = 0;   // Repete (max -1: sem limite)
    bool             guloso = true;
    std::vector<int> filhos;
};

} // namespace

class Compilador {
public:
    Compilador(const std::u32string& p, Regex& re) : p_(p), re_(re) {}

    void compila() {
        if (p_.compare(0, 4, U"(?i)") == 0) {
            re_.sem_caixa_ = true;
            i_ = 4;
        }
        const int raiz = alternativa();
        if (i_ < p_.size()) erro(p_[i_] == U')' ? "')' sem o '(' correspondente" : "caractere inesperado");
        emite({Regex::Op::Grava, 0, 0});
        gera(raiz);
        emite({Regex::Op::Grava, 0, 1});
        emite({Regex::Op::Casou});
    }

private:
    const std::u32string& p_;
    Regex&                re_;
    std::size_t           i_ = 0;
    std::vector<No>       nos_;

    [[noreturn]] void erro(const std::string& motivo) const { throw ErroDePadrao(motivo, i_); }

    int novo(No n) {
        nos_.push_back(std::move(n));
        return static_cast<int>(nos_.size() - 1);
    }
    bool fim() const { return i_ >= p_.size(); }
    char32_t olha() const { return p_[i_]; }

    // alternativa := sequência ('|' sequência)*
    int alternativa() {
        std::vector<int> ramos{sequencia()};
        while (!fim() && olha() == U'|') {
            ++i_;
            ramos.push_back(sequencia());
        }
        if (ramos.size() == 1) return ramos[0];
        No n{No::T::Alt};
        n.filhos = std::move(ramos);
        return novo(std::move(n));
    }

    // sequência := repetição*
    int sequencia() {
        No n{No::T::Concat};
        while (!fim() && olha() != U'|' && olha() != U')') n.filhos.push_back(repeticao());
        if (n.filhos.empty()) return novo(No{No::T::Vazio});
        if (n.filhos.size() == 1) return n.filhos[0];
        return novo(std::move(n));
    }

    // Um número decimal de {n,m} (até MAX_REPETICAO)
    bool numero(int& v) {
        const std::size_t ini = i_;
        long long n = 0;
        while (!fim() && olha() >= U'0' && olha() <= U'9') {
            n = n * 10 + (olha() - U'0');
            if (n > MAX_REPETICAO) erro("repetição acima de " + std::to_string(MAX_REPETICAO));
            ++i_;
        }
        v = static_cast<int>(n);
        return i_ > ini;
    }

    // {n}, {n,}, {n,m}; se não for um desses, o '{' é um caractere comum (como no RE2)
    bool chaves(int& mn, int& mx) {
        const std::size_t ini = i_;
        ++i_;
        if (!numero(mn)) { i_ = ini; return false; }
        mx = mn;
        if (!fim() && olha() == U',') {
            ++i_;
            mx = -1;
            if (!fim() && olha() != U'}' && !numero(mx)) { i_ = ini; return false; }
        }
        if (fim() || olha() != U'}') { i_ = ini; return false; }
        ++i_;
        if (mx != -1 && mx < mn) { i_ = ini; erro("repetição {n,m} com m menor que n"); }
        return true;
    }

    // repetição := átomo quantificador?
    int repeticao() {
        int a = atomo();
        bool repetiu = false;
        while (!fim()) {
            int mn = 0, mx = 0;
            const std::size_t onde = i_;
            const char32_t q = olha();
            if (q == U'*') { mn = 0; mx = -1; ++i_; }
            else if (q == U'+') { mn = 1; mx = -1; ++i_; }
            else if (q == U'?') { mn = 0; mx = 1; ++i_; }
            else if (q == U'{') { if (!chaves(mn, mx)) break; }
            else break;
            if (repetiu) {
                i_ = onde;
                erro("quantificador repetido (para repetir de novo, use um grupo: (?:a*)*)");
            }
            repetiu = true;
            No n{No::T::Repete};
            n.min = mn;
            n.max = mx;
            if (!fim() && olha() == U'?') { n.guloso = false; ++i_; }
            n.filhos.push_back(a);
            a = novo(std::move(n));
        }
        return a;
    }

    // Um escape de classe (\d \w \s e as negadas) nas faixas; false se não for um
    bool escapeDeClasse(char32_t e, Regex::Classe& k, bool& negada) {
        negada = e == U'D' || e == U'W' || e == U'S';
        switch (e) {
            case U'd': case U'D': k.faixas.push_back({U'0', U'9'}); return true;
            case U'w': case U'W':
                k.faixas.insert(k.faixas.end(), {{U'0', U'9'}, {U'A', U'Z'}, {U'_', U'_'}, {U'a', U'z'},
                                                 {0xC0, 0xD6}, {0xD8, 0xF6}, {0xF8, 0xFF}});
                return true;
            case U's': case U'S':
                k.faixas.insert(k.faixas.end(), {{U'\t', U'\r'}, {U' ', U' '}});   // \t \n \v \f \r
                return true;
            default: return false;
        }
    }

    // O caractere de um escape simples (\n, \t, pontuação); erro se for desconhecido
    char32_t escapeSimples(char32_t e) {
        switch (e) {
            case U'n': return U'\n';
            case U't': return U'\t';
            case U'r': return U'\r';
            case U'f': return U'\f';
            case U'v': return U'\v';
            default: break;
        }
        if (e < 0x80 && !utf8::letra(e) && !utf8::digito(e)) return e;   // \. \* \\ \( ...
        --i_;
        erro(std::string("escape desconhecido '\\") + utf8::codifica(std::u32string(1, e)) + "'");
    }

    int classeNova(Regex::Classe k) {
        re_.classes_.push_back(std::move(k));
        No n{No::T::Classe};
        n.classe = static_cast<int>(re_.classes_.size() - 1);
        return novo(std::move(n));
    }

    // [...] — o ']' logo depois do '[' (ou do '[^') é um caractere
    int classe() {
        const std::size_t abre = i_;
        ++i_;
        Regex::Classe k;
        if (!fim() && olha() == U'^') { k.negada = true; ++i_; }
        bool primeiro = true;
        while (true) {
            if (fim()) { i_ = abre; erro("'[' sem o ']' correspondente"); }
            char32_t c = olha();
            if (c == U']' && !primeiro) { ++i_; break; }
            primeiro = false;
            ++i_;
            if (c == U'\\') {
                if (fim()) erro("'\\' no fim do padrão");
                const char32_t e = olha();
                ++i_;
                Regex::Classe sub;
                bool negada = false;
                if (escapeDeClasse(e, sub, negada)) {
                    if (negada) {   // o complemento das faixas, dentro da classe
                        std::sort(sub.faixas.begin(), sub.faixas.end());
                        char32_t de = 0;
                        for (auto [a, b] : sub.faixas) {
                            if (a > de) k.faixas.push_back({de, a - 1});
                            de = std::max<char32_t>(de, b + 1);
                        }
                        k.faixas.push_back({de, 0x10FFFF});
                    } else {
                        k.faixas.insert(k.faixas.end(), sub.faixas.begin(), sub.faixas.end());
                    }
                    continue;
                }
                c = escapeSimples(e);
            }
            // uma faixa a-z (o '-' no fim ou antes do ']' é um caractere)
            if (!fim() && olha() == U'-' && i_ + 1 < p_.size() && p_[i_ + 1] != U']') {
                ++i_;
                char32_t ate = olha();
                ++i_;
                if (ate == U'\\') {
                    if (fim()) erro("'\\' no fim do padrão");
                    ate = escapeSimples(olha());
                    ++i_;
                }
                if (ate < c) { i_ -= 1; erro("faixa invertida na classe"); }
                k.faixas.push_back({c, ate});
            } else {
                k.faixas.push_back({c, c});
            }
        }
        return classeNova(std::move(k));
    }

    int atomo() {
        const char32_t c = olha();
        switch (c) {
            case U'(': {
                const std::size_t abre = i_;
                ++i_;
                int grupo = -1;
                if (!fim() && olha() == U'?') {
                    ++i_;
                    if (!fim() && olha() == U':') {
                        ++i_;
                    } else if (!fim() && olha() == U'<') {
                        ++i_;
                        std::u32string nome;
                        while (!fim() && (palavra(olha()) && olha() < 0x80)) nome += p_[i_++];
                        if (fim() || olha() != U'>' || nome.empty() || utf8::digito(nome[0]))
                            erro("nome de grupo inválido (letras ASCII, dígitos e '_', sem começar por dígito)");
                        ++i_;
                        const std::string n8 = utf8::codifica(nome);
                        if (std::find(re_.nomes_.begin(), re_.nomes_.end(), n8) != re_.nomes_.end())
                            erro("grupo '" + n8 + "' repetido");
                        re_.nomes_.push_back(n8);
                        grupo = static_cast<int>(re_.nomes_.size());
                    } else if (p_.compare(i_, 2, U"i)") == 0) {
                        erro("(?i) só vale no começo do padrão");
                    } else {
                        erro("grupo '(?' desconhecido (há (?:...) e (?<nome>...); sem lookaround)");
                    }
                } else {
                    re_.nomes_.push_back("");
                    grupo = static_cast<int>(re_.nomes_.size());
                }
                const int dentro = alternativa();
                if (fim() || olha() != U')') { i_ = abre; erro("'(' sem o ')' correspondente"); }
                ++i_;
                No n{No::T::Grupo};
                n.grupo = grupo;
                n.filhos.push_back(dentro);
                return novo(std::move(n));
            }
            case U'[': return classe();
            case U'.': ++i_; return novo(No{No::T::Qualquer});
            case U'^': ++i_; return novo(No{No::T::Inicio});
            case U'$': ++i_; return novo(No{No::T::Fim});
            case U'*': case U'+': case U'?': erro("quantificador sem nada antes para repetir");
            case U'\\': {
                ++i_;
                if (fim()) { --i_; erro("'\\' no fim do padrão"); }
                const char32_t e = olha();
                ++i_;
                if (e == U'b') return novo(No{No::T::Borda});
                if (e == U'B') return novo(No{No::T::NaoBorda});
                Regex::Classe k;
                bool negada = false;
                if (escapeDeClasse(e, k, negada)) {
                    k.negada = negada;
                    return classeNova(std::move(k));
                }
                No n{No::T::Char};
                n.c = escapeSimples(e);
                return novo(std::move(n));
            }
            default: {
                ++i_;
                No n{No::T::Char};
                n.c = c;
                return novo(std::move(n));
            }
        }
    }

    // ── a geração do programa ──────────────────────────────────────────────
    int emite(Regex::Inst in) {
        if (re_.prog_.size() >= MAX_PROGRAMA) throw ErroDePadrao("padrão grande demais", 0);
        re_.prog_.push_back(in);
        return static_cast<int>(re_.prog_.size() - 1);
    }
    int aqui() const { return static_cast<int>(re_.prog_.size()); }
    Regex::Inst& em(int pc) { return re_.prog_[static_cast<std::size_t>(pc)]; }

    // Divide em `pc` com o preferido conforme gulosa/preguiçosa: `dentro` repete, `fora` sai
    void divide(int pc, int dentro, int fora, bool guloso) {
        em(pc).x = guloso ? dentro : fora;
        em(pc).y = guloso ? fora : dentro;
    }

    void gera(int id) {
        const No& n = nos_[static_cast<std::size_t>(id)];
        switch (n.t) {
            case No::T::Vazio: break;
            case No::T::Char: emite({Regex::Op::Char, re_.sem_caixa_ ? utf8::minuscula(n.c) : n.c}); break;
            case No::T::Qualquer: emite({Regex::Op::Qualquer}); break;
            case No::T::Classe: emite({Regex::Op::Classe, 0, n.classe}); break;
            case No::T::Inicio: emite({Regex::Op::Inicio}); break;
            case No::T::Fim: emite({Regex::Op::Fim}); break;
            case No::T::Borda: emite({Regex::Op::Borda}); break;
            case No::T::NaoBorda: emite({Regex::Op::NaoBorda}); break;
            case No::T::Concat:
                for (int f : n.filhos) gera(f);
                break;
            case No::T::Alt: {   // divide a, (divide b, c ...): a primeira tem a preferência
                std::vector<int> pulos;
                for (std::size_t k = 0; k + 1 < n.filhos.size(); ++k) {
                    const int d = emite({Regex::Op::Divide});
                    em(d).x = aqui();
                    gera(n.filhos[k]);
                    pulos.push_back(emite({Regex::Op::Pula}));
                    em(d).y = aqui();
                }
                gera(n.filhos.back());
                for (int p : pulos) em(p).x = aqui();
                break;
            }
            case No::T::Grupo:
                if (n.grupo < 0) { gera(n.filhos[0]); break; }
                emite({Regex::Op::Grava, 0, 2 * n.grupo});
                gera(n.filhos[0]);
                emite({Regex::Op::Grava, 0, 2 * n.grupo + 1});
                break;
            case No::T::Repete: {
                const int e = n.filhos[0];
                for (int k = 0; k < n.min; ++k) gera(e);
                if (n.max == -1) {   // L: divide corpo, fim; corpo; pula L
                    const int d = emite({Regex::Op::Divide});
                    gera(e);
                    emite({Regex::Op::Pula, 0, d});
                    divide(d, d + 1, aqui(), n.guloso);
                } else {   // (e (e (...)?)?)? : cada um opcional, todos saindo para o fim
                    std::vector<int> ds;
                    for (int k = n.min; k < n.max; ++k) {
                        ds.push_back(emite({Regex::Op::Divide}));
                        gera(e);
                    }
                    for (int d : ds) divide(d, d + 1, aqui(), n.guloso);
                }
                break;
            }
        }
    }
};

Regex::Regex(const std::u32string& padrao) {
    Compilador c(padrao, *this);
    c.compila();
}

bool Regex::naClasse(const Classe& k, char32_t c) const {
    auto dentro = [&](char32_t x) {
        for (auto [a, b] : k.faixas)
            if (x >= a && x <= b) return true;
        return false;
    };
    bool sim = dentro(c);
    if (!sim && sem_caixa_) sim = dentro(utf8::minuscula(c)) || dentro(utf8::maiuscula(c));
    return sim != k.negada;
}

bool Regex::casa(const Inst& in, char32_t c) const {
    switch (in.op) {
        case Op::Char: return (sem_caixa_ ? utf8::minuscula(c) : c) == in.c;
        case Op::Qualquer: return c != U'\n';
        case Op::Classe: return naClasse(classes_[static_cast<std::size_t>(in.x)], c);
        default: return false;
    }
}

namespace {

struct Fio {
    int              pc;
    std::vector<int> caps;
};

// Uma lista de fios da máquina de Pike, cada instrução no máximo uma vez por posição
struct Lista {
    std::vector<Fio>      fios;
    std::vector<unsigned> marca;
    unsigned              geracao = 0;
    explicit Lista(std::size_t n) : marca(n, 0) {}
    void limpa() {
        fios.clear();
        ++geracao;
    }
};

} // namespace

bool Regex::procura(const std::u32string& s, std::size_t inicio, std::vector<int>& caps, bool inteiro) const {
    const int n = static_cast<int>(s.size());
    const std::size_t ncaps = 2 * (nomes_.size() + 1);
    Lista a(prog_.size()), b(prog_.size());
    Lista* atual = &a;
    Lista* proxima = &b;
    atual->limpa();
    bool achou = false;

    // Acrescenta o fio e segue as instruções que não consomem caractere (em ordem de
    // preferência: o ramo preferido de um Divide entra antes do outro)
    struct Pendente {
        int              pc;
        std::vector<int> caps;
    };
    std::vector<Pendente> pilha;
    auto adiciona = [&](Lista& l, int pc0, std::vector<int> caps0, int pos) {
        pilha.clear();
        pilha.push_back({pc0, std::move(caps0)});
        while (!pilha.empty()) {
            Pendente q = std::move(pilha.back());
            pilha.pop_back();
            int pc = q.pc;
            while (true) {
                if (l.marca[static_cast<std::size_t>(pc)] == l.geracao) break;
                l.marca[static_cast<std::size_t>(pc)] = l.geracao;
                const Inst& in = prog_[static_cast<std::size_t>(pc)];
                bool segue = true;
                switch (in.op) {
                    case Op::Pula: pc = in.x; continue;
                    case Op::Divide:
                        pilha.push_back({in.y, q.caps});
                        pc = in.x;
                        continue;
                    case Op::Grava: q.caps[static_cast<std::size_t>(in.x)] = pos; ++pc; continue;
                    case Op::Inicio: segue = pos == 0; break;
                    case Op::Fim: segue = pos == n; break;
                    case Op::Borda: case Op::NaoBorda: {
                        const bool antes = pos > 0 && palavra(s[static_cast<std::size_t>(pos - 1)]);
                        const bool depois = pos < n && palavra(s[static_cast<std::size_t>(pos)]);
                        segue = (antes != depois) == (in.op == Op::Borda);
                        break;
                    }
                    default:
                        l.fios.push_back({pc, std::move(q.caps)});
                        segue = false;
                        break;
                }
                if (!segue) break;
                ++pc;
            }
        }
    };

    const std::vector<int> vazio(ncaps, -1);
    for (int pos = static_cast<int>(inicio); pos <= n; ++pos) {
        // um fio novo em cada posição (o de menor preferência), enquanto não achou
        if (!achou && (!inteiro || pos == static_cast<int>(inicio))) adiciona(*atual, 0, vazio, pos);
        if (atual->fios.empty()) {   // nenhum fio vivo: acabou, ou tenta a próxima posição
            if (achou || inteiro) break;
            atual->limpa();
            continue;
        }
        proxima->limpa();
        for (Fio& f : atual->fios) {
            const Inst& in = prog_[static_cast<std::size_t>(f.pc)];
            if (in.op == Op::Casou) {
                if (inteiro && pos != n) continue;
                caps = std::move(f.caps);
                achou = true;
                break;   // os fios de menor preferência caem
            }
            if (pos < n && casa(in, s[static_cast<std::size_t>(pos)])) adiciona(*proxima, f.pc + 1, std::move(f.caps), pos + 1);
        }
        std::swap(atual, proxima);
        if (achou && atual->fios.empty()) break;
    }
    return achou;
}

} // namespace cinza::regex
