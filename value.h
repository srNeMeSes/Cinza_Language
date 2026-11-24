#ifndef CINZA_VALUE_H
#define CINZA_VALUE_H

#include "types.h"        // TypeInfo: o valor de type() (P1: o runtime não depende da AST)
#include "source_files.h" // C5: displayName
#include "gc_object.h"   // coleta de ciclos: contêineres rastreados
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

// ============================================================================
// DESCRITORES (P1, llvm/DESENHO.md 2.4)
//
// O que o runtime precisa saber de uma classe, struct ou enum, sem apontar para
// a AST: o backend que criou o objeto (a CVM, a partir da AST; o código nativo,
// como constantes) é o dono dos descritores, que vivem até o fim do programa.
// Os nomes são os completos ("modulos.geom::Ponto"); as mensagens tiram o prefixo.
// ============================================================================
struct CzClasseDesc {
    std::string   nome;
    std::uint32_t indice = 0;   // na tabela de classes do backend (métodos, interfaces)
    std::uint32_t campos = 0;
    bool          rastreia = true;   // Q11: falso se o tipo é acíclico (fora da coleta de ciclos)
};
struct CzStructDesc {
    std::string              nome;
    std::vector<std::string> campos;   // na ordem da declaração (print, s.campo)
    bool                     rastreia = true;   // Q11: como em CzClasseDesc
};
struct CzEnumDesc {
    std::string              nome;
    std::vector<std::string> valores;
};

// Valor de enum: o descritor e o índice do valor. Não há i64 visível na
// linguagem: o índice só serve para comparar e ordenar chaves de dict.
struct EnumValue {
    const CzEnumDesc* desc  = nullptr;
    std::uint32_t     index = 0;
};
struct StructValue;
struct ErrorValue;

// P1 (llvm/DESENHO.md 2.2): string imutável com contagem. Antes o Value guardava
// um std::string por valor, e atribuir, passar ou pôr numa lista copiava o texto
// inteiro; agora copia só a referência. Não é rastreada pela coleta (não contém
// contêineres). O texto fica num std::string interno (o código nativo o lê só por
// funções do runtime, então o layout pode mudar sem afetar ninguém).
struct CzString : CzObjeto {
    const std::string texto;

    explicit CzString(const std::string& t) : CzObjeto(GcKind::String), texto(t) {}
    explicit CzString(std::string&& t) noexcept : CzObjeto(GcKind::String), texto(std::move(t)) {}
};

// ============================================================================
// VALUE
//
// Representa qualquer valor em memória durante a execução da Cinza.
// Todos os tipos da linguagem mapeiam para um Kind aqui.
//
// Semântica de cópia:
//   - I32, I64, F32, F64, STRING, BOOL, PAIR → value semantics (cópia por valor)
//   - STRUCT (C3)                      → value semantics: copiar clona os campos
//                                        (cópia rasa, ver Value(const Value&))
//   - LIST, DICT, INSTANCE             → reference semantics (Ref: contagem embutida, gc_object.h)
//
// A semântica de referência para coleções e instâncias garante que
// mutações (list.add(), campo = x) sejam visíveis para todos os
// "donos" do mesmo objeto — comportamento esperado em linguagens imperativas.
// ============================================================================

// P1 (llvm/DESENHO.md 2.3): a representação de um valor, 16 bytes, compatível
// com C — é o que vai atravessar a interface com o código nativo e o que as
// coleções guardam. Não tem construtor nem destrutor: quem a segura cuida da
// posse (o Value, abaixo, ou o código nativo).
struct CzValor {
    std::uint8_t  tipo;       // um Value::Kind
    std::uint8_t  _r[3];
    std::uint32_t extra;      // ENUM: o índice do valor
    union {
        std::int32_t  i32;
        std::int64_t  i64;
        float         f32;
        double        f64;
        std::uint8_t  b;
        CzObjeto*     obj;    // STRING, LIST, DICT, PAIR, INSTANCE, STRUCT, ERROR
        const void*   ptr;    // TYPE (TypeInfo*), ENUM (CzEnumDesc*)
    } u;
};
static_assert(sizeof(CzValor) == 16 && alignof(CzValor) == 8, "llvm/DESENHO.md 2.3");
static_assert(offsetof(CzValor, tipo) == 0 && offsetof(CzValor, extra) == 4 &&
              offsetof(CzValor, u) == 8, "llvm/DESENHO.md 2.3");

