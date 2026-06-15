#ifndef CINZA_VALUE_H
#define CINZA_VALUE_H

#include "ast.h"          // ClassDecl* para lookup de métodos no executor
#include <variant>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <stdexcept>
#include <sstream>

namespace cinza {

// ============================================================================
// FORWARD DECLARATIONS
// Necessário porque Value é recursivo: uma lista contém Values,
// um dict mapeia Value → Value, um par contém dois Values, etc.
// ============================================================================

struct CinzaList;
struct CinzaDict;
struct CinzaPair;
struct ClassInstance;

// ============================================================================
// VALUE
//
// Representa qualquer valor em memória durante a execução da Cinza.
// Todos os tipos da linguagem mapeiam para um Kind aqui.
//
// Semântica de cópia:
//   - INT, DECIMAL, STRING, BOOL, PAIR → value semantics (cópia por valor)
//   - LIST, DICT, INSTANCE             → reference semantics (shared_ptr)
//
// A semântica de referência para coleções e instâncias garante que
// mutações (list.add(), campo = x) sejam visíveis para todos os
// "donos" do mesmo objeto — comportamento esperado em linguagens imperativas.
// ============================================================================

struct Value {
    enum class Kind {
        INT,
        DECIMAL,
        STRING,
        BOOL,
        LIST,
        DICT,
        PAIR,
        INSTANCE,
        // VOID_VAL: valor técnico interno do runtime.
        // Usos legítimos: retorno de funções void, ausência de valor em
        // caminhos internos do executor (ex.: função sem return explícito).
        // NÃO representa variável não-inicializada do usuário — a linguagem
        // não possui estados UNBOUND. Toda variável não-coleção chega ao
        // executor com um valor concreto avaliado pelo inicializador obrigatório.
        VOID_VAL
    };

    Kind kind = Kind::VOID_VAL;

    std::variant<
        std::monostate,                         // VOID_VAL
        int,                                    // INT
        double,                                 // DECIMAL
        std::string,                            // STRING
        bool,                                   // BOOL
        std::shared_ptr<CinzaList>,             // LIST
        std::shared_ptr<CinzaDict>,             // DICT
        std::shared_ptr<CinzaPair>,             // PAIR
        std::shared_ptr<ClassInstance>          // INSTANCE
    > data;

    // ── Construtores convenientes ─────────────────────────────────────────

    Value() : kind(Kind::VOID_VAL), data(std::monostate{}) {}

    explicit Value(int v)                : kind(Kind::INT),      data(v)              {}
    explicit Value(double v)             : kind(Kind::DECIMAL),  data(v)              {}
    explicit Value(const std::string& v) : kind(Kind::STRING),   data(v)              {}
    explicit Value(std::string&& v)      : kind(Kind::STRING),   data(std::move(v))   {}
    explicit Value(bool v)               : kind(Kind::BOOL),     data(v)              {}
    explicit Value(std::shared_ptr<CinzaList>     v) : kind(Kind::LIST),     data(std::move(v)) {}
    explicit Value(std::shared_ptr<CinzaDict>     v) : kind(Kind::DICT),     data(std::move(v)) {}
    explicit Value(std::shared_ptr<CinzaPair>     v) : kind(Kind::PAIR),     data(std::move(v)) {}
    explicit Value(std::shared_ptr<ClassInstance> v) : kind(Kind::INSTANCE), data(std::move(v)) {}

    // ── Acessores tipados ─────────────────────────────────────────────────

    int         asInt()      const { return std::get<int>(data); }
    double      asDecimal()  const { return std::get<double>(data); }
    bool        asBool()     const { return std::get<bool>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }

    std::shared_ptr<CinzaList>     asList()     const { return std::get<std::shared_ptr<CinzaList>>(data); }
    std::shared_ptr<CinzaDict>     asDict()     const { return std::get<std::shared_ptr<CinzaDict>>(data); }
    std::shared_ptr<CinzaPair>     asPair()     const { return std::get<std::shared_ptr<CinzaPair>>(data); }
    std::shared_ptr<ClassInstance> asInstance() const { return std::get<std::shared_ptr<ClassInstance>>(data); }

    // ── Utilitário de display ─────────────────────────────────────────────

    std::string toString() const;

    // ── Operadores de comparação (necessários para dict key e ==) ─────────
    //
    // Ordem total: VOID < BOOL < INT < DECIMAL < STRING < LIST < DICT < PAIR < INSTANCE
    // (dentro do mesmo Kind, compara pelo valor)

    bool operator==(const Value& other) const;
    bool operator< (const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool operator<=(const Value& other) const { return !(other < *this);  }
    bool operator> (const Value& other) const { return other < *this;     }
    bool operator>=(const Value& other) const { return !(*this < other);  }
};

// ============================================================================
// TIPOS DE COLEÇÃO
// Definidos APÓS Value para que Value seja completo quando são usados.
// ============================================================================

// Lista homogênea: list<T>
struct CinzaList {
    std::vector<Value> elements;

    CinzaList() = default;
    explicit CinzaList(std::vector<Value> elems)
        : elements(std::move(elems)) {}
};

// Dicionário: dict<K, V>
// Usa std::map para aproveitar a ordem total definida em Value.
struct CinzaDict {
    std::map<Value, Value> entries;

    CinzaDict() = default;
};

// Par imutável nos campos, mas o todo pode ser reatribuído: pair<A, B>
struct CinzaPair {
    Value first;
    Value second;

