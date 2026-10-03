#ifndef CINZA_VALUE_H
#define CINZA_VALUE_H

#include "ast.h"          // ClassDecl* para lookup de métodos no executor
#include "source_files.h" // C5: displayName
#include "gc_object.h"   // coleta de ciclos: contêineres rastreados
#include <variant>
#include <cstdint>
#include <type_traits>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <stdexcept>
#include <sstream>
#include <charconv>

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

// Valor de enum: a declaração e o índice do valor. Não há int visível na
// linguagem: o índice só serve para comparar e ordenar chaves de dict.
struct EnumValue {
    const EnumDecl* decl  = nullptr;
    std::uint32_t   index = 0;
};
struct StructValue;
struct ErrorValue;

// ============================================================================
// VALUE
//
// Representa qualquer valor em memória durante a execução da Cinza.
// Todos os tipos da linguagem mapeiam para um Kind aqui.
//
// Semântica de cópia:
//   - INT, DECIMAL, STRING, BOOL, PAIR → value semantics (cópia por valor)
//   - STRUCT (C3)                      → value semantics: copiar clona os campos
//                                        (cópia rasa, ver Value(const Value&))
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
        STRUCT,      // C3
        ERROR,       // C4: erro capturado em except / criado com Tipo("msg")
        TYPE,        // op<...>: valor de type() e literal de tipo (int, list<int>, Pessoa)
        ENUM,        // valor de enum (Cor.Verde): a declaração e o índice do valor
        // VOID_VAL: valor técnico interno do runtime.
        // Usos legítimos: retorno de funções void, ausência de valor em
        // caminhos internos do executor (ex.: função sem return explícito).
        // NÃO representa variável não-inicializada do usuário — a linguagem
        // não possui estados UNBOUND. Toda variável não-coleção chega ao
        // executor com um valor concreto avaliado pelo inicializador obrigatório.
        VOID_VAL
    };

    std::variant<
        std::monostate,                         // VOID_VAL
        std::int64_t,                           // INT (A5: 64 bits)
        double,                                 // DECIMAL
        std::string,                            // STRING
        bool,                                   // BOOL
        std::shared_ptr<CinzaList>,             // LIST
        std::shared_ptr<CinzaDict>,             // DICT
        std::shared_ptr<CinzaPair>,             // PAIR
        std::shared_ptr<ClassInstance>,         // INSTANCE
        std::shared_ptr<StructValue>,           // STRUCT (C3)
        std::shared_ptr<ErrorValue>,            // ERROR (C4)
        const TypeInfo*,                        // TYPE (op<...>)
        EnumValue                               // ENUM
    > data;

    // ── Construtores convenientes ─────────────────────────────────────────

    Value() : data(std::monostate{}) {}

    explicit Value(std::int64_t v)       : data(v)              {}
    explicit Value(int v)                : Value(static_cast<std::int64_t>(v))       {}
    explicit Value(double v)             : data(v)              {}
    explicit Value(const std::string& v) : data(v)              {}
    explicit Value(std::string&& v)      : data(std::move(v))   {}
    // A11: sem este construtor, Value("x") escolhia a conversão padrão
    // const char* → bool e virava BOOL true
    explicit Value(const char* v)        : data(std::string(v))  {}
    explicit Value(bool v)               : data(v)              {}
    explicit Value(std::shared_ptr<CinzaList>     v) : data(std::move(v)) {}
    explicit Value(std::shared_ptr<CinzaDict>     v) : data(std::move(v)) {}
    explicit Value(std::shared_ptr<CinzaPair>     v) : data(std::move(v)) {}
    explicit Value(std::shared_ptr<ClassInstance> v) : data(std::move(v)) {}
    explicit Value(std::shared_ptr<StructValue>   v) : data(std::move(v)) {}
    explicit Value(std::shared_ptr<ErrorValue>    v) : data(std::move(v)) {}
    explicit Value(const TypeInfo* t)                : data(t) {}
    explicit Value(EnumValue e)                      : data(e) {}

    // ── Cópia (C3) ────────────────────────────────────────────────────────
    // struct é valor: copiar um Value que guarda struct clona os campos. É
    // cópia rasa: list/dict/objetos dentro dele continuam compartilhados,
    // porque copiar esses Values copia só o ponteiro. Assim toda atribuição,
    // argumento, return e .add() copia o struct sem código extra no executor.
    // Mover não clona.
    Value(const Value& other);
    Value& operator=(const Value& other);
    Value(Value&&) noexcept            = default;
    Value& operator=(Value&&) noexcept = default;

    // ── Kind ──────────────────────────────────────────────────────────────
    // A11: derivado de data.index(), então nunca diverge do valor guardado.
    // Índices do variant: 0 = monostate (VOID_VAL); 1..10 = INT..ERROR,
    // na mesma ordem do enum Kind.
    Kind kind() const noexcept {
        const auto i = data.index();
        return i == 0 ? Kind::VOID_VAL : static_cast<Kind>(i - 1);
    }

    // ── Acessores tipados ─────────────────────────────────────────────────
    // Otimização (CVM, seção 11): os ponteiros compartilhados são devolvidos por
    // referência — devolver por cópia mexia no contador de referências (atômico)
    // a cada leitura de campo ou elemento.

    std::int64_t asInt()     const { return std::get<std::int64_t>(data); }
    double      asDecimal()  const { return std::get<double>(data); }
    // int ou decimal como double (comparações e contas que misturam os dois)
    double      asNumber()   const {
        return kind() == Kind::INT ? static_cast<double>(asInt()) : asDecimal();
    }
    bool        asBool()     const { return std::get<bool>(data); }
    const std::string& asString() const { return std::get<std::string>(data); }

    const std::shared_ptr<CinzaList>&    asList() const { return std::get<std::shared_ptr<CinzaList>>(data); }
    const std::shared_ptr<CinzaDict>&    asDict() const { return std::get<std::shared_ptr<CinzaDict>>(data); }
    const std::shared_ptr<CinzaPair>&    asPair() const { return std::get<std::shared_ptr<CinzaPair>>(data); }
    const std::shared_ptr<ClassInstance>& asInstance() const { return std::get<std::shared_ptr<ClassInstance>>(data); }
    const std::shared_ptr<StructValue>&  asStruct() const { return std::get<std::shared_ptr<StructValue>>(data); }
    const std::shared_ptr<ErrorValue>&   asError() const { return std::get<std::shared_ptr<ErrorValue>>(data); }
    const TypeInfo*                asType()     const { return std::get<const TypeInfo*>(data); }
    EnumValue                      asEnum()     const { return std::get<EnumValue>(data); }

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