struct Value {
    enum class Kind : std::uint8_t {
        I64,         // inteiro de 64 bits (int64_t)
        F64,         // ponto flutuante de 64 bits (double, IEEE 754)
        STRING,
        BOOL,
        LIST,
        DICT,
        PAIR,
        INSTANCE,
        STRUCT,      // C3
        ERROR,       // C4: erro capturado em except / criado com Tipo("msg")
        TYPE,        // valor de type() e literal de tipo (i64, list<i64>, Pessoa)
        ENUM,        // valor de enum (Cor.Verde): a declaração e o índice do valor
        I32,         // inteiro de 32 bits (int32_t) — no fim, para não mudar a ordem dos demais
        F32,         // ponto flutuante de 32 bits (float, IEEE 754)
        // VOID_VAL: valor técnico interno do runtime.
        // Usos legítimos: retorno de funções void, ausência de valor em
        // caminhos internos da CVM (ex.: função sem return explícito).
        // NÃO representa variável não-inicializada do usuário — a linguagem
        // não possui estados UNBOUND. Toda variável não-coleção chega ao
        // runtime com um valor concreto avaliado pelo inicializador obrigatório.
        VOID_VAL,
        NULO         // Q1b: o null de um T? (a ausência de um valor válido ou conhecido)
    };

    // P1: o valor, no formato C (o antigo std::variant saiu). O Value é o dono:
    // copiar faz retain do objeto (e clona o struct), destruir faz release.
    CzValor v;

    // Os kinds que guardam um objeto do runtime (o ponteiro em u.obj, com posse)
    static constexpr bool guardaObjeto(Kind k) noexcept {
        constexpr unsigned mascara = (1u << static_cast<unsigned>(Kind::STRING)) |
            (1u << static_cast<unsigned>(Kind::LIST)) | (1u << static_cast<unsigned>(Kind::DICT)) |
            (1u << static_cast<unsigned>(Kind::PAIR)) | (1u << static_cast<unsigned>(Kind::INSTANCE)) |
            (1u << static_cast<unsigned>(Kind::STRUCT)) | (1u << static_cast<unsigned>(Kind::ERROR));
        return (mascara >> static_cast<unsigned>(k)) & 1u;
    }

    // ── Construtores convenientes ─────────────────────────────────────────

    Value() noexcept { poe(Kind::VOID_VAL); v.u.i64 = 0; }
    // Q1b: o null de um T?
    static Value nulo() noexcept { Value r; r.poe(Kind::NULO); return r; }
    bool isNull() const noexcept { return kind() == Kind::NULO; }

    explicit Value(std::int64_t x) noexcept { poe(Kind::I64); v.u.i64 = x; }
    // int do C++ (literais do próprio runtime: Value(0)) é i64; i32 só por Value::i32
    explicit Value(int x) noexcept          : Value(static_cast<std::int64_t>(x)) {}
    explicit Value(double x) noexcept       { poe(Kind::F64); v.u.f64 = x; }
    explicit Value(const std::string& s)    : Value(novo<CzString>(s)) {}
    explicit Value(std::string&& s)         : Value(novo<CzString>(std::move(s))) {}
    // A11: sem este construtor, Value("x") escolhia a conversão padrão
    // const char* → bool e virava BOOL true
    explicit Value(const char* s)           : Value(novo<CzString>(std::string(s))) {}
    explicit Value(bool x) noexcept         { poe(Kind::BOOL); v.u.i64 = 0; v.u.b = x ? 1 : 0; }
    explicit Value(Ref<CzString> o) noexcept;
    explicit Value(Ref<CinzaList> o) noexcept;
    explicit Value(Ref<CinzaDict> o) noexcept;
    explicit Value(Ref<CinzaPair> o) noexcept;
    explicit Value(Ref<ClassInstance> o) noexcept;
    explicit Value(Ref<StructValue> o) noexcept;
    explicit Value(Ref<ErrorValue> o) noexcept;
    explicit Value(const TypeInfo* t) noexcept  { poe(Kind::TYPE); v.u.ptr = t; }
    explicit Value(EnumValue e) noexcept        { poe(Kind::ENUM); v.u.ptr = e.desc; v.extra = e.index; }

