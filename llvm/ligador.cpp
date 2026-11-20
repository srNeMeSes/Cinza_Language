// O lld como biblioteca (llvm/ligador.h). É a única parte do cinzac que usa a API em C++ do LLVM:
// por isso o cinzac é compilado com o mesmo C++ das bibliotecas estáticas do LLVM (no Windows, o
// clang do CLANG64 com a libc++).
#include "ligador.h"
#include <lld/Common/Driver.h>
#include <llvm/Support/raw_ostream.h>

#ifdef _WIN32
LLD_HAS_DRIVER(mingw)
#else
LLD_HAS_DRIVER(elf)
#endif

namespace cinza::llvm {

bool liga(const std::vector<std::string>& args, std::string& mensagens) {
    std::vector<const char*> v;
    for (const std::string& a : args) v.push_back(a.c_str());
    std::string saida, erros;
    ::llvm::raw_string_ostream so(saida), se(erros);
#ifdef _WIN32
    const lld::Result r = lld::lldMain(v, so, se, {{lld::MinGW, &lld::mingw::link}});
#else
    const lld::Result r = lld::lldMain(v, so, se, {{lld::Gnu, &lld::elf::link}});
#endif
    so.flush();
    se.flush();
    mensagens = saida + erros;
    return r.retCode == 0;
}

} // namespace cinza::llvm
