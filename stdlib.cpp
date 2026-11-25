#include "natives.h"
#include "operacoes.h"   // convertValue, doubleToF32 (seção K)
#include "plataforma.h"
#include "regex.h"
#include "runtime_error.h"
#include "utf8.h"
#include <algorithm>
#include <type_traits>
#include <cctype>
#include <charconv>
#include <chrono>
#include <ctime>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>

namespace cinza {

// ============================================================================
// BIBLIOTECA PADRÃO (Fase 7)
//
// Módulos nativos, usados com import: `import Strings as st;` → st.upper(s).
// Cada função recebe os argumentos já checados pelo semântico (tipos da
// assinatura) e lança RuntimeError("Tipo: mensagem") sem linha; a CVM
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

// Maiúsculas/minúsculas: ASCII e Latin-1 (utf8.h, as mesmas do Regex)
char32_t paraMaiuscula(char32_t c) { return utf8::maiuscula(c); }
char32_t paraMinuscula(char32_t c) { return utf8::minuscula(c); }

// Número (inteiro ou ponto flutuante) como double
double numero(const Value& v) { return v.asNumber(); }

// Seção K: posição, tamanho ou contagem — i32 (cabe sempre: nenhuma string ou
// lista chega a 2^31 elementos)
Value i32Valor(std::int64_t v) { return Value::i32(static_cast<std::int32_t>(v)); }

// Resultado de ponto flutuante de uma função (seção M: infinito e NaN não
// existem): sem resultado real → ValueError; grande demais → OverflowError. Na
// largura do argumento: f32 entra e sai f32 (conta em double, arredondada uma
// vez), e precisa caber em f32.
Value naLargura(const Value& arg, double r, const char* fn) {
    if (r != r) falha(std::string("ValueError: '") + fn + "' sem resultado real");
    if (std::isinf(r)) falha(std::string("OverflowError: resultado de '") + fn + "' grande demais");
    if (arg.kind() != Value::Kind::F32) return Value(r);
    if (!cabeEmF32(r)) falha(std::string("OverflowError: resultado de '") + fn + "' não cabe em f32");
    return Value::f32(doubleToF32(r));
}

// ponto flutuante → i64 (floor, ceil, round, trunc): fora da faixa, OverflowError
Value flutuanteParaInt(double d, const char* fn) {
    // 2^63 é o primeiro double fora da faixa (i64 vai até 2^63 - 1)
    if (d < -9223372036854775808.0 || d >= 9223372036854775808.0)
        falha(std::string("OverflowError: resultado de '") + fn + "' fora do intervalo de i64");
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

// Q1b: as buscas devolvem um i32? — null quando não acham (não há -1, um null com outro nome)

// find(s, trecho): índice (em caracteres) da primeira ocorrência, ou null
Value strFind(Args a) {
    const std::string& s = a[0].asString();
    const size_t pos = s.find(a[1].asString());
    if (pos == std::string::npos) return Value::nulo();
    return i32Valor(utf8::indiceDoByte(s, pos));
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
    if (pos == std::string::npos) return Value::nulo();
    return i32Valor(utf8::indiceDoByte(s, pos));
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
    if (pos == std::string::npos) return Value::nulo();
    return i32Valor(utf8::indiceDoByte(s, pos));
}

// positions(s, trecho): os índices (em caracteres) de todas as ocorrências, sem sobreposição — as
// mesmas que o count conta; nenhuma dá []
Value strPositions(Args a) {
    const std::string& s = a[0].asString();
    const std::string& t = a[1].asString();
    if (t.empty()) falha("ValueError: o trecho de 'positions' não pode ser vazio");
    std::vector<Value> out;
    size_t anterior = 0;
    std::int64_t indice = 0;
    for (size_t pos = s.find(t); pos != std::string::npos; pos = s.find(t, pos + t.size())) {
        for (size_t i = anterior; i < pos; ++i) if (!utf8::continuacao(s[i])) ++indice;
        anterior = pos;
        out.push_back(i32Valor(indice));
    }
    return makeList(std::move(out));
}

// count: ocorrências sem sobreposição ("aaaa" tem 2 de "aa")
Value strCount(Args a) {
    const std::string& s = a[0].asString();
    const std::string& t = a[1].asString();
    if (t.empty()) falha("ValueError: o trecho de 'count' não pode ser vazio");
    std::int64_t n = 0;
    for (size_t pos = s.find(t); pos != std::string::npos; pos = s.find(t, pos + t.size())) ++n;
    return i32Valor(n);
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

// Quantos caracteres faltam para a largura (0 se já tem). Largura absurda é
// ValueError, como em repeat (antes esgotava a memória: erro interno)
size_t falta(const std::u32string& s, std::int64_t largura, const char* fn) {
    if (largura > (std::int64_t{1} << 28))   // 2^28 caracteres: até 1 GiB em UTF-8
        falha(std::string("ValueError: a largura de '") + fn + "' passaria de 1 GiB (recebeu " +
              std::to_string(largura) + ")");
    return largura > static_cast<std::int64_t>(s.size()) ? static_cast<size_t>(largura) - s.size() : 0;
}

Value strPadLeft(Args a) {
    const char32_t p = preenchimento(a, "pad_left");
    const std::u32string s = decodifica(a[0].asString());
    return Value(codifica(std::u32string(falta(s, a[1].asInt(), "pad_left"), p) + s));
}
Value strPadRight(Args a) {
    const char32_t p = preenchimento(a, "pad_right");
    const std::u32string s = decodifica(a[0].asString());
    return Value(codifica(s + std::u32string(falta(s, a[1].asInt(), "pad_right"), p)));
}
// center: a sobra ímpar fica à direita
Value strCenter(Args a) {
    const char32_t p = preenchimento(a, "center");
    const std::u32string s = decodifica(a[0].asString());
    const size_t f = falta(s, a[1].asInt(), "center");
    return Value(codifica(std::u32string(f / 2, p) + s + std::u32string(f - f / 2, p)));
}

// Letras: ASCII e as acentuadas do Latin-1 (as mesmas de upper/lower)
bool ehLetra(char32_t c) { return utf8::letra(c); }
bool ehDigito(char32_t c) { return utf8::digito(c); }
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
    return i32Valor(static_cast<std::int64_t>(s[0]));
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
    return i32Valor(r < 0 ? -1 : (r > 0 ? 1 : 0));
}

// fixed(x, casas): o número como texto com exatamente `casas` casas
// (arredonda como Math.round_to; sem "-0.00"; f32 pela forma decimal do float)
Value strFixed(Args a) {
    const double x = a[0].asFloat();
    const std::int64_t casas = a[1].asInt();
    if (casas < 0 || casas > 100)
        falha("ValueError: o número de casas de 'fixed' precisa estar entre 0 e 100");
    return Value(fixedText(x, casas, a[0].kind() == Value::Kind::F32));
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

// read_bytes / write_bytes: o arquivo como bytes (0 a 255), sem passar por texto
// UTF-8 — permite formatos binários (J2)
Value filesReadBytes(Args a) {
    const std::string s = lerArquivo(a[0].asString());
    std::vector<Value> bytes;
    bytes.reserve(s.size());
    for (unsigned char c : s) bytes.push_back(Value::i32(c));
    return makeList(std::move(bytes));
}

Value filesWriteBytes(Args a) {
    const auto& elems = a[1].asList()->elements;
    std::string s;
    s.reserve(elems.size());
    for (size_t i = 0; i < elems.size(); ++i) {
        const std::int64_t b = elems[i].asInt();
        if (b < 0 || b > 255)
            falha("ValueError: 'write_bytes': o byte " + std::to_string(b) + " (posição " +
                  std::to_string(i) + ") não está entre 0 e 255");
        s += static_cast<char>(static_cast<unsigned char>(b));
    }
    gravarArquivo(a[0].asString(), s, std::ios::trunc);
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

// Seção M: infinito e NaN não existem na Cinza. Fora do domínio lança
// ValueError (sqrt de negativo, log de 0, asin(2)) e o que passa da faixa lança
// OverflowError (exp(1000)). O resultado tem a largura do argumento (naLargura).
Value mathSqrt(Args a) {
    if (a[0].asFloat() < 0) falha("ValueError: 'sqrt' de número negativo");
    return naLargura(a[0], std::sqrt(a[0].asFloat()), "sqrt");
}
Value mathPow(Args a) {
    if (a[0].asFloat() == 0 && a[1].asFloat() < 0)
        falha("ZeroDivisionError: 'pow' de 0 com expoente negativo (divisão por zero)");
    return naLargura(a[0], std::pow(a[0].asFloat(), a[1].asFloat()), "pow");
}

// abs: o resultado tem o tipo do argumento (seção K)
Value mathAbs(Args a) {
    switch (a[0].kind()) {
        case Value::Kind::I64: {
            const std::int64_t v = a[0].asInt();
            if (v == std::numeric_limits<std::int64_t>::min())
                falha("OverflowError: 'abs' do menor i64 não cabe em i64");
            return Value(v < 0 ? -v : v);
        }
        case Value::Kind::I32: {
            const std::int32_t v = a[0].asI32();
            if (v == std::numeric_limits<std::int32_t>::min())
                falha("OverflowError: 'abs' do menor i32 não cabe em i32");
            return Value::i32(v < 0 ? -v : v);
        }
        case Value::Kind::F32: return Value::f32(std::fabs(a[0].asF32()));
        default:               return Value(std::fabs(a[0].asFloat()));
    }
}

Value mathFloor(Args a) { return flutuanteParaInt(std::floor(a[0].asFloat()), "floor"); }
Value mathCeil (Args a) { return flutuanteParaInt(std::ceil (a[0].asFloat()), "ceil"); }
// round: metade se afasta do zero (2.5 → 3, -2.5 → -3)
Value mathRound(Args a) { return flutuanteParaInt(std::round(a[0].asFloat()), "round"); }

Value mathMin(Args a) { return numero(a[1]) < numero(a[0]) ? a[1] : a[0]; }
Value mathMax(Args a) { return numero(a[1]) > numero(a[0]) ? a[1] : a[0]; }

Value mathSin(Args a)   { return naLargura(a[0], std::sin(a[0].asFloat()), "sin"); }
Value mathCos(Args a)   { return naLargura(a[0], std::cos(a[0].asFloat()), "cos"); }
Value mathTan(Args a)   { return naLargura(a[0], std::tan(a[0].asFloat()), "tan"); }
// asin/acos: só de -1 a 1
Value mathAsin(Args a) {
    if (a[0].asFloat() < -1 || a[0].asFloat() > 1) falha("ValueError: 'asin' só é definido de -1 a 1");
    return naLargura(a[0], std::asin(a[0].asFloat()), "asin");
}
Value mathAcos(Args a) {
    if (a[0].asFloat() < -1 || a[0].asFloat() > 1) falha("ValueError: 'acos' só é definido de -1 a 1");
    return naLargura(a[0], std::acos(a[0].asFloat()), "acos");
}
Value mathAtan(Args a)  { return naLargura(a[0], std::atan(a[0].asFloat()), "atan"); }
Value mathAtan2(Args a) { return naLargura(a[0], std::atan2(a[0].asFloat(), a[1].asFloat()), "atan2"); }

constexpr double PI = 3.14159265358979323846;
Value mathDegrees(Args a) { return naLargura(a[0], a[0].asFloat() * (180.0 / PI), "degrees"); }
Value mathRadians(Args a) { return naLargura(a[0], a[0].asFloat() * (PI / 180.0), "radians"); }

Value mathSinh(Args a)  { return naLargura(a[0], std::sinh(a[0].asFloat()), "sinh"); }
Value mathCosh(Args a)  { return naLargura(a[0], std::cosh(a[0].asFloat()), "cosh"); }
Value mathTanh(Args a)  { return naLargura(a[0], std::tanh(a[0].asFloat()), "tanh"); }
// Q4: o cbrt da libm pode errar o último bit (o do glibc dá 3.0000000000000004 para 27, o do
// MinGW dá 3): fica, entre ele e os dois vizinhos, o de cubo mais perto de x (em long double) —
// o mesmo resultado em todo sistema, e exato nos cubos perfeitos
double raizCubica(double x) {
    const double r = std::cbrt(x);
    if (!std::isfinite(r) || r == 0) return r;
    double melhor = r;
    auto erro = [x](double c) {
        const long double lc = c;
        return std::fabs(lc * lc * lc - static_cast<long double>(x));
    };
    long double menor = erro(r);
    for (double c : {std::nextafter(r, -HUGE_VAL), std::nextafter(r, HUGE_VAL)})
        if (const long double e = erro(c); e < menor) {
            menor = e;
            melhor = c;
        }
    return melhor;
}
Value mathCbrt(Args a)  { return naLargura(a[0], raizCubica(a[0].asFloat()), "cbrt"); }
Value mathExp(Args a)   { return naLargura(a[0], std::exp(a[0].asFloat()), "exp"); }
Value mathHypot(Args a) { return naLargura(a[0], std::hypot(a[0].asFloat(), a[1].asFloat()), "hypot"); }

// log, log10, log2: só para positivos
Value positivo(const Value& x, const char* fn) {
    if (x.asFloat() <= 0) falha(std::string("ValueError: '") + fn + "' só é definido para números positivos");
    return x;
}
Value mathLog(Args a)   { return naLargura(a[0], std::log(positivo(a[0], "log").asFloat()), "log"); }
Value mathLog10(Args a) { return naLargura(a[0], std::log10(positivo(a[0], "log10").asFloat()), "log10"); }
Value mathLog2(Args a)  { return naLargura(a[0], std::log2(positivo(a[0], "log2").asFloat()), "log2"); }
// log_base(x, base): logaritmo de x na base dada
Value mathLogBase(Args a) {
    const double x = positivo(a[0], "log_base").asFloat(), base = a[1].asFloat();
    if (base <= 0 || base == 1) falha("ValueError: a base de 'log_base' precisa ser positiva e diferente de 1");
    return naLargura(a[0], std::log(x) / std::log(base), "log_base");
}

// trunc: corta as casas, em direção ao zero
Value mathTrunc(Args a) { return flutuanteParaInt(std::trunc(a[0].asFloat()), "trunc"); }

// round_to(x, casas): arredonda o número como ele é escrito (a forma decimal
// mais curta, a mesma do print), não o valor binário — round_to(2.675, 2) é
// 2.68 (em binário 2.675 é 2.67499999...). Metade se afasta do zero, como round.
// |x| arredondado para `casas` casas, como texto sem sinal e com exatamente
// `casas` dígitos depois do ponto (nenhum ponto se casas = 0). Usado por
// round_to e Strings.fixed, que assim arredondam igual.
std::string arredondaTexto(double x, std::int64_t casas, bool f32 = false) {
    char buf[512];   // o maior decimal tem 309 dígitos inteiros
    // f32: a forma decimal mais curta do próprio float, não a do double que o guarda
    auto [fim, ec] = f32
        ? std::to_chars(buf, buf + sizeof buf, std::fabs(static_cast<float>(x)), std::chars_format::fixed)
        : std::to_chars(buf, buf + sizeof buf, std::fabs(x), std::chars_format::fixed);
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

// f32 arredonda a forma decimal do float (round_to(2.675f, 2) = 2.68f)
Value mathRoundTo(Args a) {
    const double x = a[0].asFloat();
    const std::int64_t casas = a[1].asInt();
    if (casas < 0) falha("ValueError: o número de casas de 'round_to' não pode ser negativo");
    if (casas > 400) return a[0];   // além de 400 nenhum número tem casas para cortar
    const bool f32 = a[0].kind() == Value::Kind::F32;
    const std::string r = arredondaTexto(x, casas, f32);
    double v = 0;
    std::from_chars(r.data(), r.data() + r.size(), v);
    if (v == 0) return naLargura(a[0], 0.0, "round_to");   // sem -0
    return naLargura(a[0], x < 0 ? -v : v, "round_to");
}

// Magnitude de um i64 como unsigned (|menor i64| não cabe em i64)
std::uint64_t magnitude(std::int64_t v) {
    return v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

std::uint64_t mdcU(std::uint64_t x, std::uint64_t y) {
    while (y != 0) { const std::uint64_t r = x % y; x = y; y = r; }
    return x;
}

// Inteiro na largura do argumento (gcd/lcm de i32 dão i32), com OverflowError
// se não couber
Value inteiroNaLargura(const Value& arg, std::uint64_t r, const char* fn) {
    const bool i32 = arg.kind() == Value::Kind::I32;
    const std::uint64_t max = i32 ? static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
                                  : static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (r > max)
        falha(std::string("OverflowError: resultado de '") + fn + "' fora do intervalo de " + (i32 ? "i32" : "i64"));
    return i32 ? Value::i32(static_cast<std::int32_t>(r)) : Value(static_cast<std::int64_t>(r));
}

// gcd: máximo divisor comum, sempre >= 0; gcd(0, 0) = 0
Value mathGcd(Args a) {
    return inteiroNaLargura(a[0], mdcU(magnitude(a[0].asInt()), magnitude(a[1].asInt())), "gcd");
}

// lcm: mínimo múltiplo comum, sempre >= 0; com um zero, 0
Value mathLcm(Args a) {
    const std::uint64_t x = magnitude(a[0].asInt()), y = magnitude(a[1].asInt());
    if (x == 0 || y == 0) return inteiroNaLargura(a[0], 0, "lcm");
    std::uint64_t r;
    if (__builtin_mul_overflow(x / mdcU(x, y), y, &r))
        r = std::numeric_limits<std::uint64_t>::max();   // não cabe em nenhuma largura
    return inteiroNaLargura(a[0], r, "lcm");
}

Value mathIsEven(Args a) { return Value(a[0].asInt() % 2 == 0); }
Value mathIsOdd(Args a)  { return Value(a[0].asInt() % 2 != 0); }

// sign: -1, 0 ou 1 (i32)
Value mathSign(Args a) {
    const double x = numero(a[0]);
    return i32Valor(x > 0 ? 1 : (x < 0 ? -1 : 0));
}

// clamp(x, min, max): x limitado ao intervalo
Value mathClamp(Args a) {
    if (numero(a[1]) > numero(a[2]))
        falha("ValueError: em 'clamp' o mínimo não pode ser maior que o máximo");
    if (numero(a[0]) < numero(a[1])) return a[1];
    if (numero(a[0]) > numero(a[2])) return a[2];
    return a[0];
}

// factorial(n): até 20! (o maior que cabe em i64)
Value mathFactorial(Args a) {
    const std::int64_t n = a[0].asInt();
    if (n < 0)  falha("ValueError: 'factorial' de número negativo");
    if (n > 20) falha("OverflowError: 'factorial(" + std::to_string(n) + ")' não cabe em i64 (o máximo é 20)");
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
// is_close: em f64, diferença relativa até 1e-9 (ou absoluta até 1e-12, perto de
// zero); em f32, que tem 7 dígitos, relativa até 1e-5 (ou absoluta até 1e-7)
Value mathIsClose(Args a) {
    const double x = a[0].asFloat(), y = a[1].asFloat();
    const bool f32 = a[0].kind() == Value::Kind::F32;
    const double dif = std::fabs(x - y);
    return Value(dif <= (f32 ? 1e-5 : 1e-9) * std::max(std::fabs(x), std::fabs(y)) || dif <= (f32 ? 1e-7 : 1e-12));
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

// i32(a, b) / i64(a, b): inteiro de a até b, os dois inclusive
Value sorteiaInteiro(Args a, const char* fn) {
    const std::int64_t lo = a[0].asInt();
    const std::int64_t hi = a[1].asInt();
    if (lo > hi)
        falha(std::string("ValueError: '") + fn + "(" + std::to_string(lo) + ", " + std::to_string(hi) +
              ")': o início é maior que o fim");
    std::uniform_int_distribution<std::int64_t> d(lo, hi);
    return Value(d(gerador()));
}
Value randI32(Args a) { return i32Valor(sorteiaInteiro(a, "i32").asInt()); }
Value randI64(Args a) { return sorteiaInteiro(a, "i64"); }

// f64(): de 0.0 (inclusive) até 1.0 (exclusive)
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

// f64_range(a, b): de a (inclusive) até b (exclusive)
Value randDecimalRange(Args a) {
    const double lo = a[0].asFloat(), hi = a[1].asFloat();
    if (!(lo < hi))
        falha("ValueError: em 'f64_range' o início precisa ser menor que o fim");
    if (!std::isfinite(hi - lo)) falha("ValueError: intervalo de 'f64_range' grande demais");
    std::uniform_real_distribution<double> d(lo, hi);
    const double r = d(gerador());
    return Value(r < hi ? r : lo);   // arredondamento nunca entrega o fim
}

Value randBool(Args) { return Value(std::uniform_int_distribution<int>(0, 1)(gerador()) == 1); }

// chance(p): true com probabilidade p; chance(0) nunca, chance(1) sempre
Value randChance(Args a) {
    const double p = a[0].asFloat();
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
    const double media = a[0].asFloat(), desvio = a[1].asFloat();
    if (desvio < 0) falha("ValueError: o desvio de 'gauss' não pode ser negativo");
    if (desvio == 0) return Value(media);
    const double r = std::normal_distribution<double>(media, desvio)(gerador());
    if (!std::isfinite(r)) falha("OverflowError: resultado de 'gauss' grande demais");
    return Value(r);
}

// ============================================================================
// Convert
// ============================================================================

// is_* e to_*_or aceitam exatamente o texto que cast<T> aceita (a leitura
// estrita mora em convertValue, operacoes.cpp): is_i32(s) é true se cast<i32>(s)
// não lançaria erro. Só bool tem leitura própria ("true" ou "false").
bool converte(const Value& s, TypeRef t, Value& out) {
    try {
        out = convertValue(s, t);
        return true;
    } catch (const RuntimeError&) {
        return false;
    }
}
// Q1b: o texto convertido como no cast<T>, ou null se ele não for um T válido (formato e faixa)
template <TypeRef (TypeContext::*TIPO)()>
Value convTo(Args a) { Value v; return converte(a[0], (TypeContext::instance().*TIPO)(), v) ? v : Value::nulo(); }

bool lerBool(const std::string& s, bool& v) {
    if (s == "true")  { v = true;  return true; }
    if (s == "false") { v = false; return true; }
    return false;
}

// Q1b: só "true" ou "false"; outro texto dá null
Value convToBool(Args a) { bool v = false; return lerBool(a[0].asString(), v) ? Value(v) : Value::nulo(); }

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
            falha("OverflowError: '" + s + "' na base " + std::to_string(base) + " não cabe em i64");
    }
    return Value(negativo ? static_cast<std::int64_t>(std::uint64_t{0} - m) : static_cast<std::int64_t>(m));
}

// ============================================================================
// Lists
// ============================================================================

// Ordem de valores comparáveis (números ou strings; a lista é homogênea)
bool menor(const Value& x, const Value& y) {
    if (x.kind() == Value::Kind::STRING) return x.asString() < y.asString();
    if (x.isFloat()) return x.asFloat() < y.asFloat();
    return x.asInt() < y.asInt();
}

// Q11: uma lista só de números do mesmo tipo é ordenada pelos números crus (a mesma ordenação
// estável e a mesma comparação do `menor`), sem comparar Value por um ponteiro de função
template <typename Num>
void ordenaCrus(std::vector<Value>& elems, bool desc) {
    std::vector<Num> n;
    n.reserve(elems.size());
    for (const Value& v : elems) {
        if constexpr (std::is_same_v<Num, double>) n.push_back(v.asFloat());
        else n.push_back(v.asInt());
    }
    if (desc) std::stable_sort(n.begin(), n.end(), [](Num x, Num y) { return y < x; });
    else std::stable_sort(n.begin(), n.end());
    const Value::Kind k = elems[0].kind();
    for (std::size_t i = 0; i < n.size(); ++i) {
        switch (k) {
            case Value::Kind::I32: elems[i] = Value::i32(static_cast<std::int32_t>(n[i])); break;
            case Value::Kind::F32: elems[i] = Value::f32(static_cast<float>(n[i])); break;
            default:               elems[i] = Value(n[i]); break;
        }
    }
}
// Ordena no lugar; devolve false se a lista não é só de números do mesmo tipo
bool ordenaNumeros(std::vector<Value>& elems, bool desc) {
    if (elems.empty()) return true;
    const Value::Kind k = elems[0].kind();
    for (const Value& v : elems) if (v.kind() != k) return false;
    switch (k) {
        case Value::Kind::I32: case Value::Kind::I64: ordenaCrus<std::int64_t>(elems, desc); return true;
        case Value::Kind::F32: case Value::Kind::F64: ordenaCrus<double>(elems, desc); return true;
        default: return false;
    }
}

Value listSort(Args a) {
    auto& elems = a[0].asList()->elements;
    if (!ordenaNumeros(elems, false)) std::stable_sort(elems.begin(), elems.end(), menor);
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
    if (it == elems.end()) return Value::nulo();
    return i32Valor(static_cast<std::int64_t>(it - elems.begin()));
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

// Soma/produto de lista com o tipo dos elementos (seções K e M): ponto
// flutuante que passa da faixa lança OverflowError (f32 conta em float); i32
// conta em 64 bits e o total precisa caber em i32. Lista vazia → o
// neutro como inteiro de 64 bits, que a CVM ajusta ao tipo estático da chamada
// (CONV depois do CALLNATIVE de uma nativa genérica)
static Value foldNumeros(Args a, bool soma, const char* nome) {
    const auto& elems = a[0].asList()->elements;
    const std::string oque = soma ? "a soma de '" : "o produto de '";
    if (!elems.empty() && elems[0].isFloat()) {
        if (elems[0].kind() == Value::Kind::F32) {
            float total = soma ? 0.0f : 1.0f;
            for (const auto& v : elems) total = soma ? total + v.asF32() : total * v.asF32();
            if (!std::isfinite(total)) falha("OverflowError: " + oque + nome + "' excede a faixa de f32");
            return Value::f32(total);
        }
        double total = soma ? 0.0 : 1.0;
        for (const auto& v : elems) total = soma ? total + v.asFloat() : total * v.asFloat();
        if (!std::isfinite(total)) falha("OverflowError: " + oque + nome + "' excede a faixa de f64");
        return Value(total);
    }
    const bool i32 = !elems.empty() && elems[0].kind() == Value::Kind::I32;
    std::int64_t total = soma ? 0 : 1;
    for (const auto& v : elems)
        if (soma ? __builtin_add_overflow(total, v.asInt(), &total)
                 : __builtin_mul_overflow(total, v.asInt(), &total))
            falha("OverflowError: " + oque + nome + "' não cabe em " + (i32 ? "i32" : "i64"));
    if (i32) {
        if (total < std::numeric_limits<std::int32_t>::min() || total > std::numeric_limits<std::int32_t>::max())
            falha("OverflowError: " + oque + nome + "' não cabe em i32");
        return Value::i32(static_cast<std::int32_t>(total));
    }
    return Value(total);
}

// sum(l): soma; lista vazia → 0
Value listSum(Args a) { return foldNumeros(a, true, "sum"); }

Value listSortDesc(Args a) {
    auto& elems = a[0].asList()->elements;
    if (!ordenaNumeros(elems, true))
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
    if (!ordenaNumeros(out, false)) std::stable_sort(out.begin(), out.end(), menor);
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

// average: sempre f64. A soma é acumulada em long double (faixa bem maior que a
// do double no g++/clang), então só a média final precisa caber em f64:
// average([1e308, 1e308]) é 1e308, não um estouro da soma intermediária
Value listAverage(Args a) {
    const auto& elems = naoVazia(a, "average");
    long double total = 0;
    for (const auto& v : elems) total += static_cast<long double>(numero(v));
    const long double media = total / static_cast<long double>(elems.size());
    if (!std::isfinite(media) || std::fabs(media) > static_cast<long double>(std::numeric_limits<double>::max()))
        falha("OverflowError: a média de 'average' excede a faixa de f64");
    return Value(static_cast<double>(media));
}

// product: lista vazia dá 1; estouro lança OverflowError
Value listProduct(Args a) { return foldNumeros(a, false, "product"); }

Value listCount(Args a) {
    const auto& elems = a[0].asList()->elements;
    return i32Valor(static_cast<std::int64_t>(std::count(elems.begin(), elems.end(), a[1])));
}

Value listLastIndexOf(Args a) {
    const auto& elems = a[0].asList()->elements;
    for (size_t i = elems.size(); i > 0; --i)
        if (elems[i - 1] == a[1]) return i32Valor(static_cast<std::int64_t>(i - 1));
    return Value::nulo();
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

// Valores em que operator< é uma ordem total, coerente com == (os demais são
// comparados com ==)
bool ordenavel(const Value& v) {
    if (v.isNumber()) return true;
    switch (v.kind()) {
        case Value::Kind::STRING: case Value::Kind::BOOL: case Value::Kind::ENUM:
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
// Time — uma data/hora é um i64: segundos desde 1970-01-01 00:00 UTC. As partes,
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
        falha(std::string("OverflowError: resultado de '") + fn + "' fora do intervalo de i64");
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

// ============================================================================
// Dicts (Q3) — funções de dict, como Lists faz para list. O dict é ordenado pela
// chave (std::map); nada falha em silêncio: chave repetida ao montar um dict e
// valor repetido ao inverter lançam ValueError, chave ausente lança KeyError.
// ============================================================================

Entradas& entradas(const Value& d) { return d.asDict()->entries; }

// Q1b: o valor da chave, ou null se ela não existir (Dicts.get(d, k) else padrão)
Value dictGet(Args a) {
    const auto& e = entradas(a[0]);
    auto it = e.find(a[1]);
    return it == e.end() ? Value::nulo() : it->second;
}

Value dictContainsValue(Args a) {
    for (const auto& [k, v] : entradas(a[0]))
        if (v == a[1]) return Value(true);
    return Value(false);
}

// keys_of(d, v): as chaves com o valor v, em ordem
Value dictKeysOf(Args a) {
    std::vector<Value> out;
    for (const auto& [k, v] : entradas(a[0]))
        if (v == a[1]) out.push_back(k);
    return makeList(std::move(out));
}

Value dictIsEmpty(Args a) { return Value(entradas(a[0]).empty()); }

Value dictItems(Args a) {
    std::vector<Value> out;
    for (const auto& [k, v] : entradas(a[0])) out.push_back(makePair(k, v));
    return makeList(std::move(out));
}

// update(d, outra): as entradas de outra entram em d (as chaves repetidas ficam com o valor de outra)
Value dictUpdate(Args a) {
    const Entradas outra = entradas(a[1]);   // cópia: outra pode ser d
    for (const auto& [k, v] : outra) entradas(a[0])[k] = v;
    return Value();
}

// pop(d, k): remove a chave e devolve o valor
Value dictPop(Args a) {
    auto& e = entradas(a[0]);
    auto it = e.find(a[1]);
    if (it == e.end()) falha("KeyError: 'pop': a chave '" + a[1].toString() + "' não existe");
    Value v = std::move(it->second);
    e.erase(it);
    return v;
}

Value dictClear(Args a) {
    entradas(a[0]).clear();
    return Value();
}

Value dictMerge(Args a) {
    Value r = makeDict();
    entradas(r) = entradas(a[0]);
    for (const auto& [k, v] : entradas(a[1])) entradas(r)[k] = v;
    return r;
}

Value dictCopy(Args a) {
    Value r = makeDict();
    entradas(r) = entradas(a[0]);
    return r;
}

// insere sem substituir: chave repetida é ValueError (o dict perderia um valor calado)
void insereNova(Entradas& e, const Value& k, const Value& v, const char* fn) {
    if (!e.emplace(k, v).second)
        falha(std::string("ValueError: '") + fn + "': a chave '" + k.toString() + "' aparece mais de uma vez");
}

Value dictFromLists(Args a) {
    const auto& ks = a[0].asList()->elements;
    const auto& vs = a[1].asList()->elements;
    if (ks.size() != vs.size())
        falha("ValueError: 'from_lists': " + std::to_string(ks.size()) + " chaves e " +
              std::to_string(vs.size()) + " valores");
    Value r = makeDict();
    for (size_t i = 0; i < ks.size(); ++i) insereNova(entradas(r), ks[i], vs[i], "from_lists");
    return r;
}

Value dictFromPairs(Args a) {
    Value r = makeDict();
    for (const Value& p : a[0].asList()->elements)
        insereNova(entradas(r), p.asPair()->first, p.asPair()->second, "from_pairs");
    return r;
}

// invert(d): os valores viram as chaves; valor repetido é ValueError
Value dictInvert(Args a) {
    Value r = makeDict();
    for (const auto& [k, v] : entradas(a[0]))
        if (!entradas(r).emplace(v, k).second)
            falha("ValueError: 'invert': o valor '" + v.toString() + "' aparece em mais de uma chave");
    return r;
}

// ============================================================================
// Env e Process (Q3) — o ambiente, só para ler (mudá-lo seria um estado global), e
// rodar outro programa sem shell: o programa e a lista de argumentos, sem nenhuma
// interpretação (sem injeção de comando, igual em todo sistema).
// ============================================================================

std::string nomeDeAmbiente(const Value& v, const char* fn) {
    const std::string n = v.asString();
    if (n.empty() || n.find('=') != std::string::npos || n.find('\0') != std::string::npos)
        falha(std::string("ValueError: '") + fn + "': nome de variável de ambiente inválido: '" + n + "'");
    return n;
}

// Q1b: o valor da variável, ou null se ela não existir (Env.get("X") else "padrão")
Value envGet(Args a) {
    const std::string n = nomeDeAmbiente(a[0], "get");
    std::string v;
    if (!ambienteUtf8(n.c_str(), v)) return Value::nulo();
    return Value(v);
}

Value envAll(Args) {
    Value r = makeDict();
    for (auto& [n, v] : ambienteTodo()) r.asDict()->entries[Value(n)] = Value(v);
    return r;
}

// O programa e os argumentos, conferidos: um script .bat/.cmd o Windows roda pelo cmd,
// com as regras de shell dele — sem shell, ele precisa ser pedido explicitamente
std::vector<std::string> comando(Args a, const char* fn) {
    const std::string prog = a[0].asString();
    if (prog.empty()) falha(std::string("ValueError: '") + fn + "': o nome do programa está vazio");
    std::string ext = textoCaminho(caminhoUtf8(prog).extension());
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".bat" || ext == ".cmd")
        falha(std::string("ValueError: '") + fn + "': '" + prog + "' é um script do cmd; rode-o pelo "
              "cmd explicitamente: Process." + fn + "(\"cmd\", [\"/c\", \"" + prog + "\"])");
    std::vector<std::string> cmd{prog};
    if (a.size() > 1)   // os argumentos são opcionais: Process.run("programa")
        for (const Value& x : a[1].asList()->elements) cmd.push_back(x.asString());
    for (const std::string& s : cmd)
        if (s.find('\0') != std::string::npos)
            falha(std::string("ValueError: '") + fn + "': um argumento tem o caractere nulo");
    return cmd;
}

int roda(Args a, const char* fn, std::string* saida) {
    const std::vector<std::string> cmd = comando(a, fn);
    std::cout.flush();   // o que o programa já imprimiu sai antes do outro
    std::fflush(stdout);
    int codigo = 0;
    if (!executa(cmd, codigo, saida)) falhaIO(std::string("'") + fn + "': não foi possível executar", cmd[0]);
    return codigo;
}

Value procRun(Args a) { return i32Valor(roda(a, "run", nullptr)); }

Value procCapture(Args a) {
    std::string saida;
    const int codigo = roda(a, "capture", &saida);
    return makePair(i32Valor(codigo), Value(saida));
}

// ============================================================================
// Regex (Q3) — o motor próprio de regex.h (tempo linear, em caracteres). Como em
// Strings, o texto vem primeiro e as posições contam caracteres. Várias
// ocorrências seguem o RE2: depois de uma vazia a busca anda um caractere, e uma
// vazia logo depois da anterior não conta.
// ============================================================================

// O padrão compilado, guardado: um laço que repete o mesmo padrão não recompila
const regex::Regex& padrao(const Value& v, const char* fn) {
    static std::map<std::string, std::unique_ptr<regex::Regex>> cache;
    const std::string p = v.asString();
    auto it = cache.find(p);
    if (it != cache.end()) return *it->second;
    std::unique_ptr<regex::Regex> re;
    try {
        re = std::make_unique<regex::Regex>(decodifica(p));
    } catch (const regex::ErroDePadrao& e) {
        falha(std::string("ValueError: '") + fn + "': padrão inválido: " + e.what() + " (na posição " +
              std::to_string(e.posicao) + " de '" + p + "')");
    }
    if (cache.size() >= 64) cache.clear();
    return *cache.emplace(p, std::move(re)).first->second;
}

std::string trecho(const std::u32string& s, int a, int b) {
    return a < 0 ? std::string() : codifica(s.substr(static_cast<std::size_t>(a), static_cast<std::size_t>(b - a)));
}

// Todas as ocorrências (as posições de cada uma), no máximo `limite` (-1: todas)
std::vector<std::vector<int>> ocorrencias(const regex::Regex& re, const std::u32string& s, int limite = -1) {
    std::vector<std::vector<int>> r;
    const int n = static_cast<int>(s.size());
    int anterior = -1;
    for (int pos = 0; pos <= n && (limite < 0 || static_cast<int>(r.size()) < limite);) {
        std::vector<int> caps;
        if (!re.procura(s, static_cast<std::size_t>(pos), caps)) break;
        const bool aceita = !(caps[1] == caps[0] && caps[0] == anterior);
        pos = caps[1] > caps[0] ? caps[1] : caps[1] + 1;
        anterior = caps[1];
        if (aceita) r.push_back(std::move(caps));
    }
    return r;
}

Value rxMatches(Args a) {
    std::vector<int> caps;
    return Value(padrao(a[1], "matches").procura(decodifica(a[0].asString()), 0, caps, true));
}

Value rxContains(Args a) {
    std::vector<int> caps;
    return Value(padrao(a[1], "contains").procura(decodifica(a[0].asString()), 0, caps));
}

Value rxFind(Args a) {
    std::vector<int> caps;
    if (!padrao(a[1], "find").procura(decodifica(a[0].asString()), 0, caps)) return Value::nulo();
    return i32Valor(caps[0]);
}

Value rxFindAll(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    std::vector<Value> out;
    for (const auto& c : ocorrencias(padrao(a[1], "find_all"), s)) out.push_back(Value(trecho(s, c[0], c[1])));
    return makeList(std::move(out));
}

Value rxCount(Args a) {
    return i32Valor(static_cast<std::int64_t>(ocorrencias(padrao(a[1], "count"), decodifica(a[0].asString())).size()));
}

// A ocorrência e os grupos — Q1b: o que não participou é null (o que casou o vazio, "")
Value grupo(const std::u32string& s, int ini, int fim) {
    return ini < 0 ? Value::nulo() : Value(trecho(s, ini, fim));
}
Value gruposDe(const std::u32string& s, const std::vector<int>& c) {
    std::vector<Value> g;
    for (std::size_t i = 0; i + 1 < c.size(); i += 2) g.push_back(grupo(s, c[i], c[i + 1]));
    return makeList(std::move(g));
}

Value rxGroups(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    std::vector<int> caps;
    if (!padrao(a[1], "groups").procura(s, 0, caps)) return makeList();
    return gruposDe(s, caps);
}

Value rxGroupsAll(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    std::vector<Value> out;
    for (const auto& c : ocorrencias(padrao(a[1], "groups_all"), s)) out.push_back(gruposDe(s, c));
    return makeList(std::move(out));
}

Value rxNamed(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    const regex::Regex& re = padrao(a[1], "named");
    Value r = makeDict();
    std::vector<int> caps;
    if (!re.procura(s, 0, caps)) return r;
    for (std::size_t g = 0; g < re.grupos(); ++g)
        if (!re.nomes()[g].empty())
            r.asDict()->entries[Value(re.nomes()[g])] = grupo(s, caps[2 * (g + 1)], caps[2 * (g + 1) + 1]);
    return r;
}

// O texto de troca, com $1, ${1}, ${nome} e $$ — uma referência inválida é erro
std::string expande(const std::u32string& troca, const regex::Regex& re, const std::u32string& s,
                    const std::vector<int>& c, const char* fn) {
    std::u32string out;
    auto grupo = [&](std::size_t g) {
        if (c[2 * g] >= 0) out += s.substr(static_cast<std::size_t>(c[2 * g]), static_cast<std::size_t>(c[2 * g + 1] - c[2 * g]));
    };
    auto invalida = [&](const std::u32string& ref) {
        falha(std::string("ValueError: '") + fn + "': referência inválida no texto de troca: '" + codifica(ref) +
              "' (use $1, ${1}, ${nome}, ou $$ para um '$')");
    };
    for (std::size_t i = 0; i < troca.size(); ++i) {
        if (troca[i] != U'$') { out += troca[i]; continue; }
        if (i + 1 >= troca.size()) invalida(U"$");
        const char32_t d = troca[i + 1];
        if (d == U'$') { out += U'$'; ++i; continue; }
        std::u32string nome;
        std::size_t fim = i + 1;
        if (d == U'{') {
            const std::size_t fecha = troca.find(U'}', i + 2);
            if (fecha == std::u32string::npos) invalida(troca.substr(i));
            nome = troca.substr(i + 2, fecha - i - 2);
            fim = fecha;
        } else if (utf8::digito(d)) {
            while (fim < troca.size() && utf8::digito(troca[fim])) nome += troca[fim++];
            --fim;
        } else {
            invalida(troca.substr(i, 2));
        }
        const std::u32string ref = troca.substr(i, fim - i + 1);
        if (nome.empty()) invalida(ref);
        if (std::all_of(nome.begin(), nome.end(), utf8::digito)) {
            const std::string n8 = codifica(nome);
            if (n8.size() > 6 || std::stoul(n8) > re.grupos()) invalida(ref);
            grupo(std::stoul(n8));
        } else {
            auto it = std::find(re.nomes().begin(), re.nomes().end(), codifica(nome));
            if (it == re.nomes().end()) invalida(ref);
            grupo(static_cast<std::size_t>(it - re.nomes().begin()) + 1);
        }
        i = fim;
    }
    return codifica(out);
}

Value troca(Args a, const char* fn, int limite) {
    const std::u32string s = decodifica(a[0].asString());
    const regex::Regex& re = padrao(a[1], fn);
    const std::u32string t = decodifica(a[2].asString());
    std::string out;
    int ate = 0;
    for (const auto& c : ocorrencias(re, s, limite)) {
        out += codifica(s.substr(static_cast<std::size_t>(ate), static_cast<std::size_t>(c[0] - ate)));
        out += expande(t, re, s, c, fn);
        ate = c[1];
    }
    out += codifica(s.substr(static_cast<std::size_t>(ate)));
    return Value(out);
}

Value rxReplaceAll(Args a) { return troca(a, "replace_all", -1); }
Value rxReplaceFirst(Args a) { return troca(a, "replace_first", 1); }

// split: as partes entre as ocorrências (como o RE2: uma ocorrência vazia no começo
// não gera parte vazia; texto vazio dá [""])
Value rxSplit(Args a) {
    const std::u32string s = decodifica(a[0].asString());
    const regex::Regex& re = padrao(a[1], "split");
    if (s.empty()) return makeList({Value(std::string())});
    std::vector<Value> out;
    int ini = 0, fim = 0;
    for (const auto& c : ocorrencias(re, s)) {
        fim = c[0];
        if (c[1] != 0) out.push_back(Value(trecho(s, ini, fim)));
        ini = c[1];
    }
    if (fim != static_cast<int>(s.size())) out.push_back(Value(trecho(s, ini, static_cast<int>(s.size()))));
    return makeList(std::move(out));
}

// escape: o texto como padrão literal
Value rxEscape(Args a) {
    std::string out;
    for (char c : a[0].asString()) {
        if (std::strchr("\\.+*?()|[]{}^$-", c) && c != '\0') out += '\\';
        out += c;
    }
    return Value(out);
}

Value rxIsValid(Args a) {
    try {
        regex::Regex re(decodifica(a[0].asString()));
        return Value(true);
    } catch (const regex::ErroDePadrao&) {
        return Value(false);
    }
}

std::vector<NativeModule> criaModulos() {
    auto& t = TypeContext::instance();
    const TypeRef I = t.i64Type(), D = t.f64Type(), S = t.stringType(),
                  B = t.boolType(), V = t.voidType();
    const TypeRef T   = t.typeVar("T");
    const TypeRef I32 = t.i32Type();   // posições, tamanhos e contagens (seção K)
    const TypeRef F32 = t.f32Type();
    const TypeRef N   = t.typeVar("número");
    const TypeRef F   = t.typeVar("flutuante");   // f32 ou f64: o resultado tem a mesma largura
    const TypeRef Z   = t.typeVar("inteiro");     // i32 ou i64
    const TypeRef C   = t.typeVar("comparável");

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
        {"find",        {S, S},    t.optional(I32), strFind},
        {"starts_with", {S, S},    B,           strStartsWith},
        {"ends_with",   {S, S},    B,           strEndsWith},
        {"find_last",   {S, S},    t.optional(I32), strFindLast},
        {"find_from",   {S, S, I}, t.optional(I32), strFindFrom},
        {"positions",   {S, S},    t.list(I32), strPositions},
        {"count",       {S, S},    I32,         strCount},
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
        {"ord",         {S},       I32,         strOrd},
        {"chr",         {I},       S,           strChr},
        {"equals_ignore_case", {S, S}, B,       strEqualsIgnoreCase},
        {"compare",     {S, S},    I32,         strCompare},
        {"fixed",       {F, I},    S,           strFixed},
    }, {}});

    m.push_back({"Files", {
        {"read",        {S},             S,         filesRead},
        {"lines",       {S},             t.list(S), filesLines},
        {"write",       {S, S},          V,         filesWrite},
        {"read_bytes",  {S},             t.list(I32), filesReadBytes},
        {"write_bytes", {S, t.list(Z)},  V,         filesWriteBytes},
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
        {"sqrt",  {F},    F, mathSqrt},
        {"pow",   {F, F}, F, mathPow},
        {"abs",   {N},    N, mathAbs},
        {"floor", {F},    I, mathFloor},
        {"ceil",  {F},    I, mathCeil},
        {"round", {F},    I, mathRound},
        {"min",   {N, N}, N, mathMin},
        {"max",   {N, N}, N, mathMax},
        {"sin",   {F},    F, mathSin},
        {"cos",   {F},    F, mathCos},
        {"log",   {F},    F, mathLog},
        {"tan",       {F},       F, mathTan},
        {"asin",      {F},       F, mathAsin},
        {"acos",      {F},       F, mathAcos},
        {"atan",      {F},       F, mathAtan},
        {"atan2",     {F, F},    F, mathAtan2},
        {"degrees",   {F},       F, mathDegrees},
        {"radians",   {F},       F, mathRadians},
        {"sinh",      {F},       F, mathSinh},
        {"cosh",      {F},       F, mathCosh},
        {"tanh",      {F},       F, mathTanh},
        {"cbrt",      {F},       F, mathCbrt},
        {"exp",       {F},       F, mathExp},
        {"log10",     {F},       F, mathLog10},
        {"log2",      {F},       F, mathLog2},
        {"log_base",  {F, F},    F, mathLogBase},
        {"hypot",     {F, F},    F, mathHypot},
        {"trunc",     {F},       I, mathTrunc},
        {"round_to",  {F, I},    F, mathRoundTo},
        {"gcd",       {Z, Z},    Z, mathGcd},
        {"lcm",       {Z, Z},    Z, mathLcm},
        {"is_even",   {Z},       B, mathIsEven},
        {"is_odd",    {Z},       B, mathIsOdd},
        {"sign",      {N},       I32, mathSign},
        {"clamp",     {N, N, N}, N, mathClamp},
        {"factorial", {I},       I, mathFactorial},
        {"is_prime",  {I},       B, mathIsPrime},
        {"is_close",  {F, F},    B, mathIsClose},
    }, {
        {"pi",  D, Value(PI)},
        {"e",   D, Value(2.71828182845904523536)},
        {"tau", D, Value(2 * PI)},
    }});

    m.push_back({"Random", {
        {"seed",    {I},         V, randSeed},
        {"i32",     {I32, I32},  I32, randI32},
        {"i64",     {I, I},      I, randI64},
        {"f64",     {},          D, randDecimal},
        {"choice",  {t.list(T)}, T, randChoice},
        {"f64_range",     {D, D},         D,         randDecimalRange},
        {"bool",          {},             B,         randBool},
        {"chance",        {D},            B,         randChance},
        {"shuffle",       {t.list(T)},    V,         randShuffle, 0, false, true},
        {"sample",        {t.list(T), I}, t.list(T), randSample},
        {"choices",       {t.list(T), I}, t.list(T), randChoices},
        {"gauss",         {D, D},         D,         randGauss},
    }, {}});

    m.push_back({"Convert", {
        {"to_i32",     {S},   t.optional(I32), convTo<&TypeContext::i32Type>},   // Q1b
        {"to_i64",     {S},   t.optional(I),   convTo<&TypeContext::i64Type>},
        {"to_f32",     {S},   t.optional(F32), convTo<&TypeContext::f32Type>},
        {"to_f64",     {S},   t.optional(D),   convTo<&TypeContext::f64Type>},
        {"to_bool",    {S},   t.optional(B),   convToBool},
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
        {"index_of", {t.list(T), T},    t.optional(I32), listIndexOf},
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
        {"count",         {t.list(T), T},          I32,       listCount},
        {"last_index_of", {t.list(T), T},          t.optional(I32), listLastIndexOf},
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

    {
        const TypeRef K = t.typeVar("K"), W = t.typeVar("V");
        const TypeRef DKW = t.dict(K, W);
        m.push_back({"Dicts", {
            {"get",            {DKW, K},                 t.optional(W),      dictGet},   // Q1b
            {"contains_value", {DKW, W},                 B,                  dictContainsValue},
            {"keys_of",        {DKW, W},                 t.list(K),          dictKeysOf},
            {"is_empty",       {DKW},                    B,                  dictIsEmpty},
            {"items",          {DKW},                    t.list(t.pair(K, W)), dictItems},
            {"update",         {DKW, DKW},               V,                  dictUpdate, 0, false, 1},
            {"pop",            {DKW, K},                 W,                  dictPop,    0, false, 1},
            {"clear",          {DKW},                    V,                  dictClear,  0, false, 1},
            {"merge",          {DKW, DKW},               DKW,                dictMerge},
            {"copy",           {DKW},                    DKW,                dictCopy},
            {"from_lists",     {t.list(K), t.list(W)},   DKW,                dictFromLists},
            {"from_pairs",     {t.list(t.pair(K, W))},   DKW,                dictFromPairs},
            {"invert",         {DKW},                    t.dict(W, K),       dictInvert},
        }, {}});
    }

    m.push_back({"Env", {
        {"get",    {S},    t.optional(S),   envGet},   // Q1b
        {"all",    {},     t.dict(S, S),    envAll},
    }, {}});

    m.push_back({"Process", {
        {"run",     {S, t.list(S)}, I32,              procRun,     1},
        {"capture", {S, t.list(S)}, t.pair(I32, S),   procCapture, 1},
    }, {}});

    m.push_back({"Regex", {
        {"matches",       {S, S},    B,                    rxMatches},
        {"contains",      {S, S},    B,                    rxContains},
        {"find",          {S, S},    t.optional(I32),      rxFind},
        {"find_all",      {S, S},    t.list(S),            rxFindAll},
        {"count",         {S, S},    I32,                  rxCount},
        {"groups",        {S, S},    t.list(t.optional(S)),          rxGroups},      // Q1b
        {"groups_all",    {S, S},    t.list(t.list(t.optional(S))),  rxGroupsAll},
        {"named",         {S, S},    t.dict(S, t.optional(S)),       rxNamed},
        {"replace_all",   {S, S, S}, S,                    rxReplaceAll},
        {"replace_first", {S, S, S}, S,                    rxReplaceFirst},
        {"split",         {S, S},    t.list(S),            rxSplit},
        {"escape",        {S},       S,                    rxEscape},
        {"is_valid",      {S},       B,                    rxIsValid},
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
std::string fixedText(double x, std::int64_t casas, bool f32) {
    std::string r = arredondaTexto(x, casas, f32);
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
