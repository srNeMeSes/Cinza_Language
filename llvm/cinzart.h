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

// Uma posição do programa (constante gerada pelo cinzac): o arquivo é o file_id dos
// diagnósticos (cz_arquivos registra os nomes na mesma ordem do front-end)
struct CzPos {
    std::int32_t arquivo, linha, coluna;
};

// Uma função da Cinza, para o stack trace e o StackOverflowError: o nome do trace
// ("f", "Classe.metodo") e a posição da declaração
struct CzFuncao {
    const char*  nome;
    std::int32_t arquivo, linha, coluna;
};

// Um quadro da sombra da pilha (llvm/DESENHO.md 6): a função e a posição da chamada
// em andamento (gravada antes de cada chamada de função da Cinza)
struct CzQuadro {
    const CzFuncao* f;
    const CzPos*    pos;
};

constexpr std::int32_t CZ_PROFUNDIDADE = 2000;   // spec 5.8, a mesma da CVM

// Estado de execução (seção 5.2). Os campos são contrato de layout com o código
// gerado (conferido pelo cinzac --confere-layout): o erro pendente, a profundidade
// (quadros em uso) e a sombra da pilha. A função de inicialização dos const fica fora
// dela, como na CVM.
struct CzExec {
    CzObjeto*    erro = nullptr;   // erro pendente (um ErrorValue), ou nulo
    std::int32_t prof = 0;
    std::int32_t _reservado = 0;
    CzQuadro     quadros[CZ_PROFUNDIDADE];
};
static_assert(offsetof(CzExec, erro) == 0 && offsetof(CzExec, prof) == 8 &&
              offsetof(CzExec, quadros) == 16 && sizeof(CzQuadro) == 16, "llvm/DESENHO.md 5.2");
static_assert(sizeof(CzPos) == 12 && sizeof(CzFuncao) == 24 && offsetof(CzFuncao, arquivo) == 8,
              "llvm/DESENHO.md 5.2");

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
// 2.3) e o resultado vem com posse em *resultado; um erro sem posição fica com `p`
CZ_FALHA void cz_nativa(const void* fn, const cinza::CzValor* args, std::uint64_t n,
                        cinza::CzValor* resultado, const cinza::CzPos* p) noexcept;

// Solta a referência que um CzValor segura (fim de um temporário embrulhado)
void cz_solta_valor(cinza::CzValor* v) noexcept;

// Os nomes dos arquivos do programa, na ordem dos file_id (0 = o principal)
void cz_arquivos(const char* const* nomes, std::uint64_t n) noexcept;

// ── Caminho de erro (o caminho rápido fica no código gerado) ──────────────
// Cada uma refaz a operação pela mesma regra da CVM (operacoes.cpp), que lança o erro
// com a mesma mensagem, e o deixa pendente com a posição `p` e o stack trace da sombra
// da pilha. Os valores chegam como os bits do campo `u` do CzValor; `kind`, o
// Value::Kind.

// a op b (op: o TokenType) que estourou ou dividiu por zero
CZ_FALHA void cz_falha_binaria(std::uint32_t op, std::uint8_t kind, std::uint64_t a, std::uint64_t b,
                               const cinza::CzPos* p) noexcept;
// -a no menor valor da largura
CZ_FALHA void cz_falha_neg(std::uint8_t kind, std::uint64_t a, const cinza::CzPos* p) noexcept;
// cast<para>(a) fora da faixa
CZ_FALHA void cz_falha_conv(std::uint8_t de, std::uint64_t a, std::uint8_t para, const cinza::CzPos* p) noexcept;
// range com passo 0
CZ_FALHA void cz_falha_passo(const cinza::CzPos* p) noexcept;
// a chamada de `f` passaria da profundidade máxima (posição: a declaração de f)
CZ_FALHA void cz_estouro(const cinza::CzFuncao* f) noexcept;

// Converte no lugar o resultado de uma nativa genérica para o Value::Kind pedido
CZ_FALHA void cz_conv_valor(cinza::CzValor* v, std::uint8_t kind, const cinza::CzPos* p) noexcept;

// P4: todo erro é fatal, como um erro não capturado na CVM — relata o erro pendente
// (ou sai com o código do exit) e encerra o processo
[[noreturn]] void cz_morre() noexcept;

}  // extern "C"

#endif  // CINZA_CINZART_H
