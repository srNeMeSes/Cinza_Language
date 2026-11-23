#include "operacoes.h"
#include "natives.h"         // fixedText (Strings.fixed)
#include "runtime_error.h"
#include "utf8.h"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace cinza {

using utf8::tamanho;
using utf8::byteDoCaractere;

// ── Regras numéricas (seção K do REVISAO_V2.md) ──────────────────────────
// Duas famílias que não se misturam: inteiros (i32, i64) e ponto flutuante
// (f32, f64). Dentro da família, o operando mais estreito é promovido ANTES da
// conta e o resultado tem o tipo mais largo; i32 op i32 fica em i32. Famílias
// diferentes não chegam aqui: o semântico as rejeita.

static constexpr std::int64_t MIN_I32 = std::numeric_limits<std::int32_t>::min();
static constexpr std::int64_t MAX_I32 = std::numeric_limits<std::int32_t>::max();

static std::string faixa(bool i64) {
    return i64 ? "i64 (-9223372036854775808 a 9223372036854775807)"
               : "i32 (-2147483648 a 2147483647)";
}

[[noreturn]] static void estouro(std::int64_t a, const char* simbolo, std::int64_t b, bool i64) {
    throw RuntimeError("OverflowError: " + std::to_string(a) + " " + simbolo + " " +
                       std::to_string(b) + " excede a faixa de " + faixa(i64));
}

// Conta inteira. Em i32 a conta é feita em 64 bits (nada estoura no meio: o
// produto de dois i32 cabe em i64) e o resultado precisa caber em 32 bits —
// assim nem INT32_MIN / -1 é comportamento indefinido do C++.
static Value contaInteira(TokenType op, std::int64_t a, std::int64_t b, bool i64) {
    std::int64_t r = 0;
    switch (op) {
        case TokenType::OP_PLUS:     if (__builtin_add_overflow(a, b, &r)) estouro(a, "+", b, i64); break;
        case TokenType::OP_MINUS:    if (__builtin_sub_overflow(a, b, &r)) estouro(a, "-", b, i64); break;
        case TokenType::OP_MULTIPLY: if (__builtin_mul_overflow(a, b, &r)) estouro(a, "*", b, i64); break;
        case TokenType::OP_DIVIDE:
            if (b == 0) throw RuntimeError("ZeroDivisionError: divisão por zero");
            if (b == -1 && a == std::numeric_limits<std::int64_t>::min()) estouro(a, "/", b, true);
            r = a / b;   // trunca em direção a zero
            break;
        case TokenType::OP_MODULO:
            if (b == 0) throw RuntimeError("ZeroDivisionError: módulo por zero");
            // x % -1 é sempre 0, que cabe (decisão de 2026-10-03); em C++, menor % -1 é indefinido
            r = (b == -1) ? 0 : a % b;   // o sinal segue o dividendo
            break;
        default: throw RuntimeError("Erro interno: operador inteiro desconhecido");
    }
    if (i64) return Value(r);
    if (r < MIN_I32 || r > MAX_I32) {
        const char* simbolo = op == TokenType::OP_PLUS ? "+" : op == TokenType::OP_MINUS ? "-" :
                              op == TokenType::OP_MULTIPLY ? "*" : op == TokenType::OP_DIVIDE ? "/" : "%";
        estouro(a, simbolo, b, false);
    }
    return Value::i32(static_cast<std::int32_t>(r));
}

// Conta de ponto flutuante (seção M): infinito e NaN não existem na Cinza.
// Divisão por zero (inclusive 0 / 0) lança ZeroDivisionError e um resultado
// além do maior valor finito do tipo lança OverflowError. Resultado pequeno
// demais vira 0 (ou um subnormal), e -0 continua existindo (I4). f32 é
// calculado em float, sem passar por double.
static Value contaFlutuante(TokenType op, const Value& left, const Value& right, bool f64) {
    const char* simbolo = op == TokenType::OP_PLUS ? "+" : op == TokenType::OP_MINUS ? "-" :
                          op == TokenType::OP_MULTIPLY ? "*" : "/";
    if (op == TokenType::OP_MODULO)
        throw RuntimeError("Erro interno: '%' entre ponto flutuante (o semântico só aceita inteiros)");
    if (op == TokenType::OP_DIVIDE && right.asFloat() == 0.0)
        throw RuntimeError("ZeroDivisionError: divisão por zero");
    double r;
    if (f64) {
        const double a = left.asFloat(), b = right.asFloat();
        r = op == TokenType::OP_PLUS ? a + b : op == TokenType::OP_MINUS ? a - b :
            op == TokenType::OP_MULTIPLY ? a * b : a / b;
    } else {
        const float a = left.asF32(), b = right.asF32();
        r = op == TokenType::OP_PLUS ? a + b : op == TokenType::OP_MINUS ? a - b :
            op == TokenType::OP_MULTIPLY ? a * b : a / b;
    }
    if (!std::isfinite(r))
        throw RuntimeError("OverflowError: " + left.toString() + " " + simbolo + " " + right.toString() +
                           " excede a faixa de " + (f64 ? "f64" : "f32"));
    return f64 ? Value(r) : Value::f32(static_cast<float>(r));
}

