#ifndef CINZA_GERA_CIR_H
#define CINZA_GERA_CIR_H

// AST analisada → CIR (llvm/DESENHO.md, seção 4)

#include "cir.h"
#include "../ast.h"
#include "../module_loader.h"
#include <string>
#include <vector>

namespace cinza::cir {

// Recurso da linguagem que o cinzac ainda não compila (as etapas estão na seção P do
// REVISAO_V2.md): o cinzac relata e sai com código 75, que o run_tests.py --cinzac
// conta à parte (nem passou nem falhou)
struct NaoSuportado {
    std::string o_que;
    Token       tok;
};

// Os módulos em ordem topológica, o principal por último (o ModuleLoader), já
// analisados; `layout`: os slots dos const globais (os dos módulos nativos, como
// Math.pi, já têm o valor); `arquivos`: os nomes dos arquivos por file_id
Modulo gera(const std::vector<Module>& modulos, const GlobalLayout& layout,
            const std::vector<std::string>& arquivos);

}  // namespace cinza::cir

#endif  // CINZA_GERA_CIR_H
