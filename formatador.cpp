#include "formatador.h"
#include "lexer.h"
#include <cctype>
#include <vector>

namespace cinza {

namespace {

// Um pedaço da linha, com o texto como está na fonte
struct Peca {
    enum class T { Palavra, Numero, Texto, Comentario, Simbolo } t;
    std::string s;
    int espaco_antes = 0;   // os espaços que havia antes dele na fonte (o do comentário fica)
};

bool letra(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }

const char* const SIMBOLOS_DUPLOS[] = {"==", "!=", "<=", ">=", "&&", "||", "->", "+=", "-=", "*=", "/=", "%="};

// Os pedaços de uma linha; false se uma string não terminar nela
bool pedacos(const std::string& linha, std::vector<Peca>& out) {
    std::size_t i = 0;
    int espacos = 0;
    while (i < linha.size()) {
        const unsigned char c = static_cast<unsigned char>(linha[i]);
        if (c == ' ' || c == '\t') { ++espacos; ++i; continue; }
        Peca p;
        p.espaco_antes = espacos;
        espacos = 0;
        if (c == '/' && i + 1 < linha.size() && linha[i + 1] == '/') {
            p.t = Peca::T::Comentario;
            p.s = linha.substr(i);
            std::size_t fim = p.s.find_last_not_of(" \t");
            p.s.resize(fim + 1);
            out.push_back(std::move(p));
            return true;
        }
        if (c == '"') {
            std::size_t j = i + 1;
            while (j < linha.size() && linha[j] != '"') j += linha[j] == '\\' ? 2 : 1;
            if (j >= linha.size()) return false;
            p.t = Peca::T::Texto;
            p.s = linha.substr(i, j + 1 - i);
            i = j + 1;
        } else if (std::isdigit(c)) {
            std::size_t j = i;
            while (j < linha.size() && (std::isalnum(static_cast<unsigned char>(linha[j])) || linha[j] == '_' ||
                                        linha[j] == '.'))
                ++j;
            p.t = Peca::T::Numero;
            p.s = linha.substr(i, j - i);
            i = j;
        } else if (letra(c)) {
            std::size_t j = i;
            while (j < linha.size() && (letra(static_cast<unsigned char>(linha[j])) ||
                                        std::isdigit(static_cast<unsigned char>(linha[j]))))
                ++j;
            p.t = Peca::T::Palavra;
            p.s = linha.substr(i, j - i);
            i = j;
        } else {
            p.t = Peca::T::Simbolo;
            p.s = std::string(1, static_cast<char>(c));
            for (const char* d : SIMBOLOS_DUPLOS)
                if (linha.compare(i, 2, d) == 0) { p.s = d; break; }
            i += p.s.size();
        }
        out.push_back(std::move(p));
    }
    return true;
}

bool eh(const Peca& p, const char* s) { return p.t != Peca::T::Texto && p.t != Peca::T::Comentario && p.s == s; }

// Palavras depois das quais um ( é de uma instrução (if (...)), não de uma chamada
bool controle(const std::string& s) {
    return s == "if" || s == "while" || s == "for" || s == "except" || s == "return" || s == "in" ||
           s == "and" || s == "or" || s == "not" || s == "throw" ||
           s == "else" || s == "yield";   // Q1b: x else (a + b), x else [1], x else -1; yield v
}

// Palavras depois das quais vem uma expressão (então - e ! são unários, [ e { abrem um literal)
bool antesDeExpressao(const std::string& s) { return controle(s); }

// Um aberto: o tipo, se é uma chave de expressão (literal de dict) e, para ( e [, o deslocamento
// que a linha onde ele abriu ganhou (as continuações guardam o alinhamento do autor)
struct Aberto {
    char c;
    bool expressao = false;
    int delta = 0;
};

class Formatador {
public:
    explicit Formatador(const std::string& nl) : nl_(nl) {}

