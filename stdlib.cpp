#include "natives.h"
#include "plataforma.h"
#include "runtime_error.h"
#include "utf8.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <ctime>
#include <thread>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <set>
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

// UTF-8: regras comuns em utf8.h
using utf8::decodifica;
using utf8::codifica;

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
double numero(const Value& v) { return v.asNumber(); }

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
    return Value(utf8::indiceDoByte(s, pos));
}

Value strStartsWith(Args a) {
    const std::string& s = a[0].asString();
    const std::string& p = a[1].asString();
    return Value(s.compare(0, p.size(), p) == 0);
}

Value strEndsWith(Args a) {
    const std::string& s = a[0].asString();
    const std::string& p = a[1].asString();
    return Value(s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0);
}

Value strFindLast(Args a) {
    const std::string& s = a[0].asString();
    const size_t pos = s.rfind(a[1].asString());
    return Value(pos == std::string::npos ? std::int64_t{-1} : utf8::indiceDoByte(s, pos));
}

// find_from(s, trecho, inicio): como find, a partir do caractere `inicio`
Value strFindFrom(Args a) {
    const std::string& s = a[0].asString();
    const std::int64_t ini = a[2].asInt();
    const std::int64_t n = utf8::tamanho(s);
    if (ini < 0 || ini > n)
        falha("IndexError: início " + std::to_string(ini) + " fora dos limites de uma string de tamanho " +
              std::to_string(n));
    const size_t pos = s.find(a[1].asString(), utf8::byteDoCaractere(s, ini));
    return Value(pos == std::string::npos ? std::int64_t{-1} : utf8::indiceDoByte(s, pos));
}

// count: ocorrências sem sobreposição ("aaaa" tem 2 de "aa")
Value strCount(Args a) {
    const std::string& s = a[0].asString();
    const std::string& t = a[1].asString();
    if (t.empty()) falha("ValueError: o trecho de 'count' não pode ser vazio");
    std::int64_t n = 0;
    for (size_t pos = s.find(t); pos != std::string::npos; pos = s.find(t, pos + t.size())) ++n;
    return Value(n);
}

Value strCharAt(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    const std::int64_t i = a[1].asInt();
    if (i < 0 || i >= static_cast<std::int64_t>(s.size()))
        falha("IndexError: índice " + std::to_string(i) + " fora dos limites de uma string de tamanho " +
              std::to_string(s.size()));
    return Value(codifica(std::u32string(1, s[static_cast<size_t>(i)])));
}

// left/right: os n primeiros/últimos caracteres; n maior que o tamanho dá o texto todo
std::int64_t quantidade(std::int64_t n, const char* fn) {
    if (n < 0) falha(std::string("ValueError: a quantidade de '") + fn + "' não pode ser negativa");
    return n;
}
Value strLeft(Args a) {
    const std::string& s = a[0].asString();
    return Value(s.substr(0, utf8::byteDoCaractere(s, quantidade(a[1].asInt(), "left"))));
}
Value strRight(Args a) {
    const std::string& s = a[0].asString();
    const std::int64_t n = quantidade(a[1].asInt(), "right"), total = utf8::tamanho(s);
    return Value(n >= total ? s : s.substr(utf8::byteDoCaractere(s, total - n)));
}

// slice(s, ini, fim): de ini até fim (exclusive), como Lists.slice
Value strSlice(Args a) {
    const std::string& s = a[0].asString();
    const std::int64_t ini = a[1].asInt(), fim = a[2].asInt(), n = utf8::tamanho(s);
    if (ini < 0 || fim < ini || fim > n)
        falha("IndexError: 'slice(" + std::to_string(ini) + ", " + std::to_string(fim) +
              ")' fora dos limites de uma string de tamanho " + std::to_string(n));
    const size_t b = utf8::byteDoCaractere(s, ini);
    return Value(s.substr(b, utf8::byteDoCaractere(s, fim) - b));
}

Value strChars(Args a) {
    std::vector<Value> out;
    for (char32_t c : decodifica(a[0].asString())) out.emplace_back(codifica(std::u32string(1, c)));
    return makeList(std::move(out));
}

