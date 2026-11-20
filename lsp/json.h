#ifndef CINZA_LSP_JSON_H
#define CINZA_LSP_JSON_H

// Um JSON pequeno para o servidor de linguagem (Q6): o que o protocolo LSP usa (objetos, listas,
// textos, números, booleanos e null), sem depender de biblioteca de fora. Os textos são UTF-8;
// \uXXXX (com pares substitutos) vira UTF-8 na leitura, e na escrita só os caracteres de controle,
// a aspa e a barra são escapados.

#include <cctype>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cinza::json {

struct Valor {
    enum class T { Nulo, Bool, Numero, Texto, Lista, Objeto } t = T::Nulo;
    bool                                        b = false;
    double                                      n = 0;
    std::string                                 s;
    std::vector<Valor>                          lista;
    std::vector<std::pair<std::string, Valor>>  campos;

    Valor() = default;
    Valor(std::nullptr_t) {}
    Valor(bool v) : t(T::Bool), b(v) {}
    Valor(int v) : t(T::Numero), n(v) {}
    Valor(std::int64_t v) : t(T::Numero), n(static_cast<double>(v)) {}
    Valor(double v) : t(T::Numero), n(v) {}
    Valor(const char* v) : t(T::Texto), s(v) {}
    Valor(std::string v) : t(T::Texto), s(std::move(v)) {}

    static Valor objeto() { Valor v; v.t = T::Objeto; return v; }
    static Valor listaVazia() { Valor v; v.t = T::Lista; return v; }

    // objeto: o campo (nulo se não existir) e gravar um campo
    const Valor& operator[](const std::string& chave) const {
        static const Valor nulo;
        for (const auto& [k, v] : campos)
            if (k == chave) return v;
        return nulo;
    }
    Valor& set(const std::string& chave, Valor v) {
        for (auto& [k, x] : campos)
            if (k == chave) { x = std::move(v); return *this; }
        campos.emplace_back(chave, std::move(v));
        return *this;
    }
    void add(Valor v) { lista.push_back(std::move(v)); }
    bool nulo() const { return t == T::Nulo; }
    const std::string& texto() const { return s; }
    std::int64_t inteiro() const { return static_cast<std::int64_t>(n); }
};

class Leitor {
public:
    explicit Leitor(const std::string& s) : s_(s) {}
    Valor documento() {
        Valor v = valor();
        pula();
        if (i_ != s_.size()) throw std::runtime_error("JSON: texto depois do valor");
        return v;
    }

private:
    const std::string& s_;
    std::size_t        i_ = 0;

    void pula() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
    char olha() { pula(); if (i_ >= s_.size()) throw std::runtime_error("JSON: fim do texto"); return s_[i_]; }
    void espera(char c) { if (olha() != c) throw std::runtime_error(std::string("JSON: esperado '") + c + "'"); ++i_; }

    static void codifica(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    std::uint32_t hex4() {
        if (i_ + 4 > s_.size()) throw std::runtime_error("JSON: \\u incompleto");
        std::uint32_t v = static_cast<std::uint32_t>(std::stoul(s_.substr(i_, 4), nullptr, 16));
        i_ += 4;
        return v;
    }
    std::string texto() {
        espera('"');
        std::string out;
        while (true) {
            if (i_ >= s_.size()) throw std::runtime_error("JSON: texto sem fim");
            const char c = s_[i_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) throw std::runtime_error("JSON: escape no fim");
            const char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                        i_ += 2;
                        const std::uint32_t baixo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (baixo - 0xDC00);
                    }
                    codifica(out, cp);
                    break;
                }
                default: throw std::runtime_error("JSON: escape desconhecido");
            }
        }
    }
    Valor valor() {
        const char c = olha();
        if (c == '{') {
            ++i_;
            Valor o = Valor::objeto();
            if (olha() == '}') { ++i_; return o; }
            while (true) {
                std::string k = texto();
                espera(':');
                o.campos.emplace_back(std::move(k), valor());
                if (olha() == ',') { ++i_; continue; }
                espera('}');
                return o;
            }
        }
        if (c == '[') {
            ++i_;
            Valor l = Valor::listaVazia();
            if (olha() == ']') { ++i_; return l; }
            while (true) {
                l.lista.push_back(valor());
                if (olha() == ',') { ++i_; continue; }
                espera(']');
                return l;
            }
        }
        if (c == '"') return Valor(texto());
        if (s_.compare(i_, 4, "true") == 0) { i_ += 4; return Valor(true); }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return Valor(false); }
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return Valor(); }
        std::size_t fim = i_;
        while (fim < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[fim])) || s_[fim] == '-' ||
                                   s_[fim] == '+' || s_[fim] == '.' || s_[fim] == 'e' || s_[fim] == 'E'))
            ++fim;
        if (fim == i_) throw std::runtime_error("JSON: valor inválido");
        const double d = std::stod(s_.substr(i_, fim - i_));
        i_ = fim;
        return Valor(d);
    }
};

inline void escreveTexto(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* h = "0123456789abcdef";
                    out += "\\u00";
                    out += h[c >> 4];
                    out += h[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

inline void escreve(std::string& out, const Valor& v) {
    switch (v.t) {
        case Valor::T::Nulo: out += "null"; break;
        case Valor::T::Bool: out += v.b ? "true" : "false"; break;
        case Valor::T::Numero: {
            const auto i = static_cast<std::int64_t>(v.n);
            if (static_cast<double>(i) == v.n) out += std::to_string(i);
            else out += std::to_string(v.n);
            break;
        }
        case Valor::T::Texto: escreveTexto(out, v.s); break;
        case Valor::T::Lista: {
            out += '[';
            for (std::size_t k = 0; k < v.lista.size(); ++k) {
                if (k) out += ',';
                escreve(out, v.lista[k]);
            }
            out += ']';
            break;
        }
        case Valor::T::Objeto: {
            out += '{';
            for (std::size_t k = 0; k < v.campos.size(); ++k) {
                if (k) out += ',';
                escreveTexto(out, v.campos[k].first);
                out += ':';
                escreve(out, v.campos[k].second);
            }
            out += '}';
            break;
        }
    }
}

inline std::string texto(const Valor& v) {
    std::string out;
    escreve(out, v);
    return out;
}

} // namespace cinza::json

#endif // CINZA_LSP_JSON_H