// A11: kind() depende de a ordem do variant casar com a do enum Kind;
// se alguém reordenar um dos dois, a compilação falha aqui.
namespace detail {
using ValueData = decltype(Value::data);
template <std::size_t I> using Alt = std::variant_alternative_t<I, ValueData>;
static_assert(std::variant_size_v<ValueData> == static_cast<std::size_t>(Value::Kind::VOID_VAL) + 1);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::INT)      + 1>, std::int64_t>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::DECIMAL)  + 1>, double>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::STRING)   + 1>, std::string>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::BOOL)     + 1>, bool>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::LIST)     + 1>, std::shared_ptr<CinzaList>>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::DICT)     + 1>, std::shared_ptr<CinzaDict>>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::PAIR)     + 1>, std::shared_ptr<CinzaPair>>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::INSTANCE) + 1>, std::shared_ptr<ClassInstance>>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::STRUCT)   + 1>, std::shared_ptr<StructValue>>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::ERROR)    + 1>, std::shared_ptr<ErrorValue>>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::TYPE)     + 1>, const TypeInfo*>);
static_assert(std::is_same_v<Alt<static_cast<std::size_t>(Value::Kind::ENUM)     + 1>, EnumValue>);
} // namespace detail

// ============================================================================
// TIPOS DE COLEÇÃO
// Definidos APÓS Value para que Value seja completo quando são usados.
// ============================================================================

// Lista homogênea: list<T>
struct CinzaList : GcObject {
    std::vector<Value> elements;

    CinzaList() : GcObject(GcKind::List) {}
    explicit CinzaList(std::vector<Value> elems)
        : GcObject(GcKind::List), elements(std::move(elems)) {}
};

// Dicionário: dict<K, V>
// Usa std::map para aproveitar a ordem total definida em Value.
struct CinzaDict : GcObject {
    std::map<Value, Value> entries;

