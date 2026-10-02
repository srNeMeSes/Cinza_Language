#ifndef CINZA_CVM_BYTECODE_H
#define CINZA_CVM_BYTECODE_H

#include "../diagnostics.h"
#include "../natives.h"
#include "../value.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cinza::cvm {

// ============================================================================
// BYTECODE DA CVM (desenho: cvm/DESENHO.md)
//
// Instrução de 8 bytes: código e três operandos de 16 bits. Uma instrução usa
// b e c separados OU bc (os dois juntos, inteiro de 32 bits com sinal), nunca
// os dois. R[x] registrador da janela, K[x] constante, G[x] slot global.
// ============================================================================

#define CVM_OPCODES(X)                                                          \
    /* carga e movimento */                                                     \
    X(MOVE)       /* R[a] = R[b]                                     */          \
    X(LOADK)      /* R[a] = K[b]                                     */          \
    X(LOADINT)    /* R[a] = bc                                       */          \
    X(LOADBOOL)   /* R[a] = (b != 0)                                 */          \
    X(LOADVOID)   /* R[a] = valor vazio                              */          \
    X(GETGLOBAL)  /* R[a] = G[b]                                     */          \
    X(SETGLOBAL)  /* G[b] = R[a]                                     */          \
    /* aritmética específica por tipo */                                        \
    X(ADD_I) X(SUB_I) X(MUL_I) X(DIV_I) X(MOD_I)  /* R[a] = R[b] op R[c] */      \
    X(ADD_D) X(SUB_D) X(MUL_D) X(DIV_D) X(MOD_D)                                \
    X(NEG_I) X(NEG_D)                             /* R[a] = -R[b]        */      \
    X(I2D)        /* R[a] = decimal(R[b])                            */          \
    X(CONCAT)     /* R[a] = texto(R[b]) + texto(R[c])                */          \
    /* comparação */                                                            \
    X(LT_I) X(LE_I) X(LT_D) X(LE_D) X(LT_S) X(LE_S) /* R[a] = R[b] < R[c] */     \
    X(EQ) X(NE)   /* igualdade da spec 5.3                           */          \
    X(NOT)        /* R[a] = !R[b]                                    */          \
    /* saltos */                                                                \
    X(JMP)        /* pc += bc                                        */          \
    X(JMPIF)      /* se R[a]: pc += bc                               */          \
    X(JMPIFNOT)   /* se !R[a]: pc += bc                              */          \
    /* chamadas */                                                              \
    X(CALL)       /* R[a] = proto b(R[a] ... R[a+c-1])               */          \
    X(CALLNATIVE) /* R[a] = nativa b(R[a] ... R[a+c-1])              */          \
    X(RET)        /* retorna R[a]                                    */          \
    X(RETVOID)    /* retorna sem valor                               */

enum class Op : std::uint16_t {
#define CVM_ENUM(n) n,
    CVM_OPCODES(CVM_ENUM)
#undef CVM_ENUM
    COUNT
};

const char* opName(Op op);

struct Instr {
    Op            op;
    std::uint16_t a = 0;
    std::uint16_t b = 0;
    std::uint16_t c = 0;

    // b e c juntos: inteiro de 32 bits com sinal (saltos, LOADINT)
    std::int32_t bc() const {
        return static_cast<std::int32_t>((static_cast<std::uint32_t>(b) << 16) | c);
    }
    void setBc(std::int32_t v) {
        const auto u = static_cast<std::uint32_t>(v);
        b = static_cast<std::uint16_t>(u >> 16);
        c = static_cast<std::uint16_t>(u & 0xFFFF);
    }
};
static_assert(sizeof(Instr) == 8, "instrução da CVM tem 8 bytes");

// Função compilada (protótipo)
struct Proto {
    std::string                 name;          // nome no stack trace (trace_name)
    SourceLocation              decl;          // posição da declaração
    std::uint16_t               num_params = 0;
    std::uint16_t               num_regs   = 0;
    std::vector<Instr>          code;
    std::vector<SourceLocation> lines;         // origem de cada instrução (arquivo, linha, coluna)
    std::vector<Value>          consts;
};

// Imagem: o programa compilado (protótipos e tabelas globais)
struct Image {
    std::vector<Proto>           protos;
    std::vector<const NativeFn*> natives;
    std::vector<std::size_t>     inits;        // inicialização dos const de cada módulo, em ordem
    std::size_t                  main = 0;     // protótipo da main
    bool                         main_has_args = false;
    std::uint32_t                num_globals = 0;
    std::vector<std::pair<std::uint32_t, const NativeConst*>> native_globals;
};

// Desmontador (--bytecode)
std::string disassemble(const Image& img);

} // namespace cinza::cvm

#endif // CINZA_CVM_BYTECODE_H
