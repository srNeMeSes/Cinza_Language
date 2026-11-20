# Licenças de terceiros

A Cinza é distribuída sob a licença MIT (`LICENSE`). Os executáveis deste pacote e a pasta
`bin/runtime/` (ligada aos programas gerados pelo `cinzac`) contêm código dos projetos abaixo,
todos de licença permissiva. Os textos das licenças estão na pasta `licencas/`.

Os programas que o `cinzac` gera levam o runtime da Cinza (MIT) e as bibliotecas da pasta
`bin/runtime/`: a licença deles é a que o autor do programa escolher, desde que mantenha os avisos
destas bibliotecas (as licenças abaixo pedem isso para distribuição em forma binária, menos as de
domínio público).

## No `cinzac` (as duas plataformas)

| Componente | Versão | Licença | Fonte |
|------------|--------|---------|-------|
| LLVM e lld | 20.1.8 | Apache-2.0 WITH LLVM-exception | https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8 |
| zlib | 1.3.1 | Zlib | https://zlib.net |
| zstd | 1.5.7 | BSD-3-Clause | https://github.com/facebook/zstd |

No Windows o `cinzac` é compilado com a libc++ e o mingw-w64 (abaixo); no Linux, com a libstdc++ e
a libgcc do GCC do Alpine, ligadas de forma estática — o que a GCC Runtime Library Exception permite
distribuir com qualquer licença —, e com a musl.

## No Windows (`bin/runtime/` e os executáveis)

| Componente | Versão | Licença | Fonte |
|------------|--------|---------|-------|
| libc++, libc++abi, libunwind, compiler-rt | 20.1.8 | Apache-2.0 WITH LLVM-exception | https://github.com/llvm/llvm-project |
| mingw-w64 (CRT, cabeçalhos) | 13.0.0 | ZPL-2.1 e domínio público | https://www.mingw-w64.org |
| winpthreads | 13.0.0 | MIT e BSD-3-Clause | https://www.mingw-w64.org |

O `cinza.exe` (a CVM) é compilado pelo GCC (MinGW-w64) com a libstdc++ e a libgcc estáticas,
distribuídas com qualquer licença pela GCC Runtime Library Exception.

## No Linux (`bin/runtime/` e os executáveis)

| Componente | Versão | Licença | Fonte |
|------------|--------|---------|-------|
| musl | a do Alpine 3.22 (1.2.5) | MIT | https://musl.libc.org |
| libc++, libc++abi, libunwind, compiler-rt | 20.1.8 | Apache-2.0 WITH LLVM-exception | https://github.com/llvm/llvm-project |

O `cinza` (a CVM) é compilado pelo clang do Alpine com a libstdc++ e a libgcc estáticas (GCC Runtime
Library Exception) e a musl.
