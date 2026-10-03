#include "operacoes.h"
#include "natives.h"         // fixedText (Strings.fixed)
#include "runtime_error.h"
#include "utf8.h"
#include "semantic.h"   // TypeChecker::isAssignable
#include <cmath>
#include <limits>

namespace cinza {

using utf8::tamanho;
using utf8::byteDoCaractere;

Value applyBinaryOp(TokenType op, const Value& left, const Value& right) {

    // v2.00 #3: concatenação string + qualquer primitivo
    if (op == TokenType::OP_PLUS) {
        if (left.kind() == Value::Kind::STRING && right.kind() == Value::Kind::STRING)
            return Value(left.asString() + right.asString());
        if (left.kind() == Value::Kind::STRING)
            return Value(left.asString() + right.toString());
        if (right.kind() == Value::Kind::STRING)
            return Value(left.toString() + right.asString());
    }

    // A5: int op int usa aritmética nativa de 64 bits com checagem de
    // overflow, sem passar por double (que perdia precisão e gerava UB)
    if (left.kind() == Value::Kind::INT && right.kind() == Value::Kind::INT) {
        const std::int64_t a = left.asInt();
        const std::int64_t b = right.asInt();
        std::int64_t r = 0;

        auto overflow = [&](const char* simbolo) -> Value {
            throw RuntimeError("OverflowError: " + std::to_string(a) + " " + simbolo + " " +
                              std::to_string(b) + " excede a faixa de int "
                              "(-9223372036854775808 a 9223372036854775807)");
        };
        constexpr std::int64_t int_min = std::numeric_limits<std::int64_t>::min();

        switch (op) {
            case TokenType::OP_PLUS:
                if (__builtin_add_overflow(a, b, &r)) return overflow("+");
                return Value(r);
            case TokenType::OP_MINUS:
                if (__builtin_sub_overflow(a, b, &r)) return overflow("-");
                return Value(r);
            case TokenType::OP_MULTIPLY:
                if (__builtin_mul_overflow(a, b, &r)) return overflow("*");
                return Value(r);
            case TokenType::OP_DIVIDE:
                if (b == 0) throw RuntimeError("ZeroDivisionError: divisão por zero");
                if (a == int_min && b == -1) return overflow("/");
                return Value(a / b);
            case TokenType::OP_MODULO:
                if (b == 0) throw RuntimeError("ZeroDivisionError: módulo por zero");
                // x % -1 é sempre 0, que cabe em int (decisão de 2026-10-03; antes,
                // menor_int % -1 lançava OverflowError). Em C++ essa conta é indefinida.
                if (b == -1) return Value(std::int64_t{0});
                return Value(a % b);
            default:
                break;  // comparações: tratadas abaixo
        }
    }

    auto toDouble = [](const Value& v) { return v.asNumber(); };

    // Revisão: decimal que estoura vira OverflowError, como int (antes saía
    // inf e depois nan em silêncio)
    auto finito = [&](double r, const char* simbolo) -> Value {
        if (!std::isfinite(r))
            throw RuntimeError(std::string("OverflowError: ") + left.toString() + " " + simbolo +
                              " " + right.toString() + " excede a faixa de decimal");
        return Value(r);
    };

    switch (op) {
        case TokenType::OP_PLUS:
            return finito(toDouble(left) + toDouble(right), "+");

        case TokenType::OP_MINUS:
            return finito(toDouble(left) - toDouble(right), "-");

        case TokenType::OP_MULTIPLY:
            return finito(toDouble(left) * toDouble(right), "*");

        case TokenType::OP_DIVIDE: {
            double d = toDouble(right);
            if (d == 0.0) throw RuntimeError("ZeroDivisionError: divisão por zero");
            return finito(toDouble(left) / d, "/");
        }

        case TokenType::OP_MODULO:
            // A10: divisor 0 ou 0.0 (antes 0.0 gerava NaN em silêncio)
            if (toDouble(right) == 0.0)
                throw RuntimeError("ZeroDivisionError: módulo por zero");
            return Value(std::fmod(toDouble(left), toDouble(right)));

        case TokenType::OP_LESS:          return Value(left <  right);
        case TokenType::OP_LESS_EQUAL:    return Value(left <= right);
        case TokenType::OP_GREATER:       return Value(left >  right);
        case TokenType::OP_GREATER_EQUAL: return Value(left >= right);
        case TokenType::OP_EQUAL:         return Value(left == right);
        case TokenType::OP_NOT_EQUAL:     return Value(left != right);

        default: throw RuntimeError("Operador binário desconhecido");
    }
}

Value negateOp(const Value& v) {
    if (v.kind() == Value::Kind::INT) {
        // A5: -INT_MIN não cabe em 64 bits
        if (v.asInt() == std::numeric_limits<std::int64_t>::min())
            throw RuntimeError("OverflowError: -(" + std::to_string(v.asInt()) +
                               ") excede a faixa de int "
                               "(-9223372036854775808 a 9223372036854775807)");
        return Value(-v.asInt());
    }
    return Value(-v.asDecimal());
}

// ============================================================================
// COLEÇÕES
// ============================================================================

Value indexGet(const Value& obj, const Value& idx) {
    if (obj.kind() == Value::Kind::LIST) {
        const auto& elems = obj.asList()->elements;
        if (idx.kind() != Value::Kind::INT)
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
    const bool numero = v.kind() == Value::Kind::INT || v.kind() == Value::Kind::DECIMAL;
    if (casas >= 0 && numero)
        t = fixedText(v.asNumber(), casas);
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
        if (it == entries.end())
            throw RuntimeError("KeyError: chave '" + key.toString() + "' não existe no dicionário. "
                               "Use .add({\"" + key.toString() + "\", valor}) para inserir "
                               "novas entradas.");
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
            return Value(static_cast<std::int64_t>(obj.asList()->elements.size()));
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
            return Value(static_cast<std::int64_t>(obj.asDict()->entries.size()));
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
            return Value(tamanho(obj.asString()));
        case Builtin::COUNT: break;
    }
    throw RuntimeError("Erro interno: método embutido desconhecido");
}

// ============================================================================
// op<...>, INTERFACES E type()
// ============================================================================

TypeRef runtimeType(const Value& v, TypeRef st) {
    auto& t = TypeContext::instance();
    using K = TypeInfo::Kind;

    // Erro guardado como Error (a raiz): o tipo real é o do erro
    if (v.kind() == Value::Kind::ERROR) return t.errorType(v.asError()->kind);
    // Objeto guardado como interface: o tipo real é a classe
    if (v.kind() == Value::Kind::INSTANCE && st->is(K::Interface))
        return t.classType(v.asInstance()->class_name);
    if (!st->is(K::Op)) return st;

    auto doOp = [&](K kind) -> TypeRef {
        for (TypeRef m : st->params) if (m->is(kind)) return m;
        return t.varType();   // não acontece: o semântico só deixa entrar tipos do op
    };
    switch (v.kind()) {
        case Value::Kind::INT:      return t.intType();
        case Value::Kind::DECIMAL:  return t.decimalType();
        case Value::Kind::STRING:   return t.stringType();
        case Value::Kind::BOOL:     return t.boolType();
        case Value::Kind::TYPE:     return t.typeType();
        case Value::Kind::LIST:     return doOp(K::List);
        case Value::Kind::DICT:     return doOp(K::Dict);
        case Value::Kind::PAIR:     return doOp(K::Pair);
        case Value::Kind::INSTANCE: return t.classType(v.asInstance()->class_name);
        case Value::Kind::STRUCT:   return t.structType(v.asStruct()->decl->name);
        case Value::Kind::ENUM:     return t.enumType(v.asEnum().decl->name);
        default:                    return t.voidType();
    }
}

Value narrowValue(Value v, TypeRef from, TypeRef to) {
    using K = TypeInfo::Kind;
    TypeRef real = runtimeType(v, from);
    if (!TypeChecker::isAssignable(to, real))
        throw RuntimeError("TypeError: esperado '" + stripModulePrefixes(to->str()) +
                           "', mas o valor é '" + stripModulePrefixes(real->str()) + "'");

    // int → decimal: destino decimal, ou op que tem decimal e não tem int
    if (v.kind() == Value::Kind::INT) {
        const bool quer_decimal = to->is(K::Decimal) ||
            (to->is(K::Op) && to->hasMember(TypeContext::instance().decimalType()) &&
             !to->hasMember(TypeContext::instance().intType()));
        if (quer_decimal) return Value(static_cast<double>(v.asInt()));
    }
    return v;
}

Value keepLock(const Value& atual, Value novo) {
    if (atual.kind() == Value::Kind::DECIMAL && novo.kind() == Value::Kind::INT)
        return Value(static_cast<double>(novo.asInt()));
    return novo;
}

} // namespace cinza
