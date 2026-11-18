#ifndef CINZA_GC_OBJECT_H
#define CINZA_GC_OBJECT_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace cinza {

// ============================================================================
// OBJETOS DO RUNTIME: CONTAGEM EMBUTIDA (P1, llvm/DESENHO.md seção 2.1)
//
// Todo objeto do runtime (list, dict, pair, objeto de classe, struct, erro)
// começa com o cabeçalho CzObjeto — herança só da implementação em C++, a
// linguagem continua sem herança. O cabeçalho guarda a contagem de referências
// (antes num std::shared_ptr, fora do objeto e atômica): o código nativo do
// backend LLVM vai incrementá-la e decrementá-la direto.
//
//   - contagem não atômica: a Cinza não tem threads (decisão explícita: com
//     threads, revisar);
//   - todo objeto nasce com refs = 1, a posse de quem o criou (Ref::adota);
//   - chegando a zero, o objeto entra na FILA DE DESTRUIÇÃO (intrusiva, pelo
//     campo `prox`): soltar uma cadeia longa não destrói em recursão (antes o
//     shared_ptr estourava a pilha do sistema com 400 mil nós — P1a);
//   - os contêineres (os que podem formar ciclo) ficam também num registro
//     duplamente ligado (`ant`/`prox`), que a coleta de ciclos (gc.h) percorre.
// ============================================================================

// O tipo do objeto (o enum continua com o nome da coleta de ciclos)
enum class GcKind : std::uint8_t { List, Dict, Pair, Instance, Struct, Error, String };

// Bits de CzObjeto::marcas
namespace marca {
inline constexpr std::uint8_t IMORTAL = 1;   // não conta referências (literais, no futuro)
inline constexpr std::uint8_t LIGADO  = 2;   // está no registro da coleta de ciclos
}

struct CzObjeto {
    std::uint32_t refs       = 1;
    GcKind        gc_kind;
    std::uint8_t  marcas     = 0;
    std::uint16_t _reservado = 0;
    const void*   desc       = nullptr;   // descritor: CzClasseDesc, CzStructDesc (value.h)
    CzObjeto*     ant        = nullptr;   // registro da coleta (só contêineres)
    CzObjeto*     prox       = nullptr;   // registro da coleta; morto: fila de destruição

    explicit CzObjeto(GcKind k) noexcept;
    // Cópia (um struct é clonado a cada cópia): o clone é um objeto novo, com
    // cabeçalho próprio
    CzObjeto(const CzObjeto& o) noexcept : CzObjeto(o.gc_kind) { desc = o.desc; }
    CzObjeto& operator=(const CzObjeto&) noexcept { return *this; }   // o cabeçalho não muda
    ~CzObjeto();
};

static_assert(sizeof(CzObjeto) == 32 && offsetof(CzObjeto, refs) == 0,
              "o cabeçalho é parte da interface com o código nativo (llvm/DESENHO.md 2.3)");

// Estado global (sem construtor nem destrutor: inicialização constante, válido
// mesmo para objetos destruídos no fim do programa)
struct GcRegistry {
    CzObjeto*   head       = nullptr;   // contêineres rastreados
    std::size_t vivos      = 0;         // contêineres existentes agora
    std::size_t novos      = 0;         // contêineres criados desde a última coleta
    std::size_t objetos    = 0;         // todos os objetos existentes (o modo sem vazamento)
    std::size_t por_tipo[7] = {};       // idem, por GcKind
    CzObjeto*   fila       = nullptr;   // fila de destruição
    bool        esvaziando = false;     // um czDestroi mais acima já esvazia a fila
};
inline GcRegistry gc_registro{};

inline CzObjeto::CzObjeto(GcKind k) noexcept : gc_kind(k) {
    ++gc_registro.objetos;
    ++gc_registro.por_tipo[static_cast<std::size_t>(k)];
}
inline CzObjeto::~CzObjeto() {
    --gc_registro.objetos;
    --gc_registro.por_tipo[static_cast<std::size_t>(gc_kind)];
}

// Nome do tipo nas mensagens do modo sem vazamento
inline const char* nomeGcKind(GcKind k) noexcept {
    static const char* const nomes[] = {"lista", "dict", "par", "objeto", "struct", "erro", "string"};
    return nomes[static_cast<std::size_t>(k)];
}

