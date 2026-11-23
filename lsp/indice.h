#ifndef CINZA_LSP_INDICE_H
#define CINZA_LSP_INDICE_H

#include "../module_loader.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cinza::lsp {

// ============================================================================
// O ÍNDICE DO SERVIDOR DE LINGUAGEM (Q6)
//
// Montado da AST já analisada pelo semântico (os tipos de cada expressão, a
// função e o método de cada chamada, o slot de cada variável): cada nome que
// aparece no código vira uma ocorrência, com o texto do "passar o mouse" (a
// declaração: `i32 x`, `fn soma(i32 a) -> i32`) e o lugar onde foi declarado
// (ir para a definição). Para completar nomes, guarda as locais de cada função,
// os nomes do nível de cima, os membros de cada módulo importado e os campos e
// métodos de cada tipo. Posições em bytes, linha e coluna a partir de 1, como no
// front-end; os arquivos pelo caminho canônico.
// ============================================================================

struct Ocorrencia {
    std::string arquivo;
    int         linha = 0, coluna = 0, tamanho = 0;
    std::string nome;
    std::string info;   // a declaração, para o "passar o mouse"
    std::string tipo;   // o tipo do valor (para completar os membros dele); vazio se não é valor
    std::string def_arquivo;
    int         def_linha = 0, def_coluna = 0, def_tamanho = 0;
};

// Um nome para completar: o tipo é o CompletionItemKind do LSP
struct Item {
    std::string nome;
    int         tipo = 6;
    std::string detalhe;
};

// Uma local (ou parâmetro), visível da declaração ao fim da função
struct Local {
    std::string arquivo;
    int         linha = 0, coluna = 0, fim = 0;
    Item        item;
};

struct Indice {
    std::vector<Ocorrencia>                  ocorrencias;
    std::vector<Local>                       locais;
    std::vector<Item>                        globais;   // o nível de cima do principal, os apelidos, os embutidos
    std::map<std::string, std::vector<Item>> modulos;   // apelido de um import → membros públicos
    std::map<std::string, std::vector<Item>> tipos;     // nome do tipo (sem prefixo de módulo) → membros
    std::string                              principal;
};

// O caminho canônico de um arquivo (para comparar o do editor com o do front-end)
std::string canonico(const std::string& caminho);

// `linhas(file_id)` devolve as linhas do arquivo (sem o fim de linha)
Indice constroi(std::vector<Module>& modulos, const std::function<const std::vector<std::string>&(int)>& linhas);

} // namespace cinza::lsp

#endif // CINZA_LSP_INDICE_H
