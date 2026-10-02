#ifndef CINZA_TYPES_H
#define CINZA_TYPES_H

#include <algorithm>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace cinza {

// ============================================================================
// TIPOS COMO ESTRUTURA (B1)
//
// Antes, o semântico representava tipos como strings ("dict<string, int>") e
// os desmontava com extractTypeParams/substr/trim. Agora cada tipo é um
// TypeInfo imutável, criado uma única vez pelo TypeContext (hash-consing):
//
//   - igualdade de tipos = comparação de ponteiros (TypeRef)
//   - parâmetros acessados direto em `params`, sem parsing de texto
//   - `str()` devolve a forma canônica, usada só em mensagens de erro
//
// Tempo de vida: os TypeInfo vivem até o fim do processo (o executor lê
// resolved_type de CastExpr depois que o semântico terminou).
// ============================================================================

struct TypeInfo;
using TypeRef = const TypeInfo*;

struct TypeInfo {
    enum class Kind { Int, Decimal, String, Bool, Void, Var, List, Dict, Pair, Class, Struct, Error,
                      TypeVar /* C6: só em assinaturas nativas */,
                      Op      /* op<T1, T2, ...>: aceita um valor de qualquer tipo listado */,
                      Type    /* valor devolvido por type() e escrito como int, list<int>, Pessoa */,
                      Enum    /* enum Cor { ... }: só nomes, sem int por trás */,
                      Interface /* interface Forma { ... }: aceita objeto de classe que a cumpre */ };

    Kind                 kind;
    std::vector<TypeRef> params;   // List: {T}; Dict: {K, V}; Pair: {A, B}; Op: os membros
    std::string          name;     // Class/Struct/Error/Enum: nome do tipo (vazio nos demais)
    std::string          text;     // forma canônica: "int", "dict<string, int>", "Pessoa"

    const std::string& str() const { return text; }

    bool is(Kind k)       const { return kind == k; }
    bool isNumeric()      const { return kind == Kind::Int || kind == Kind::Decimal; }
    bool isPrimitive()    const { return isNumeric() || kind == Kind::String || kind == Kind::Bool; }
    bool isGeneric()      const { return kind == Kind::List || kind == Kind::Dict || kind == Kind::Pair; }

    // Parâmetros com nome (só válidos no kind correspondente)
    TypeRef elem()  const { return params[0]; }   // list<T>
    TypeRef key()   const { return params[0]; }   // dict<K, V>
    TypeRef value() const { return params[1]; }   // dict<K, V>
    TypeRef first() const { return params[0]; }   // pair<A, B>
    TypeRef second()const { return params[1]; }   // pair<A, B>

    // op<...>: `t` é um dos tipos listados
    bool hasMember(TypeRef t) const {
        return std::find(params.begin(), params.end(), t) != params.end();
    }
};

// Fábrica única de tipos. Todo TypeRef vem daqui, então dois tipos iguais
// são sempre o mesmo ponteiro.
class TypeContext {
public:
    using Kind = TypeInfo::Kind;

    static TypeContext& instance() {
        static TypeContext ctx;
        return ctx;
    }

    TypeRef intType()     { return intern(Kind::Int,     {}, ""); }
    TypeRef decimalType() { return intern(Kind::Decimal, {}, ""); }
    TypeRef stringType()  { return intern(Kind::String,  {}, ""); }
    TypeRef boolType()    { return intern(Kind::Bool,    {}, ""); }
    TypeRef voidType()    { return intern(Kind::Void,    {}, ""); }
    TypeRef varType()     { return intern(Kind::Var,     {}, ""); }

