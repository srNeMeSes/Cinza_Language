#ifndef CINZA_CVM_VM_H
#define CINZA_CVM_VM_H

#include "bytecode.h"
#include "../runtime_error.h"
#include <string>
#include <vector>

namespace cinza::cvm {

// ============================================================================
// MÁQUINA VIRTUAL (desenho: cvm/DESENHO.md, seção 3)
//
// Registradores numa área contínua; cada chamada ocupa uma janela que começa
// nos argumentos passados pelo chamador. Um CALL empilha um frame no vetor
// `frames` e continua o mesmo laço de despacho (sem recursão em C++).
// ============================================================================

class VM {
public:
    explicit VM(const Image& img) : img(img) {}

    // Avalia os const de cada módulo e chama a main. Erro não capturado sai
    // como RuntimeError, com posição e stack trace (como no interpretador).
    void run(const std::vector<std::string>& args);

    static constexpr std::size_t max_call_depth = 2000;   // spec 5.8

private:
    struct Frame {
        const Proto* proto;
        std::size_t  pc;          // próxima instrução
        std::size_t  base;        // início da janela em `regs`
        int          call_line;   // linha da chamada em quem chamou (0 na main)
    };

    const Image&        img;
    std::vector<Value>  regs;
    std::vector<Value>  globals;
    std::vector<Frame>  frames;

    // Executa o protótipo `idx` com a janela em `base` até ele retornar
    Value execute(std::size_t idx, std::size_t base);

    void ensure(std::size_t n) { if (regs.size() < n) regs.resize(n + n / 2 + 64); }
    std::vector<std::string> trace(int error_line) const;
    [[noreturn]] void fail(const RuntimeError& err, const Frame& f) const;
};

} // namespace cinza::cvm

#endif // CINZA_CVM_VM_H