Value applyBinaryOp(TokenType op, const Value& left, const Value& right) {
    // concatenação: string + string, ou string com qualquer primitivo
    if (op == TokenType::OP_PLUS) {
        if (left.kind() == Value::Kind::STRING && right.kind() == Value::Kind::STRING)
            return Value(left.asString() + right.asString());
        if (left.kind() == Value::Kind::STRING)
            return Value(left.asString() + right.toString());
        if (right.kind() == Value::Kind::STRING)
            return Value(left.toString() + right.asString());
    }

    const bool inteiros    = left.isInteger() && right.isInteger();
    const bool flutuantes  = left.isFloat() && right.isFloat();
    switch (op) {
        case TokenType::OP_PLUS: case TokenType::OP_MINUS: case TokenType::OP_MULTIPLY:
        case TokenType::OP_DIVIDE: case TokenType::OP_MODULO:
            if (inteiros)
                return contaInteira(op, left.asInt(), right.asInt(),
                                    left.kind() == Value::Kind::I64 || right.kind() == Value::Kind::I64);
            if (flutuantes)
                return contaFlutuante(op, left, right,
                                      left.kind() == Value::Kind::F64 || right.kind() == Value::Kind::F64);
            throw RuntimeError("Erro interno: operação aritmética entre '" + left.toString() + "' e '" +
                               right.toString() + "' (o semântico deveria ter recusado)");

        // comparações: na mesma família, pelo valor (f32 → f64 é exato);
        // strings, pela ordem dos bytes
        case TokenType::OP_LESS:
            if (inteiros)   return Value(left.asInt()   <  right.asInt());
            if (flutuantes) return Value(left.asFloat() <  right.asFloat());
            return Value(left <  right);
        case TokenType::OP_LESS_EQUAL:
            if (inteiros)   return Value(left.asInt()   <= right.asInt());
            if (flutuantes) return Value(left.asFloat() <= right.asFloat());
            return Value(left <= right);
        case TokenType::OP_GREATER:
            if (inteiros)   return Value(left.asInt()   >  right.asInt());
            if (flutuantes) return Value(left.asFloat() >  right.asFloat());
            return Value(left >  right);
        case TokenType::OP_GREATER_EQUAL:
            if (inteiros)   return Value(left.asInt()   >= right.asInt());
            if (flutuantes) return Value(left.asFloat() >= right.asFloat());
            return Value(left >= right);
        case TokenType::OP_EQUAL:     return Value(left == right);
        case TokenType::OP_NOT_EQUAL: return Value(left != right);

        default: throw RuntimeError("Operador binário desconhecido");
    }
}

// ── cast<T> e promoções ──────────────────────────────────────────────────

// Texto estrito → inteiro de 64 bits: [+|-] dígitos, sem espaços
static bool lerInteiro(const std::string& s, std::int64_t& v, bool& fora) {
    fora = false;
    size_t i = 0;
    const bool menos = !s.empty() && s[0] == '-';
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) i = 1;
    if (i == s.size()) return false;
    for (size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    // from_chars aceita '-' mas não '+'
    const char* ini = s.data() + (menos ? 0 : i);
    auto [ptr, ec] = std::from_chars(ini, s.data() + s.size(), v);
    if (ec == std::errc::result_out_of_range) { fora = true; return false; }
    return ec == std::errc() && ptr == s.data() + s.size();
}

