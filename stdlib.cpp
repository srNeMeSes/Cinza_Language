#include "natives.h"
#include "runtime_error.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

namespace cinza {

// ============================================================================
// BIBLIOTECA PADRÃO (Fase 7)
//
// Módulos nativos, usados com import: `import Strings as st;` → st.upper(s).
// Cada função recebe os argumentos já checados pelo semântico (tipos da
// assinatura) e lança RuntimeError("Tipo: mensagem") sem linha; o executor
// completa com a posição da chamada.
// ============================================================================

namespace {

using Args = std::span<const Value>;
namespace fs = std::filesystem;

[[noreturn]] void falha(const std::string& tipo_e_mensagem) {
    throw RuntimeError(tipo_e_mensagem);
}

// ── UTF-8 ──────────────────────────────────────────────────────────────────
// As strings guardam UTF-8; índices e tamanhos da linguagem contam caracteres
// (code points), como string.size() (A10).

std::u32string decodifica(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        char32_t cp = b;
        if      (b >= 0xF0) { len = 4; cp = b & 0x07; }
        else if (b >= 0xE0) { len = 3; cp = b & 0x0F; }
        else if (b >= 0xC0) { len = 2; cp = b & 0x1F; }
        for (size_t k = 1; k < len && i + k < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string codifica(const std::u32string& cps) {
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

// Maiúsculas/minúsculas: ASCII e Latin-1 (á é ç ã õ ... ↔ Á É Ç Ã Õ ...)
char32_t paraMaiuscula(char32_t c) {
    if (c >= U'a' && c <= U'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    return c;
}
char32_t paraMinuscula(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}

// Número (int ou decimal) como double
double numero(const Value& v) {
    return v.kind() == Value::Kind::INT ? static_cast<double>(v.asInt()) : v.asDecimal();
}

// decimal → int, com OverflowError fora da faixa de int
Value decimalParaInt(double d, const char* fn) {
    // 2^63 é o primeiro double fora da faixa (int64 vai até 2^63 - 1)
    if (!std::isfinite(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0)
        falha(std::string("OverflowError: resultado de '") + fn + "' fora do intervalo de int");
    return Value(static_cast<std::int64_t>(d));
}

// ============================================================================
// Strings
// ============================================================================

Value strUpper(Args a) {
    std::u32string s = decodifica(a[0].asString());
    for (char32_t& c : s) c = paraMaiuscula(c);
    return Value(codifica(s));
}

Value strLower(Args a) {
    std::u32string s = decodifica(a[0].asString());
    for (char32_t& c : s) c = paraMinuscula(c);
    return Value(codifica(s));
}

Value strTrim(Args a) {
    const std::string& s = a[0].asString();
    const char* brancos = " \t\n\r\f\v";
    const size_t ini = s.find_first_not_of(brancos);
    if (ini == std::string::npos) return Value(std::string());
    const size_t fim = s.find_last_not_of(brancos);
    return Value(s.substr(ini, fim - ini + 1));
}

Value strSplit(Args a) {
    const std::string& s   = a[0].asString();
    const std::string& sep = a[1].asString();
    if (sep.empty()) falha("ValueError: o separador de 'split' não pode ser vazio");
    std::vector<Value> partes;
    size_t ini = 0;
    for (size_t pos; (pos = s.find(sep, ini)) != std::string::npos; ini = pos + sep.size())
        partes.emplace_back(s.substr(ini, pos - ini));
    partes.emplace_back(s.substr(ini));
    return makeList(std::move(partes));
}

Value strJoin(Args a) {
    const auto& elems = a[0].asList()->elements;
    const std::string& sep = a[1].asString();
    std::string out;
    for (size_t i = 0; i < elems.size(); ++i) {
        if (i > 0) out += sep;
        out += elems[i].asString();
    }
    return Value(std::move(out));
}

Value strContains(Args a) {
    return Value(a[0].asString().find(a[1].asString()) != std::string::npos);
}

Value strReplace(Args a) {
    const std::string& s    = a[0].asString();
    const std::string& de   = a[1].asString();
    const std::string& para = a[2].asString();
    if (de.empty()) falha("ValueError: o trecho procurado em 'replace' não pode ser vazio");
    std::string out;
    size_t ini = 0;
    for (size_t pos; (pos = s.find(de, ini)) != std::string::npos; ini = pos + de.size())
        out += s.substr(ini, pos - ini) + para;
    out += s.substr(ini);
    return Value(std::move(out));
}

// substr(s, inicio, tamanho): em caracteres; o trecho precisa caber na string
Value strSubstr(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    const std::int64_t ini = a[1].asInt();
    const std::int64_t len = a[2].asInt();
    const auto n = static_cast<std::int64_t>(s.size());
    if (ini < 0 || len < 0 || ini > n || len > n - ini)
        falha("IndexError: 'substr(" + std::to_string(ini) + ", " + std::to_string(len) +
              ")' fora dos limites de uma string de tamanho " + std::to_string(n));
    return Value(codifica(s.substr(static_cast<size_t>(ini), static_cast<size_t>(len))));
}

// find(s, trecho): índice (em caracteres) da primeira ocorrência, ou -1
Value strFind(Args a) {
    const std::string& s = a[0].asString();
    const size_t pos = s.find(a[1].asString());
    if (pos == std::string::npos) return Value(std::int64_t{-1});
    std::int64_t idx = 0;
    for (size_t i = 0; i < pos; ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++idx;
    return Value(idx);
}

Value strStartsWith(Args a) {
    const std::string& s = a[0].asString();
    const std::string& p = a[1].asString();
    return Value(s.compare(0, p.size(), p) == 0);
}

// ============================================================================
// Files — cada função faz a operação inteira (não há arquivo "aberto").
// Caminhos: o texto da linguagem é UTF-8; no Windows, std::string viraria
// caminho pela página de código ANSI, então a conversão é sempre explícita.
// Caminhos devolvidos usam '/' em qualquer sistema. Falhas: IOError.
// ============================================================================

fs::path caminho(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string texto(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

Value textoValor(const fs::path& p) { return Value(texto(p)); }

[[noreturn]] void falhaIO(const std::string& motivo, const std::string& c) {
    falha("IOError: " + motivo + " '" + c + "'");
}

std::string lerArquivo(const std::string& c) {
    std::ifstream f(caminho(c), std::ios::binary);
    if (!f.is_open()) falha("IOError: não foi possível abrir '" + c + "' para leitura");
    std::ostringstream conteudo;
    conteudo << f.rdbuf();
    return conteudo.str();
}

void gravarArquivo(const std::string& c, const std::string& conteudo, std::ios::openmode modo) {
    std::ofstream f(caminho(c), std::ios::binary | modo);
    if (!f.is_open()) falha("IOError: não foi possível abrir '" + c + "' para escrita");
    f << conteudo;
    if (!f) falha("IOError: falha ao escrever em '" + c + "'");
}

Value filesRead(Args a) { return Value(lerArquivo(a[0].asString())); }

// lines: sem o fim de linha (\n ou \r\n); o \n final não gera linha vazia
Value filesLines(Args a) {
    const std::string s = lerArquivo(a[0].asString());
    std::vector<Value> linhas;
    size_t ini = 0;
    while (ini < s.size()) {
        size_t fim = s.find('\n', ini);
        if (fim == std::string::npos) fim = s.size();
        size_t corte = fim;
        if (corte > ini && s[corte - 1] == '\r') --corte;
        linhas.emplace_back(s.substr(ini, corte - ini));
        ini = fim + 1;
    }
    return makeList(std::move(linhas));
}

Value filesWrite(Args a) {
    gravarArquivo(a[0].asString(), a[1].asString(), std::ios::trunc);
    return Value();
}

Value filesAppend(Args a) {
    gravarArquivo(a[0].asString(), a[1].asString(), std::ios::app);
    return Value();
}

Value filesWriteLines(Args a) {
    std::string conteudo;
    for (const auto& l : a[1].asList()->elements) { conteudo += l.asString(); conteudo += '\n'; }
    gravarArquivo(a[0].asString(), conteudo, std::ios::trunc);
    return Value();
}

Value filesAppendLine(Args a) {
    gravarArquivo(a[0].asString(), a[1].asString() + "\n", std::ios::app);
    return Value();
}

Value filesExists(Args a) {
    std::error_code ec;
    return Value(fs::exists(caminho(a[0].asString()), ec));
}

Value filesIsFile(Args a) {
    std::error_code ec;
    return Value(fs::is_regular_file(caminho(a[0].asString()), ec));
}

Value filesIsDir(Args a) {
    std::error_code ec;
    return Value(fs::is_directory(caminho(a[0].asString()), ec));
}

// Exige um arquivo existente (size, copy, delete)
fs::path arquivoExistente(const std::string& c) {
    const fs::path p = caminho(c);
    std::error_code ec;
    if (!fs::exists(p, ec))          falhaIO("não existe:", c);
    if (!fs::is_regular_file(p, ec)) falhaIO("não é um arquivo:", c);
    return p;
}

// Exige uma pasta existente (list_dir, delete_dir, delete_tree)
fs::path pastaExistente(const std::string& c) {
    const fs::path p = caminho(c);
    std::error_code ec;
    if (!fs::exists(p, ec))       falhaIO("não existe:", c);
    if (!fs::is_directory(p, ec)) falhaIO("não é uma pasta:", c);
    return p;
}

// size: em bytes (não em caracteres)
Value filesSize(Args a) {
    const fs::path p = arquivoExistente(a[0].asString());
    std::error_code ec;
    const auto n = fs::file_size(p, ec);
    if (ec) falhaIO("não foi possível ler o tamanho de", a[0].asString());
    return Value(static_cast<std::int64_t>(n));
}

// Destino de copy/move: existente só com substituir = true, e só se for arquivo
void confereDestino(const fs::path& d, const std::string& c, bool substituir) {
    std::error_code ec;
    if (!fs::exists(d, ec)) return;
    if (!substituir)                 falhaIO("o destino já existe:", c);
    if (!fs::is_regular_file(d, ec)) falhaIO("o destino é uma pasta, não pode ser substituído:", c);
}

Value filesCopy(Args a) {
    const fs::path o = arquivoExistente(a[0].asString());
    const fs::path d = caminho(a[1].asString());
    confereDestino(d, a[1].asString(), a[2].asBool());
    std::error_code ec;
    fs::copy_file(o, d, fs::copy_options::overwrite_existing, ec);
    if (ec) falhaIO("não foi possível copiar para", a[1].asString());
    return Value();
}

// move: arquivo ou pasta; também renomeia
Value filesMove(Args a) {
    const fs::path o = caminho(a[0].asString());
    std::error_code ec;
    if (!fs::exists(o, ec)) falhaIO("não existe:", a[0].asString());
    const fs::path d = caminho(a[1].asString());
    confereDestino(d, a[1].asString(), a[2].asBool());
    if (fs::exists(d, ec)) fs::remove(d, ec);   // arquivo, com substituir = true
    fs::rename(o, d, ec);
    if (ec) falhaIO("não foi possível mover para", a[1].asString());
    return Value();
}

Value filesDelete(Args a) {
    const std::string& c = a[0].asString();
    const fs::path p = caminho(c);
    std::error_code ec;
    if (!fs::exists(p, ec))          falhaIO("não existe:", c);
    if (fs::is_directory(p, ec))     falhaIO("é uma pasta (use delete_dir ou delete_tree):", c);
    if (!fs::remove(p, ec) || ec)    falhaIO("não foi possível apagar", c);
    return Value();
}

// make_dir: cria também as pastas intermediárias; já existir não é erro
Value filesMakeDir(Args a) {
    const std::string& c = a[0].asString();
    const fs::path p = caminho(c);
    std::error_code ec;
    if (fs::is_directory(p, ec)) return Value();
    if (fs::exists(p, ec)) falhaIO("já existe um arquivo com esse nome:", c);
    fs::create_directories(p, ec);
    if (ec) falhaIO("não foi possível criar a pasta", c);
    return Value();
}

// list_dir: só os nomes, em ordem alfabética
Value filesListDir(Args a) {
    const fs::path p = pastaExistente(a[0].asString());
    std::vector<std::string> nomes;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(p, ec)) nomes.push_back(texto(e.path().filename()));
    if (ec) falhaIO("não foi possível listar", a[0].asString());
    std::sort(nomes.begin(), nomes.end());
    std::vector<Value> out;
    out.reserve(nomes.size());
    for (auto& n : nomes) out.emplace_back(std::move(n));
    return makeList(std::move(out));
}

// delete_dir: só pasta vazia
Value filesDeleteDir(Args a) {
    const std::string& c = a[0].asString();
    const fs::path p = pastaExistente(c);
    std::error_code ec;
    if (!fs::is_empty(p, ec)) falhaIO("a pasta não está vazia (use delete_tree):", c);
    if (!fs::remove(p, ec) || ec) falhaIO("não foi possível apagar", c);
    return Value();
}

// delete_tree: a pasta e todo o conteúdo
Value filesDeleteTree(Args a) {
    const fs::path p = pastaExistente(a[0].asString());
    std::error_code ec;
    fs::remove_all(p, ec);
    if (ec) falhaIO("não foi possível apagar", a[0].asString());
    return Value();
}

Value filesCurrentDir(Args) {
    std::error_code ec;
    const fs::path p = fs::current_path(ec);
    if (ec) falha("IOError: não foi possível obter a pasta atual");
    return textoValor(p);
}

// Caminhos: só texto, sem tocar no disco
Value filesJoin(Args a)      { return textoValor(caminho(a[0].asString()) / caminho(a[1].asString())); }
Value filesName(Args a)      { return textoValor(caminho(a[0].asString()).filename()); }
Value filesExtension(Args a) { return textoValor(caminho(a[0].asString()).extension()); }
Value filesParent(Args a)    { return textoValor(caminho(a[0].asString()).parent_path()); }

Value filesAbsolute(Args a) {
    std::error_code ec;
    const fs::path p = fs::absolute(caminho(a[0].asString()), ec);
    if (ec) falhaIO("não foi possível obter o caminho completo de", a[0].asString());
    return textoValor(p.lexically_normal());
}

// ============================================================================
// Math
// ============================================================================

Value mathSqrt(Args a) {
    const double x = a[0].asDecimal();
    if (x < 0) falha("ValueError: 'sqrt' de número negativo");
    return Value(std::sqrt(x));
}

Value mathPow(Args a) {
    const double r = std::pow(a[0].asDecimal(), a[1].asDecimal());
    if (std::isnan(r)) falha("ValueError: 'pow' sem resultado real");
    if (std::isinf(r)) falha("OverflowError: resultado de 'pow' grande demais");
    return Value(r);
}

Value mathAbs(Args a) {
    if (a[0].kind() == Value::Kind::INT) {
        const std::int64_t v = a[0].asInt();
        if (v == std::numeric_limits<std::int64_t>::min())
            falha("OverflowError: 'abs' do menor int não cabe em int");
        return Value(v < 0 ? -v : v);
    }
    return Value(std::fabs(a[0].asDecimal()));
}

Value mathFloor(Args a) { return decimalParaInt(std::floor(a[0].asDecimal()), "floor"); }
Value mathCeil (Args a) { return decimalParaInt(std::ceil (a[0].asDecimal()), "ceil"); }
// round: metade se afasta do zero (2.5 → 3, -2.5 → -3)
Value mathRound(Args a) { return decimalParaInt(std::round(a[0].asDecimal()), "round"); }

Value mathMin(Args a) { return numero(a[1]) < numero(a[0]) ? a[1] : a[0]; }
Value mathMax(Args a) { return numero(a[1]) > numero(a[0]) ? a[1] : a[0]; }

Value mathSin(Args a) { return Value(std::sin(a[0].asDecimal())); }
Value mathCos(Args a) { return Value(std::cos(a[0].asDecimal())); }

// log(x): logaritmo natural
Value mathLog(Args a) {
    const double x = a[0].asDecimal();
    if (x <= 0) falha("ValueError: 'log' só é definido para números positivos");
    return Value(std::log(x));
}

// Resultado decimal de uma função: a linguagem nunca produz infinito nem NaN
Value decimalFinito(double r, const char* fn) {
    if (std::isnan(r)) falha(std::string("ValueError: '") + fn + "' sem resultado real");
    if (std::isinf(r)) falha(std::string("OverflowError: resultado de '") + fn + "' grande demais");
    return Value(r);
}

Value mathTan(Args a)  { return decimalFinito(std::tan(a[0].asDecimal()), "tan"); }
Value mathAtan(Args a) { return Value(std::atan(a[0].asDecimal())); }
Value mathAtan2(Args a) { return Value(std::atan2(a[0].asDecimal(), a[1].asDecimal())); }

// asin/acos: só de -1 a 1
Value mathAsin(Args a) {
    const double x = a[0].asDecimal();
    if (x < -1 || x > 1) falha("ValueError: 'asin' só é definido de -1 a 1");
    return Value(std::asin(x));
}
Value mathAcos(Args a) {
    const double x = a[0].asDecimal();
    if (x < -1 || x > 1) falha("ValueError: 'acos' só é definido de -1 a 1");
    return Value(std::acos(x));
}

constexpr double PI = 3.14159265358979323846;
Value mathDegrees(Args a) { return decimalFinito(a[0].asDecimal() * (180.0 / PI), "degrees"); }
Value mathRadians(Args a) { return Value(a[0].asDecimal() * (PI / 180.0)); }

Value mathSinh(Args a) { return decimalFinito(std::sinh(a[0].asDecimal()), "sinh"); }
Value mathCosh(Args a) { return decimalFinito(std::cosh(a[0].asDecimal()), "cosh"); }
Value mathTanh(Args a) { return Value(std::tanh(a[0].asDecimal())); }

Value mathCbrt(Args a)  { return Value(std::cbrt(a[0].asDecimal())); }
Value mathExp(Args a)   { return decimalFinito(std::exp(a[0].asDecimal()), "exp"); }
Value mathHypot(Args a) { return decimalFinito(std::hypot(a[0].asDecimal(), a[1].asDecimal()), "hypot"); }

Value mathLog10(Args a) {
    const double x = a[0].asDecimal();
    if (x <= 0) falha("ValueError: 'log10' só é definido para números positivos");
    return Value(std::log10(x));
}
Value mathLog2(Args a) {
    const double x = a[0].asDecimal();
    if (x <= 0) falha("ValueError: 'log2' só é definido para números positivos");
    return Value(std::log2(x));
}
// log_base(x, base): logaritmo de x na base dada
Value mathLogBase(Args a) {
    const double x = a[0].asDecimal(), base = a[1].asDecimal();
    if (x <= 0) falha("ValueError: 'log_base' só é definido para números positivos");
    if (base <= 0 || base == 1) falha("ValueError: a base de 'log_base' precisa ser positiva e diferente de 1");
    return Value(std::log(x) / std::log(base));
}

// trunc: corta as casas, em direção ao zero
Value mathTrunc(Args a) { return decimalParaInt(std::trunc(a[0].asDecimal()), "trunc"); }

// round_to(x, casas): arredonda o número como ele é escrito (a forma decimal
// mais curta, a mesma do print), não o valor binário — round_to(2.675, 2) é
// 2.68 (em binário 2.675 é 2.67499999...). Metade se afasta do zero, como round.
Value mathRoundTo(Args a) {
    const double x = a[0].asDecimal();
    const std::int64_t casas = a[1].asInt();
    if (casas < 0) falha("ValueError: o número de casas de 'round_to' não pode ser negativo");
    char buf[512];
    auto [fim, ec] = std::to_chars(buf, buf + sizeof buf, std::fabs(x), std::chars_format::fixed);
    if (ec != std::errc()) return Value(x);
    std::string s(buf, fim);
    const size_t ponto = s.find('.');
    if (ponto == std::string::npos || static_cast<std::int64_t>(s.size() - ponto - 1) <= casas)
        return Value(x);   // já tem casas de menos
    std::string digitos = s.substr(0, ponto) + s.substr(ponto + 1, static_cast<size_t>(casas));
    const bool sobe = s[ponto + 1 + static_cast<size_t>(casas)] >= '5';
    if (sobe) {   // soma 1 na última casa mantida, com vai-um
        size_t i = digitos.size();
        while (i > 0 && digitos[i - 1] == '9') digitos[--i] = '0';
        if (i == 0) digitos.insert(digitos.begin(), '1');
        else        ++digitos[i - 1];
    }
    const size_t int_len = digitos.size() - static_cast<size_t>(casas);
    std::string r = digitos.substr(0, int_len);
    if (casas > 0) r += "." + digitos.substr(int_len);
    double v = 0;
    std::from_chars(r.data(), r.data() + r.size(), v);
    if (v == 0) return Value(0.0);   // sem -0
    return decimalFinito(x < 0 ? -v : v, "round_to");
}

// Magnitude de um int como unsigned (|menor int| não cabe em int)
std::uint64_t magnitude(std::int64_t v) {
    return v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

std::uint64_t mdcU(std::uint64_t x, std::uint64_t y) {
    while (y != 0) { const std::uint64_t r = x % y; x = y; y = r; }
    return x;
}

// gcd: máximo divisor comum, sempre >= 0; gcd(0, 0) = 0
Value mathGcd(Args a) {
    const std::uint64_t g = mdcU(magnitude(a[0].asInt()), magnitude(a[1].asInt()));
    if (g > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        falha("OverflowError: resultado de 'gcd' fora do intervalo de int");
    return Value(static_cast<std::int64_t>(g));
}

// lcm: mínimo múltiplo comum, sempre >= 0; com um zero, 0
Value mathLcm(Args a) {
    const std::uint64_t x = magnitude(a[0].asInt()), y = magnitude(a[1].asInt());
    if (x == 0 || y == 0) return Value(std::int64_t{0});
    std::uint64_t r;
    if (__builtin_mul_overflow(x / mdcU(x, y), y, &r) ||
        r > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        falha("OverflowError: resultado de 'lcm' fora do intervalo de int");
    return Value(static_cast<std::int64_t>(r));
}

Value mathIsEven(Args a) { return Value(a[0].asInt() % 2 == 0); }
Value mathIsOdd(Args a)  { return Value(a[0].asInt() % 2 != 0); }

// sign: -1, 0 ou 1
Value mathSign(Args a) {
    const double x = numero(a[0]);
    return Value(std::int64_t{x > 0 ? 1 : (x < 0 ? -1 : 0)});
}

// clamp(x, min, max): x limitado ao intervalo
Value mathClamp(Args a) {
    if (numero(a[1]) > numero(a[2]))
        falha("ValueError: em 'clamp' o mínimo não pode ser maior que o máximo");
    if (numero(a[0]) < numero(a[1])) return a[1];
    if (numero(a[0]) > numero(a[2])) return a[2];
    return a[0];
}

// factorial(n): até 20! (o maior que cabe em int)
Value mathFactorial(Args a) {
    const std::int64_t n = a[0].asInt();
    if (n < 0)  falha("ValueError: 'factorial' de número negativo");
    if (n > 20) falha("OverflowError: 'factorial(" + std::to_string(n) + ")' não cabe em int (o máximo é 20)");
    std::int64_t r = 1;
    for (std::int64_t i = 2; i <= n; ++i) r *= i;
    return Value(r);
}

// is_prime: Miller-Rabin determinístico para 64 bits (estas bases bastam)
__extension__ typedef unsigned __int128 u128;   // extensão do GCC/Clang, de propósito
std::uint64_t mulMod(std::uint64_t x, std::uint64_t y, std::uint64_t m) {
    return static_cast<std::uint64_t>(static_cast<u128>(x) * y % m);
}
std::uint64_t powMod(std::uint64_t b, std::uint64_t e, std::uint64_t m) {
    std::uint64_t r = 1;
    for (b %= m; e; e >>= 1, b = mulMod(b, b, m))
        if (e & 1) r = mulMod(r, b, m);
    return r;
}
Value mathIsPrime(Args a) {
    const std::int64_t v = a[0].asInt();
    if (v < 2) return Value(false);
    const auto n = static_cast<std::uint64_t>(v);
    for (std::uint64_t p : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        if (n == p) return Value(true);
        if (n % p == 0) return Value(false);
    }
    std::uint64_t d = n - 1;
    int s = 0;
    while (d % 2 == 0) { d /= 2; ++s; }
    for (std::uint64_t b : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        std::uint64_t x = powMod(b, d, n);
        if (x == 1 || x == n - 1) continue;
        bool composto = true;
        for (int r = 1; r < s; ++r) {
            x = mulMod(x, x, n);
            if (x == n - 1) { composto = false; break; }
        }
        if (composto) return Value(false);
    }
    return Value(true);
}

// is_close(a, b): iguais a menos de erro de arredondamento — diferença
// relativa até 1e-9, ou absoluta até 1e-12 (perto de zero)
Value mathIsClose(Args a) {
    const double x = a[0].asDecimal(), y = a[1].asDecimal();
    const double dif = std::fabs(x - y);
    return Value(dif <= 1e-9 * std::max(std::fabs(x), std::fabs(y)) || dif <= 1e-12);
}

// ============================================================================
// Random — estado único do processo; seed(n) torna a sequência reproduzível
// ============================================================================

std::mt19937_64& gerador() {
    static std::mt19937_64 g{std::random_device{}()};
    return g;
}

Value randSeed(Args a) {
    gerador().seed(static_cast<std::uint64_t>(a[0].asInt()));
    return Value();
}

// int(a, b): inteiro de a até b, os dois inclusive
Value randInt(Args a) {
    const std::int64_t lo = a[0].asInt();
    const std::int64_t hi = a[1].asInt();
    if (lo > hi)
        falha("ValueError: 'int(" + std::to_string(lo) + ", " + std::to_string(hi) +
              ")': o início é maior que o fim");
    std::uniform_int_distribution<std::int64_t> d(lo, hi);
    return Value(d(gerador()));
}

// decimal(): de 0.0 (inclusive) até 1.0 (exclusive)
Value randDecimal(Args) {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return Value(d(gerador()));
}

Value randChoice(Args a) {
    const auto& elems = a[0].asList()->elements;
    if (elems.empty()) falha("IndexError: 'choice' de uma lista vazia");
    std::uniform_int_distribution<size_t> d(0, elems.size() - 1);
    return elems[d(gerador())];
}

// ============================================================================
// Convert
// ============================================================================

// to_int(string): só sinal opcional e dígitos, sem espaços
Value convToInt(Args a) {
    const std::string& s = a[0].asString();
    const char* fim = s.data() + s.size();
    // from_chars aceita '-' mas não '+'; "+-5" não é válido
    const size_t pula = (!s.empty() && s[0] == '+') ? 1 : 0;
    if (s.empty() || (pula && (s.size() == 1 || s[1] == '-')))
        falha("ValueError: '" + s + "' não é um int válido");
    std::int64_t v = 0;
    auto [ptr, ec] = std::from_chars(s.data() + pula, fim, v);
    if (ptr != fim || ec == std::errc::invalid_argument)
        falha("ValueError: '" + s + "' não é um int válido");
    if (ec == std::errc::result_out_of_range)
        falha("ValueError: '" + s + "' está fora do intervalo de int");
    return Value(v);
}

// to_decimal(string): [sinal] dígitos [. dígitos] [e [sinal] dígitos]
Value convToDecimal(Args a) {
    const std::string& s = a[0].asString();
    size_t i = 0;
    auto digitos = [&]() {
        const size_t antes = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        return i > antes;
    };
    bool ok = true;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    ok = digitos();
    if (ok && i < s.size() && s[i] == '.') { ++i; ok = digitos(); }
    if (ok && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        ok = digitos();
    }
    if (!ok || i != s.size()) falha("ValueError: '" + s + "' não é um decimal válido");
    const char* ini = s.data() + (s[0] == '+' ? 1 : 0);
    double v = 0;
    std::from_chars(ini, s.data() + s.size(), v);
    if (!std::isfinite(v)) falha("ValueError: '" + s + "' está fora do intervalo de decimal");
    return Value(v);
}

Value convToString(Args a) { return Value(a[0].toString()); }

// to_bool(string): só "true" ou "false"
Value convToBool(Args a) {
    const std::string& s = a[0].asString();
    if (s == "true")  return Value(true);
    if (s == "false") return Value(false);
    falha("ValueError: '" + s + "' não é um bool válido (use \"true\" ou \"false\")");
}

// ============================================================================
// Lists
// ============================================================================

// Ordem de valores comparáveis (int, decimal ou string; a lista é homogênea)
bool menor(const Value& x, const Value& y) {
    if (x.kind() == Value::Kind::STRING) return x.asString() < y.asString();
    return numero(x) < numero(y);
}

Value listSort(Args a) {
    auto& elems = a[0].asList()->elements;
    std::stable_sort(elems.begin(), elems.end(), menor);
    return Value();
}

Value listReverse(Args a) {
    auto& elems = a[0].asList()->elements;
    std::reverse(elems.begin(), elems.end());
    return Value();
}

Value listContains(Args a) {
    const auto& elems = a[0].asList()->elements;
    return Value(std::find(elems.begin(), elems.end(), a[1]) != elems.end());
}

Value listIndexOf(Args a) {
    const auto& elems = a[0].asList()->elements;
    auto it = std::find(elems.begin(), elems.end(), a[1]);
    return Value(it == elems.end() ? std::int64_t{-1}
                                   : static_cast<std::int64_t>(it - elems.begin()));
}

// slice(l, inicio, fim): nova lista com os elementos de inicio até fim (exclusive)
Value listSlice(Args a) {
    const auto& elems = a[0].asList()->elements;
    const std::int64_t ini = a[1].asInt();
    const std::int64_t fim = a[2].asInt();
    const auto n = static_cast<std::int64_t>(elems.size());
    if (ini < 0 || fim < ini || fim > n)
        falha("IndexError: 'slice(" + std::to_string(ini) + ", " + std::to_string(fim) +
              ")' fora dos limites de uma lista de tamanho " + std::to_string(n));
    return makeList(std::vector<Value>(elems.begin() + ini, elems.begin() + fim));
}

// sum(l): soma de int (com OverflowError) ou de decimal; lista vazia → 0
// (o executor ajusta para 0.0 quando o tipo do resultado é decimal)
Value listSum(Args a) {
    const auto& elems = a[0].asList()->elements;
    if (!elems.empty() && elems[0].kind() == Value::Kind::DECIMAL) {
        double total = 0;
        for (const auto& v : elems) total += v.asDecimal();
        if (!std::isfinite(total)) falha("OverflowError: a soma de 'sum' excede a faixa de decimal");
        return Value(total);
    }
    std::int64_t total = 0;
    for (const auto& v : elems)
        if (__builtin_add_overflow(total, v.asInt(), &total))
            falha("OverflowError: a soma de 'sum' não cabe em int");
    return Value(total);
}

std::vector<NativeModule> criaModulos() {
    auto& t = TypeContext::instance();
    const TypeRef I = t.intType(), D = t.decimalType(), S = t.stringType(),
                  B = t.boolType(), V = t.voidType();
    const TypeRef T   = t.typeVar("T");
    const TypeRef N   = t.typeVar("número");
    const TypeRef C   = t.typeVar("comparável");
    const TypeRef ANY = t.typeVar("any");

    std::vector<NativeModule> m;

    m.push_back({"Strings", {
        {"upper",       {S},       S,           strUpper},
        {"lower",       {S},       S,           strLower},
        {"trim",        {S},       S,           strTrim},
        {"split",       {S, S},    t.list(S),   strSplit},
        {"join",        {t.list(S), S}, S,      strJoin},
        {"contains",    {S, S},    B,           strContains},
        {"replace",     {S, S, S}, S,           strReplace},
        {"substr",      {S, I, I}, S,           strSubstr},
        {"find",        {S, S},    I,           strFind},
        {"starts_with", {S, S},    B,           strStartsWith},
    }, {}});

    m.push_back({"Files", {
        {"read",        {S},             S,         filesRead},
        {"lines",       {S},             t.list(S), filesLines},
        {"write",       {S, S},          V,         filesWrite},
        {"append",      {S, S},          V,         filesAppend},
        {"write_lines", {S, t.list(S)},  V,         filesWriteLines},
        {"append_line", {S, S},          V,         filesAppendLine},
        {"exists",      {S},             B,         filesExists},
        {"is_file",     {S},             B,         filesIsFile},
        {"is_dir",      {S},             B,         filesIsDir},
        {"size",        {S},             I,         filesSize},
        {"copy",        {S, S, B},       V,         filesCopy},
        {"move",        {S, S, B},       V,         filesMove},
        {"delete",      {S},             V,         filesDelete},
        {"make_dir",    {S},             V,         filesMakeDir},
        {"list_dir",    {S},             t.list(S), filesListDir},
        {"delete_dir",  {S},             V,         filesDeleteDir},
        {"delete_tree", {S},             V,         filesDeleteTree},
        {"current_dir", {},              S,         filesCurrentDir},
        {"join",        {S, S},          S,         filesJoin},
        {"name",        {S},             S,         filesName},
        {"extension",   {S},             S,         filesExtension},
        {"parent",      {S},             S,         filesParent},
        {"absolute",    {S},             S,         filesAbsolute},
    }, {}});

    m.push_back({"Math", {
        {"sqrt",  {D},    D, mathSqrt},
        {"pow",   {D, D}, D, mathPow},
        {"abs",   {N},    N, mathAbs},
        {"floor", {D},    I, mathFloor},
        {"ceil",  {D},    I, mathCeil},
        {"round", {D},    I, mathRound},
        {"min",   {N, N}, N, mathMin},
        {"max",   {N, N}, N, mathMax},
        {"sin",   {D},    D, mathSin},
        {"cos",   {D},    D, mathCos},
        {"log",   {D},    D, mathLog},
        {"tan",       {D},       D, mathTan},
        {"asin",      {D},       D, mathAsin},
        {"acos",      {D},       D, mathAcos},
        {"atan",      {D},       D, mathAtan},
        {"atan2",     {D, D},    D, mathAtan2},
        {"degrees",   {D},       D, mathDegrees},
        {"radians",   {D},       D, mathRadians},
        {"sinh",      {D},       D, mathSinh},
        {"cosh",      {D},       D, mathCosh},
        {"tanh",      {D},       D, mathTanh},
        {"cbrt",      {D},       D, mathCbrt},
        {"exp",       {D},       D, mathExp},
        {"log10",     {D},       D, mathLog10},
        {"log2",      {D},       D, mathLog2},
        {"log_base",  {D, D},    D, mathLogBase},
        {"hypot",     {D, D},    D, mathHypot},
        {"trunc",     {D},       I, mathTrunc},
        {"round_to",  {D, I},    D, mathRoundTo},
        {"gcd",       {I, I},    I, mathGcd},
        {"lcm",       {I, I},    I, mathLcm},
        {"is_even",   {I},       B, mathIsEven},
        {"is_odd",    {I},       B, mathIsOdd},
        {"sign",      {N},       I, mathSign},
        {"clamp",     {N, N, N}, N, mathClamp},
        {"factorial", {I},       I, mathFactorial},
        {"is_prime",  {I},       B, mathIsPrime},
        {"is_close",  {D, D},    B, mathIsClose},
    }, {
        {"pi",  D, Value(PI)},
        {"e",   D, Value(2.71828182845904523536)},
        {"tau", D, Value(2 * PI)},
    }});

    m.push_back({"Random", {
        {"seed",    {I},         V, randSeed},
        {"int",     {I, I},      I, randInt},
        {"decimal", {},          D, randDecimal},
        {"choice",  {t.list(T)}, T, randChoice},
    }, {}});

    m.push_back({"Convert", {
        {"to_int",     {S},   I, convToInt},
        {"to_decimal", {S},   D, convToDecimal},
        {"to_string",  {ANY}, S, convToString},
        {"to_bool",    {S},   B, convToBool},
    }, {}});

    m.push_back({"Lists", {
        {"sort",     {t.list(C)},       V,         listSort,    0, false, true},
        {"reverse",  {t.list(T)},       V,         listReverse, 0, false, true},
        {"contains", {t.list(T), T},    B,         listContains},
        {"index_of", {t.list(T), T},    I,         listIndexOf},
        {"slice",    {t.list(T), I, I}, t.list(T), listSlice},
        {"sum",      {t.list(N)},       N,         listSum},
    }, {}});

    return m;
}

} // namespace

const std::vector<NativeModule>& nativeModules() {
    static const std::vector<NativeModule> modulos = criaModulos();
    return modulos;
}

const NativeModule* findNativeModule(const std::string& name) {
    for (const auto& m : nativeModules())
        if (m.name == name) return &m;
    return nullptr;
}

} // namespace cinza
