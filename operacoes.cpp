#include "operacoes.h"
#include "runtime_error.h"
#include "semantic.h"   // TypeChecker::isAssignable
#include <cmath>
#include <limits>

namespace cinza {

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

        auto overflow = [&](const char* op) -> Value {
            throw RuntimeError("OverflowError: " + std::to_string(a) + " " + op + " " +
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
                if (a == int_min && b == -1) return overflow("%");
                return Value(a % b);
            default:
                break;  // comparações: tratadas abaixo
        }
    }

    auto toDouble = [](const Value& v) -> double {
        return (v.kind() == Value::Kind::INT)
            ? static_cast<double>(v.asInt()) : v.asDecimal();
    };

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
    throw RuntimeError("Operador '[]' em tipo inválido");
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
        case Builtin::StrSize: {
            // A10: conta caracteres (code points UTF-8), não bytes
            std::int64_t count = 0;
            for (unsigned char c : obj.asString())
                if ((c & 0xC0) != 0x80) ++count;
            return Value(count);
        }
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