    CinzaDict() : GcObject(GcKind::Dict) {}
};

// Par imutável nos campos, mas o todo pode ser reatribuído: pair<A, B>
struct CinzaPair : GcObject {
    Value first;
    Value second;

    CinzaPair() : GcObject(GcKind::Pair) {}
    CinzaPair(Value f, Value s) : GcObject(GcKind::Pair), first(std::move(f)), second(std::move(s)) {}
};

// ============================================================================
// CLASS INSTANCE
//
// Representa um objeto instanciado com `new NomeClasse(...)`.
// Carrega:
//   - class_name: para mensagens de erro e lookup de métodos
//   - fields:     valores dos campos, na ordem da declaração (B2: o
//                 semântico anota o índice em cada acesso)
//   - decl:       ponteiro para o nó ClassDecl na AST
//                 (usado pelo executor para localizar métodos)
// ============================================================================

struct ClassInstance : GcObject {
    std::string                             class_name;
    std::vector<Value>                      fields;
    const ClassDecl*                        decl = nullptr;  // não owning

    ClassInstance() : GcObject(GcKind::Instance) {}
    ClassInstance(const std::string& name, const ClassDecl* d)
        : GcObject(GcKind::Instance), class_name(name), fields(d ? d->fields.size() : 0), decl(d) {}
};

// ============================================================================
// STRUCT VALUE (C3)
//
// Valor de um struct: os campos na ordem da declaração. `decl` dá os nomes
// (para `s.campo` e para o print). Diferente de ClassInstance, é copiado a
// cada cópia do Value que o guarda.
// ============================================================================

struct StructValue : GcObject {
    const StructDecl*  decl = nullptr;
    std::vector<Value> fields;

    StructValue() : GcObject(GcKind::Struct) {}

    // Índice do campo pelo nome, ou -1
    int indexOf(const std::string& name) const {
        if (!decl) return -1;
        for (size_t i = 0; i < decl->fields.size(); ++i)
            if (decl->fields[i].name == name) return static_cast<int>(i);
        return -1;
    }
};

// ============================================================================
// ERROR VALUE (C4)
//
// Um erro como valor da linguagem: `e` em `except (ValueError e)`, ou o
// resultado de `ValueError("mensagem")`. Campos somente leitura: e.kind,
// e.message, e.line, e.column. Compartilhado (imutável), como um objeto.
// ============================================================================

struct ErrorValue {
    std::string  kind;      // "ValueError", "SaldoInsuficiente", "Error"...
    std::string  message;
    std::int64_t line   = 0;
    std::int64_t column = 0;
    std::vector<std::string> trace;   // stack trace de onde o erro surgiu (rethrow o mantém)
    int          file_id = 0;         // C5: arquivo onde surgiu (0 = principal)
};

inline Value::Value(const Value& other) : data(other.data) {
    if (auto* s = std::get_if<std::shared_ptr<StructValue>>(&other.data))
        data = std::make_shared<StructValue>(**s);   // clona; campos copiados um a um
}

inline Value& Value::operator=(const Value& other) {
    if (this != &other) {
        Value copia(other);
        data = std::move(copia.data);
    }
    return *this;
}

// ============================================================================
// VALUE — IMPLEMENTAÇÕES INLINE
// (definidas aqui porque dependem de CinzaList/Dict/Pair/ClassInstance)
// ============================================================================

inline bool Value::operator==(const Value& other) const {
    if (kind() != other.kind()) {
        // Permite comparar INT e DECIMAL
        if ((kind() == Kind::INT || kind() == Kind::DECIMAL) &&
            (other.kind() == Kind::INT || other.kind() == Kind::DECIMAL)) {
            double a = asNumber();
            double b = other.asNumber();
            return a == b;
        }
        return false;
    }

    switch (kind()) {
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
        case Kind::ERROR:
            // C4: erros são iguais só se forem o mesmo erro (identidade)
            return asError().get() == other.asError().get();
        case Kind::TYPE:
            // tipos são únicos (TypeContext): mesmo ponteiro = mesmo tipo
            return asType() == other.asType();
        case Kind::ENUM:
            return asEnum().decl == other.asEnum().decl && asEnum().index == other.asEnum().index;
        case Kind::STRUCT: {
            // C3: igualdade estrutural, campo a campo
            const auto& a = *asStruct();
            const auto& b = *other.asStruct();
            return a.decl == b.decl && a.fields == b.fields;
        }
        default:
            return false;
    }
}

