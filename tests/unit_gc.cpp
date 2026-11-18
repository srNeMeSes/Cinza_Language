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

    // P1: contagem embutida. Nasce com 1; copiar o Value faz retain; soltar, release
    {
        Value l = makeList();
        CHECK(l.asList()->refs == 1);
        {
            Value copia = l;
            CHECK(l.asList()->refs == 2);
        }
        CHECK(l.asList()->refs == 1);
        Value movido = std::move(l);
        CHECK(movido.asList()->refs == 1);
    }
    CHECK(gc_registro.objetos == 0);

    // Atribuir a si mesmo não solta o objeto antes da hora
    {
        Value l = makeList();
        Value& mesmo = l;
        l = mesmo;
        CHECK(l.asList()->refs == 1);
        CHECK(gc_registro.vivos == 1);
    }

    // Destruição iterativa: uma cadeia de 1 milhão de listas (cada uma dentro da
    // anterior) é solta sem recursão, e nada fica na fila
    {
        Value raiz = makeList();
        Value atual = raiz;
        for (int i = 0; i < 1000000; ++i) {
            Value nova = makeList();
            atual.asList()->elements.push_back(nova);
            atual = nova;
        }
        CHECK(gc_registro.vivos == 1000001);
    }
    CHECK(gc_registro.vivos == 0);
    CHECK(gc_registro.objetos == 0);
    CHECK(gc_registro.fila == nullptr && !gc_registro.esvaziando);

    // Erro também é objeto com contagem (não rastreado pela coleta)
    {
        auto e = novo<ErrorValue>();
        e->kind = "ValueError";
        Value v(std::move(e));
        CHECK(gc_registro.objetos == 1 && gc_registro.vivos == 0);
    }
    CHECK(gc_registro.objetos == 0);

    return falhas ? 1 : 0;
}
