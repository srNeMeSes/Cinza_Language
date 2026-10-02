// Testes de unidade da coleta de ciclos (gc.h): o que está em ciclo e
// inalcançável é liberado; o que é alcançável de fora nunca é.
// Rodado por `make test` antes da suíte de .cinza.
// g++ acusa -Wmaybe-uninitialized ao expandir a comparação de chaves do dict
// (std::map<Value, Value>) sobre o std::variant: falso positivo conhecido, que só
// aparece aqui porque o teste monta o dict inteiro dentro de main
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "../gc.h"
#include <cstdio>

using namespace cinza;

static int falhas = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            std::printf("PASS  unit_gc: %s\n", #cond);                     \
        } else {                                                           \
            std::printf("FAIL  unit_gc: %s (linha %d)\n", #cond, __LINE__);  \
            ++falhas;                                                      \
        }                                                                  \
    } while (0)

int main() {
    CHECK(gc_registro.vivos == 0);

    // Lista que contém a si mesma: a contagem de referências sozinha não libera
    {
        Value l = makeList();
        l.asList()->elements.push_back(l);
    }
    CHECK(gc_registro.vivos == 1);
    CHECK(gcCollect() == 1);
    CHECK(gc_registro.vivos == 0);

    // Ciclo segurado por uma referência externa: não é lixo
    Value externo = makeList();
    {
        Value outro = makeList();
        externo.asList()->elements.push_back(outro);
        outro.asList()->elements.push_back(externo);
    }
    CHECK(gcCollect() == 0);
    CHECK(gc_registro.vivos == 2);
    CHECK(externo.asList()->elements.size() == 1);
    CHECK(externo.asList()->elements[0].asList()->elements.size() == 1);   // intacto

    // Soltando a referência externa, o ciclo inteiro vira lixo
    externo = Value();
    CHECK(gcCollect() == 2);
    CHECK(gc_registro.vivos == 0);

    // Ciclo que passa por pair e dict (valor do dict aponta para o dict)
    {
        Value d = makeDict();
        d.asDict()->entries[Value(1)] = makePair(Value(2), d);
    }
    CHECK(gcCollect() == 2);   // o dict e o pair
    CHECK(gc_registro.vivos == 0);

    // Sem ciclo: a contagem de referências já libera, nada sobra para a coleta
    {
        Value a = makeList();
        a.asList()->elements.push_back(makeList());
    }
    CHECK(gc_registro.vivos == 0);
    CHECK(gcCollect() == 0);

    return falhas ? 1 : 0;
}
