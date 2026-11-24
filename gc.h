#ifndef CINZA_GC_H
#define CINZA_GC_H

#include "value.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace cinza {

// ============================================================================
// COLETA DE CICLOS (trial deletion, como no CPython)
//
// A contagem de referências (embutida no objeto, gc_object.h) libera quase tudo,
// mas não um ciclo:
//   class No { list<No> xs; }   a.xs.add(a);   — `a` segura a si mesmo.
// Todo ciclo da Cinza passa por um contêiner, e todos são GcObject.
//
// Coleta:
//   1. gc_refs = referências de cada contêiner (refs);
//   2. desconta as referências que vêm de outros contêineres rastreados;
//   3. quem fica com gc_refs > 0 é referenciado de fora (frames, globais,
//      temporários da CVM): raiz. Marca tudo o que as raízes alcançam;
//   4. o resto é lixo em ciclo: esvazia esses contêineres (segurando uma
//      referência a cada um enquanto isso), e a contagem libera tudo.
//
// Não é preciso enumerar os temporários do C++: qualquer Ref fora dos
// contêineres conta como referência externa. Por isso a CVM só dispara a
// coleta em pontos seguros (NEWOBJ, cvm/vm.cpp), onde nenhum ponteiro cru
// aponta para dentro de um contêiner sem um Value forte segurando o dono.
// ============================================================================

// Contêiner referenciado por um Value (nullptr se o Value não é contêiner).
// Lê o ponteiro sem copiar o Value, para não mexer nas contagens.
inline GcObject* gcChild(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::LIST: case Value::Kind::DICT: case Value::Kind::PAIR:
        case Value::Kind::INSTANCE: case Value::Kind::STRUCT:
            return static_cast<GcObject*>(v.objeto());
        default:
            return nullptr;
    }
}

template <typename F>
inline void gcForEachChild(GcObject* o, F&& f) {
    auto visita = [&](const Value& v) { if (GcObject* c = gcChild(v)) f(c); };
    switch (o->gc_kind) {
        case GcKind::List:
            for (const Value& v : static_cast<CinzaList*>(o)->elements) visita(v);
            break;
        case GcKind::Dict:
            for (const auto& [k, v] : static_cast<CinzaDict*>(o)->entries) { visita(k); visita(v); }
            break;
        case GcKind::Pair: {
            auto* p = static_cast<CinzaPair*>(o);
            visita(p->first);
            visita(p->second);
            break;
        }
        case GcKind::Instance:
            for (const Value& v : static_cast<ClassInstance*>(o)->fields) visita(v);
            break;
        case GcKind::Struct:
            for (const Value& v : static_cast<StructValue*>(o)->fields) visita(v);
            break;
        case GcKind::Error: case GcKind::String:   // não são contêineres: fora do registro
            break;
    }
}

// Esvazia um contêiner (só para lixo): solta as referências que fecham o ciclo
inline void gcClear(GcObject* o) {
    switch (o->gc_kind) {
        case GcKind::List:     static_cast<CinzaList*>(o)->elements.clear(); break;
        case GcKind::Dict:     static_cast<CinzaDict*>(o)->entries.clear();  break;
        case GcKind::Pair: {
            auto* p = static_cast<CinzaPair*>(o);
            p->first  = Value();
            p->second = Value();
            break;
        }
        case GcKind::Instance:
            for (Value& v : static_cast<ClassInstance*>(o)->fields) v = Value();
            break;
        case GcKind::Struct:
            for (Value& v : static_cast<StructValue*>(o)->fields) v = Value();
            break;
        case GcKind::Error: case GcKind::String:
            break;
    }
}

struct GcStats {
    std::size_t coletas   = 0;
    std::size_t liberados = 0;   // contêineres liberados por estarem em ciclos
};
inline GcStats gc_stats{};

// Q11: GERAÇÕES. Uma coleta completa percorre todos os contêineres vivos; com muitos vivos (uma
// lista de 500 mil structs, uma árvore grande), era metade do tempo do programa. Como no CPython,
// os contêineres nascem jovens, e a coleta comum percorre só os jovens: uma referência de um
// velho para um jovem conta como de fora (o jovem é raiz). Quem sobrevive vira velho. Um ciclo
// entre velhos só sai na coleta completa, que roda quando os velhos cresceram metade desde a
// última completa (a memória presa em ciclos velhos fica limitada a uma fração dos vivos).