    void linha(const std::string& original, std::string& saida) {
        std::vector<Peca> ps;
        if (!pedacos(original, ps)) throw std::string("uma string não termina na linha");
        if (ps.empty()) {
            if (!saida.empty()) brancas_++;
            return;
        }
        // no máximo uma linha em branco seguida, e nenhuma logo depois de { nem antes de }
        if (brancas_ > 0 && !saida.empty() && !(tem_anterior_ && eh(anterior_, "{")) && !eh(ps[0], "}"))
            saida += nl_;
        brancas_ = 0;

        // o recuo: pelas chaves abertas; uma continuação (dentro de ( ou [) guarda o do autor
        int recuo_original = 0;
        for (char c : original) {
            if (c == ' ') ++recuo_original;
            else if (c == '\t') recuo_original += 2;
            else break;
        }
        int chaves = 0;
        for (const Aberto& a : abertos_)
            if (a.c == '{') chaves++;
        const Aberto* topo = abertos_.empty() ? nullptr : &abertos_.back();
        // a linha continua uma instrução (a anterior não terminou em ; { ou }), fora de um literal
        const bool continua_instrucao = tem_anterior_ && !eh(anterior_, ";") && !eh(anterior_, "{") &&
                                        !eh(anterior_, "}") && !eh(ps[0], "}") && !eh(ps[0], "{") &&
                                        !(topo && topo->expressao);
        int recuo;
        if (topo && topo->c != '{') {
            // continuação de uma expressão aberta: o alinhamento do autor, deslocado como a linha
            // onde o ( ou [ abriu; nunca menos que o recuo do bloco
            recuo = recuo_original + topo->delta;
            if (recuo < 2 * chaves) recuo = 2 * chaves;
        } else if (continua_instrucao) {
            // continuação de uma instrução: o alinhamento do autor, deslocado como a linha onde a
            // instrução começou; ao menos um nível além do bloco
            recuo = recuo_original + delta_instrucao_;
            if (recuo < 2 * chaves + 2) recuo = 2 * chaves + 2;
        } else {
            // uma linha que começa fechando a chave sai no nível de quem abriu
            const int n = chaves - (topo && eh(ps[0], "}") ? 1 : 0);
            recuo = 2 * (n < 0 ? 0 : n);
        }
        const int delta = recuo - recuo_original;
        if (!(topo && topo->c != '{') && !continua_instrucao) delta_instrucao_ = delta;

        std::string texto(static_cast<std::size_t>(recuo), ' ');
        for (std::size_t i = 0; i < ps.size(); ++i) {
            if (i > 0) texto += espaco(ps, i);
            texto += ps[i].s;
            anda(ps, i, delta);
        }
        saida += texto;
        saida += nl_;
        for (std::size_t i = ps.size(); i-- > 0;)
            if (ps[i].t != Peca::T::Comentario) {
                anterior_ = ps[i];
                tem_anterior_ = true;
                break;
            }
    }

    // No fim do arquivo: tudo o que abriu fechou (um arquivo com erro de sintaxe não é formatado)
    void termina() const {
        if (!abertos_.empty())
            throw std::string(std::string("um '") + abertos_.back().c + "' não foi fechado");
    }

private:
    std::string         nl_;
    std::vector<Aberto> abertos_;
    int                 brancas_ = 0;
    int                 angulos_ = 0;       // dentro de list<...>, dict<...>, pair<...>, cast<...>
    std::vector<bool>   angulo_generico_;   // por peça da linha atual: um < ou > de tipo
    bool                cabecalho_classe_ = false;
    Peca                anterior_{Peca::T::Simbolo, ";"};
    bool                tem_anterior_ = false;
    int                 delta_instrucao_ = 0;   // o deslocamento da linha onde a instrução começou

    static bool tipoGenerico(const std::string& s) {
        return s == "list" || s == "dict" || s == "pair" || s == "cast";
    }

    bool dentroDeColchetes() const { return !abertos_.empty() && abertos_.back().c == '['; }

    // O pedaço anterior a ps[i] faz o próximo - ou ! ser unário (e o [ ou { abrir um literal)? No
    // começo da linha, vale o último pedaço da linha anterior (numa continuação, `+ x` é binário)
    bool esperaExpressao(const std::vector<Peca>& ps, std::size_t i) const {
        if (i == 0) {
            if (!tem_anterior_) return true;
            return esperaDepois(anterior_);
        }
        return esperaDepois(ps[i - 1]);
    }
    static bool esperaDepois(const Peca& a) {
        if (a.t == Peca::T::Palavra) return antesDeExpressao(a.s);
        if (a.t == Peca::T::Simbolo)
            return a.s != ")" && a.s != "]" && a.s != "}";
        return false;
    }

