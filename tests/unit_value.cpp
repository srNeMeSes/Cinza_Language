// Testes de unidade de Value (A11): itens do C++ que nenhum programa .cinza
// consegue reproduzir. Rodado por `make test` antes da suíte de .cinza.
#include "../value.h"
#include <cstdio>

using namespace cinza;
using K = Value::Kind;

static int falhas = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            std::printf("PASS  unit_value: %s\n", #cond);                  \
        } else {                                                           \
            std::printf("FAIL  unit_value: %s (linha %d)\n", #cond, __LINE__); \
            ++falhas;                                                      \
        }                                                                  \
    } while (0)

int main() {
    // A11: Value("x") é STRING (antes virava BOOL true pela conversão const char* → bool)
    Value s("x");
    CHECK(s.kind() == K::STRING);
    CHECK(s.kind() == K::STRING && s.asString() == "x");   // && evita bad_variant_access

    // Cada construtor produz o kind esperado
    CHECK(Value().kind() == K::VOID_VAL);
    CHECK(Value(3).kind() == K::INT);
    CHECK(Value(std::int64_t{3}).kind() == K::INT);
    CHECK(Value(2.5).kind() == K::DECIMAL);
    CHECK(Value(true).kind() == K::BOOL);
    CHECK(Value(std::string("y")).kind() == K::STRING);
    CHECK(makeList().kind() == K::LIST);
    CHECK(makeDict().kind() == K::DICT);
    CHECK(makePair(Value(1), Value(2)).kind() == K::PAIR);
    CHECK(makeInstance("C", nullptr).kind() == K::INSTANCE);

    // A11: kind() acompanha o valor guardado, inclusive após reatribuir data
    Value v(1);
    v.data = std::string("texto");
    CHECK(v.kind() == K::STRING);
    v = Value(false);
    CHECK(v.kind() == K::BOOL);

    // C3: copiar um struct clona os campos (cópia rasa: list interna compartilhada)
    auto sv = std::make_shared<StructValue>();
    sv->fields = {Value(1), makeList()};
    Value original(sv);
    Value copia = original;
    CHECK(copia.kind() == K::STRUCT);
    CHECK(copia.asStruct().get() != original.asStruct().get());   // struct próprio
    copia.asStruct()->fields[0] = Value(99);
    CHECK(original.asStruct()->fields[0].asInt() == 1);            // original intacto
    CHECK(copia.asStruct()->fields[1].asList().get() ==
          original.asStruct()->fields[1].asList().get());          // list compartilhada
    CHECK(copia != original);
    copia.asStruct()->fields[0] = Value(1);
    CHECK(copia == original);                                      // == campo a campo
    Value movido = std::move(copia);
    CHECK(movido.kind() == K::STRUCT);                             // mover não clona nem perde

    return falhas ? 1 : 0;
}