    TypeRef list(TypeRef elem)              { return intern(Kind::List, {elem}, ""); }
    TypeRef dict(TypeRef key, TypeRef val)  { return intern(Kind::Dict, {key, val}, ""); }
    TypeRef pair(TypeRef a, TypeRef b)      { return intern(Kind::Pair, {a, b}, ""); }
    TypeRef classType(const std::string& n) { return intern(Kind::Class, {}, n); }
    TypeRef structType(const std::string& n){ return intern(Kind::Struct, {}, n); }
    // C4: tipo de erro ("Error" é a raiz; ValueError, SaldoInsuficiente...)
    TypeRef errorType(const std::string& n) { return intern(Kind::Error, {}, n); }
    // C6: variável de tipo de assinatura nativa ("T"); "any" aceita qualquer tipo
    TypeRef typeVar(const std::string& n)   { return intern(Kind::TypeVar, {}, n); }
    // op<...>: a ordem não importa, então os membros ficam em ordem canônica
    // (pelo texto) e op<int, string> é o mesmo TypeRef que op<string, int>
    TypeRef op(std::vector<TypeRef> membros) {
        std::sort(membros.begin(), membros.end(),
                  [](TypeRef a, TypeRef b) { return a->text < b->text; });
        membros.erase(std::unique(membros.begin(), membros.end()), membros.end());
        return intern(Kind::Op, std::move(membros), "");
    }
    // tipo dos valores de type() e dos literais de tipo
    TypeRef typeType() { return intern(Kind::Type, {}, ""); }
    TypeRef enumType(const std::string& n) { return intern(Kind::Enum, {}, n); }
    TypeRef interfaceType(const std::string& n) { return intern(Kind::Interface, {}, n); }
    void    declareInterface(const std::string& n) { interface_names.insert(n); }
    // class X : Forma — a relação declarada (conferida no semântico)
    void    declareImplements(const std::string& cls, const std::string& iface) {
        implementa.insert({cls, iface});
    }
    bool    implements(const std::string& cls, const std::string& iface) const {
        return implementa.count({cls, iface}) > 0;
    }
    void    declareEnum(const std::string& n)  { enum_names.insert(n); }
    void    declareError(const std::string& n)       { error_names.insert(n); }
    bool    isError(const std::string& n)      const { return error_names.count(n) > 0; }

    // C3: um nome de tipo escrito no código (Type::CUSTOM) pode ser class ou
    // struct. Os structs são declarados antes de qualquer conversão de tipo.
    void    declareStruct(const std::string& n) { struct_names.insert(n); }
    TypeRef named(const std::string& n) {
        if (struct_names.count(n)) return structType(n);
        if (error_names.count(n))  return errorType(n);   // C4
        if (enum_names.count(n))   return enumType(n);
        if (interface_names.count(n)) return interfaceType(n);
        return classType(n);
    }

private:
    using Key = std::tuple<Kind, std::vector<TypeRef>, std::string>;
    std::map<Key, std::unique_ptr<TypeInfo>> table;
    std::set<std::string>                    struct_names;   // C3
    std::set<std::string>                    error_names;    // C4
    std::set<std::string>                    enum_names;
    std::set<std::string>                    interface_names;
    std::set<std::pair<std::string, std::string>> implementa;   // (classe, interface)

    TypeContext() = default;
    TypeContext(const TypeContext&)            = delete;
    TypeContext& operator=(const TypeContext&) = delete;

    TypeRef intern(Kind kind, std::vector<TypeRef> params, const std::string& name) {
        Key key{kind, params, name};
        auto it = table.find(key);
        if (it != table.end()) return it->second.get();

        auto info    = std::make_unique<TypeInfo>();
        info->kind   = kind;
        info->params = std::move(params);
        info->name   = name;
        info->text   = canonical(*info);
        TypeRef ref  = info.get();
        table.emplace(std::move(key), std::move(info));
        return ref;
    }

    // Mesmo formato de Type::toString() (ast.cpp), para as mensagens não mudarem
    static std::string canonical(const TypeInfo& t) {
        switch (t.kind) {
            case Kind::Int:     return "int";
            case Kind::Decimal: return "decimal";
            case Kind::String:  return "string";
            case Kind::Bool:    return "bool";
            case Kind::Void:    return "void";
            case Kind::Var:     return "var";
            case Kind::List:    return "list<" + t.params[0]->text + ">";
            case Kind::Dict:    return "dict<" + t.params[0]->text + ", " + t.params[1]->text + ">";
            case Kind::Pair:    return "pair<" + t.params[0]->text + ", " + t.params[1]->text + ">";
            case Kind::Class:   return t.name;
            case Kind::Struct:  return t.name;
            case Kind::Error:   return t.name;
            case Kind::TypeVar: return t.name;
            case Kind::Op: {
                std::string s = "op<";
                for (size_t i = 0; i < t.params.size(); ++i)
                    s += (i ? ", " : "") + t.params[i]->text;
                return s + ">";
            }
            case Kind::Type:    return "type";
            case Kind::Enum:    return t.name;
            case Kind::Interface: return t.name;
        }
        return "unknown";
    }
};

} // namespace cinza

#endif // CINZA_TYPES_H
