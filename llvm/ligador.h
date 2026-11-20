#ifndef CINZA_LLVM_LIGADOR_H
#define CINZA_LLVM_LIGADOR_H

#include <string>
#include <vector>

namespace cinza::llvm {

// O ligador dentro do cinzac (Q5): o lld do LLVM como biblioteca — o driver MinGW no Windows,
// o ELF no Linux —, para o cinzac não depender de um g++ nem do MSYS2. `args` é a linha de
// comando do ld.lld (args[0] = "ld.lld"); `mensagens` recebe o que o lld escreveu. Devolve
// true se ligou.
bool liga(const std::vector<std::string>& args, std::string& mensagens);

} // namespace cinza::llvm

#endif // CINZA_LLVM_LIGADOR_H
