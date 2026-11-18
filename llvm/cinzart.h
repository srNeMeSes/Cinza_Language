#ifndef CINZA_CINZART_H
#define CINZA_CINZART_H

// ============================================================================
// INTERFACE C DO RUNTIME (libcinzart) — llvm/DESENHO.md seção 7
//
// O que o código nativo gerado pelo cinzac chama. Convenções:
//   - funções extern "C", prefixo cz_; nenhuma exceção do C++ escapa delas
//     (seção 5.3: RuntimeError vira o erro pendente; falta de memória é erro
//     fatal; qualquer outra coisa é erro interno fatal);
//   - argumentos emprestados (+0); objetos devolvidos com posse (+1);
//   - as marcadas CZ_FALHA podem deixar um erro pendente em cz_exec.erro.
//
// Os tipos (CzValor, CzObjeto) são os de value.h / gc_object.h: layout C, conferido
// por static_assert e pelo `cinzac --confere-layout`.
// ============================================================================

#include "../value.h"
#include <cstdint>

#define CZ_FALHA   // documentação: a função pode deixar erro pendente

namespace cinza {

// Estado de execução (seção 5.2). Os campos são contrato de layout com o código
// gerado; no P3 só `erro` existe (a profundidade e a sombra da pilha entram no P8).
struct CzExec {
    CzObjeto* erro = nullptr;   // erro pendente (um ErrorValue), ou nulo
};
static_assert(offsetof(CzExec, erro) == 0, "llvm/DESENHO.md 5.2");

}  // namespace cinza

extern "C" {

extern cinza::CzExec cz_exec;

// Partida do programa: console em UTF-8, sem janela de relatório de erro do Windows,
// argumentos em UTF-8 (para main(list<string> args), P9)
void cz_inicio(int argc, char** argv) noexcept;

// Fim do programa: relata um erro pendente (como a CVM), solta o que o runtime guarda,
// confere vazamentos (CINZA_VAZAMENTOS=1) e devolve o código de saída
int cz_fim() noexcept;

// String literal do programa: imortal (sem contagem), solta pelo cz_fim
cinza::CzObjeto* cz_str_literal(const char* bytes, std::uint64_t n) noexcept;

// Uma função da biblioteca pelo nome do módulo ("" = embutidas: print, range...) e da
// função; nulo se não existir. Resolvida uma vez, na partida.
const void* cz_nativa_resolve(const char* modulo, const char* nome) noexcept;

// Chama uma função da biblioteca: os argumentos são copiados para Value (llvm/DESENHO.md
// 2.3) e o resultado vem com posse em *resultado
CZ_FALHA void cz_nativa(const void* fn, const cinza::CzValor* args, std::uint64_t n,
                        cinza::CzValor* resultado) noexcept;

// Solta a referência que um CzValor segura (fim de um temporário embrulhado)
void cz_solta_valor(cinza::CzValor* v) noexcept;

}  // extern "C"

#endif  // CINZA_CINZART_H