namespace detail {
// O passo comum das duas coletas sobre a lista `cabeca`: `conta(c)` diz se o filho c está na
// geração percorrida (os outros não entram na conta: são de fora)
template <typename Conta>
inline std::size_t gcVarre(CzObjeto* cabeca, Conta&& conta) {
    auto contem = [](CzObjeto* o) { return static_cast<GcObject*>(o); };
    // 1. referências de cada contêiner
    for (CzObjeto* c = cabeca; c; c = c->prox) {
        GcObject* o = contem(c);
        o->gc_refs  = static_cast<std::int64_t>(o->refs);
        o->gc_marca = false;
    }
    // 2. desconta as referências internas (só as de dentro da geração)
    for (CzObjeto* c = cabeca; c; c = c->prox)
        gcForEachChild(contem(c), [&](GcObject* f) { if (conta(f)) --f->gc_refs; });

    // 3. marca o que as raízes alcançam (dentro da geração)
    std::vector<GcObject*> pilha;
    for (CzObjeto* c = cabeca; c; c = c->prox)
        if (GcObject* o = contem(c); o->gc_refs > 0) { o->gc_marca = true; pilha.push_back(o); }
    while (!pilha.empty()) {
        GcObject* o = pilha.back();
        pilha.pop_back();
        gcForEachChild(o, [&](GcObject* c) {
            if (conta(c) && !c->gc_marca) { c->gc_marca = true; pilha.push_back(c); }
        });
    }

    // 4. lixo: segura cada um (nada é destruído no meio) e esvazia; os
    //    contadores chegam a zero ao soltar, e a destruição vai pela fila
    std::vector<GcObject*> lixo;
    for (CzObjeto* c = cabeca; c; c = c->prox)
        if (GcObject* o = contem(c); !o->gc_marca) { czRetain(o); lixo.push_back(o); }
    for (GcObject* o : lixo) gcClear(o);
    const std::size_t liberados = lixo.size();
    for (GcObject* o : lixo) czRelease(o);
    return liberados;
}

// Os jovens que sobraram passam para a lista dos velhos
inline void gcPromove() {
    CzObjeto* c = gc_registro.head;
    if (!c) return;
    CzObjeto* ultimo = nullptr;
    for (; c; c = c->prox) { c->marcas |= marca::VELHO; ultimo = c; }
    ultimo->prox = gc_registro.velhos;
    if (gc_registro.velhos) gc_registro.velhos->ant = ultimo;
    gc_registro.velhos = gc_registro.head;
    gc_registro.head   = nullptr;
    gc_registro.jovens = 0;
}
}  // namespace detail

// Coleta só os jovens; devolve quantos contêineres liberou
inline std::size_t gcCollectJovens() {
    ++gc_stats.coletas;
    gc_registro.novos = 0;
    const std::size_t liberados =
        detail::gcVarre(gc_registro.head, [](GcObject* f) { return !(f->marcas & marca::VELHO); });
    detail::gcPromove();
    gc_stats.liberados += liberados;
    return liberados;
}

// Coleta completa (as duas gerações): os ciclos inalcançáveis; devolve quantos contêineres liberou
inline std::size_t gcCollect() {
    ++gc_stats.coletas;
    gc_registro.novos = 0;
    detail::gcPromove();   // uma lista só, a dos velhos
    const std::size_t liberados = detail::gcVarre(gc_registro.velhos, [](GcObject*) { return true; });
    gc_registro.velhos_base = gc_registro.vivos;
    gc_stats.liberados += liberados;
    return liberados;
}

// A cada 10 mil contêineres novos, a coleta dos jovens; a completa quando os velhos cresceram
// metade desde a última (ou passaram de 10 mil, na primeira)
inline constexpr std::size_t GC_LIMIAR = 10000;

// Fora de linha: o gcMaybeCollect fica dentro do laço de despacho da CVM, e um corpo grande ali
// muda a alocação de registradores do laço inteiro (o fib, que nem cria contêineres, ficava 35%
// mais lento)
[[gnu::noinline]] inline void gcColetaAgora() {
    gcCollectJovens();
    const std::size_t velhos = gc_registro.vivos - gc_registro.jovens;
    if (velhos > std::max(GC_LIMIAR, gc_registro.velhos_base + gc_registro.velhos_base / 2)) gcCollect();
}

inline void gcMaybeCollect() {
    if (gc_registro.novos >= GC_LIMIAR) [[unlikely]] gcColetaAgora();
}

} // namespace cinza

#endif // CINZA_GC_H