// As linhas de um texto: \n ou \r\n; o \n final não gera linha vazia
// (Strings.lines e Files.lines)
Value dividirLinhas(const std::string& s) {
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

Value strLines(Args a) { return dividirLinhas(a[0].asString()); }

const char* const BRANCOS = " \t\n\r\f\v";

// words: separa por qualquer sequência de espaços em branco, sem partes vazias
Value strWords(Args a) {
    const std::string& s = a[0].asString();
    std::vector<Value> out;
    for (size_t ini = s.find_first_not_of(BRANCOS); ini != std::string::npos;) {
        const size_t fim = s.find_first_of(BRANCOS, ini);
        out.emplace_back(s.substr(ini, fim == std::string::npos ? std::string::npos : fim - ini));
        ini = fim == std::string::npos ? fim : s.find_first_not_of(BRANCOS, fim);
    }
    return makeList(std::move(out));
}

Value strTrimStart(Args a) {
    const std::string& s = a[0].asString();
    const size_t ini = s.find_first_not_of(BRANCOS);
    return Value(ini == std::string::npos ? std::string() : s.substr(ini));
}
Value strTrimEnd(Args a) {
    const std::string& s = a[0].asString();
    const size_t fim = s.find_last_not_of(BRANCOS);
    return Value(fim == std::string::npos ? std::string() : s.substr(0, fim + 1));
}

Value strReplaceFirst(Args a) {
    std::string s = a[0].asString();
    const std::string& de = a[1].asString();
    if (de.empty()) falha("ValueError: o trecho procurado em 'replace_first' não pode ser vazio");
    const size_t pos = s.find(de);
    if (pos != std::string::npos) s.replace(pos, de.size(), a[2].asString());
    return Value(std::move(s));
}

// remove(s, trecho): apaga todas as ocorrências
Value strRemove(Args a) {
    const std::string& s = a[0].asString();
    const std::string& t = a[1].asString();
    if (t.empty()) falha("ValueError: o trecho de 'remove' não pode ser vazio");
    std::string out;
    size_t ini = 0;
    for (size_t pos; (pos = s.find(t, ini)) != std::string::npos; ini = pos + t.size())
        out += s.substr(ini, pos - ini);
    out += s.substr(ini);
    return Value(std::move(out));
}

Value strRepeat(Args a) {
    const std::string& s = a[0].asString();
    const std::int64_t n = a[1].asInt();
    if (n < 0) falha("ValueError: a quantidade de 'repeat' não pode ser negativa");
    if (s.empty()) return Value(std::string());   // sem isso, n voltas sem acrescentar nada
    std::uint64_t total;
    if (__builtin_mul_overflow(static_cast<std::uint64_t>(s.size()), static_cast<std::uint64_t>(n), &total) ||
        total > (std::uint64_t{1} << 30))
        falha("ValueError: o resultado de 'repeat' passaria de 1 GiB");
    std::string out;
    out.reserve(total);
    for (std::int64_t i = 0; i < n; ++i) out += s;
    return Value(std::move(out));
}

Value strReverse(Args a) {
    std::u32string s = decodifica(a[0].asString());
    std::reverse(s.begin(), s.end());
    return Value(codifica(s));
}

// Preenchimento de pad_left/pad_right/center: opcional (espaço), exatamente 1 caractere
char32_t preenchimento(Args a, const char* fn) {
    if (a.size() < 3) return U' ';
    const std::u32string p = decodifica(a[2].asString());
    if (p.size() != 1)
        falha(std::string("ValueError: o preenchimento de '") + fn + "' precisa ter exatamente 1 caractere");
    return p[0];
}

// Quantos caracteres faltam para a largura (0 se já tem)
size_t falta(const std::u32string& s, std::int64_t largura) {
    return largura > static_cast<std::int64_t>(s.size()) ? static_cast<size_t>(largura) - s.size() : 0;
}

Value strPadLeft(Args a) {
    const char32_t p = preenchimento(a, "pad_left");
    const std::u32string s = decodifica(a[0].asString());
    return Value(codifica(std::u32string(falta(s, a[1].asInt()), p) + s));
}
Value strPadRight(Args a) {
    const char32_t p = preenchimento(a, "pad_right");
    const std::u32string s = decodifica(a[0].asString());
    return Value(codifica(s + std::u32string(falta(s, a[1].asInt()), p)));
}
// center: a sobra ímpar fica à direita
Value strCenter(Args a) {
    const char32_t p = preenchimento(a, "center");
    const std::u32string s = decodifica(a[0].asString());
    const size_t f = falta(s, a[1].asInt());
    return Value(codifica(std::u32string(f / 2, p) + s + std::u32string(f - f / 2, p)));
}

// Letras: ASCII e as acentuadas do Latin-1 (as mesmas de upper/lower)
bool ehLetra(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') ||
           (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7);
}
bool ehDigito(char32_t c) { return c >= U'0' && c <= U'9'; }
bool ehBranco(char32_t c) { return c < 0x80 && std::strchr(BRANCOS, static_cast<char>(c)) && c != 0; }
bool ehMaiuscula(char32_t c) { return ehLetra(c) && paraMinuscula(c) != c; }
// ß e ÿ são minúsculas sem maiúscula no Latin-1 (sem elas, "ÿ" passava por maiúsculo)
bool ehMinuscula(char32_t c) { return ehLetra(c) && (paraMaiuscula(c) != c || c == 0xDF || c == 0xFF); }

// capitalize: primeira letra maiúscula, o resto minúsculo
Value strCapitalize(Args a) {
    std::u32string s = decodifica(a[0].asString());
    for (size_t i = 0; i < s.size(); ++i) s[i] = i == 0 ? paraMaiuscula(s[i]) : paraMinuscula(s[i]);
    return Value(codifica(s));
}

// title: cada palavra (separada por espaço em branco) com a primeira letra maiúscula
Value strTitle(Args a) {
    std::u32string s = decodifica(a[0].asString());
    bool inicio = true;
    for (char32_t& c : s) {
        c = inicio ? paraMaiuscula(c) : paraMinuscula(c);
        inicio = ehBranco(c);
    }
    return Value(codifica(s));
}

Value strIsEmpty(Args a) { return Value(a[0].asString().empty()); }

// Todos os caracteres passam no teste (string vazia: false)
template <bool (*teste)(char32_t)>
Value todos(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    return Value(!s.empty() && std::all_of(s.begin(), s.end(), teste));
}
bool ehAlnum(char32_t c) { return ehLetra(c) || ehDigito(c); }

// is_upper/is_lower: tem letra e nenhuma do outro caso ("ABC 1" é maiúsculo)
Value strIsUpper(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    return Value(std::any_of(s.begin(), s.end(), ehLetra) && std::none_of(s.begin(), s.end(), ehMinuscula));
}
Value strIsLower(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    return Value(std::any_of(s.begin(), s.end(), ehLetra) && std::none_of(s.begin(), s.end(), ehMaiuscula));
}

// ord(c): código Unicode de um único caractere
Value strOrd(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    if (s.size() != 1) falha("ValueError: 'ord' espera exatamente 1 caractere (recebeu " +
                             std::to_string(s.size()) + ")");
    return Value(static_cast<std::int64_t>(s[0]));
}

// chr(n): o caractere do código Unicode n
Value strChr(Args a) {
    const std::int64_t n = a[0].asInt();
    if (n < 0 || n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF))
        falha("ValueError: " + std::to_string(n) + " não é um código Unicode válido");
    return Value(codifica(std::u32string(1, static_cast<char32_t>(n))));
}

Value strEqualsIgnoreCase(Args a) {
    std::u32string x = decodifica(a[0].asString()), y = decodifica(a[1].asString());
    for (char32_t& c : x) c = paraMinuscula(c);
    for (char32_t& c : y) c = paraMinuscula(c);
    return Value(x == y);
}

// compare: -1, 0 ou 1, na mesma ordem de < entre strings
Value strCompare(Args a) {
    const int r = a[0].asString().compare(a[1].asString());
    return Value(std::int64_t{r < 0 ? -1 : (r > 0 ? 1 : 0)});
}

