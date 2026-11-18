// P2: o lado "código gerado" do protótipo da pilha, compilado pelo clang 20.1.8 (o LLVM
// que o cinzac vai usar). A função tem o quadro extremo do quadros.py (caso D: dez structs
// de 8 KB e vinte chamadas que os recebem por valor) e se chama 2000 vezes — o máximo da
// spec 5.8 —, chamando o runtime (runtime_sim.cpp, GCC) no fundo.
//
// A rede de segurança (llvm/DESENHO.md 6.3, item 3) é o teste na entrada: se o endereço
// do quadro passou do limite (o fim da pilha mais a margem), para com erro interno claro,
// nunca com o 0xC00000FD silencioso.
//
//   recursao medir                 pilha usada por um print e por nível de lista aninhada
//   recursao fundo N MARGEM        N níveis; MARGEM em bytes (0 = sem rede de segurança)
#define _WIN32_WINNT 0x0602   // GetCurrentThreadStackLimits (Windows 8+)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { long long c[1000]; } S1000;

void runtime_print(int k);
size_t mede_print(void);
size_t mede_aninhado(int prof);

static char* cz_limite = 0;   // fim da pilha + margem; 0 = sem rede

#define ENTRA()                                                                   \
    do {                                                                          \
        if ((char*)__builtin_frame_address(0) < cz_limite) {                      \
            fputs("Erro interno: pilha do sistema esgotada\n", stderr);           \
            fflush(stdout);                                                       \
            exit(3);                                                              \
        }                                                                         \
    } while (0)

__attribute__((noinline)) long long ext_s1000(S1000 s) { return s.c[0] + s.c[999]; }

__attribute__((noinline)) long long d_extremo(long long n, int nivel) {
    ENTRA();
    S1000 v0 = {{n}}, v1 = {{n + 1}}, v2 = {{n + 2}}, v3 = {{n + 3}}, v4 = {{n + 4}},
          v5 = {{n + 5}}, v6 = {{n + 6}}, v7 = {{n + 7}}, v8 = {{n + 8}}, v9 = {{n + 9}};
    long long r = ext_s1000(v0) + ext_s1000(v1) + ext_s1000(v2) + ext_s1000(v3) + ext_s1000(v4) +
                  ext_s1000(v5) + ext_s1000(v6) + ext_s1000(v7) + ext_s1000(v8) + ext_s1000(v9) +
                  ext_s1000(v0) + ext_s1000(v1) + ext_s1000(v2) + ext_s1000(v3) + ext_s1000(v4) +
                  ext_s1000(v5) + ext_s1000(v6) + ext_s1000(v7) + ext_s1000(v8) + ext_s1000(v9);
    if (nivel > 1) r += d_extremo(n + 1, nivel - 1);
    else runtime_print(nivel);   // o runtime em C++ no topo da pilha
    return r;
}

int main(int argc, char** argv) {
    // sem a janela de relatório de erro do Windows: um estouro termina o processo na
    // hora, em vez de deixá-lo preso segurando o executável
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    ULONG_PTR baixo = 0, alto = 0;
    GetCurrentThreadStackLimits(&baixo, &alto);
    printf("reserva de pilha: %llu KB\n", (unsigned long long)((alto - baixo) / 1024));

    if (argc >= 2 && strcmp(argv[1], "medir") == 0) {
        printf("print: %zu bytes\n", mede_print());
        const size_t a100 = mede_aninhado(100), a1000 = mede_aninhado(1000);
        printf("lista aninhada: 100 níveis %zu bytes, 1000 níveis %zu bytes, %zu bytes por nível\n",
               a100, a1000, (a1000 - a100) / 900);
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "fundo") == 0) {
        const int n = atoi(argv[2]);
        const long long margem = atoll(argv[3]);
        if (margem > 0) cz_limite = (char*)baixo + margem;
        long long r = d_extremo(1, n);
        printf("\nok: %d níveis (%lld)\n", n, r & 0xff);
        return 0;
    }
    fputs("uso: recursao medir | recursao fundo N MARGEM\n", stderr);
    return 2;
}
