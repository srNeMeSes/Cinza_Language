#ifndef CINZA_GC_OBJECT_H
#define CINZA_GC_OBJECT_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
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

// ============================================================================
// ALOCADOR DE OBJETOS PEQUENOS (Q11)
//
// Os objetos do runtime (o cabeçalho e o resto), os vetores de campos e os nós dos dicts são
// pequenos e nascem e morrem aos milhões; pelo heap do sistema (o HeapAlloc no Windows), eram
// 30 a 45% do tempo dos programas com dados. Aqui: classes de 16 em 16 bytes até 256, cada uma
// com a sua lista de blocos livres, tirados de pedaços de 64 KB. Um bloco solto volta para a
// lista da classe dele (a memória fica com o programa, para ser reusada, como no pymalloc).
// Sem trava: o runtime roda numa thread só (a Cinza não tem threads). No build de depuração
// (CINZA_CONFERE_REFS, ASan) vai direto ao heap, para o ASan ver uso depois de liberar.
// ============================================================================
namespace aloca {
inline constexpr std::size_t PASSO = 16, MAXIMO = 256, CLASSES = MAXIMO / PASSO, PEDACO = 64 * 1024;
struct Livre { Livre* prox; };
struct Estado {   // sem construtor: inicialização constante (válido antes e depois do main)
    Livre*      livres[CLASSES + 1] = {};
    char*       pedaco = nullptr;
    std::size_t resto  = 0;
};
inline Estado estado{};

#if defined(CINZA_CONFERE_REFS) || defined(__SANITIZE_ADDRESS__)
inline void* pega(std::size_t n) { return ::operator new(n); }
inline void  solta(void* p, std::size_t) noexcept { ::operator delete(p); }
#else
[[gnu::noinline]] inline void* novoPedaco(std::size_t t) {
    void* p = std::malloc(PEDACO);
    if (!p) throw std::bad_alloc();
    estado.pedaco = static_cast<char*>(p) + t;
    estado.resto  = PEDACO - t;
    return p;
}
inline void* pega(std::size_t n) {
    if (n > MAXIMO || n == 0) return ::operator new(n);
    const std::size_t c = (n + PASSO - 1) / PASSO;
    if (Livre* l = estado.livres[c]) { estado.livres[c] = l->prox; return l; }
    const std::size_t t = c * PASSO;
    if (estado.resto < t) return novoPedaco(t);
    void* p = estado.pedaco;
    estado.pedaco += t;
    estado.resto -= t;
    return p;
}
inline void solta(void* p, std::size_t n) noexcept {
    if (!p) return;
    if (n > MAXIMO || n == 0) { ::operator delete(p); return; }
    const std::size_t c = (n + PASSO - 1) / PASSO;
    Livre* l = static_cast<Livre*>(p);
    l->prox = estado.livres[c];
    estado.livres[c] = l;
}
#endif

// Para os contêineres da biblioteca padrão (campos, nós do dict): sem estado, então o layout do
// std::vector continua o mesmo (o código nativo lê o início do vetor de campos)
template <typename T>
struct Pequeno {
    using value_type = T;
    Pequeno() noexcept = default;
    template <typename U> Pequeno(const Pequeno<U>&) noexcept {}
    T* allocate(std::size_t n) { return static_cast<T*>(pega(n * sizeof(T))); }
    void deallocate(T* p, std::size_t n) noexcept { solta(p, n * sizeof(T)); }
    template <typename U> bool operator==(const Pequeno<U>&) const noexcept { return true; }
};
} // namespace aloca

// O tipo do objeto (o enum continua com o nome da coleta de ciclos)
enum class GcKind : std::uint8_t { List, Dict, Pair, Instance, Struct, Error, String };

// Bits de CzObjeto::marcas
namespace marca {
inline constexpr std::uint8_t IMORTAL = 1;   // não conta referências (literais, no futuro)
inline constexpr std::uint8_t LIGADO  = 2;   // está no registro da coleta de ciclos
inline constexpr std::uint8_t VELHO   = 4;   // Q11: sobreviveu a uma coleta (na lista dos velhos)
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

