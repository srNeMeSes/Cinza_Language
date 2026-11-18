#ifndef CINZA_EMISSOR_H
#define CINZA_EMISSOR_H

// CIR → LLVM → objeto (llvm/DESENHO.md, seção 8.4). Único lugar do cinzac que
// inclui a llvm-c (no .cpp): a interface aqui não expõe nenhum tipo do LLVM.

#include "cir.h"
#include <memory>
#include <string>

namespace cinza::llvm {

class Emissor {
public:
    explicit Emissor(bool otimiza);
    ~Emissor();
    Emissor(const Emissor&) = delete;
    Emissor& operator=(const Emissor&) = delete;

    // Gera o IR do módulo e o verifica. Devolve false com a mensagem em `erro`
    // (IR inválido é erro interno do cinzac).
    bool gera(const cir::Modulo& m, std::string& erro);
    // Roda os passes (default<O0> ou default<O2>); o emiteObjeto roda se ainda não rodou
    bool otimiza(std::string& erro);
    // Grava o objeto
    bool emiteObjeto(const std::string& caminho, std::string& erro);
    // O IR em texto (cinzac --emit-llvm)
    std::string textoIR() const;

    // Confere que o LLVM vê as estruturas da interface (CzValor, CzObjeto, CzExec) com o
    // mesmo tamanho e os mesmos deslocamentos que o GCC (llvm/DESENHO.md 8.4)
    static bool confereLayout(std::string& relato);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace cinza::llvm

#endif  // CINZA_EMISSOR_H