// Contêiner rastreado pela coleta de ciclos: o cabeçalho e o que a coleta usa
// durante a varredura (fora dos 32 bytes do cabeçalho comum)
struct GcObject : CzObjeto {
    long gc_refs  = 0;       // usado só durante a coleta
    bool gc_marca = false;   // idem: alcançável a partir de fora

    explicit GcObject(GcKind k) noexcept : CzObjeto(k) { liga(); }
    GcObject(const GcObject& o) noexcept : CzObjeto(o) { liga(); }
    GcObject& operator=(const GcObject&) noexcept { return *this; }
    ~GcObject() { desliga(); }

    void desliga() noexcept {
        if (!(marcas & marca::LIGADO)) return;
        if (ant) ant->prox = prox;
        else     gc_registro.head = prox;
        if (prox) prox->ant = ant;
        ant = prox = nullptr;
        marcas &= static_cast<std::uint8_t>(~marca::LIGADO);
        --gc_registro.vivos;
    }

private:
    void liga() noexcept {
        prox = gc_registro.head;
        if (prox) prox->ant = this;
        gc_registro.head = this;
        marcas |= marca::LIGADO;
        ++gc_registro.vivos;
        ++gc_registro.novos;
    }
};

// Destrói um objeto que chegou a zero (definida em value.h, onde os tipos
// concretos estão completos)
inline void czDestroi(CzObjeto* o) noexcept;

// Conferência da contagem: só no build de depuração (make debug)
[[noreturn]] inline void czFalhaContagem(const char* o_que) noexcept {
    std::fprintf(stderr, "Erro interno do runtime: %s\n", o_que);
    std::abort();
}

inline void czRetain(CzObjeto* o) noexcept {
    if (o->marcas & marca::IMORTAL) return;
#ifdef CINZA_CONFERE_REFS
    if (o->refs == 0) czFalhaContagem("retain de objeto já destruído");
    if (o->refs == UINT32_MAX) czFalhaContagem("contagem de referências estourou");
#endif
    ++o->refs;
}

inline void czRelease(CzObjeto* o) noexcept {
    if (o->marcas & marca::IMORTAL) return;
#ifdef CINZA_CONFERE_REFS
    if (o->refs == 0) czFalhaContagem("release de objeto já destruído (liberação dupla)");
#endif
    if (--o->refs == 0) czDestroi(o);
}

// ============================================================================
// Ref<T>: o dono de uma referência (no lugar do std::shared_ptr). Copiar faz
// retain; destruir faz release; mover não mexe na contagem.
// ============================================================================
template <typename T>
class Ref {
    T* p_ = nullptr;

public:
    Ref() noexcept = default;
    // Assume a referência que o ponteiro já tem (um objeto recém-criado: refs = 1)
    static Ref adota(T* p) noexcept { Ref r; r.p_ = p; return r; }
    // Ganha uma referência nova a um objeto que outro segura
    static Ref empresta(T* p) noexcept { if (p) czRetain(p); return adota(p); }

    Ref(const Ref& o) noexcept : p_(o.p_) { if (p_) czRetain(p_); }
    Ref(Ref&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~Ref() { if (p_) czRelease(p_); }

    // retain do novo antes do release do antigo: vale `a = a`
    Ref& operator=(const Ref& o) noexcept {
        if (o.p_) czRetain(o.p_);
        T* velho = p_;
        p_ = o.p_;
        if (velho) czRelease(velho);
        return *this;
    }
    Ref& operator=(Ref&& o) noexcept {
        if (this != &o) {
            T* velho = p_;
            p_ = o.p_;
            o.p_ = nullptr;
            if (velho) czRelease(velho);
        }
        return *this;
    }

    // Entrega a referência (sem release): quem recebe passa a ser o dono
    T* solta() noexcept { T* p = p_; p_ = nullptr; return p; }
    T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    T& operator*() const noexcept { return *p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }
    bool operator==(const Ref& o) const noexcept { return p_ == o.p_; }
};

// Cria um objeto: nasce com refs = 1, adotado pelo Ref devolvido
template <typename T, typename... A>
Ref<T> novo(A&&... args) {
    return Ref<T>::adota(new T(std::forward<A>(args)...));
}

} // namespace cinza

#endif // CINZA_GC_OBJECT_H