    // Q11: todo objeto do runtime vem do alocador de objetos pequenos (o delete de czApaga é
    // pelo tipo exato, então o tamanho que chega aqui é o do objeto)
    static void* operator new(std::size_t n) { return aloca::pega(n); }
    static void  operator delete(void* p, std::size_t n) noexcept { aloca::solta(p, n); }
};

static_assert(sizeof(CzObjeto) == 32 && offsetof(CzObjeto, refs) == 0,
              "o cabeçalho é parte da interface com o código nativo (llvm/DESENHO.md 2.3)");

// Estado global (sem construtor nem destrutor: inicialização constante, válido
// mesmo para objetos destruídos no fim do programa)
struct GcRegistry {
    // Q11: duas gerações (gc.h): os contêineres nascem jovens; quem sobrevive a uma coleta passa
    // para os velhos, que só a coleta completa percorre
    CzObjeto*   head       = nullptr;   // contêineres rastreados jovens
    CzObjeto*   velhos     = nullptr;   // contêineres rastreados velhos
    std::size_t vivos      = 0;         // contêineres existentes agora (das duas gerações)
    std::size_t jovens     = 0;         // idem, só os jovens
    std::size_t novos      = 0;         // contêineres criados desde a última coleta
    std::size_t velhos_base = 0;        // velhos logo depois da última coleta completa
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
    // usado só durante a coleta; de tamanho fixo em todo sistema (o long tem 4 bytes no Windows e 8
    // no Linux, e o layout dos objetos precisa ser o mesmo dos dois lados, Q5). Q11: 32 bits (nunca
    // passa do refs, que tem 32) e a marca ao lado — o GcObject com 40 bytes, não 48: a coleta
    // percorre os objetos em passos menores, e um objeto de dois campos cabe em 80
    std::int32_t gc_refs = 0;
    bool gc_marca = false;   // idem: alcançável a partir de fora

    // Q11: `rastreia` falso para os objetos de tipo acíclico (o descritor diz): eles nunca estão
    // num ciclo, e a coleta não precisa percorrê-los; o clone segue o original
    explicit GcObject(GcKind k, bool rastreia = true) noexcept : CzObjeto(k) { if (rastreia) liga(); }
    GcObject(const GcObject& o) noexcept : CzObjeto(o) { if (o.marcas & marca::LIGADO) liga(); }
    GcObject& operator=(const GcObject&) noexcept { return *this; }
    ~GcObject() { desliga(); }

    void desliga() noexcept {
        if (!(marcas & marca::LIGADO)) return;
        const bool velho = marcas & marca::VELHO;
        if (ant) ant->prox = prox;
        else     (velho ? gc_registro.velhos : gc_registro.head) = prox;
        if (prox) prox->ant = ant;
        ant = prox = nullptr;
        marcas &= static_cast<std::uint8_t>(~(marca::LIGADO | marca::VELHO));
        --gc_registro.vivos;
        if (!velho) --gc_registro.jovens;
    }

private:
    void liga() noexcept {
        prox = gc_registro.head;
        if (prox) prox->ant = this;
        gc_registro.head = this;
        marcas |= marca::LIGADO;
        ++gc_registro.vivos;
        ++gc_registro.jovens;
        ++gc_registro.novos;
    }
};

// Apaga um objeto pelo tipo exato (definida em value.h, onde os tipos concretos estão completos)
inline void czApaga(CzObjeto* o) noexcept;

// Destrói um objeto que chegou a zero: sai do registro e entra na fila de destruição (value.h,
// DESTRUIÇÃO ITERATIVA). Q11: fora de linha — o release está em todo destrutor de Value, e com a
// destruição inlinada o destrutor fica grande demais para ser inlinado nos chamadores (o
// stable_sort de uma lista destrói milhões de valores movidos: cada um virava uma chamada)
[[gnu::noinline, gnu::cold]] inline void czDestroi(CzObjeto* o) noexcept {
    if (o->marcas & marca::LIGADO) static_cast<GcObject*>(o)->desliga();
    o->prox = gc_registro.fila;
    gc_registro.fila = o;
    if (gc_registro.esvaziando) return;
    gc_registro.esvaziando = true;
    while (CzObjeto* x = gc_registro.fila) {
        gc_registro.fila = x->prox;
        x->prox = nullptr;
        czApaga(x);
    }
    gc_registro.esvaziando = false;
}


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
