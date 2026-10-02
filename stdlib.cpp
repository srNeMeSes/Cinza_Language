#include "natives.h"
#include "runtime_error.h"
#include <algorithm>
#include <charconv>
#include <cmath>
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
// Files
// ============================================================================

Value filesRead(Args a) {
    const std::string& caminho = a[0].asString();
    std::ifstream f(caminho, std::ios::binary);
    if (!f.is_open()) falha("IOError: não foi possível abrir '" + caminho + "' para leitura");
    std::ostringstream conteudo;
    conteudo << f.rdbuf();
    return Value(conteudo.str());
}

Value filesWrite(Args a) {
    const std::string& caminho = a[0].asString();
    std::ofstream f(caminho, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) falha("IOError: não foi possível abrir '" + caminho + "' para escrita");
    f << a[1].asString();
    if (!f) falha("IOError: falha ao escrever em '" + caminho + "'");
    return Value();
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
        {"read",  {S},    S, filesRead},
        {"write", {S, S}, V, filesWrite},
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
    }, {
        {"pi", D, Value(3.14159265358979323846)},
        {"e",  D, Value(2.71828182845904523536)},
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