    CinzaPair() = default;
    CinzaPair(Value f, Value s) : first(std::move(f)), second(std::move(s)) {}
};

// ============================================================================
// CLASS INSTANCE
//
// Representa um objeto instanciado com `new NomeClasse(...)`.
// Carrega:
//   - class_name: para mensagens de erro e lookup de métodos
//   - fields:     mapa mutável de nome → valor dos campos
//   - decl:       ponteiro para o nó ClassDecl na AST
//                 (usado pelo executor para localizar métodos)
// ============================================================================

struct ClassInstance {
    std::string                             class_name;
    std::unordered_map<std::string, Value>  fields;
    const ClassDecl*                        decl = nullptr;  // não owning

    ClassInstance() = default;
    ClassInstance(const std::string& name, const ClassDecl* d)
        : class_name(name), decl(d) {}
};

// ============================================================================
// VALUE — IMPLEMENTAÇÕES INLINE
// (definidas aqui porque dependem de CinzaList/Dict/Pair/ClassInstance)
// ============================================================================

inline bool Value::operator==(const Value& other) const {
    if (kind != other.kind) {
        // Permite comparar INT e DECIMAL
        if ((kind == Kind::INT || kind == Kind::DECIMAL) &&
            (other.kind == Kind::INT || other.kind == Kind::DECIMAL)) {
            double a = (kind == Kind::INT) ? static_cast<double>(asInt()) : asDecimal();
            double b = (other.kind == Kind::INT) ? static_cast<double>(other.asInt()) : other.asDecimal();
            return a == b;
        }
        return false;
    }

    switch (kind) {
        case Kind::VOID_VAL:  return true;
        case Kind::INT:       return asInt()     == other.asInt();
        case Kind::DECIMAL:   return asDecimal() == other.asDecimal();
        case Kind::STRING:    return asString()  == other.asString();
        case Kind::BOOL:      return asBool()    == other.asBool();
        case Kind::LIST: {
            const auto& a = asList()->elements;
            const auto& b = other.asList()->elements;
            return a == b;
        }
        case Kind::DICT: {
            return asDict()->entries == other.asDict()->entries;
        }
        case Kind::PAIR: {
            return asPair()->first == other.asPair()->first &&
                   asPair()->second == other.asPair()->second;
        }
        case Kind::INSTANCE:
            // Dois objetos são iguais apenas se são o mesmo (identidade de referência)
            return asInstance().get() == other.asInstance().get();
        default:
            return false;
    }
}

inline bool Value::operator<(const Value& other) const {
    // Tipos diferentes: ordem por Kind (int e decimal: comparação numérica)
    if (kind != other.kind) {
        if ((kind == Kind::INT || kind == Kind::DECIMAL) &&
            (other.kind == Kind::INT || other.kind == Kind::DECIMAL)) {
            double a = (kind == Kind::INT) ? static_cast<double>(asInt()) : asDecimal();
            double b = (other.kind == Kind::INT) ? static_cast<double>(other.asInt()) : other.asDecimal();
            return a < b;
        }
        return static_cast<int>(kind) < static_cast<int>(other.kind);
    }

    switch (kind) {
        case Kind::INT:     return asInt()     < other.asInt();
        case Kind::DECIMAL: return asDecimal() < other.asDecimal();
        case Kind::STRING:  return asString()  < other.asString();
        case Kind::BOOL:    return static_cast<int>(asBool()) < static_cast<int>(other.asBool());
        default:            return false;   // listas/dicts/pares/instâncias: não ordenáveis
    }
}

inline std::string Value::toString() const {
    std::ostringstream oss;

    switch (kind) {
        case Kind::VOID_VAL:  return "void";
        case Kind::INT:       return std::to_string(asInt());
        case Kind::DECIMAL: {
            // Remove zeros à direita desnecessários para display limpo
            oss << asDecimal();
            return oss.str();
        }
        case Kind::STRING:  return asString();
        case Kind::BOOL:    return asBool() ? "true" : "false";
        case Kind::LIST: {
            oss << "[";
            const auto& elems = asList()->elements;
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) oss << ", ";
                // Strings dentro de listas aparecem com aspas
                if (elems[i].kind == Kind::STRING)
                    oss << "\"" << elems[i].asString() << "\"";
                else
                    oss << elems[i].toString();
            }
            oss << "]";
            return oss.str();
        }
        case Kind::DICT: {
            oss << "{";
            bool first = true;
            for (const auto& [k, v] : asDict()->entries) {
                if (!first) oss << ", ";
                first = false;
                if (k.kind == Kind::STRING) oss << "\"" << k.asString() << "\"";
                else oss << k.toString();
                oss << ": ";
                if (v.kind == Kind::STRING) oss << "\"" << v.asString() << "\"";
                else oss << v.toString();
            }
            oss << "}";
            return oss.str();
        }
        case Kind::PAIR: {
            oss << "{";
            const auto& p = *asPair();
            if (p.first.kind == Kind::STRING)  oss << "\"" << p.first.asString()  << "\"";
            else                               oss << p.first.toString();
            oss << ", ";
            if (p.second.kind == Kind::STRING) oss << "\"" << p.second.asString() << "\"";
            else                               oss << p.second.toString();
            oss << "}";
            return oss.str();
        }
        case Kind::INSTANCE:
            return "<" + asInstance()->class_name + " object>";
        default:
            return "<unknown>";
    }
}

// ============================================================================
// FACTORY HELPERS
// Funções globais para criar valores de coleção de forma expressiva.
// ============================================================================

inline Value makeList(std::vector<Value> elems = {}) {
    return Value(std::make_shared<CinzaList>(std::move(elems)));
}

inline Value makeDict() {
    return Value(std::make_shared<CinzaDict>());
}

inline Value makePair(Value first, Value second) {
    return Value(std::make_shared<CinzaPair>(std::move(first), std::move(second)));
}

inline Value makeInstance(const std::string& class_name, const ClassDecl* decl) {
    return Value(std::make_shared<ClassInstance>(class_name, decl));
}

} // namespace cinza

#endif // CINZA_VALUE_H