    std::string espaco(const std::vector<Peca>& ps, std::size_t i) {
        const Peca& a = ps[i - 1];
        const Peca& b = ps[i];
        const bool ga = angulo_generico_[i - 1], gb = angulo_generico_[i];
        if (b.t == Peca::T::Comentario) return std::string(static_cast<std::size_t>(b.espaco_antes < 1 ? 1 : b.espaco_antes), ' ');
        if (eh(a, ".") || eh(b, ".")) return "";
        if (eh(b, "?")) return "";   // Q1b: o opcional, colado ao tipo (string?, list<i32>?)
        if (eh(b, ",") || eh(b, ";") || eh(b, ")") || eh(b, "]")) return "";
        if (eh(a, "(") || eh(a, "[")) return "";
        if (eh(a, ",") || eh(a, ";")) return " ";
        // os < > de um tipo (list<i32>, cast<string>(x)): sem espaço
        if (gb && (eh(b, "<") || eh(b, ">"))) return "";
        if (ga && eh(a, "<")) return "";
        if (ga && eh(a, ">")) return eh(b, "(") || eh(b, ">") ? "" : " ";
        // : — numa fatia s[1:2], colado; no cabeçalho de uma classe, com espaços; num argumento
        // pelo nome (y: 4), depois
        if (eh(a, ":") || eh(b, ":")) {
            if (dentroDeColchetes()) return "";
            if (cabecalho_classe_) return " ";
            return eh(a, ":") ? " " : "";
        }
        if (eh(b, "(")) {
            if (a.t == Peca::T::Palavra) return controle(a.s) ? " " : "";
            if (eh(a, ")") || eh(a, "]")) return "";
            if (a.t == Peca::T::Simbolo && unario(ps, i - 1)) return "";
            return " ";
        }
        if (eh(b, "[")) {
            if ((a.t == Peca::T::Palavra && !antesDeExpressao(a.s)) || eh(a, ")") || eh(a, "]") ||
                a.t == Peca::T::Texto || (eh(a, "}") && expressaoDaChave_[i - 1]))
                return "";
            if (a.t == Peca::T::Simbolo && unario(ps, i - 1)) return "";
            return " ";
        }
        if (eh(a, "{")) return eh(b, "}") || expressaoDaChave_[i - 1] ? "" : " ";
        if (eh(b, "}")) return expressaoDaChave_[i] ? "" : " ";
        if (eh(b, "{")) return eh(a, "{") ? "" : " ";
        if (a.t == Peca::T::Simbolo && unario(ps, i - 1)) return "";
        return " ";
    }

    // ps[i] é um - , + ou ! unário?
    bool unario(const std::vector<Peca>& ps, std::size_t i) const {
        const Peca& p = ps[i];
        if (!(eh(p, "-") || eh(p, "+") || eh(p, "!"))) return false;
        if (eh(p, "!")) return true;
        return esperaExpressao(ps, i);
    }

    void anda(const std::vector<Peca>& ps, std::size_t i, int delta) {
        const Peca& p = ps[i];
        if (i == 0) {
            angulo_generico_.assign(ps.size(), false);
            cabecalho_classe_ = false;
            std::size_t k = 0;
            if (k < ps.size() && eh(ps[k], "pub")) ++k;
            if (k < ps.size() && eh(ps[k], "class")) cabecalho_classe_ = true;
            // as < > de tipo desta linha
            int prof = angulos_;
            for (std::size_t j = 0; j < ps.size(); ++j) {
                if (eh(ps[j], "<") && j > 0 && ps[j - 1].t == Peca::T::Palavra && tipoGenerico(ps[j - 1].s)) {
                    angulo_generico_[j] = true;
                    ++prof;
                } else if (eh(ps[j], ">") && prof > 0) {
                    angulo_generico_[j] = true;
                    --prof;
                }
            }
            angulos_ = prof;
            precalculaChaves(ps);
        }
        if (p.t != Peca::T::Simbolo) return;
        if (p.s == "{" || p.s == "(" || p.s == "[") {
            Aberto a;
            a.c = p.s[0];
            a.expressao = p.s == "{" && expressaoDaChave_[i];
            a.delta = delta;
            abertos_.push_back(a);
        } else if (p.s == "}" || p.s == ")" || p.s == "]") {
            const char abre = p.s == "}" ? '{' : p.s == ")" ? '(' : '[';
            if (abertos_.empty() || abertos_.back().c != abre)
                throw std::string("um '" + p.s + "' sem o '" + abre + "' correspondente");
            abertos_.pop_back();
        }
    }