// fixed(x, casas): o decimal como texto com exatamente `casas` casas
// (arredonda como Math.round_to; sem "-0.00")
Value strFixed(Args a) {
    const double x = a[0].asDecimal();
    const std::int64_t casas = a[1].asInt();
    if (casas < 0 || casas > 100)
        falha("ValueError: o número de casas de 'fixed' precisa estar entre 0 e 100");
    return Value(fixedText(x, casas));
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

// lines: as linhas do arquivo, com as regras de dividirLinhas
Value filesLines(Args a) { return dividirLinhas(lerArquivo(a[0].asString())); }

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

// Destino de copy/move: existente só com substituir = true, e só se for arquivo;
// nunca o próprio arquivo de origem (move apagaria o destino antes de renomear,
// perdendo o arquivo)
void confereDestino(const fs::path& o, const fs::path& d, const std::string& c, bool substituir) {
    std::error_code ec;
    if (!fs::exists(d, ec)) return;
    if (fs::equivalent(o, d, ec)) falhaIO("origem e destino são o mesmo arquivo:", c);
    if (!substituir)                 falhaIO("o destino já existe:", c);
    if (!fs::is_regular_file(d, ec)) falhaIO("o destino é uma pasta, não pode ser substituído:", c);
}

Value filesCopy(Args a) {
    const fs::path o = arquivoExistente(a[0].asString());
    const fs::path d = caminho(a[1].asString());
    confereDestino(o, d, a[1].asString(), a[2].asBool());
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
    confereDestino(o, d, a[1].asString(), a[2].asBool());
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
    if (a[0].asDecimal() == 0 && a[1].asDecimal() < 0)
        falha("ZeroDivisionError: 'pow' de 0 com expoente negativo (divisão por zero)");
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
// |x| arredondado para `casas` casas, como texto sem sinal e com exatamente
// `casas` dígitos depois do ponto (nenhum ponto se casas = 0). Usado por
// round_to e Strings.fixed, que assim arredondam igual.
std::string arredondaTexto(double x, std::int64_t casas) {
    char buf[512];   // o maior decimal tem 309 dígitos inteiros
    auto [fim, ec] = std::to_chars(buf, buf + sizeof buf, std::fabs(x), std::chars_format::fixed);
    (void)ec;
    std::string s(buf, fim);
    size_t ponto = s.find('.');
    if (ponto == std::string::npos) { ponto = s.size(); s += '.'; }
    const auto n = static_cast<size_t>(casas);
    const size_t tem = s.size() - ponto - 1;
    if (tem < n) s.append(n - tem, '0');
    std::string digitos = s.substr(0, ponto) + s.substr(ponto + 1, n);
    if (tem > n && s[ponto + 1 + n] >= '5') {   // soma 1 na última casa mantida, com vai-um
        size_t i = digitos.size();
        while (i > 0 && digitos[i - 1] == '9') digitos[--i] = '0';
        if (i == 0) digitos.insert(digitos.begin(), '1');
        else        ++digitos[i - 1];
    }
    const size_t int_len = digitos.size() - n;
    std::string r = digitos.substr(0, int_len);
    if (n > 0) r += "." + digitos.substr(int_len);
    return r;
}

Value mathRoundTo(Args a) {
    const double x = a[0].asDecimal();
    const std::int64_t casas = a[1].asInt();
    if (casas < 0) falha("ValueError: o número de casas de 'round_to' não pode ser negativo");
    if (casas > 400) return Value(x);   // além disso nenhum decimal tem casas para cortar
    const std::string r = arredondaTexto(x, casas);
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

// decimal_range(a, b): de a (inclusive) até b (exclusive)
Value randDecimalRange(Args a) {
    const double lo = a[0].asDecimal(), hi = a[1].asDecimal();
    if (!(lo < hi))
        falha("ValueError: em 'decimal_range' o início precisa ser menor que o fim");
    if (!std::isfinite(hi - lo)) falha("ValueError: intervalo de 'decimal_range' grande demais");
    std::uniform_real_distribution<double> d(lo, hi);
    const double r = d(gerador());
    return Value(r < hi ? r : lo);   // arredondamento nunca entrega o fim
}

Value randBool(Args) { return Value(std::uniform_int_distribution<int>(0, 1)(gerador()) == 1); }

// chance(p): true com probabilidade p; chance(0) nunca, chance(1) sempre
Value randChance(Args a) {
    const double p = a[0].asDecimal();
    if (p < 0 || p > 1) falha("ValueError: a probabilidade de 'chance' precisa estar entre 0 e 1");
    return Value(std::uniform_real_distribution<double>(0.0, 1.0)(gerador()) < p);
}

Value randShuffle(Args a) {
    auto& elems = a[0].asList()->elements;
    std::shuffle(elems.begin(), elems.end(), gerador());
    return Value();
}

std::int64_t quantidadeSorteio(std::int64_t k, const char* fn) {
    if (k < 0) falha(std::string("ValueError: a quantidade de '") + fn + "' não pode ser negativa");
    return k;
}

// sample(l, k): k elementos de posições distintas, em ordem aleatória
Value randSample(Args a) {
    const auto& elems = a[0].asList()->elements;
    const auto k = static_cast<size_t>(quantidadeSorteio(a[1].asInt(), "sample"));
    if (k > elems.size())
        falha("ValueError: 'sample' de " + std::to_string(k) + " elementos de uma lista de tamanho " +
              std::to_string(elems.size()));
    std::vector<size_t> idx(elems.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::vector<Value> out;
    out.reserve(k);
    for (size_t i = 0; i < k; ++i) {   // Fisher-Yates parcial
        std::uniform_int_distribution<size_t> d(i, idx.size() - 1);
        std::swap(idx[i], idx[d(gerador())]);
        out.push_back(elems[idx[i]]);
    }
    return makeList(std::move(out));
}

// choices(l, k): k sorteios com repetição
Value randChoices(Args a) {
    const auto& elems = a[0].asList()->elements;
    const auto k = static_cast<size_t>(quantidadeSorteio(a[1].asInt(), "choices"));
    if (k > 0 && elems.empty()) falha("IndexError: 'choices' de uma lista vazia");
    std::vector<Value> out;
    out.reserve(k);
    for (size_t i = 0; i < k; ++i) {
        std::uniform_int_distribution<size_t> d(0, elems.size() - 1);
        out.push_back(elems[d(gerador())]);
    }
    return makeList(std::move(out));
}

// gauss(media, desvio): distribuição normal; desvio 0 devolve a média
Value randGauss(Args a) {
    const double media = a[0].asDecimal(), desvio = a[1].asDecimal();
    if (desvio < 0) falha("ValueError: o desvio de 'gauss' não pode ser negativo");
    if (desvio == 0) return Value(media);
    return decimalFinito(std::normal_distribution<double>(media, desvio)(gerador()), "gauss");
}

// ============================================================================
// Convert
// ============================================================================

// Leitura estrita de texto, comum a to_*, is_* e to_*_or (aceitam os mesmos textos)
enum class Leitura { ok, invalido, fora };

// int: só sinal opcional e dígitos, sem espaços
Leitura lerInt(const std::string& s, std::int64_t& v) {
    const char* fim = s.data() + s.size();
    // from_chars aceita '-' mas não '+'; "+-5" não é válido
    const size_t pula = (!s.empty() && s[0] == '+') ? 1 : 0;
    if (s.empty() || (pula && (s.size() == 1 || s[1] == '-'))) return Leitura::invalido;
    auto [ptr, ec] = std::from_chars(s.data() + pula, fim, v);
    if (ptr != fim || ec == std::errc::invalid_argument) return Leitura::invalido;
    if (ec == std::errc::result_out_of_range) return Leitura::fora;
    return Leitura::ok;
}

// decimal: [sinal] dígitos [. dígitos] [e [sinal] dígitos]
Leitura lerDecimal(const std::string& s, double& v) {
    size_t i = 0;
    auto digitos = [&]() {
        const size_t antes = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        return i > antes;
    };
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    bool ok = digitos();
    if (ok && i < s.size() && s[i] == '.') { ++i; ok = digitos(); }
    if (ok && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        ok = digitos();
    }
    if (!ok || i != s.size()) return Leitura::invalido;
    const char* ini = s.data() + (s[0] == '+' ? 1 : 0);
    auto [ptr, ec] = std::from_chars(ini, s.data() + s.size(), v);
    (void)ptr;
    if (ec == std::errc::result_out_of_range) {
        // from_chars não diz se estourou ou ficou pequeno demais (e não toca em
        // v); strtod diz: grande demais é erro, pequeno demais vira 0
        const double r = std::strtod(std::string(ini, s.data() + s.size()).c_str(), nullptr);
        if (std::isinf(r)) return Leitura::fora;
        v = 0.0;
    }
    return Leitura::ok;
}

// bool: só "true" ou "false"
Leitura lerBool(const std::string& s, bool& v) {
    if (s == "true")  { v = true;  return Leitura::ok; }
    if (s == "false") { v = false; return Leitura::ok; }
    return Leitura::invalido;
}

Value convToInt(Args a) {
    const std::string& s = a[0].asString();
    std::int64_t v = 0;
    switch (lerInt(s, v)) {
        case Leitura::invalido: falha("ValueError: '" + s + "' não é um int válido");
        case Leitura::fora:     falha("ValueError: '" + s + "' está fora do intervalo de int");
        case Leitura::ok:       break;
    }
    return Value(v);
}

Value convToDecimal(Args a) {
    const std::string& s = a[0].asString();
    double v = 0;
    switch (lerDecimal(s, v)) {
        case Leitura::invalido: falha("ValueError: '" + s + "' não é um decimal válido");
        case Leitura::fora:     falha("ValueError: '" + s + "' está fora do intervalo de decimal");
        case Leitura::ok:       break;
    }
    return Value(v);
}

Value convToString(Args a) { return Value(a[0].toString()); }

Value convToBool(Args a) {
    const std::string& s = a[0].asString();
    bool v = false;
    if (lerBool(s, v) != Leitura::ok)
        falha("ValueError: '" + s + "' não é um bool válido (use \"true\" ou \"false\")");
    return Value(v);
}

// is_*: o texto seria aceito pelo to_* correspondente
Value convIsInt(Args a)     { std::int64_t v; return Value(lerInt(a[0].asString(), v) == Leitura::ok); }
Value convIsDecimal(Args a) { double v;       return Value(lerDecimal(a[0].asString(), v) == Leitura::ok); }
Value convIsBool(Args a)    { bool v;         return Value(lerBool(a[0].asString(), v) == Leitura::ok); }

// to_*_or: o valor convertido, ou o padrão se o texto não for aceito
Value convToIntOr(Args a) {
    std::int64_t v;
    return lerInt(a[0].asString(), v) == Leitura::ok ? Value(v) : a[1];
}
Value convToDecimalOr(Args a) {
    double v;
    return lerDecimal(a[0].asString(), v) == Leitura::ok ? Value(v) : a[1];
}
Value convToBoolOr(Args a) {
    bool v;
    return lerBool(a[0].asString(), v) == Leitura::ok ? Value(v) : a[1];
}

std::int64_t confereBase(std::int64_t base, const char* fn) {
    if (base < 2 || base > 36)
        falha(std::string("ValueError: a base de '") + fn + "' precisa estar entre 2 e 36 (recebeu " +
              std::to_string(base) + ")");
    return base;
}

// n na base dada, com dígitos 0-9 e a-z; negativo com '-'; sem prefixo
std::string paraBase(std::int64_t n, std::int64_t base) {
    std::uint64_t m = magnitude(n);
    std::string r;
    do {
        r += "0123456789abcdefghijklmnopqrstuvwxyz"[m % static_cast<std::uint64_t>(base)];
        m /= static_cast<std::uint64_t>(base);
    } while (m != 0);
    if (n < 0) r += '-';
    std::reverse(r.begin(), r.end());
    return r;
}

Value convToBase(Args a)   { return Value(paraBase(a[0].asInt(), confereBase(a[1].asInt(), "to_base"))); }
Value convToHex(Args a)    { return Value(paraBase(a[0].asInt(), 16)); }
Value convToBinary(Args a) { return Value(paraBase(a[0].asInt(), 2)); }
Value convToOctal(Args a)  { return Value(paraBase(a[0].asInt(), 8)); }

// from_base(s, base): sinal opcional e dígitos da base (maiúsculas ou minúsculas), sem prefixo
Value convFromBase(Args a) {
    const std::string& s = a[0].asString();
    const auto base = static_cast<std::uint64_t>(confereBase(a[1].asInt(), "from_base"));
    const std::string invalido = "ValueError: '" + s + "' não é um número válido na base " + std::to_string(base);
    size_t i = 0;
    const bool negativo = !s.empty() && s[0] == '-';
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) ++i;
    if (i == s.size()) falha(invalido);
    // limite da magnitude: 2^63 - 1, ou 2^63 para negativo
    const std::uint64_t limite = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negativo ? 1 : 0);
    std::uint64_t m = 0;
    for (; i < s.size(); ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
        std::uint64_t d;
        if (c >= '0' && c <= '9')      d = static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'z') d = static_cast<std::uint64_t>(c - 'a' + 10);
        else falha(invalido);
        if (d >= base) falha(invalido);
        if (__builtin_mul_overflow(m, base, &m) || __builtin_add_overflow(m, d, &m) || m > limite)
            falha("ValueError: '" + s + "' está fora do intervalo de int");
    }
    return Value(negativo ? static_cast<std::int64_t>(std::uint64_t{0} - m) : static_cast<std::int64_t>(m));
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

Value listSortDesc(Args a) {
    auto& elems = a[0].asList()->elements;
    std::stable_sort(elems.begin(), elems.end(), [](const Value& x, const Value& y) { return menor(y, x); });
    return Value();
}

// sort_by(l, chaves): ordena l pela chave de mesma posição (estável); as chaves
// são reorganizadas junto
Value listSortBy(Args a) {
    auto& elems  = a[0].asList()->elements;
    auto& chaves = a[1].asList()->elements;
    if (elems.size() != chaves.size())
        falha("ValueError: 'sort_by' precisa de uma chave por elemento (lista de tamanho " +
              std::to_string(elems.size()) + ", " + std::to_string(chaves.size()) + " chaves)");
    std::vector<size_t> ordem(elems.size());
    for (size_t i = 0; i < ordem.size(); ++i) ordem[i] = i;
    std::stable_sort(ordem.begin(), ordem.end(),
                     [&](size_t x, size_t y) { return menor(chaves[x], chaves[y]); });
    std::vector<Value> e2, c2;
    e2.reserve(ordem.size());
    c2.reserve(ordem.size());
    for (size_t i : ordem) { e2.push_back(elems[i]); c2.push_back(chaves[i]); }
    // mesma lista nos dois argumentos: elems e chaves são o mesmo vetor
    elems = std::move(e2);
    if (&elems != &chaves) chaves = std::move(c2);
    return Value();
}

Value listSorted(Args a) {
    std::vector<Value> out = a[0].asList()->elements;
    std::stable_sort(out.begin(), out.end(), menor);
    return makeList(std::move(out));
}

Value listReversed(Args a) {
    const auto& elems = a[0].asList()->elements;
    return makeList(std::vector<Value>(elems.rbegin(), elems.rend()));
}

const std::vector<Value>& naoVazia(Args a, const char* fn) {
    const auto& elems = a[0].asList()->elements;
    if (elems.empty()) falha(std::string("ValueError: '") + fn + "' de uma lista vazia");
    return elems;
}

Value listMin(Args a) {
    const auto& elems = naoVazia(a, "min");
    return *std::min_element(elems.begin(), elems.end(), menor);
}
Value listMax(Args a) {
    const auto& elems = naoVazia(a, "max");
    // o primeiro dos maiores, como min
    return *std::max_element(elems.begin(), elems.end(), menor);
}

// average: sempre decimal
Value listAverage(Args a) {
    const auto& elems = naoVazia(a, "average");
    double total = 0;
    for (const auto& v : elems) total += numero(v);
    return decimalFinito(total / static_cast<double>(elems.size()), "average");
}

// product: lista vazia dá 1; estouro lança OverflowError
Value listProduct(Args a) {
    const auto& elems = a[0].asList()->elements;
    if (!elems.empty() && elems[0].kind() == Value::Kind::DECIMAL) {
        double total = 1;
        for (const auto& v : elems) total *= v.asDecimal();
        if (!std::isfinite(total)) falha("OverflowError: o produto de 'product' excede a faixa de decimal");
        return Value(total);
    }
    std::int64_t total = 1;
    for (const auto& v : elems)
        if (__builtin_mul_overflow(total, v.asInt(), &total))
            falha("OverflowError: o produto de 'product' não cabe em int");
    return Value(total);
}

Value listCount(Args a) {
    const auto& elems = a[0].asList()->elements;
    return Value(static_cast<std::int64_t>(std::count(elems.begin(), elems.end(), a[1])));
}

Value listLastIndexOf(Args a) {
    const auto& elems = a[0].asList()->elements;
    for (size_t i = elems.size(); i > 0; --i)
        if (elems[i - 1] == a[1]) return Value(static_cast<std::int64_t>(i - 1));
    return Value(std::int64_t{-1});
}

Value listFirst(Args a) {
    const auto& elems = a[0].asList()->elements;
    if (elems.empty()) falha("IndexError: 'first' de uma lista vazia");
    return elems.front();
}
Value listLast(Args a) {
    const auto& elems = a[0].asList()->elements;
    if (elems.empty()) falha("IndexError: 'last' de uma lista vazia");
    return elems.back();
}

Value listIsEmpty(Args a) { return Value(a[0].asList()->elements.empty()); }

std::string foraDosLimites(std::int64_t i, size_t n) {
    return "IndexError: índice " + std::to_string(i) + " fora dos limites de uma lista de tamanho " +
           std::to_string(n);
}

// insert(l, i, x): i de 0 ao tamanho (no fim = acrescentar)
Value listInsert(Args a) {
    auto& elems = a[0].asList()->elements;
    const std::int64_t i = a[1].asInt();
    if (i < 0 || i > static_cast<std::int64_t>(elems.size())) falha(foraDosLimites(i, elems.size()));
    Value x = a[2];   // cópia antes de mexer no vetor (x pode ser elemento dele)
    elems.insert(elems.begin() + i, std::move(x));
    return Value();
}

// pop(l): remove e devolve o último
Value listPop(Args a) {
    auto& elems = a[0].asList()->elements;
    if (elems.empty()) falha("IndexError: 'pop' de uma lista vazia");
    Value v = std::move(elems.back());
    elems.pop_back();
    return v;
}

// remove_value(l, x): remove a primeira ocorrência; ausente lança ValueError
Value listRemoveValue(Args a) {
    auto& elems = a[0].asList()->elements;
    auto it = std::find(elems.begin(), elems.end(), a[1]);
    if (it == elems.end()) falha("ValueError: 'remove_value': " + a[1].toString() + " não está na lista");
    elems.erase(it);
    return Value();
}

Value listClear(Args a) {
    a[0].asList()->elements.clear();
    return Value();
}

// extend(l, outra): acrescenta os elementos de outra ao fim de l
Value listExtend(Args a) {
    auto& elems = a[0].asList()->elements;
    const std::vector<Value> outra = a[1].asList()->elements;   // cópia: outra pode ser l
    elems.insert(elems.end(), outra.begin(), outra.end());
    return Value();
}

Value listSwap(Args a) {
    auto& elems = a[0].asList()->elements;
    const std::int64_t i = a[1].asInt(), j = a[2].asInt();
    const auto n = static_cast<std::int64_t>(elems.size());
    if (i < 0 || i >= n) falha(foraDosLimites(i, elems.size()));
    if (j < 0 || j >= n) falha(foraDosLimites(j, elems.size()));
    std::swap(elems[static_cast<size_t>(i)], elems[static_cast<size_t>(j)]);
    return Value();
}

Value listConcat(Args a) {
    std::vector<Value> out = a[0].asList()->elements;
    const auto& b = a[1].asList()->elements;
    out.insert(out.end(), b.begin(), b.end());
    return makeList(std::move(out));
}

// Tipos em que operator< é uma ordem total (os demais são comparados com ==)
bool ordenavel(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::INT: case Value::Kind::DECIMAL: case Value::Kind::STRING:
        case Value::Kind::BOOL: case Value::Kind::ENUM:
            return true;
        default:
            return false;
    }
}

// unique: sem repetidos, mantendo a primeira ocorrência e a ordem
Value listUnique(Args a) {
    const auto& elems = a[0].asList()->elements;
    std::vector<Value> out;
    const bool rapido = std::all_of(elems.begin(), elems.end(), ordenavel);
    std::set<Value> vistos;
    for (const auto& v : elems) {
        if (rapido) { if (!vistos.insert(v).second) continue; }
        else if (std::find(out.begin(), out.end(), v) != out.end()) continue;
        out.push_back(v);
    }
    return makeList(std::move(out));
}

// repeat(x, n): lista com n cópias de x (cópia rasa, como a atribuição)
Value listRepeat(Args a) {
    const std::int64_t n = a[1].asInt();
    if (n < 0) falha("ValueError: a quantidade de 'repeat' não pode ser negativa");
    if (n > 100'000'000) falha("ValueError: 'repeat' de mais de 100 milhões de elementos");
    return makeList(std::vector<Value>(static_cast<size_t>(n), a[0]));
}

Value listCopy(Args a) { return makeList(std::vector<Value>(a[0].asList()->elements)); }

// ============================================================================
// Time — uma data/hora é um int: segundos desde 1970-01-01 00:00 UTC. As partes,
// make, format e parse usam o horário local; as variantes _utc, o UTC. O
// calendário é calculado aqui (algoritmos civis de H. Hinnant, exatos em toda a
// faixa); do sistema só vem o deslocamento do fuso local.
// ============================================================================

constexpr std::int64_t DIA = 86400;

std::int64_t divPiso(std::int64_t a, std::int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

// Dias desde 1970-01-01 da data civil (ano, mês 1-12, dia 1-31)
std::int64_t diasDaData(std::int64_t y, std::int64_t m, std::int64_t d) {
    y -= m <= 2;
    const std::int64_t era = divPiso(y, 400);
    const std::int64_t yoe = y - era * 400;
    const std::int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct Data { std::int64_t ano; int mes, dia; };

Data dataDosDias(std::int64_t z) {
    z += 719468;
    const std::int64_t era = divPiso(z, 146097);
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const int d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    return {yoe + era * 400 + (m <= 2), m, d};
}

bool bissexto(std::int64_t y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int diasNoMes(std::int64_t y, int m) {
    static const int dias[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && bissexto(y) ? 29 : dias[m - 1];
}

// Faixa de datas: anos 1 a 9999 (o "yyyy" do format tem 4 dígitos)
const std::int64_t T_MIN = diasDaData(1, 1, 1) * DIA;
const std::int64_t T_MAX = diasDaData(9999, 12, 31) * DIA + DIA - 1;

void confereFaixa(std::int64_t t) {
    if (t < T_MIN || t > T_MAX)
        falha("ValueError: o instante " + std::to_string(t) + " está fora do intervalo de datas (anos 1 a 9999)");
}

// Segundos a somar ao UTC para ter o horário local no instante t. O sistema só
// conhece parte da faixa (no Windows, de 1970 a 3000): fora dela, usa a borda.
std::int64_t deslocamento(std::int64_t t) {
    const std::int64_t limite = diasDaData(3000, 12, 31) * DIA;
    const std::time_t tt = static_cast<std::time_t>(std::clamp<std::int64_t>(t, DIA, limite));
    std::tm tm{};
#ifdef _WIN32
    if (localtime_s(&tm, &tt) != 0) return 0;
#else
    if (!localtime_r(&tt, &tm)) return 0;
#endif
    const std::int64_t local = diasDaData(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * DIA +
                               tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
    return local - static_cast<std::int64_t>(tt);
}

struct Partes { std::int64_t ano; int mes, dia, hora, minuto, segundo; std::int64_t dias; };

// Partes de t (já no fuso desejado: UTC, ou UTC + deslocamento)
Partes partesDe(std::int64_t t) {
    const std::int64_t dias = divPiso(t, DIA);
    const std::int64_t s = t - dias * DIA;
    const Data d = dataDosDias(dias);
    return {d.ano, d.mes, d.dia, static_cast<int>(s / 3600), static_cast<int>(s / 60 % 60),
            static_cast<int>(s % 60), dias};
}

Partes partesLocal(std::int64_t t) {
    confereFaixa(t);
    const std::int64_t l = t + deslocamento(t);
    confereFaixa(l);
    return partesDe(l);
}
Partes partesUtc(std::int64_t t) { confereFaixa(t); return partesDe(t); }

// Valida a data e devolve o instante como se fosse UTC
std::int64_t instanteUtc(std::int64_t ano, std::int64_t mes, std::int64_t dia,
                         std::int64_t hora, std::int64_t minuto, std::int64_t segundo) {
    if (ano < 1 || ano > 9999) falha("ValueError: ano " + std::to_string(ano) + " fora do intervalo (use 1 a 9999)");
    if (mes < 1 || mes > 12)   falha("ValueError: mês " + std::to_string(mes) + " inválido (use 1 a 12)");
    const int n = diasNoMes(ano, static_cast<int>(mes));
    if (dia < 1 || dia > n)
        falha("ValueError: dia " + std::to_string(dia) + " inválido para " + std::to_string(mes) + "/" +
              std::to_string(ano) + " (o mês tem " + std::to_string(n) + " dias)");
    if (hora < 0 || hora > 23)     falha("ValueError: hora " + std::to_string(hora) + " inválida (use 0 a 23)");
    if (minuto < 0 || minuto > 59) falha("ValueError: minuto " + std::to_string(minuto) + " inválido (use 0 a 59)");
    if (segundo < 0 || segundo > 59) falha("ValueError: segundo " + std::to_string(segundo) + " inválido (use 0 a 59)");
    return diasDaData(ano, mes, dia) * DIA + hora * 3600 + minuto * 60 + segundo;
}

// Instante cujo horário local é `l` (l = data local contada como UTC). Perto da
// mudança de horário de verão o deslocamento muda, então cada candidato é
// conferido: na hora repetida (fim do horário de verão) fica a primeira
// ocorrência; numa hora que não existe (o relógio pulou), adianta — 2h30 vira
// 3h30, como no Python e no mktime.
std::int64_t deLocal(std::int64_t l) {
    const std::int64_t t1 = l - deslocamento(l);
    const std::int64_t t2 = l - deslocamento(t1);
    if (t2 + deslocamento(t2) == l) return t2;
    if (t1 + deslocamento(t1) == l) return t1;
    return std::max(t1, t2);
}

// make(ano, mes, dia[, hora, minuto, segundo]): as horas que faltam são 0
std::int64_t instanteDosArgs(Args a) {
    auto arg = [&](size_t i) { return i < a.size() ? a[i].asInt() : std::int64_t{0}; };
    return instanteUtc(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
}

Value timeMake(Args a)    { return Value(deLocal(instanteDosArgs(a))); }
Value timeMakeUtc(Args a) { return Value(instanteDosArgs(a)); }

Value timeNow(Args) {
    return Value(static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
}

// clock: milissegundos de um relógio que só avança (para medir duração)
Value timeClock(Args) {
    return Value(static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()));
}

Value timeSleep(Args a) {
    const std::int64_t ms = a[0].asInt();
    if (ms < 0) falha("ValueError: o tempo de 'sleep' não pode ser negativo");
    // espera pelo menos ms pelo relógio de clock(): o sleep_for do MinGW às vezes
    // acorda antes do prazo, então repete até lá
    const auto prazo = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (auto agora = std::chrono::steady_clock::now(); agora < prazo; agora = std::chrono::steady_clock::now())
        std::this_thread::sleep_for(prazo - agora);
    return Value();
}

// weekday: ISO, 1 = segunda ... 7 = domingo (1970-01-01 foi quinta)
int diaDaSemana(std::int64_t dias) { return static_cast<int>(((dias % 7 + 7) % 7 + 3) % 7 + 1); }
int diaDoAno(const Partes& p) { return static_cast<int>(p.dias - diasDaData(p.ano, 1, 1) + 1); }

#define CINZA_PARTES(sufixo, partes)                                                              \
    Value timeYear##sufixo(Args a)   { return Value(partes(a[0].asInt()).ano); }                  \
    Value timeMonth##sufixo(Args a)  { return Value(std::int64_t{partes(a[0].asInt()).mes}); }     \
    Value timeDay##sufixo(Args a)    { return Value(std::int64_t{partes(a[0].asInt()).dia}); }     \
    Value timeHour##sufixo(Args a)   { return Value(std::int64_t{partes(a[0].asInt()).hora}); }    \
    Value timeMinute##sufixo(Args a) { return Value(std::int64_t{partes(a[0].asInt()).minuto}); }  \
    Value timeSecond##sufixo(Args a) { return Value(std::int64_t{partes(a[0].asInt()).segundo}); } \
    Value timeWeekday##sufixo(Args a) {                                                           \
        return Value(std::int64_t{diaDaSemana(partes(a[0].asInt()).dias)});                       \
    }                                                                                             \
    Value timeDayOfYear##sufixo(Args a) { return Value(std::int64_t{diaDoAno(partes(a[0].asInt()))}); }
CINZA_PARTES(, partesLocal)
CINZA_PARTES(Utc, partesUtc)
#undef CINZA_PARTES

// Campos do padrão de format/parse: yyyy, MM, dd, HH, mm, ss; o resto é literal
struct Campo { const char* nome; int digitos; };
const Campo CAMPOS[] = {{"yyyy", 4}, {"MM", 2}, {"dd", 2}, {"HH", 2}, {"mm", 2}, {"ss", 2}};

const Campo* campoEm(const std::string& p, size_t i) {
    for (const Campo& c : CAMPOS)
        if (p.compare(i, std::strlen(c.nome), c.nome) == 0) return &c;
    return nullptr;
}

std::string formata(const Partes& t, const std::string& padrao) {
    std::string out;
    for (size_t i = 0; i < padrao.size();) {
        const Campo* c = campoEm(padrao, i);
        if (!c) { out += padrao[i++]; continue; }
        std::int64_t v = 0;
        switch (c->nome[0]) {
            case 'y': v = t.ano; break;
            case 'M': v = t.mes; break;
            case 'd': v = t.dia; break;
            case 'H': v = t.hora; break;
            case 'm': v = t.minuto; break;
            default:  v = t.segundo; break;
        }
        std::string s = std::to_string(v);
        if (static_cast<int>(s.size()) < c->digitos) s.insert(0, static_cast<size_t>(c->digitos) - s.size(), '0');
        out += s;
        i += std::strlen(c->nome);
    }
    return out;
}

Value timeFormat(Args a)    { return Value(formata(partesLocal(a[0].asInt()), a[1].asString())); }
Value timeFormatUtc(Args a) { return Value(formata(partesUtc(a[0].asInt()), a[1].asString())); }

// parse(texto, padrao): o texto precisa casar exatamente com o padrão (campos com
// o número exato de dígitos); o que o padrão não traz vale 1970, mês 1, dia 1, 0 h
std::int64_t interpreta(const std::string& texto, const std::string& padrao) {
    std::int64_t v[6] = {1970, 1, 1, 0, 0, 0};   // ano, mês, dia, hora, minuto, segundo
    const std::string erro = "ValueError: '" + texto + "' não corresponde ao padrão '" + padrao + "'";
    size_t j = 0;
    for (size_t i = 0; i < padrao.size();) {
        const Campo* c = campoEm(padrao, i);
        if (!c) {
            if (j >= texto.size() || texto[j] != padrao[i]) falha(erro);
            ++i; ++j;
            continue;
        }
        std::int64_t n = 0;
        for (int k = 0; k < c->digitos; ++k, ++j) {
            if (j >= texto.size() || texto[j] < '0' || texto[j] > '9') falha(erro);
            n = n * 10 + (texto[j] - '0');
        }
        v[c - CAMPOS] = n;
        i += std::strlen(c->nome);
    }
    if (j != texto.size()) falha(erro);
    return instanteUtc(v[0], v[1], v[2], v[3], v[4], v[5]);
}

Value timeParse(Args a)    { return Value(deLocal(interpreta(a[0].asString(), a[1].asString()))); }
Value timeParseUtc(Args a) { return Value(interpreta(a[0].asString(), a[1].asString())); }

Value somaTempo(Args a, std::int64_t unidade, const char* fn) {
    std::int64_t r;
    if (__builtin_mul_overflow(a[1].asInt(), unidade, &r) || __builtin_add_overflow(a[0].asInt(), r, &r))
        falha(std::string("OverflowError: resultado de '") + fn + "' fora do intervalo de int");
    return Value(r);
}
Value timeAddDays(Args a)    { return somaTempo(a, DIA, "add_days"); }
Value timeAddHours(Args a)   { return somaTempo(a, 3600, "add_hours"); }
Value timeAddMinutes(Args a) { return somaTempo(a, 60, "add_minutes"); }
Value timeAddSeconds(Args a) { return somaTempo(a, 1, "add_seconds"); }

// days_between(a, b): dias de calendário (local) de a até b; negativo se b vem antes
Value timeDaysBetween(Args a) {
    return Value(partesLocal(a[1].asInt()).dias - partesLocal(a[0].asInt()).dias);
}

Value timeIsLeapYear(Args a) { return Value(bissexto(a[0].asInt())); }

Value timeDaysInMonth(Args a) {
    const std::int64_t m = a[1].asInt();
    if (m < 1 || m > 12) falha("ValueError: mês " + std::to_string(m) + " inválido (use 1 a 12)");
    return Value(std::int64_t{diasNoMes(a[0].asInt(), static_cast<int>(m))});
}

// Files.modified(caminho): instante da última modificação
Value filesModified(Args a) {
    const fs::path p = caminho(a[0].asString());
    std::error_code ec;
    if (!fs::exists(p, ec)) falhaIO("não existe:", a[0].asString());
    std::int64_t t = 0;
    if (!dataModificacao(p, t)) falhaIO("não foi possível ler a data de", a[0].asString());
    return Value(t);
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
        {"ends_with",   {S, S},    B,           strEndsWith},
        {"find_last",   {S, S},    I,           strFindLast},
        {"find_from",   {S, S, I}, I,           strFindFrom},
        {"count",       {S, S},    I,           strCount},
        {"char_at",     {S, I},    S,           strCharAt},
        {"left",        {S, I},    S,           strLeft},
        {"right",       {S, I},    S,           strRight},
        {"slice",       {S, I, I}, S,           strSlice},
        {"chars",       {S},       t.list(S),   strChars},
        {"lines",       {S},       t.list(S),   strLines},
        {"words",       {S},       t.list(S),   strWords},
        {"trim_start",  {S},       S,           strTrimStart},
        {"trim_end",    {S},       S,           strTrimEnd},
        {"replace_first", {S, S, S}, S,         strReplaceFirst},
        {"remove",      {S, S},    S,           strRemove},
        {"repeat",      {S, I},    S,           strRepeat},
        {"reverse",     {S},       S,           strReverse},
        {"pad_left",    {S, I, S}, S,           strPadLeft,  2},
        {"pad_right",   {S, I, S}, S,           strPadRight, 2},
        {"center",      {S, I, S}, S,           strCenter,   2},
        {"capitalize",  {S},       S,           strCapitalize},
        {"title",       {S},       S,           strTitle},
        {"is_empty",    {S},       B,           strIsEmpty},
        {"is_digit",    {S},       B,           todos<ehDigito>},
        {"is_alpha",    {S},       B,           todos<ehLetra>},
        {"is_alnum",    {S},       B,           todos<ehAlnum>},
        {"is_space",    {S},       B,           todos<ehBranco>},
        {"is_upper",    {S},       B,           strIsUpper},
        {"is_lower",    {S},       B,           strIsLower},
        {"ord",         {S},       I,           strOrd},
        {"chr",         {I},       S,           strChr},
        {"equals_ignore_case", {S, S}, B,       strEqualsIgnoreCase},
        {"compare",     {S, S},    I,           strCompare},
        {"fixed",       {D, I},    S,           strFixed},
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
        {"modified",    {S},             I,         filesModified},
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
        {"decimal_range", {D, D},         D,         randDecimalRange},
        {"bool",          {},             B,         randBool},
        {"chance",        {D},            B,         randChance},
        {"shuffle",       {t.list(T)},    V,         randShuffle, 0, false, true},
        {"sample",        {t.list(T), I}, t.list(T), randSample},
        {"choices",       {t.list(T), I}, t.list(T), randChoices},
        {"gauss",         {D, D},         D,         randGauss},
    }, {}});

    m.push_back({"Convert", {
        {"to_int",     {S},   I, convToInt},
        {"to_decimal", {S},   D, convToDecimal},
        {"to_string",  {ANY}, S, convToString},
        {"to_bool",    {S},   B, convToBool},
        {"is_int",        {S},    B, convIsInt},
        {"is_decimal",    {S},    B, convIsDecimal},
        {"is_bool",       {S},    B, convIsBool},
        {"to_int_or",     {S, I}, I, convToIntOr},
        {"to_decimal_or", {S, D}, D, convToDecimalOr},
        {"to_bool_or",    {S, B}, B, convToBoolOr},
        {"to_base",       {I, I}, S, convToBase},
        {"from_base",     {S, I}, I, convFromBase},
        {"to_hex",        {I},    S, convToHex},
        {"to_binary",     {I},    S, convToBinary},
        {"to_octal",      {I},    S, convToOctal},
    }, {}});

    m.push_back({"Lists", {
        {"sort",     {t.list(C)},       V,         listSort,    0, false, true},
        {"reverse",  {t.list(T)},       V,         listReverse, 0, false, true},
        {"contains", {t.list(T), T},    B,         listContains},
        {"index_of", {t.list(T), T},    I,         listIndexOf},
        {"slice",    {t.list(T), I, I}, t.list(T), listSlice},
        {"sum",      {t.list(N)},       N,         listSum},
        {"sort_desc",     {t.list(C)},             V,         listSortDesc,    0, false, 1},
        {"sort_by",       {t.list(T), t.list(C)},  V,         listSortBy,      0, false, 3},
        {"sorted",        {t.list(C)},             t.list(C), listSorted},
        {"reversed",      {t.list(T)},             t.list(T), listReversed},
        {"min",           {t.list(C)},             C,         listMin},
        {"max",           {t.list(C)},             C,         listMax},
        {"average",       {t.list(N)},             D,         listAverage},
        {"product",       {t.list(N)},             N,         listProduct},
        {"count",         {t.list(T), T},          I,         listCount},
        {"last_index_of", {t.list(T), T},          I,         listLastIndexOf},
        {"first",         {t.list(T)},             T,         listFirst},
        {"last",          {t.list(T)},             T,         listLast},
        {"is_empty",      {t.list(T)},             B,         listIsEmpty},
        {"insert",        {t.list(T), I, T},       V,         listInsert,      0, false, 1},
        {"pop",           {t.list(T)},             T,         listPop,         0, false, 1},
        {"remove_value",  {t.list(T), T},          V,         listRemoveValue, 0, false, 1},
        {"clear",         {t.list(T)},             V,         listClear,       0, false, 1},
        {"extend",        {t.list(T), t.list(T)},  V,         listExtend,      0, false, 1},
        {"swap",          {t.list(T), I, I},       V,         listSwap,        0, false, 1},
        {"concat",        {t.list(T), t.list(T)},  t.list(T), listConcat},
        {"unique",        {t.list(T)},             t.list(T), listUnique},
        {"repeat",        {T, I},                  t.list(T), listRepeat},
        {"copy",          {t.list(T)},             t.list(T), listCopy},
    }, {}});

    m.push_back({"Time", {
        {"now",             {},                 I, timeNow},
        {"clock",           {},                 I, timeClock},
        {"sleep",           {I},                V, timeSleep},
        {"make",            {I, I, I, I, I, I}, I, timeMake,    3},
        {"make_utc",        {I, I, I, I, I, I}, I, timeMakeUtc, 3},
        {"year",            {I},    I, timeYear},
        {"month",           {I},    I, timeMonth},
        {"day",             {I},    I, timeDay},
        {"hour",            {I},    I, timeHour},
        {"minute",          {I},    I, timeMinute},
        {"second",          {I},    I, timeSecond},
        {"weekday",         {I},    I, timeWeekday},
        {"day_of_year",     {I},    I, timeDayOfYear},
        {"year_utc",        {I},    I, timeYearUtc},
        {"month_utc",       {I},    I, timeMonthUtc},
        {"day_utc",         {I},    I, timeDayUtc},
        {"hour_utc",        {I},    I, timeHourUtc},
        {"minute_utc",      {I},    I, timeMinuteUtc},
        {"second_utc",      {I},    I, timeSecondUtc},
        {"weekday_utc",     {I},    I, timeWeekdayUtc},
        {"day_of_year_utc", {I},    I, timeDayOfYearUtc},
        {"format",          {I, S}, S, timeFormat},
        {"format_utc",      {I, S}, S, timeFormatUtc},
        {"parse",           {S, S}, I, timeParse},
        {"parse_utc",       {S, S}, I, timeParseUtc},
        {"add_days",        {I, I}, I, timeAddDays},
        {"add_hours",       {I, I}, I, timeAddHours},
        {"add_minutes",     {I, I}, I, timeAddMinutes},
        {"add_seconds",     {I, I}, I, timeAddSeconds},
        {"days_between",    {I, I}, I, timeDaysBetween},
        {"is_leap_year",    {I},    B, timeIsLeapYear},
        {"days_in_month",   {I, I}, I, timeDaysInMonth},
    }, {}});

    return m;
}

} // namespace

// sem "-0.00": um resultado que arredonda para zero não leva sinal
std::string fixedText(double x, std::int64_t casas) {
    std::string r = arredondaTexto(x, casas);
    const bool zero = r.find_first_not_of("0.") == std::string::npos;
    return x < 0 && !zero ? "-" + r : r;
}

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