// Texto estrito → ponto flutuante na precisão pedida (f32 lido direto como
// float: decimal → double → float arredondaria duas vezes)
static bool lerFlutuante(const std::string& s, bool f32, double& v, bool& fora) {
    fora = false;
    size_t i = 0;
    auto digitos = [&]() { const size_t a = i; while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i; return i > a; };
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    bool ok = digitos();
    if (ok && i < s.size() && s[i] == '.') { ++i; ok = digitos(); }
    if (ok && i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        ok = digitos();
    }
    if (!ok || i != s.size()) return false;
    const char* ini = s.data() + (s[0] == '+' ? 1 : 0);
    std::errc ec;
    if (f32) {
        float f = 0;
        ec = std::from_chars(ini, s.data() + s.size(), f).ec;
        v = f;
    } else {
        ec = std::from_chars(ini, s.data() + s.size(), v).ec;
    }
    if (ec == std::errc::result_out_of_range) {
        // grande demais é erro; pequeno demais (abaixo do menor subnormal) vira 0
        const double r = std::strtod(std::string(ini, s.data() + s.size()).c_str(), nullptr);
        if (std::isinf(r) || (f32 && std::fabs(r) > std::numeric_limits<float>::max())) {
            fora = true;
            return false;
        }
        v = (s[0] == '-') ? -0.0 : 0.0;
    }
    return true;
}

// double → float arredonda para o mais próximo; só não cabe a partir de FLT_MAX
// + meia unidade (2^103), onde o arredondamento daria infinito. Em C++,
// converter um double fora da faixa do float é comportamento indefinido, então
// quem chama confere antes com cabeEmF32.
bool cabeEmF32(double d) {
    static const double limite = static_cast<double>(std::numeric_limits<float>::max()) + std::ldexp(1.0, 103);
    return d > -limite && d < limite;
}
float doubleToF32(double d) { return static_cast<float>(d); }

Value literalValue(const std::variant<std::int64_t, double, std::string, bool>& v, TypeRef t) {
    using K = TypeInfo::Kind;
    if (auto* i = std::get_if<std::int64_t>(&v))
        return (t && t->is(K::I32)) ? Value::i32(static_cast<std::int32_t>(*i)) : Value(*i);
    if (auto* d = std::get_if<double>(&v))
        return (t && t->is(K::F32)) ? Value::f32(static_cast<float>(*d)) : Value(*d);
    if (auto* str = std::get_if<std::string>(&v)) return Value(*str);
    return Value(std::get<bool>(v));
}

Value convertValue(const Value& v, TypeRef to) {
    using K = TypeInfo::Kind;
    const std::string alvo = to->str();
    auto naoCabe = [&]() {
        throw RuntimeError("OverflowError: cast<" + alvo + ">(" +
                           (v.kind() == Value::Kind::STRING ? "\"" + v.asString() + "\"" : v.toString()) +
                           "): o valor não cabe em " + alvo);
    };

    if (to->is(K::I32) || to->is(K::I64)) {
        const bool i64 = to->is(K::I64);
        std::int64_t r = 0;
        if (v.isInteger()) {
            r = v.asInt();
        } else if (v.isFloat()) {
            const double t = std::trunc(v.asFloat());   // em direção a zero
            // 2^63 é o primeiro double fora de i64: conferir antes de converter (UB)
            if (t < -9223372036854775808.0 || t >= 9223372036854775808.0) naoCabe();
            r = static_cast<std::int64_t>(t);
        } else if (v.kind() == Value::Kind::STRING) {
            bool fora = false;
            if (!lerInteiro(v.asString(), r, fora)) {
                if (fora) naoCabe();
                throw RuntimeError("ValueError: cast<" + alvo + ">(\"" + v.asString() + "\"): não é um "
                                   "número inteiro (só sinal e dígitos)");
            }
        } else {
            throw RuntimeError("Erro interno: cast<" + alvo + "> de um valor que não é número nem texto");
        }
        if (i64) return Value(r);
        if (r < MIN_I32 || r > MAX_I32) naoCabe();
        return Value::i32(static_cast<std::int32_t>(r));
    }

    if (to->is(K::F32) || to->is(K::F64)) {
        const bool f32 = to->is(K::F32);
        if (v.isInteger())   // direto do inteiro: sem arredondar duas vezes
            return f32 ? Value::f32(static_cast<float>(v.asInt())) : Value(static_cast<double>(v.asInt()));
        double d = 0;
        if (v.isFloat()) {
            d = v.asFloat();
        } else if (v.kind() == Value::Kind::STRING) {
            bool fora = false;
            if (!lerFlutuante(v.asString(), f32, d, fora)) {
                if (fora) naoCabe();
                throw RuntimeError("ValueError: cast<" + alvo + ">(\"" + v.asString() + "\"): não é um "
                                   "número (ex.: 2.5, -3, 1e10)");
            }
        } else {
            throw RuntimeError("Erro interno: cast<" + alvo + "> de um valor que não é número nem texto");
        }
        if (f32 && !cabeEmF32(d)) naoCabe();   // f64 → f32 grande demais
        return f32 ? Value::f32(doubleToF32(d)) : Value(d);
    }

    if (to->is(K::String))
        return v.kind() == Value::Kind::STRING ? v : Value(v.toString());

    throw RuntimeError("Erro interno: cast<" + alvo + "> não existe");
}

