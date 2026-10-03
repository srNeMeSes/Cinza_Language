#ifndef CINZA_UTF8_H
#define CINZA_UTF8_H

#include <cstdint>
#include <string>

// ============================================================================
// UTF-8 — as strings da linguagem guardam UTF-8, e índices e tamanhos contam
// caracteres (code points), não bytes (A10). Regras num lugar só, usadas pelo
// interpretador, pela CVM e pela biblioteca padrão.
// ============================================================================

namespace cinza::utf8 {

// Byte de continuação (10xxxxxx): faz parte do caractere anterior
inline bool continuacao(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// Número de caracteres
inline std::int64_t tamanho(const std::string& s) {
    std::int64_t n = 0;
    for (char c : s) if (!continuacao(c)) ++n;
    return n;
}

// Bytes do caractere que começa em s[i]
inline std::size_t bytesDoCaractere(const std::string& s, std::size_t i) {
    std::size_t len = 1;
    while (i + len < s.size() && continuacao(s[i + len])) ++len;
    return len;
}

// Byte onde começa o caractere de índice `idx` (idx >= tamanho: s.size())
inline std::size_t byteDoCaractere(const std::string& s, std::int64_t idx) {
    std::size_t i = 0;
    for (std::int64_t k = 0; k < idx && i < s.size(); ++k) i += bytesDoCaractere(s, i);
    return i;
}

// Índice (em caracteres) do caractere que começa no byte `pos`
inline std::int64_t indiceDoByte(const std::string& s, std::size_t pos) {
    std::int64_t idx = 0;
    for (std::size_t i = 0; i < pos && i < s.size(); ++i) if (!continuacao(s[i])) ++idx;
    return idx;
}

// Texto → code points (para operar caractere a caractere)
inline std::u32string decodifica(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        char32_t cp = b;
        if      (b >= 0xF0) { len = 4; cp = b & 0x07; }
        else if (b >= 0xE0) { len = 3; cp = b & 0x0F; }
        else if (b >= 0xC0) { len = 2; cp = b & 0x1F; }
        for (std::size_t k = 1; k < len && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += len;
    }
    return out;
}

// Code points → texto
inline std::string codifica(const std::u32string& cps) {
    std::string out;
    out.reserve(cps.size());
    for (char32_t cp : cps) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
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
    return out;
}

} // namespace cinza::utf8

#endif // CINZA_UTF8_H
