#ifndef CINZA_CVM_COMPILER_H
#define CINZA_CVM_COMPILER_H

#include "bytecode.h"
#include "../ast.h"
#include <stdexcept>
#include <vector>

namespace cinza::cvm {

// Recurso da linguagem que a CVM ainda não compila (as etapas estão na seção
// F do REVISAO_V2.md). Vira um diagnóstico na posição do recurso.
struct Unsupported : std::runtime_error {
    SourceLocation loc;
    Unsupported(const std::string& msg, const Token& tok)
        : std::runtime_error(msg), loc(tok.loc()) {}
};

// Compila os módulos (já analisados pelo semântico, em ordem topológica, o
// principal por último) para uma imagem da CVM.
Image compile(const std::vector<const ::cinza::Program*>& programs, const GlobalLayout& layout);

} // namespace cinza::cvm

#endif // CINZA_CVM_COMPILER_H
