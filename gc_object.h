#ifndef CINZA_GC_OBJECT_H
#define CINZA_GC_OBJECT_H

#include <cstddef>
#include <cstdint>
#include <memory>

namespace cinza {

// ============================================================================
// OBJETO RASTREADO PELA COLETA DE CICLOS
//
// Todo contêiner de runtime (list, dict, pair, objeto de classe, struct) herda
// GcObject — herança só da implementação em C++, a linguagem continua sem
// herança. O GcObject:
//   - entra numa lista duplamente ligada global ao nascer e sai ao morrer, para
//     o coletor (gc.h) conseguir percorrer todos os contêineres vivos;
//   - herda enable_shared_from_this, para o coletor ler quantas referências
//     fortes (shared_ptr) cada contêiner tem.
// A liberação normal continua sendo a contagem de referências do shared_ptr;
// a coleta só existe para os ciclos, que a contagem sozinha não libera.
// ============================================================================

enum class GcKind : std::uint8_t { List, Dict, Pair, Instance, Struct };

struct GcObject;

// Registro global: sem construtor nem destrutor (inicialização constante), então
// continua válido mesmo para contêineres destruídos no fim do programa
struct GcRegistry {
    GcObject*   head   = nullptr;
    std::size_t vivos  = 0;   // contêineres existentes agora
    std::size_t novos  = 0;   // criados desde a última coleta
};
inline GcRegistry gc_registro{};

struct GcObject : std::enable_shared_from_this<GcObject> {
    GcKind    gc_kind;
    GcObject* gc_prev = nullptr;
    GcObject* gc_next = nullptr;
    long      gc_refs = 0;       // usado só durante a coleta
    bool      gc_marca = false;  // idem: alcançável a partir de fora

    explicit GcObject(GcKind k) : gc_kind(k) { gcLink(); }
    // Cópia (struct é clonado a cada cópia): o clone é um contêiner novo
    GcObject(const GcObject& o) : std::enable_shared_from_this<GcObject>(o), gc_kind(o.gc_kind) {
        gcLink();
    }
    GcObject& operator=(const GcObject&) { return *this; }   // a posição no registro não muda
    ~GcObject() { gcUnlink(); }

private:
    void gcLink() {
        gc_next = gc_registro.head;
        if (gc_next) gc_next->gc_prev = this;
        gc_registro.head = this;
        ++gc_registro.vivos;
        ++gc_registro.novos;
    }
    void gcUnlink() {
        if (gc_prev) gc_prev->gc_next = gc_next;
        else         gc_registro.head = gc_next;
        if (gc_next) gc_next->gc_prev = gc_prev;
        --gc_registro.vivos;
    }
};

} // namespace cinza

#endif // CINZA_GC_OBJECT_H