inline bool Value::operator<(const Value& other) const {
    // Tipos diferentes: ordem por Kind (int e decimal: comparação numérica)
    if (kind() != other.kind()) {
        if ((kind() == Kind::INT || kind() == Kind::DECIMAL) &&
            (other.kind() == Kind::INT || other.kind() == Kind::DECIMAL)) {
            double a = asNumber();
            double b = other.asNumber();
            return a < b;
        }
        return static_cast<int>(kind()) < static_cast<int>(other.kind());
    }

    switch (kind()) {
        case Kind::INT:     return asInt()     < other.asInt();
        case Kind::DECIMAL: return asDecimal() < other.asDecimal();
        case Kind::STRING:  return asString()  < other.asString();
        case Kind::BOOL:    return static_cast<int>(asBool()) < static_cast<int>(other.asBool());
        case Kind::ENUM: {   // chave de dict: ordem da declaração (e por enum)
            const EnumValue a = asEnum(), b = other.asEnum();
            if (a.decl != b.decl) return std::less<const EnumDecl*>()(a.decl, b.decl);
            return a.index < b.index;
        }
        default:            return false;   // listas/dicts/pares/instâncias: não ordenáveis
    }
}

inline std::string Value::toString() const {
    std::ostringstream oss;

    switch (kind()) {
        case Kind::VOID_VAL:  return "void";
        case Kind::INT:       return std::to_string(asInt());
        case Kind::DECIMAL: {
            // Revisão: a menor forma que volta ao mesmo número (antes o ostream
            // cortava em 6 dígitos: 123456.789 saía 123457). Valor inteiro sai
            // sem ".0" (4.0 → 4), como já era.
            char buf[64];
            auto [fim, ec] = std::to_chars(buf, buf + sizeof buf, asDecimal());
            return ec == std::errc() ? std::string(buf, fim) : std::string("?");
        }
        case Kind::STRING:  return asString();
        case Kind::BOOL:    return asBool() ? "true" : "false";
        case Kind::LIST: {
            oss << "[";
            const auto& elems = asList()->elements;
            for (size_t i = 0; i < elems.size(); ++i) {
                if (i > 0) oss << ", ";
                // Strings dentro de listas aparecem com aspas
                if (elems[i].kind() == Kind::STRING)
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
                if (k.kind() == Kind::STRING) oss << "\"" << k.asString() << "\"";
                else oss << k.toString();
                oss << ": ";
                if (v.kind() == Kind::STRING) oss << "\"" << v.asString() << "\"";
                else oss << v.toString();
            }
            oss << "}";
            return oss.str();
        }
        case Kind::PAIR: {
            oss << "{";
            const auto& p = *asPair();
            if (p.first.kind() == Kind::STRING)  oss << "\"" << p.first.asString()  << "\"";
            else                               oss << p.first.toString();
            oss << ", ";
            if (p.second.kind() == Kind::STRING) oss << "\"" << p.second.asString() << "\"";
            else                               oss << p.second.toString();
            oss << "}";
            return oss.str();
        }
        case Kind::INSTANCE:
            return "<" + displayName(asInstance()->class_name) + " object>";
        case Kind::ERROR:
            // C4: "ValueError: mensagem"
            return displayName(asError()->kind) + ": " + asError()->message;
        case Kind::TYPE:
            return stripModulePrefixes(asType()->str());   // "int", "list<int>", "Ponto"
        case Kind::ENUM:   // "Cor.Verde"
            return displayName(asEnum().decl->name) + "." + asEnum().decl->members[asEnum().index];
        case Kind::STRUCT: {
            // C3: Nome{campo: valor, ...}, com strings entre aspas
            const auto& sv = *asStruct();
            oss << (sv.decl ? displayName(sv.decl->name) : std::string("struct")) << "{";
            for (size_t i = 0; i < sv.fields.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << (sv.decl ? sv.decl->fields[i].name : "?") << ": ";
                if (sv.fields[i].kind() == Kind::STRING)
                    oss << "\"" << sv.fields[i].asString() << "\"";
                else
                    oss << sv.fields[i].toString();
            }
            oss << "}";
            return oss.str();
        }
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