Value negateOp(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::I64:
            // -menor não cabe
            if (v.asInt() == std::numeric_limits<std::int64_t>::min())
                throw RuntimeError("OverflowError: -(" + std::to_string(v.asInt()) +
                                   ") excede a faixa de " + faixa(true));
            return Value(-v.asInt());
        case Value::Kind::I32:
            if (v.asInt() == MIN_I32)
                throw RuntimeError("OverflowError: -(" + std::to_string(v.asInt()) +
                                   ") excede a faixa de " + faixa(false));
            return Value::i32(-v.asI32());
        case Value::Kind::F32: return Value::f32(-v.asF32());
        default:               return Value(-v.asFloat());
    }
}

// ============================================================================
// COLEÇÕES
// ============================================================================

Value indexGet(const Value& obj, const Value& idx) {
    if (obj.kind() == Value::Kind::LIST) {
        const auto& elems = obj.asList()->elements;
        if (!idx.isInteger())
            throw RuntimeError("Indice de lista deve ser inteiro");
        std::int64_t i  = idx.asInt();
        std::int64_t sz = static_cast<std::int64_t>(elems.size());
        if (i < 0 || i >= sz)
            throw RuntimeError("IndexError: índice " + std::to_string(i) +
                               " fora dos limites (tamanho: " + std::to_string(sz) + ")");
        return elems[static_cast<size_t>(i)];
    }
    if (obj.kind() == Value::Kind::DICT) {
        auto& entries = obj.asDict()->entries;
        auto it = entries.find(idx);
        if (it == entries.end())
            throw RuntimeError("KeyError: chave '" + idx.toString() +
                               "' não encontrada no dicionário");
        return it->second;
    }
    if (obj.kind() == Value::Kind::STRING) {
        const std::string& s = obj.asString();
        const std::int64_t n = tamanho(s);
        const std::int64_t i0 = idx.asInt();
        const std::int64_t i = i0 < 0 ? i0 + n : i0;
        if (i < 0 || i >= n)
            throw RuntimeError("IndexError: índice " + std::to_string(i0) +
                               " fora dos limites de uma string de tamanho " + std::to_string(n));
        const size_t b = byteDoCaractere(s, i);
        return Value(s.substr(b, byteDoCaractere(s, i + 1) - b));
    }
    throw RuntimeError("Operador '[]' em tipo inválido");
}

std::vector<Value> forElements(const Value& col) {
    std::vector<Value> elems;
    switch (col.kind()) {
        case Value::Kind::LIST:
            elems = col.asList()->elements;
            break;
        case Value::Kind::DICT:
            elems.reserve(col.asDict()->entries.size());
            for (const auto& [k, v] : col.asDict()->entries) elems.push_back(makePair(k, v));
            break;
        case Value::Kind::STRING: {
            const std::string& s = col.asString();
            for (size_t i = 0; i < s.size();) {
                const size_t len = utf8::bytesDoCaractere(s, i);
                elems.emplace_back(s.substr(i, len));
                i += len;
            }
            break;
        }
        default:
            throw RuntimeError("'for' esperava list, dict ou string como iterável");
    }
    return elems;
}