    // Por peça: a chave (de abrir ou de fechar) é de um literal (dict, pair), não de um bloco?
    std::vector<bool> expressaoDaChave_;
    void precalculaChaves(const std::vector<Peca>& ps) {
        expressaoDaChave_.assign(ps.size(), false);
        std::vector<bool> pilha;
        for (const Aberto& a : abertos_)
            if (a.c == '{') pilha.push_back(a.expressao);
        for (std::size_t j = 0; j < ps.size(); ++j) {
            if (eh(ps[j], "{")) {
                bool e = false;
                if (j > 0) {
                    const Peca& a = ps[j - 1];
                    e = (a.t == Peca::T::Simbolo && a.s != ")" && a.s != "]" && a.s != "}" && a.s != "->" &&
                         a.s != ">" && a.s != "?") ||   // Q1b: fn f() -> T? { é um bloco
                        (a.t == Peca::T::Palavra && antesDeExpressao(a.s) && a.s != "else");   // else { é bloco
                    if (eh(a, ">") && angulo_generico_[j - 1]) e = false;
                    if (eh(a, "}") && !pilha.empty() && pilha.back()) e = true;   // }, { num literal
                } else {
                    e = !pilha.empty() && pilha.back();   // continuação de um literal
                }
                expressaoDaChave_[j] = e;
                pilha.push_back(e);
            } else if (eh(ps[j], "}")) {
                expressaoDaChave_[j] = !pilha.empty() && pilha.back();
                if (!pilha.empty()) pilha.pop_back();
            }
        }
    }
};

// Os tokens do léxico, para conferir que o formatador não mudou o programa
bool mesmosTokens(const std::string& a, const std::string& b) {
    Lexer la(a), lb(b);
    const std::vector<Token> ta = la.tokenize(), tb = lb.tokenize();
    if (ta.size() != tb.size()) return false;
    for (std::size_t i = 0; i < ta.size(); ++i)
        if (ta[i].type != tb[i].type || ta[i].lexeme != tb[i].lexeme) return false;
    return true;
}

} // namespace

bool formata(const std::string& fonte, std::string& saida, std::string& erro) {
    {
        Lexer l(fonte);
        for (const Token& t : l.tokenize())
            if (t.type == TokenType::UNKNOWN) {
                erro = "erro léxico na linha " + std::to_string(t.line) + ": " + t.lexeme;
                return false;
            }
    }
    const std::string bom = fonte.rfind("\xEF\xBB\xBF", 0) == 0 ? "\xEF\xBB\xBF" : "";
    const bool crlf = fonte.find("\r\n") != std::string::npos;
    Formatador f(crlf ? "\r\n" : "\n");
    std::string out;
    std::size_t ini = bom.size();
    int numero = 0;
    try {
        while (ini <= fonte.size()) {
            std::size_t fim = fonte.find('\n', ini);
            if (fim == std::string::npos) fim = fonte.size();
            std::string linha = fonte.substr(ini, fim - ini);
            if (!linha.empty() && linha.back() == '\r') linha.pop_back();
            ++numero;
            if (!(fim == fonte.size() && linha.empty())) f.linha(linha, out);
            ini = fim + 1;
        }
        f.termina();
    } catch (const std::string& motivo) {
        erro = motivo + " (linha " + std::to_string(numero) + ")";
        return false;
    }
    if (!mesmosTokens(fonte, bom + out)) {
        erro = "o formatador mudaria os tokens do programa (um defeito do formatador; o arquivo não foi mudado)";
        return false;
    }
    saida = bom + out;
    return true;
}

} // namespace cinza
