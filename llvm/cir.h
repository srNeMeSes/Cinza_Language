#ifndef CINZA_CIR_H
#define CINZA_CIR_H

// ============================================================================
// CIR — a IR intermediária da Cinza (llvm/DESENHO.md, seção 4)
//
// Entre a AST analisada e o LLVM. A tradução AST → CIR (gera_cir.cpp) decide a
// ordem de avaliação, os temporários e a posse; CIR → LLVM (emissor.cpp) é
// mecânica. Não é SSA: cada temporário é um slot.
//
// P3: o mínimo do "olá mundo" — constantes, embrulhar em CzValor, chamar uma
// função da biblioteca, soltar um valor embrulhado e retornar. As etapas
// seguintes (P4...) acrescentam operações a esta mesma estrutura.
// ============================================================================

#include "../lexer.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cinza::cir {

enum class Tipo : std::uint8_t {
    Void, Bool, I32, I64, F32, F64,
    Str,     // ponteiro para CzString
    Valor,   // CzValor embrulhado (16 bytes)
};

// Um operando: constante ou temporário
struct Operando {
    enum class K : std::uint8_t { Temp, I32, I64, F32, F64, Bool, Literal } k = K::Temp;
    std::uint32_t indice = 0;   // Temp: o temporário; Literal: a string do módulo
    std::int64_t  i      = 0;
    double        f      = 0;

    static Operando temp(std::uint32_t t)  { Operando o; o.k = K::Temp; o.indice = t; return o; }
    static Operando literal(std::uint32_t l) { Operando o; o.k = K::Literal; o.indice = l; return o; }
};

enum class Op : std::uint8_t {
    Embrulha,      // dst: valor = embrulha a — a constante (ou temporário) num CzValor, sem posse
    ChamaNativa,   // dst: valor = call.nativa N(args...) — args são valores; o resultado tem posse
    Solta,         // solta a — solta a referência que o valor embrulhado a segura
    Ret,           // ret
};

struct Instr {
    Op                    op;
    std::uint32_t         dst = 0;        // o temporário escrito (Embrulha, ChamaNativa)
    std::vector<Operando> args;
    std::uint32_t         nativa = 0;     // ChamaNativa: índice em Modulo::nativas
    Token                 pos;            // onde no programa (erros, P8)
};

struct Bloco {
    std::string        nome;
    std::vector<Instr> instr;
};

struct Funcao {
    std::string        nome;      // símbolo (llvm/DESENHO.md 8.3): cz:::main
    Tipo               ret = Tipo::Void;
    std::vector<Tipo>  temps;     // o tipo de cada temporário
    std::vector<Bloco> blocos;

    std::uint32_t novoTemp(Tipo t) { temps.push_back(t); return static_cast<std::uint32_t>(temps.size() - 1); }
};

// Uma função da biblioteca usada pelo programa, resolvida pelo nome na partida
struct Nativa {
    std::string modulo;   // "" = embutida (print, range...)
    std::string nome;
};

struct Modulo {
    std::vector<std::string> literais;   // as strings do programa (imortais)
    std::vector<Nativa>      nativas;
    std::vector<Funcao>      funcoes;

    std::uint32_t literal(const std::string& s);
    std::uint32_t nativa(const std::string& modulo, const std::string& nome);
};

// O texto da CIR (cinzac --emit-cir)
std::string texto(const Modulo& m);
const char* nomeTipo(Tipo t);

}  // namespace cinza::cir

#endif  // CINZA_CIR_H
