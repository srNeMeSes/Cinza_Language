// P2: o lado "runtime" do protótipo da pilha. Compilado pelo GCC do projeto (como a
// libcinzart vai ser), com o value.h de verdade: o que uma chamada ao runtime gasta de
// pilha no topo de um programa nativo.
#include "../../../value.h"
#include <cstdio>
#include <iostream>
#include <sstream>

using namespace cinza;

// Pinta n bytes da pilha abaixo de quem chama; depois de outra chamada no mesmo ponto,
// `usado` conta quantos bytes, do fim para o começo, deixaram de ter a pintura
extern "C" __attribute__((noinline)) void pinta(std::size_t n) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(__builtin_alloca(n));
    for (std::size_t i = 0; i < n; ++i) p[i] = 0xCD;
}
extern "C" __attribute__((noinline)) std::size_t usado(std::size_t n) {
    volatile unsigned char* p = static_cast<volatile unsigned char*>(__builtin_alloca(n));
    std::size_t i = 0;
    while (i < n && p[i] == 0xCD) ++i;
    return n - i;
}

// O trabalho típico de um print: uma lista com strings, números e um dict, convertida
// em texto e escrita
extern "C" __attribute__((noinline)) void runtime_print(int k) {
    std::vector<Value> elems;
    for (int i = 0; i < 200; ++i) {
        elems.emplace_back(std::string("texto ") + std::to_string(i + k));
        elems.emplace_back(Value(3.25 * i));
        elems.emplace_back(Value::i32(i));
    }
    Value d = makeDict();
    d.asDict()->entries.poe(Value("chave"), makeList(elems));
    Value l = makeList(std::move(elems));
    std::ostringstream oss;
    oss << l.toString() << " " << d.toString() << "\n";
    std::fputs(oss.str().c_str() + oss.str().size() - 2, stdout);   // escreve algo, sem poluir
}

// Lista aninhada em `prof` níveis, convertida em texto: o toString é recursivo
extern "C" __attribute__((noinline)) void runtime_aninhado(int prof) {
    Value atual = makeList({Value(1)});
    for (int i = 0; i < prof; ++i) atual = makeList({atual});
    volatile std::size_t n = atual.toString().size();
    (void)n;
}

extern "C" std::size_t mede_print() {
    const std::size_t N = 8u << 20;   // 8 MB pintados
    pinta(N);
    runtime_print(1);
    return usado(N);
}

extern "C" std::size_t mede_aninhado(int prof) {
    const std::size_t N = 32u << 20;
    pinta(N);
    runtime_aninhado(prof);
    return usado(N);
}
