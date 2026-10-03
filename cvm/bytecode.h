#ifndef CINZA_CVM_BYTECODE_H
#define CINZA_CVM_BYTECODE_H

#include "../ast.h"
#include "../diagnostics.h"
#include "../natives.h"
#include "../value.h"
#include <cstdint>
#include <unordered_map>
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
    /* int com constante embutida (c = inteiro de 16 bits com sinal) */         \
    X(ADDK_I) X(SUBK_I) X(MULK_I) X(DIVK_I) X(MODK_I) /* R[a] = R[b] op c */    \
    X(LTK_I) X(LEK_I) X(GTK_I) X(GEK_I)               /* R[a] = R[b] cmp c */   \
    /* compara e salta: a instrução seguinte é um JMP, feito no mesmo despacho */ \
    X(JLT_I) X(JLE_I)                     /* se R[a] cmp R[b]: salta         */  \
    X(JLTK_I) X(JLEK_I) X(JGTK_I) X(JGEK_I) /* se R[a] cmp b (16 bits): salta */  \
    X(CONCAT)     /* R[a] = texto(R[b]) + texto(R[c])                */          \
    X(FMT)        /* R[a] = {R[b]:formato K[c]} (largura*1000 + casas+1) */      \
    X(SLICE)      /* R[a] = R[b][R[b+1]:R[b+2]:R[b+3]] (void = omitido)  */      \
    /* comparação */                                                            \
    X(LT_I) X(LE_I) X(LT_D) X(LE_D) X(LT_S) X(LE_S) /* R[a] = R[b] < R[c] */     \
    X(EQ) X(NE)   /* igualdade da spec 5.3                           */          \
    X(NOT)        /* R[a] = !R[b]                                    */          \
    /* saltos */                                                                \
    X(JMP)        /* pc += bc                                        */          \
    X(JMPIF)      /* se R[a]: pc += bc                               */          \
    X(JMPIFNOT)   /* se !R[a]: pc += bc                              */          \
    /* for: R[a] cópia dos elementos, R[a+1] índice, R[a+2] slot do iterador */ \
    X(FORPREP)    /* R[a] = cópia de R[b]; R[a+1] = 0; R[a+2] = c    */          \
    X(FORNEXT)    /* próximo elemento em R[slot], ou pc += bc        */          \
    X(FORNEXT_D)  /* idem, convertendo int em decimal                */          \
    /* for sobre range(a, b[, p]) sem criar a lista (otimização, seção 11): */  \
    /* R[a] atual, R[a+1] fim, R[a+2] passo, R[a+3] slot do iterador */         \
    X(RANGEPREP)  /* confere o passo (ValueError se 0); R[a+3] = b   */          \
    X(FORRANGE)   /* próximo inteiro em R[slot], ou pc += bc         */          \
    X(FORRANGE_D) /* idem, como decimal                              */          \
    /* coleções */                                                              \
    X(NEWLIST)    /* R[a] = [R[b] ... R[b+c-1]]                      */          \
    X(NEWDICT)    /* R[a] = c pares (R[b], R[b+1]), (R[b+2], ...)    */          \
    X(NEWPAIR)    /* R[a] = {R[b], R[c]}                             */          \
    X(GETINDEX)   /* R[a] = R[b][R[c]]          (leitura)            */          \
    X(INDEXPLACE) /* R[a] = R[b][R[c]]          (caminho até o lugar) */         \
    X(SETINDEX)   /* R[a][R[b]] = R[c]          (só atualiza chave)  */          \
    X(CALLBUILTIN)/* R[a] = método embutido b de R[a] com c args     */          \
    X(GETFIRST)   /* R[a] = R[b].first                               */          \
    X(GETSECOND)  /* R[a] = R[b].second                              */          \
    /* objetos e struct */                                                      \
    X(NEWOBJ)     /* R[a] = novo objeto da classe b (campos vazios)  */          \
    X(NEWSTRUCT)  /* R[a] = struct b com os campos R[a] ... R[a+c-1] */          \
    X(GETFIELD)   /* R[a] = R[b].campo[c]                            */          \
    X(SETFIELD)   /* R[a].campo[b] = R[c]                            */          \
    /* interfaces, op<...> e type() */                                          \
    X(CALLIFACE)  /* método c da interface b pela classe de R[a]     */          \
    X(TYPEOF)     /* R[a] = tipo real de R[b] (K[c]: tipo estático)  */          \
    X(CAST)       /* R[a] = conversão de R[b] para K[c] (op<...>)    */          \
    X(KEEPLOCK)   /* R[a] = R[b] mantendo o tipo travado de R[a]     */          \
    /* operações genéricas: operandos op<...> de tipo travado desconhecido */  \
    X(ADD) X(SUB) X(MUL) X(DIV) X(MOD) X(LT) X(LE) X(NEG)                       \
    /* exceções */                                                              \
    X(NEWERROR)   /* R[a] = erro de tipo K[b] com mensagem R[c] (c = sem: "") */ \
    X(ERRFIELD)   /* R[a] = kind/message/line/column (c = 0..3) de R[b] */       \
    X(THROW)      /* lança o erro R[a]                               */          \
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

// Tratador de erro: um erro numa instrução de `start` a `end` (exclusive)
// cujo tipo é `kind` vai para `target`, guardado em R[reg]. "Error" pega todo
// erro menos o exit(); vazio (o pega-tudo do finally) pega até o exit().
// A tabela fica com o try mais interno primeiro.
struct Handler {
    std::uint32_t start  = 0;
    std::uint32_t end    = 0;
    std::uint32_t target = 0;
    std::string   kind;
    std::uint16_t reg    = 0;
};
inline constexpr std::uint16_t NO_REG = 0xFFFF;   // operando ausente (NEWERROR sem mensagem)

// Função compilada (protótipo)
struct Proto {
    std::string                 name;          // nome no stack trace (trace_name)
    SourceLocation              decl;          // posição da declaração
    std::uint16_t               num_params = 0;
    std::uint16_t               num_regs   = 0;
    std::vector<Instr>          code;
    std::vector<SourceLocation> lines;         // origem de cada instrução (arquivo, linha, coluna)
    std::vector<Value>          consts;
    std::vector<Handler>        handlers;

    // sem nome: inicialização de módulo ou de campos — não é uma chamada da
    // linguagem (fica fora do stack trace e do limite de profundidade)
    bool hidden() const { return name.empty(); }
};

// Classe: a declaração e os protótipos do construtor e do inicializador de campos
struct ClassRef {
    static constexpr std::size_t none = static_cast<std::size_t>(-1);
    const ClassDecl* decl = nullptr;
    std::size_t      ctor = none;
    std::size_t      init = none;
    // para cada interface cumprida, os protótipos na ordem dos métodos dela
    std::vector<std::pair<const InterfaceDecl*, std::vector<std::size_t>>> itables;
};

// Imagem: o programa compilado (protótipos e tabelas globais)
struct Image {
    std::vector<Proto>           protos;
    std::vector<const NativeFn*> natives;
    std::vector<ClassRef>        classes;
    std::vector<const StructDecl*> structs;
    std::vector<const InterfaceDecl*> interfaces;
    std::unordered_map<const ClassDecl*, std::size_t> class_index;   // classe de um objeto → classes
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