// s[ini:fim:passo]: índices em caracteres; negativo conta do fim. Com passo
// positivo exige 0 <= ini <= fim <= tamanho (o fim pode ser a posição depois do
// último); com passo negativo, simétrico: ini é um índice válido e
// -1 <= fim <= ini (o fim pode ser a posição antes do primeiro: s[-1:-7:-1] numa
// string de 6). Omitidos: do começo ao fim (ou do último ao primeiro). Fora
// disso, IndexError.
Value sliceGet(const Value& sv, const Value& iv, const Value& fv, const Value& pv) {
    const std::string& s = sv.asString();
    const std::int64_t n = tamanho(s);
    const bool tem_i = iv.kind() != Value::Kind::VOID_VAL, tem_f = fv.kind() != Value::Kind::VOID_VAL;
    const std::int64_t passo = pv.kind() == Value::Kind::VOID_VAL ? 1 : pv.asInt();
    if (passo == 0) throw RuntimeError("ValueError: o passo da fatia não pode ser 0");
    auto normal = [n](std::int64_t x) { return x < 0 ? x + n : x; };
    auto texto = [&]() {
        auto parte = [](bool tem, const Value& v) { return tem ? std::to_string(v.asInt()) : std::string(); };
        std::string r = "[" + parte(tem_i, iv) + ":" + parte(tem_f, fv);
        if (pv.kind() != Value::Kind::VOID_VAL) r += ":" + std::to_string(passo);
        return r + "]";
    };
    auto fora = [&]() {
        throw RuntimeError("IndexError: fatia '" + texto() + "' fora dos limites de uma string de tamanho " +
                           std::to_string(n));
    };
    // byte onde começa cada caractere (e o fim do texto no último lugar)
    std::vector<size_t> inicio;
    inicio.reserve(s.size() + 1);
    for (size_t i = 0; i < s.size(); ++i)
        if (!utf8::continuacao(s[i])) inicio.push_back(i);
    inicio.push_back(s.size());
    auto caractere = [&](std::int64_t k) {
        const size_t b = inicio[static_cast<size_t>(k)];
        return s.substr(b, inicio[static_cast<size_t>(k) + 1] - b);
    };
    std::string out;
    if (passo > 0) {
        const std::int64_t a = tem_i ? normal(iv.asInt()) : 0;
        const std::int64_t b = tem_f ? normal(fv.asInt()) : n;
        if (a < 0 || b > n || a > b) fora();
        if (passo == 1)
            return Value(s.substr(inicio[static_cast<size_t>(a)], inicio[static_cast<size_t>(b)] - inicio[static_cast<size_t>(a)]));
        // passo enorme: somar estouraria o int64 — o estouro já passou do fim
        for (std::int64_t k = a; k < b;) {
            out += caractere(k);
            if (__builtin_add_overflow(k, passo, &k)) break;
        }
    } else {
        const std::int64_t a = tem_i ? normal(iv.asInt()) : n - 1;
        const std::int64_t b = tem_f ? normal(fv.asInt()) : -1;
        if ((tem_i && (a < 0 || a >= n)) || (tem_f && (b < -1 || b >= n)) || a < b) fora();
        for (std::int64_t k = a; k > b;) {
            out += caractere(k);
            if (__builtin_add_overflow(k, passo, &k)) break;
        }
    }
    return Value(std::move(out));
}

std::string formatPart(const Value& v, int largura, int casas) {
    std::string t;
    const bool numero = v.isNumber();
    if (casas >= 0 && numero)
        t = fixedText(v.asNumber(), casas, v.kind() == Value::Kind::F32);
    else
        t = v.toString();
    const std::int64_t n = tamanho(t);
    if (n < largura) {
        const std::string brancos(static_cast<size_t>(largura - n), ' ');
        t = numero ? brancos + t : t + brancos;
    }
    return t;
}

Value* indexPlace(Value& obj, const Value& key) {
    if (obj.kind() == Value::Kind::LIST) {
        auto& elems = obj.asList()->elements;
        std::int64_t i  = key.asInt();
        std::int64_t sz = static_cast<std::int64_t>(elems.size());
        if (i < 0)
            throw RuntimeError("IndexError: índice negativo em lista não é permitido");
        if (i >= sz)
            throw RuntimeError("IndexError: índice " + std::to_string(i) +
                               " fora dos limites (tamanho: " + std::to_string(sz) + ")");
        return &elems[static_cast<size_t>(i)];
    }
    if (obj.kind() == Value::Kind::DICT) {
        // v2.00: `d[k] = v` só ATUALIZA chaves existentes; para inserir, use .add
        auto& entries = obj.asDict()->entries;
        auto it = entries.find(key);
        if (it == entries.end()) {
            // a chave como se escreve no código: só texto vai entre aspas (5, true, Cor.Verde)
            const std::string escrita = key.kind() == Value::Kind::STRING
                                        ? "\"" + key.asString() + "\"" : key.toString();
            throw RuntimeError("KeyError: chave '" + key.toString() + "' não existe no dicionário. "
                               "Use .add({" + escrita + ", valor}) para inserir "
                               "novas entradas.");
        }
        return &it->second;
    }
    throw RuntimeError("Operador '[]' em tipo inválido");
}

static const char* const nomes_builtin[] = {
    "add", "size", "has", "remove",
    "add", "has", "remove", "size", "keys", "values",
    "size",
};

