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

// Coleta os ciclos inalcançáveis; devolve quantos contêineres liberou
inline std::size_t gcCollect() {
    ++gc_stats.coletas;
    gc_registro.novos = 0;

    auto contem = [](CzObjeto* o) { return static_cast<GcObject*>(o); };
    // 1. referências de cada contêiner
    for (CzObjeto* c = gc_registro.head; c; c = c->prox) {
        GcObject* o = contem(c);
        o->gc_refs  = static_cast<std::int64_t>(o->refs);
        o->gc_marca = false;
    }
    // 2. desconta as referências internas
    for (CzObjeto* c = gc_registro.head; c; c = c->prox)
        gcForEachChild(contem(c), [](GcObject* f) { --f->gc_refs; });

    // 3. marca o que as raízes alcançam
    std::vector<GcObject*> pilha;
    for (CzObjeto* c = gc_registro.head; c; c = c->prox)
        if (GcObject* o = contem(c); o->gc_refs > 0) { o->gc_marca = true; pilha.push_back(o); }
    while (!pilha.empty()) {
        GcObject* o = pilha.back();
        pilha.pop_back();
        gcForEachChild(o, [&](GcObject* c) {
            if (!c->gc_marca) { c->gc_marca = true; pilha.push_back(c); }
        });
    }

    // 4. lixo: segura cada um (nada é destruído no meio) e esvazia; os
    //    contadores chegam a zero ao soltar, e a destruição vai pela fila
    std::vector<GcObject*> lixo;
    for (CzObjeto* c = gc_registro.head; c; c = c->prox)
        if (GcObject* o = contem(c); !o->gc_marca) { czRetain(o); lixo.push_back(o); }
    for (GcObject* o : lixo) gcClear(o);
    const std::size_t liberados = lixo.size();
    for (GcObject* o : lixo) czRelease(o);

    gc_stats.liberados += liberados;
    return liberados;
}

// Coleta quando já nasceram contêineres suficientes desde a última: pelo menos
// 10 mil, ou tantos quantos sobreviveram (custo amortizado constante)
inline void gcMaybeCollect() {
    static std::size_t limite = 10000;
    if (gc_registro.novos < limite) return;
    gcCollect();
    limite = std::max<std::size_t>(10000, gc_registro.vivos);
}

} // namespace cinza

#endif // CINZA_GC_H
