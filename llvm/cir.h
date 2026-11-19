#ifndef CINZA_CIR_H
#define CINZA_CIR_H

// ============================================================================
// CIR — a IR intermediária da Cinza (llvm/DESENHO.md, seção 4)
//
// Entre a AST analisada e o LLVM. A tradução AST → CIR (gera_cir.cpp) decide a
// ordem de avaliação, os temporários e a posse; CIR → LLVM (emissor.cpp) é
// mecânica.
//
//   - slots: as variáveis (parâmetros, locais), lidas e escritas por Carrega e
//     Grava; tudo o que atravessa blocos passa por um slot;
//   - temporários: escritos uma vez (no LLVM viram valores SSA); usados só em
//     blocos que a definição domina — a tradução de uma expressão garante isso
//     (o curto-circuito de and/or abre blocos que voltam ao mesmo ponto);
//   - globais: os const do nível superior, escritos pela função de inicialização.
//
// As operações que podem falhar (contas, conversões, chamadas) levam a posição
// (`pos`) do token que a CVM usa na mesma operação: erros e stack trace iguais.
// P4: números, bool, funções, if/while/for range, print, const globais.
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

inline bool ehInteiro(Tipo t)   { return t == Tipo::I32 || t == Tipo::I64; }
inline bool ehFlutuante(Tipo t) { return t == Tipo::F32 || t == Tipo::F64; }

// Um operando: constante ou temporário
struct Operando {
    enum class K : std::uint8_t { Temp, I32, I64, F32, F64, Bool, Literal } k = K::Temp;
    std::uint32_t indice = 0;   // Temp: o temporário; Literal: a string do módulo
    std::int64_t  i      = 0;
    double        f      = 0;

    static Operando temp(std::uint32_t t)    { Operando o; o.k = K::Temp; o.indice = t; return o; }
    static Operando literal(std::uint32_t l) { Operando o; o.k = K::Literal; o.indice = l; return o; }
    static Operando i32(std::int64_t v)      { Operando o; o.k = K::I32; o.i = v; return o; }
    static Operando i64(std::int64_t v)      { Operando o; o.k = K::I64; o.i = v; return o; }
    static Operando boolean(bool v)          { Operando o; o.k = K::Bool; o.i = v; return o; }
    static Operando f32(double v)            { Operando o; o.k = K::F32; o.f = v; return o; }
    static Operando f64(double v)            { Operando o; o.k = K::F64; o.f = v; return o; }
};

enum class Op : std::uint8_t {
    Carrega,       // dst = carrega slot[aux]
    Grava,         // slot[aux] = a
    CarregaG,      // dst = carrega global[aux]
    GravaG,        // global[aux] = a
    Arit,          // dst = a op b — aux: o TokenType (+ - * / %); confere estouro e zero
    Compara,       // dst: bool = a pred b — aux: o TokenType (== != < <= > >=)
    Neg,           // dst = -a (inteiro: confere o menor valor)
    Nao,           // dst: bool = !a
    Converte,      // dst = a convertido para o tipo de dst (confere a faixa ao estreitar)
    Embrulha,      // dst: valor = embrulha a — sem posse
    Desembrulha,   // dst = desembrulha a (o tipo de dst)
    ChamaNativa,   // dst: valor = call.nativa N(args...) — aux: N; o resultado tem posse
    ConverteValor, // dst: valor = a convertido no runtime para o kind de `tipo` (nativas genéricas)
    Chama,         // [dst =] call F(args...) — aux: F
    Solta,         // solta a — solta a referência que o valor embrulhado a segura
    ConferePasso,  // confere passo (i64) != 0 — senão ValueError do range (pos: a chamada)
    PassoRange,    // dst: i64 = próximo contador do for range — args: i, passo, fim (i64);
                   // tipo: a largura do iterador; passou da largura: fim (o último)
    Br,            // br alvo
    BrC,           // brc a, alvo, senao
    Ret,           // ret [a]
    Inalcancavel,  // fim de um caminho que o semântico garante que não chega
};

struct Instr {
    Op                    op;
    std::uint32_t         dst = 0;     // o temporário escrito
    std::vector<Operando> args;
    std::uint32_t         aux = 0;     // slot, global, operador, nativa ou função
    std::uint32_t         alvo = 0, senao = 0;   // Br/BrC: os blocos
    Tipo                  tipo = Tipo::Void;     // ConverteValor: o tipo pedido
    Token                 pos;         // a posição do programa (erros, stack trace)
};

struct Bloco {
    std::string        nome;
    std::vector<Instr> instr;
};

struct Funcao {
    std::string        nome;            // símbolo (llvm/DESENHO.md 8.3): cz:::main
    std::string        nome_trace;      // no stack trace: "f", "Classe.metodo"
    Token              decl;            // a declaração (StackOverflowError, stack trace)
    bool               oculta = false;  // a inicialização dos const: fora da pilha e do trace
    std::vector<Tipo>  params;          // os primeiros slots
    Tipo               ret = Tipo::Void;
    std::vector<Tipo>  slots;
    std::vector<Tipo>  temps;           // o tipo de cada temporário
    std::vector<Bloco> blocos;

    std::uint32_t novoTemp(Tipo t) { temps.push_back(t); return static_cast<std::uint32_t>(temps.size() - 1); }
    std::uint32_t novoSlot(Tipo t) { slots.push_back(t); return static_cast<std::uint32_t>(slots.size() - 1); }
    std::uint32_t novoBloco(const std::string& n) {
        blocos.push_back(Bloco{n + std::to_string(blocos.size()), {}});
        return static_cast<std::uint32_t>(blocos.size() - 1);
    }
};

// Uma função da biblioteca usada pelo programa, resolvida pelo nome na partida
struct Nativa {
    std::string modulo;   // "" = embutida (print, range...)
    std::string nome;
};

struct Modulo {
    std::vector<std::string> arquivos;   // os nomes dos arquivos, na ordem dos file_id
    std::vector<std::string> literais;   // as strings do programa (imortais)
    std::vector<Nativa>      nativas;
    std::vector<Tipo>        globais;    // os const do nível superior
    std::vector<Funcao>      funcoes;
    int                      init = -1;  // a função oculta que inicializa os const (-1: nenhuma)
    int                      main = -1;  // a main da Cinza

    std::uint32_t literal(const std::string& s);
    std::uint32_t nativa(const std::string& modulo, const std::string& nome);
};

// O texto da CIR (cinzac --emit-cir)
std::string texto(const Modulo& m);
const char* nomeTipo(Tipo t);

}  // namespace cinza::cir

#endif  // CINZA_CIR_H
