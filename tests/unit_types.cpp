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
    CHECK(t.i64Type() == t.i64Type());
    CHECK(t.list(t.i64Type()) == t.list(t.i64Type()));
    CHECK(t.dict(t.stringType(), t.list(t.f64Type())) ==
          t.dict(t.stringType(), t.list(t.f64Type())));
    CHECK(t.classType("Pessoa") == t.classType("Pessoa"));

    // ...e tipos diferentes são ponteiros diferentes
    CHECK(t.list(t.i64Type()) != t.list(t.f64Type()));
    CHECK(t.pair(t.i64Type(), t.stringType()) != t.pair(t.stringType(), t.i64Type()));
    CHECK(t.classType("A") != t.classType("B"));

    // Forma canônica (a mesma das mensagens de erro)
    CHECK(t.list(t.i64Type())->str() == "list<i64>");
    CHECK(t.dict(t.stringType(), t.list(t.f64Type()))->str() == "dict<string, list<f64>>");
    CHECK(t.pair(t.boolType(), t.classType("Pessoa"))->str() == "pair<bool, Pessoa>");
    CHECK(t.list(t.varType())->str() == "list<var>");

    // Parâmetros acessados direto, sem parsing de texto
    TypeRef d = t.dict(t.stringType(), t.i64Type());
    CHECK(d->key() == t.stringType() && d->value() == t.i64Type());
    CHECK(t.list(t.boolType())->elem() == t.boolType());

    // tipo dos valores de type()
    CHECK(t.typeType()->str() == "type");

    return falhas ? 1 : 0;
}