    // ── Cópia (C3) ────────────────────────────────────────────────────────
    // struct é valor: copiar um Value que guarda struct clona os campos. É
    // cópia rasa: list/dict/objetos dentro dele continuam compartilhados,
    // porque copiar esses Values copia só o ponteiro. Assim toda atribuição,
    // argumento, return e .add() copia o struct sem código extra na CVM.
    // Mover não clona.
    Value(const Value& other);
    Value& operator=(const Value& other);
    Value(Value&& o) noexcept : v(o.v) { o.poe(Kind::VOID_VAL); o.v.u.i64 = 0; }
    Value& operator=(Value&& o) noexcept {
        if (this != &o) {
            const CzValor velho = v;
            v = o.v;
            o.poe(Kind::VOID_VAL);
            o.v.u.i64 = 0;
            solta(velho);
        }
        return *this;
    }
    ~Value() { solta(v); }

    // Os tipos de 32 bits têm fábrica com nome: o construtor Value(int) do C++ é i64
    static Value i32(std::int32_t x) noexcept { Value r; r.v.tipo = static_cast<std::uint8_t>(Kind::I32); r.v.u.i64 = 0; r.v.u.i32 = x; return r; }
    static Value f32(float x) noexcept        { Value r; r.v.tipo = static_cast<std::uint8_t>(Kind::F32); r.v.u.i64 = 0; r.v.u.f32 = x; return r; }

    // ── Kind ──────────────────────────────────────────────────────────────
    // A11: guardado junto do valor, e só os construtores o escrevem: nunca
    // diverge do valor guardado.
    Kind kind() const noexcept { return static_cast<Kind>(v.tipo); }

    // ── Acessores tipados ─────────────────────────────────────────────────
    // Conferem o kind (como o std::get antes): acesso com o tipo errado é erro
    // interno, nunca comportamento indefinido. Os objetos saem como ponteiro
    // sem posse: válido enquanto este Value não mudar.

    // Inteiro de qualquer largura como int64 (i32 → i64 é exato): a promoção
    // dentro da família não precisa de conversão para ser lida
    std::int64_t asInt() const {
        if (kind() == Kind::I32) return v.u.i32;
        confere(Kind::I64);
        return v.u.i64;
    }
    // Ponto flutuante de qualquer largura como double (f32 → f64 é exato)
    double asFloat() const {
        if (kind() == Kind::F32) return v.u.f32;
        confere(Kind::F64);
        return v.u.f64;
    }
    std::int32_t asI32() const { confere(Kind::I32); return v.u.i32; }
    float        asF32() const { confere(Kind::F32); return v.u.f32; }

    bool isInteger() const { const Kind k = kind(); return k == Kind::I64 || k == Kind::I32; }
    bool isFloat()   const { const Kind k = kind(); return k == Kind::F64 || k == Kind::F32; }
    bool isNumber()  const { return isInteger() || isFloat(); }
    // Número de qualquer tipo como double (ordenação, Math): só para leitura
    double asNumber() const { return isInteger() ? static_cast<double>(asInt()) : asFloat(); }
    bool        asBool()     const { confere(Kind::BOOL); return v.u.b != 0; }
    const std::string& asString() const;

    CinzaList* asList() const;
    CinzaDict* asDict() const;
    CinzaPair* asPair() const;
    ClassInstance* asInstance() const;
    StructValue* asStruct() const;
    ErrorValue* asError() const;
    const TypeInfo* asType()    const { confere(Kind::TYPE);     return static_cast<const TypeInfo*>(v.u.ptr); }
    EnumValue      asEnum()     const {
        confere(Kind::ENUM);
        return EnumValue{static_cast<const CzEnumDesc*>(v.u.ptr), v.extra};
    }
    // O objeto do runtime guardado (STRING...ERROR), ou nulo
    CzObjeto* objeto() const noexcept { return guardaObjeto(kind()) ? v.u.obj : nullptr; }