const char* builtinName(Builtin b) { return nomes_builtin[static_cast<std::size_t>(b)]; }

Builtin builtinFor(Value::Kind kind, const std::string& name) {
    if (kind == Value::Kind::LIST) {
        if (name == "add")    return Builtin::ListAdd;
        if (name == "size")   return Builtin::ListSize;
        if (name == "has")    return Builtin::ListHas;
        if (name == "remove") return Builtin::ListRemove;
        throw RuntimeError("Método '" + name + "' não existe em list");
    }
    if (kind == Value::Kind::DICT) {
        if (name == "add")    return Builtin::DictAdd;
        if (name == "has")    return Builtin::DictHas;
        if (name == "remove") return Builtin::DictRemove;
        if (name == "size")   return Builtin::DictSize;
        if (name == "keys")   return Builtin::DictKeys;
        if (name == "values") return Builtin::DictValues;
        throw RuntimeError("Método '" + name + "' não existe em dict");
    }
    if (kind == Value::Kind::STRING) {
        if (name == "size") return Builtin::StrSize;
        throw RuntimeError("Método '" + name + "' não existe em string");
    }
    throw RuntimeError("Tipo não possui métodos");
}

Value callBuiltin(Builtin b, Value& obj, std::span<const Value> args) {
    switch (b) {
        case Builtin::ListAdd:
            obj.asList()->elements.push_back(args[0]);
            return Value();
        case Builtin::ListSize:
            return Value::i32(static_cast<std::int32_t>(obj.asList()->elements.size()));   // seção K
        case Builtin::ListHas: {
            std::int64_t idx = args[0].asInt();
            return Value(idx >= 0 && idx < static_cast<std::int64_t>(obj.asList()->elements.size()));
        }
        case Builtin::ListRemove: {
            auto& elems = obj.asList()->elements;
            std::int64_t idx = args[0].asInt();
            std::int64_t sz  = static_cast<std::int64_t>(elems.size());
            if (idx < 0 || idx >= sz)
                throw RuntimeError("IndexError: índice " + std::to_string(idx) +
                                   " fora dos limites ao chamar 'remove' (tamanho: " +
                                   std::to_string(sz) + ")");
            elems.erase(elems.begin() + idx);
            return Value();
        }
        case Builtin::DictAdd: {
            const auto& p = *args[0].asPair();
            obj.asDict()->entries[p.first] = p.second;
            return Value();
        }
        case Builtin::DictHas:
            return Value(obj.asDict()->entries.count(args[0]) > 0);
        case Builtin::DictRemove: {
            auto& entries = obj.asDict()->entries;
            auto it = entries.find(args[0]);
            if (it == entries.end())
                throw RuntimeError("KeyError: chave '" + args[0].toString() +
                                   "' não encontrada ao chamar 'remove'");
            entries.erase(it);
            return Value();
        }
        case Builtin::DictSize:
            return Value::i32(static_cast<std::int32_t>(obj.asDict()->entries.size()));
        case Builtin::DictKeys: {
            std::vector<Value> keys;
            keys.reserve(obj.asDict()->entries.size());
            for (const auto& [k, _] : obj.asDict()->entries) keys.push_back(k);
            return makeList(std::move(keys));
        }
        case Builtin::DictValues: {
            std::vector<Value> vals;
            vals.reserve(obj.asDict()->entries.size());
            for (const auto& [_, v] : obj.asDict()->entries) vals.push_back(v);
            return makeList(std::move(vals));
        }
        case Builtin::StrSize:   // A10: conta caracteres (code points UTF-8), não bytes
            return Value::i32(static_cast<std::int32_t>(tamanho(obj.asString())));
        case Builtin::COUNT: break;
    }
    throw RuntimeError("Erro interno: método embutido desconhecido");
}

// ============================================================================
// INTERFACES E type()
// ============================================================================

TypeRef runtimeType(const Value& v, TypeRef st) {
    auto& t = TypeContext::instance();
    using K = TypeInfo::Kind;

    // Q1b: o tipo de um T? é o próprio T? (com valor ou null)
    if (st->is(K::Optional)) return st;
    // Erro guardado como Error (a raiz): o tipo real é o do erro
    if (v.kind() == Value::Kind::ERROR) return t.errorType(v.asError()->kind);
    // Objeto guardado como interface: o tipo real é a classe
    if (v.kind() == Value::Kind::INSTANCE && st->is(K::Interface))
        return t.classType(v.asInstance()->classe()->nome);
    return st;   // nos demais casos o tipo estático já é o real
}

} // namespace cinza
