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
    X(LOADK)      /* R[a] = K[bc] (índice de 32 bits)               */          \
    X(LOADINT)    /* R[a] = bc (i64)                                 */          \
    X(LOADI32)    /* R[a] = bc (i32)                                 */          \
    X(LOADBOOL)   /* R[a] = (b != 0)                                 */          \
    X(LOADVOID)   /* R[a] = valor vazio                              */          \
    X(GETGLOBAL)  /* R[a] = G[b]                                     */          \
    X(SETGLOBAL)  /* G[b] = R[a]                                     */          \
    /* aritmética por tipo (seção K: os dois operandos já têm o tipo da conta) */ \
    X(ADD_I64) X(SUB_I64) X(MUL_I64) X(DIV_I64) X(MOD_I64) /* R[a] = R[b] op R[c] */ \
    X(ADD_I32) X(SUB_I32) X(MUL_I32) X(DIV_I32) X(MOD_I32)                      \
    X(ADD_F64) X(SUB_F64) X(MUL_F64) X(DIV_F64)  /* /0 e estouro: erro */     \
    X(ADD_F32) X(SUB_F32) X(MUL_F32) X(DIV_F32)                                 \
    X(NEG_I64) X(NEG_I32) X(NEG_F64) X(NEG_F32)            /* R[a] = -R[b] */   \
    X(CONV)       /* R[a] = cast<T>(R[b]), T pelo código c (ConvAlvo) */        \
    /* inteiro com constante embutida (c = inteiro de 16 bits com sinal) */     \
    X(ADDK_I64) X(SUBK_I64) X(MULK_I64) X(DIVK_I64) X(MODK_I64) /* R[a] = R[b] op c */ \
    X(ADDK_I32) X(SUBK_I32) X(MULK_I32) X(DIVK_I32) X(MODK_I32)                 \
    X(LTK_I) X(LEK_I) X(GTK_I) X(GEK_I)  /* R[a] = R[b] cmp c (i32 ou i64) */  \
    /* compara e salta: a instrução seguinte é um JMP, feito no mesmo despacho */ \
    X(JLT_I) X(JLE_I)                     /* se R[a] cmp R[b]: salta (inteiros) */ \
    X(JLTK_I) X(JLEK_I) X(JGTK_I) X(JGEK_I) /* se R[a] cmp b (16 bits): salta */  \
    X(CONCAT)     /* R[a] = texto(R[b]) + texto(R[c])                */          \
    X(FMT)        /* R[a] = {R[b]:formato K[c]} (largura*1000 + casas+1) */      \
    X(SLICE)      /* R[a] = R[b][R[b+1]:R[b+2]:R[b+3]] (void = omitido)  */      \
    /* comparação */                                                            \
    X(LT_I) X(LE_I) X(LT_F) X(LE_F) X(LT_S) X(LE_S) /* R[a] = R[b] < R[c] */     \
    X(EQ) X(NE)   /* igualdade da spec 5.3                           */          \
    X(NOT)        /* R[a] = !R[b]                                    */          \
    /* saltos */                                                                \
    X(JMP)        /* pc += bc                                        */          \
    X(JMPIF)      /* se R[a]: pc += bc                               */          \
    X(JMPIFNOT)   /* se !R[a]: pc += bc                              */          \
    /* for: R[a] cópia dos elementos, R[a+1] índice, R[a+2] slot do iterador */ \
    X(FORPREP)    /* R[a] = cópia de R[b]; R[a+1] = 0; R[a+2] = c    */          \
    X(FORNEXT)    /* próximo elemento em R[slot], ou pc += bc        */          \
    X(FORNEXT_W)  /* idem, alargando o elemento (i32 → i64, f32 → f64) */        \
    /* for sobre range(a, b[, p]) sem criar a lista (otimização, seção 11): */  \
    /* R[a] atual, R[a+1] fim, R[a+2] passo, R[a+3] slot do iterador */         \
    X(RANGEPREP)  /* confere o passo (ValueError se 0); R[a+3] = b   */          \
    X(FORRANGE)   /* próximo inteiro em R[slot], ou pc += bc         */          \
    /* coleções */                                                              \
    X(NEWLIST)    /* R[a] = [R[b] ... R[b+c-1]]                      */          \
    X(NEWDICT)    /* R[a] = c pares (R[b], R[b+1]), (R[b+2], ...)    */          \
    /* literal grande, em blocos: acrescenta ao que NEWLIST/NEWDICT criou */      \
    X(APPEND)     /* R[a] (lista) += R[b] ... R[b+c-1]               */          \
    X(DICTADD)    /* R[a] (dict) += c pares a partir de R[b]          */          \
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
    /* interfaces e type() */                                                   \
    X(CALLIFACE)  /* método c da interface b pela classe de R[a]     */          \
    X(TYPEOF)     /* R[a] = tipo real de R[b] (K[c]: tipo estático)  */          \
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

// Alvo do CONV (operando c): o tipo de cast<T> e das promoções do semântico
enum class ConvAlvo : std::uint16_t { I32, I64, F32, F64, String };
ConvAlvo convAlvo(TypeRef t);       // t é i32, i64, f32, f64 ou string
TypeRef  convTipo(std::uint16_t c);

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
