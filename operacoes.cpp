#include "operacoes.h"
#include "runtime_error.h"
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

} // namespace cinza