    // P3: a fronteira com o código nativo (llvm/DESENHO.md 2.3), sempre explícita.
    // `empresta`: um Value novo a partir de um CzValor que outro segura (retain; um
    // struct é clonado, como em toda cópia). `toma`: assume a posse (+1) de um CzValor,
    // sem mexer na contagem. `entrega`: devolve o CzValor com a posse e fica vazio.
    static Value empresta(const CzValor& c);
    static Value toma(const CzValor& c) noexcept { Value r; r.v = c; return r; }
    CzValor entrega() noexcept {
        const CzValor c = v;
        poe(Kind::VOID_VAL);
        v.u.i64 = 0;
        return c;
    }

    // ── Utilitário de display ─────────────────────────────────────────────

    std::string toString() const;

    // ── Operadores de comparação (necessários para dict key e ==) ─────────
    //
    // Ordem total pelo Kind; dentro da mesma família numérica (i32/i64 ou
    // f32/f64), pelo valor, como se fosse o mesmo tipo

    bool operator==(const Value& other) const;
    bool operator< (const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }
    bool operator<=(const Value& other) const { return !(other < *this);  }
    bool operator> (const Value& other) const { return other < *this;     }
    bool operator>=(const Value& other) const { return !(*this < other);  }

private:
    void poe(Kind k) noexcept {
        v.tipo = static_cast<std::uint8_t>(k);
        v._r[0] = v._r[1] = v._r[2] = 0;
        v.extra = 0;
    }
    void adota(Kind k, CzObjeto* o) noexcept { poe(k); v.u.obj = o; }
    static void solta(const CzValor& x) noexcept {
        if (guardaObjeto(static_cast<Kind>(x.tipo)) && x.u.obj) czRelease(x.u.obj);
    }
    void confere(Kind k) const {
        if (kind() != k) [[unlikely]] falhaTipo();
    }
    // Fria e fora de linha: o teste dos acessores fica no caminho quente sem o throw
    [[noreturn, gnu::noinline, gnu::cold]] static void falhaTipo() {
        throw std::logic_error("erro interno: valor de tipo inesperado no runtime");
    }
};
static_assert(sizeof(Value) == 16, "o Value é o CzValor com posse");

// ============================================================================
// TIPOS DE COLEÇÃO
// Definidos APÓS Value para que Value seja completo quando são usados.
// ============================================================================

// Q11: os campos (struct, objeto) e os nós do dict vêm do alocador de objetos pequenos
using Campos   = std::vector<Value, aloca::Pequeno<Value>>;
using Entradas = std::map<Value, Value, std::less<Value>, aloca::Pequeno<std::pair<const Value, Value>>>;

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
    Entradas entries;   // Q11: os nós pelo alocador de objetos pequenos

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
//   - desc (no cabeçalho): o CzClasseDesc — nome (mensagens, type()) e o
//                 índice da classe no backend (métodos, interfaces)
//   - fields:     valores dos campos, na ordem da declaração (B2: o
//                 semântico anota o índice em cada acesso)
// ============================================================================

struct ClassInstance : GcObject {
    Campos fields;   // Q11: pelo alocador de objetos pequenos

    ClassInstance() : GcObject(GcKind::Instance) {}
    explicit ClassInstance(const CzClasseDesc* d)
        : GcObject(GcKind::Instance, !d || d->rastreia), fields(d ? d->campos : 0) { desc = d; }

    const CzClasseDesc* classe() const { return static_cast<const CzClasseDesc*>(desc); }
};

// ============================================================================
// STRUCT VALUE (C3)
//
// Valor de um struct: os campos na ordem da declaração. O descritor (desc, no
// cabeçalho) dá os nomes (para `s.campo` e para o print). Diferente de
// ClassInstance, é copiado a cada cópia do Value que o guarda.
// ============================================================================

struct StructValue : GcObject {
    Campos fields;   // Q11: pelo alocador de objetos pequenos

    StructValue() : GcObject(GcKind::Struct) {}
    explicit StructValue(const CzStructDesc* d) : GcObject(GcKind::Struct, !d || d->rastreia) { desc = d; }

