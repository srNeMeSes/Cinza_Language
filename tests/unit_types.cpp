// Testes de unidade de TypeContext (B1): hash-consing e forma canônica.
// Rodado por `make test` antes da suíte de .cinza.
#include "../types.h"
#include <cstdio>

using namespace cinza;

static int falhas = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            std::printf("PASS  unit_types: %s\n", #cond);                  \
        } else {                                                           \
            std::printf("FAIL  unit_types: %s (linha %d)\n", #cond, __LINE__); \
            ++falhas;                                                      \
        }                                                                  \
    } while (0)

int main() {
    auto& t = TypeContext::instance();

    // Hash-consing: tipos estruturalmente iguais são o mesmo ponteiro
    CHECK(t.intType() == t.intType());
    CHECK(t.list(t.intType()) == t.list(t.intType()));
    CHECK(t.dict(t.stringType(), t.list(t.decimalType())) ==
          t.dict(t.stringType(), t.list(t.decimalType())));
    CHECK(t.classType("Pessoa") == t.classType("Pessoa"));

    // ...e tipos diferentes são ponteiros diferentes
    CHECK(t.list(t.intType()) != t.list(t.decimalType()));
    CHECK(t.pair(t.intType(), t.stringType()) != t.pair(t.stringType(), t.intType()));
    CHECK(t.classType("A") != t.classType("B"));

    // Forma canônica igual à antiga representação em string (mensagens de erro)
    CHECK(t.list(t.intType())->str() == "list<int>");
    CHECK(t.dict(t.stringType(), t.list(t.decimalType()))->str() == "dict<string, list<decimal>>");
    CHECK(t.pair(t.boolType(), t.classType("Pessoa"))->str() == "pair<bool, Pessoa>");
    CHECK(t.list(t.varType())->str() == "list<var>");

    // Parâmetros acessados direto, sem parsing de texto
    TypeRef d = t.dict(t.stringType(), t.intType());
    CHECK(d->key() == t.stringType() && d->value() == t.intType());
    CHECK(t.list(t.boolType())->elem() == t.boolType());

    // op<...>: a ordem não importa — os membros ficam em ordem canônica
    TypeRef a = t.op({t.intType(), t.stringType()});
    TypeRef b = t.op({t.stringType(), t.intType()});
    CHECK(a == b);
    CHECK(a->str() == "op<int, string>");
    CHECK(a->hasMember(t.intType()) && !a->hasMember(t.boolType()));
    CHECK(t.op({t.intType(), t.boolType()}) != a);
    CHECK(t.typeType()->str() == "type");

    return falhas ? 1 : 0;
}