    const CzStructDesc* tipo() const { return static_cast<const CzStructDesc*>(desc); }

    // Índice do campo pelo nome, ou -1
    int indexOf(const std::string& name) const {
        if (!tipo()) return -1;
        for (size_t i = 0; i < tipo()->campos.size(); ++i)
            if (tipo()->campos[i] == name) return static_cast<int>(i);
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

struct ErrorValue : CzObjeto {   // P1: objeto com contagem (não rastreado: não forma ciclo)
    ErrorValue() noexcept : CzObjeto(GcKind::Error) {}
    std::string  kind;      // "ValueError", "SaldoInsuficiente", "Error"...
    std::string  message;
    std::int64_t line   = 0;
    std::int64_t column = 0;
    std::vector<std::string> trace;   // stack trace de onde o erro surgiu (rethrow o mantém)
    int          file_id = 0;         // C5: arquivo onde surgiu (0 = principal)
};

// Construtores e acessores que precisam dos tipos completos (P1)
inline Value::Value(Ref<CzString> o) noexcept { adota(Kind::STRING, static_cast<CzObjeto*>(o.solta())); }
inline Value::Value(Ref<CinzaList> o) noexcept { adota(Kind::LIST, static_cast<CzObjeto*>(o.solta())); }
inline Value::Value(Ref<CinzaDict> o) noexcept { adota(Kind::DICT, static_cast<CzObjeto*>(o.solta())); }
inline Value::Value(Ref<CinzaPair> o) noexcept { adota(Kind::PAIR, static_cast<CzObjeto*>(o.solta())); }
inline Value::Value(Ref<ClassInstance> o) noexcept { adota(Kind::INSTANCE, static_cast<CzObjeto*>(o.solta())); }
inline Value::Value(Ref<StructValue> o) noexcept { adota(Kind::STRUCT, static_cast<CzObjeto*>(o.solta())); }
inline Value::Value(Ref<ErrorValue> o) noexcept { adota(Kind::ERROR, static_cast<CzObjeto*>(o.solta())); }
inline const std::string& Value::asString() const {
    confere(Kind::STRING);
    return static_cast<const CzString*>(v.u.obj)->texto;
}
inline CinzaList* Value::asList() const { confere(Kind::LIST); return static_cast<CinzaList*>(v.u.obj); }
inline CinzaDict* Value::asDict() const { confere(Kind::DICT); return static_cast<CinzaDict*>(v.u.obj); }
inline CinzaPair* Value::asPair() const { confere(Kind::PAIR); return static_cast<CinzaPair*>(v.u.obj); }
inline ClassInstance* Value::asInstance() const { confere(Kind::INSTANCE); return static_cast<ClassInstance*>(v.u.obj); }
inline StructValue* Value::asStruct() const { confere(Kind::STRUCT); return static_cast<StructValue*>(v.u.obj); }
inline ErrorValue* Value::asError() const { confere(Kind::ERROR); return static_cast<ErrorValue*>(v.u.obj); }

// Q11: o clone de um struct, fora de linha — o construtor de cópia do Value está em todo lugar
// (no laço de despacho da CVM também), e o caminho comum dele é só o retain
[[gnu::noinline]] inline CzObjeto* clonaStruct(const StructValue& s) {
    return static_cast<CzObjeto*>(novo<StructValue>(s).solta());
}

inline Value::Value(const Value& other) : v(other.v) {
    if (other.kind() == Kind::STRUCT) {
        // clona; campos copiados um a um
        v.u.obj = clonaStruct(*other.asStruct());
    } else if (guardaObjeto(kind())) {
        czRetain(v.u.obj);
    }
}

inline Value Value::empresta(const CzValor& c) {
    Value alheio;   // não é dono de c: desarmado antes de ser destruído, mesmo se a cópia falhar
    alheio.v = c;
    struct Desarma {
        Value& x;
        ~Desarma() { x.poe(Kind::VOID_VAL); x.v.u.i64 = 0; }
    } desarma{alheio};
    return Value(alheio);
}

inline Value& Value::operator=(const Value& other) {
    if (this == &other) return *this;
    const Kind k = other.kind();
    if (k == Kind::STRUCT) {   // clona antes de soltar o antigo
        Value copia(other);
        return *this = std::move(copia);
    }
    if (guardaObjeto(k)) czRetain(other.v.u.obj);   // retain do novo antes do release do antigo
    const CzValor velho = v;
    v = other.v;
    solta(velho);
    return *this;
}

// ============================================================================
// VALUE — IMPLEMENTAÇÕES INLINE
// (definidas aqui porque dependem de CinzaList/Dict/Pair/ClassInstance)
// ============================================================================

// P1b: a igualdade não é recursiva. Contêineres (list, dict, pair, struct) dentro de
// contêineres podem estar aninhados a qualquer profundidade (struct No { list<No> f; }),
// e a recursão do C++ estourava a pilha do sistema com centenas de milhares de níveis;
// os pares a comparar vão numa pilha explícita, no heap.
namespace detail {
// Compara a e b sem descer: devolve false se já diferem; se são contêineres do mesmo
// tipo e tamanho, empilha os pares de filhos a comparar
inline bool igualRaso(const Value& a, const Value& b,
                      std::vector<std::pair<const Value*, const Value*>>& pendentes) {
    using Kind = Value::Kind;
    if (a.kind() != b.kind()) {
        // mesma família numérica, larguras diferentes: compara o valor
        if (a.isInteger() && b.isInteger()) return a.asInt() == b.asInt();
        if (a.isFloat() && b.isFloat())     return a.asFloat() == b.asFloat();
        return false;
    }
    switch (a.kind()) {
        case Kind::VOID_VAL:  return true;
        case Kind::NULO:      return true;   // null == null
        case Kind::I64: case Kind::I32: return a.asInt()   == b.asInt();
        case Kind::F64: case Kind::F32: return a.asFloat() == b.asFloat();
        case Kind::STRING:    return a.asString() == b.asString();
        case Kind::BOOL:      return a.asBool()   == b.asBool();
        case Kind::LIST: {
            const auto& x = a.asList()->elements;
            const auto& y = b.asList()->elements;
            if (x.size() != y.size()) return false;
            for (std::size_t i = 0; i < x.size(); ++i) pendentes.emplace_back(&x[i], &y[i]);
            return true;
        }
        case Kind::DICT: {   // as chaves estão em ordem (std::map): compara entrada a entrada
            const auto& x = a.asDict()->entries;
            const auto& y = b.asDict()->entries;
            if (x.size() != y.size()) return false;
            for (auto i = x.begin(), j = y.begin(); i != x.end(); ++i, ++j) {
                pendentes.emplace_back(&i->first, &j->first);
                pendentes.emplace_back(&i->second, &j->second);
            }
            return true;
        }
        case Kind::PAIR:
            pendentes.emplace_back(&a.asPair()->first, &b.asPair()->first);
            pendentes.emplace_back(&a.asPair()->second, &b.asPair()->second);
            return true;
        case Kind::INSTANCE:
            // Dois objetos são iguais apenas se são o mesmo (identidade de referência)
            return a.asInstance() == b.asInstance();
        case Kind::ERROR:
            // C4: erros são iguais só se forem o mesmo erro (identidade)
            return a.asError() == b.asError();
        case Kind::TYPE:
            // tipos são únicos (TypeContext): mesmo ponteiro = mesmo tipo
            return a.asType() == b.asType();
        case Kind::ENUM:
            return a.asEnum().desc == b.asEnum().desc && a.asEnum().index == b.asEnum().index;
        case Kind::STRUCT: {
            // C3: igualdade estrutural, campo a campo
            const auto& x = *a.asStruct();
            const auto& y = *b.asStruct();
            if (x.tipo() != y.tipo() || x.fields.size() != y.fields.size()) return false;
            for (std::size_t i = 0; i < x.fields.size(); ++i)
                pendentes.emplace_back(&x.fields[i], &y.fields[i]);
            return true;
        }
        default:
            return false;
    }
}
}  // namespace detail

inline bool Value::operator==(const Value& other) const {
    std::vector<std::pair<const Value*, const Value*>> pendentes;
    if (!detail::igualRaso(*this, other, pendentes)) return false;
    while (!pendentes.empty()) {   // vazio no caso comum (números, strings): sem laço
        const auto [a, b] = pendentes.back();
        pendentes.pop_back();
        if (!detail::igualRaso(*a, *b, pendentes)) return false;
    }
    return true;
}

inline bool Value::operator<(const Value& other) const {
    // Tipos diferentes: ordem por Kind (mesma família numérica: pelo valor)
    if (kind() != other.kind()) {
        if (isInteger() && other.isInteger()) return asInt() < other.asInt();
        if (isFloat() && other.isFloat())     return asFloat() < other.asFloat();
        return static_cast<int>(kind()) < static_cast<int>(other.kind());
    }

    switch (kind()) {
        case Kind::I64: case Kind::I32: return asInt()   < other.asInt();
        case Kind::F64: case Kind::F32: return asFloat() < other.asFloat();
        case Kind::STRING:  return asString()  < other.asString();
        case Kind::BOOL:    return static_cast<int>(asBool()) < static_cast<int>(other.asBool());
        case Kind::ENUM: {   // chave de dict: ordem da declaração (e por enum)
            const EnumValue a = asEnum(), b = other.asEnum();
            if (a.desc != b.desc) return std::less<const CzEnumDesc*>()(a.desc, b.desc);
            return a.index < b.index;
        }
        default:            return false;   // listas/dicts/pares/instâncias: não ordenáveis
    }
}

// P1b: a conversão em texto não é recursiva (pelo mesmo motivo da igualdade): os
// pedaços a escrever vão numa pilha explícita, no heap. Cada item é um valor (com ou
// sem aspas, se for string) ou um trecho fixo.
namespace detail {
// Um valor que não é contêiner, em texto (strings sem aspas)
inline std::string folhaTexto(const Value& v) {
    using Kind = Value::Kind;
    switch (v.kind()) {
        case Kind::VOID_VAL:  return "void";
        case Kind::NULO:      return "null";
        case Kind::I64: case Kind::I32: return std::to_string(v.asInt());
        case Kind::F64: case Kind::F32: {
            // A menor forma que volta ao mesmo número, na precisão do próprio
            // tipo (0.1f sai 0.1, não 0.10000000149011612). Valor inteiro sai
            // sem ".0" (4.0 → 4). Infinito e NaN não existem (seção M).
            char buf[64];
            auto [fim, ec] = v.kind() == Kind::F32
                ? std::to_chars(buf, buf + sizeof buf, v.asF32())
                : std::to_chars(buf, buf + sizeof buf, v.asFloat());
            return ec == std::errc() ? std::string(buf, fim) : std::string("?");
        }
        case Kind::STRING:  return v.asString();
        case Kind::BOOL:    return v.asBool() ? "true" : "false";
        case Kind::INSTANCE:
            return "<" + (v.asInstance()->classe() ? displayName(v.asInstance()->classe()->nome)
                                                   : std::string("?")) + " object>";
        case Kind::ERROR:
            // C4: "ValueError: mensagem"
            return displayName(v.asError()->kind) + ": " + v.asError()->message;
        case Kind::TYPE:
            return stripModulePrefixes(v.asType()->str());   // "i64", "list<i32>", "Ponto"
        case Kind::ENUM:   // "Cor.Verde"
            return displayName(v.asEnum().desc->nome) + "." + v.asEnum().desc->valores[v.asEnum().index];
        default:
            return "<unknown>";
    }
}

struct PedacoTexto {
    const Value* valor;   // nulo: trecho fixo
    const char*  trecho;
    bool         aspas;   // string dentro de contêiner: entre aspas
};
}  // namespace detail

inline std::string Value::toString() const {
    switch (kind()) {
        case Kind::LIST: case Kind::DICT: case Kind::PAIR: case Kind::STRUCT: break;
        default: return detail::folhaTexto(*this);
    }
    using detail::PedacoTexto;
    std::string out;
    std::vector<PedacoTexto> pilha;   // em ordem inversa: o topo é o próximo a escrever
    auto fixo  = [&](const char* t) { pilha.push_back({nullptr, t, false}); };
    auto valor = [&](const Value& v) { pilha.push_back({&v, nullptr, true}); };
    pilha.push_back({this, nullptr, false});
    while (!pilha.empty()) {
        const PedacoTexto p = pilha.back();
        pilha.pop_back();
        if (!p.valor) { out += p.trecho; continue; }
        const Value& v = *p.valor;
        switch (v.kind()) {
            case Kind::STRING:
                // Strings dentro de contêineres aparecem com aspas
                if (p.aspas) { out += '"'; out += v.asString(); out += '"'; }
                else out += v.asString();
                break;
            case Kind::LIST: {
                const auto& e = v.asList()->elements;
                out += '[';
                fixo("]");
                for (std::size_t i = e.size(); i-- > 0;) {
                    valor(e[i]);
                    if (i > 0) fixo(", ");
                }
                break;
            }
            case Kind::DICT: {
                const auto& entradas = v.asDict()->entries;
                std::vector<const std::pair<const Value, Value>*> em_ordem;
                em_ordem.reserve(entradas.size());
                for (const auto& kv : entradas) em_ordem.push_back(&kv);
                out += '{';
                fixo("}");
                for (std::size_t i = em_ordem.size(); i-- > 0;) {
                    valor(em_ordem[i]->second);
                    fixo(": ");
                    valor(em_ordem[i]->first);
                    if (i > 0) fixo(", ");
                }
                break;
            }
            case Kind::PAIR: {
                const auto& par = *v.asPair();
                out += '{';
                fixo("}");
                valor(par.second);
                fixo(", ");
                valor(par.first);
                break;
            }
            case Kind::STRUCT: {
                // C3: Nome{campo: valor, ...}, com strings entre aspas
                const auto& sv = *v.asStruct();
                out += sv.tipo() ? displayName(sv.tipo()->nome) : std::string("struct");
                out += '{';
                fixo("}");
                for (std::size_t i = sv.fields.size(); i-- > 0;) {
                    valor(sv.fields[i]);
                    fixo(": ");
                    fixo(sv.tipo() ? sv.tipo()->campos[i].c_str() : "?");
                    if (i > 0) fixo(", ");
                }
                break;
            }
            default:
                out += detail::folhaTexto(v);
                break;
        }
    }
    return out;
}

// ============================================================================
// FACTORY HELPERS
// Funções globais para criar valores de coleção de forma expressiva.
// ============================================================================

// Q11: as fábricas fora de linha (a alocação e o registro da coleta não entram em quem chama)
[[gnu::noinline]] inline Value makeList(std::vector<Value> elems = {}) {
    return Value(novo<CinzaList>(std::move(elems)));
}

[[gnu::noinline]] inline Value makeDict() {
    return Value(novo<CinzaDict>());
}

inline Value makePair(Value first, Value second) {
    return Value(novo<CinzaPair>(std::move(first), std::move(second)));
}

[[gnu::noinline]] inline Value makeInstance(const CzClasseDesc* desc) {
    return Value(novo<ClassInstance>(desc));
}

// ============================================================================
// DESTRUIÇÃO ITERATIVA (P1, llvm/DESENHO.md 2.1)
//
// Um objeto que chega a zero sai do registro da coleta (se for contêiner) e
// entra na fila de destruição, encadeada pelo próprio campo `prox`. Se ninguém
// mais acima já esvazia a fila, este esvazia: destruir um objeto solta os
// campos dele, e um filho que chega a zero só entra na fila — a profundidade da
// pilha do sistema fica constante, qualquer que seja o tamanho da cadeia.
// ============================================================================

inline void czApaga(CzObjeto* o) noexcept {
    switch (o->gc_kind) {
        case GcKind::List:     delete static_cast<CinzaList*>(o);     break;
        case GcKind::Dict:     delete static_cast<CinzaDict*>(o);     break;
        case GcKind::Pair:     delete static_cast<CinzaPair*>(o);     break;
        case GcKind::Instance: delete static_cast<ClassInstance*>(o); break;
        case GcKind::Struct:   delete static_cast<StructValue*>(o);   break;
        case GcKind::Error:    delete static_cast<ErrorValue*>(o);    break;
        case GcKind::String:   delete static_cast<CzString*>(o);      break;
    }
}

} // namespace cinza

#endif // CINZA_VALUE_H
