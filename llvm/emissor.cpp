// CIR → LLVM pela llvm-c (LLVM 20.1.8, fixo: llvm/DESENHO.md 1 e 8.4). A sequência:
// alvo X86 → TargetMachine → triple e DataLayout do módulo → IR → verificação →
// passes → objeto.
//
// A tradução é mecânica: slots viram alloca (o mem2reg do -O2 os leva a registradores),
// temporários viram valores SSA, os const globais viram globais internos. Cada operação
// que pode falhar tem o caminho rápido em linha e, no desvio frio, a chamada ao runtime
// que refaz a operação pela regra da CVM (a mesma mensagem) e o desvio para o bloco
// tratador da instrução (P8: o except de um try ou a saída de erro da função). Antes do P8,
// é fatal).
#include "emissor.h"
#include "cinzart.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace cinza::llvm {

namespace {

// O alvo é o do sistema, o mesmo do g++ que liga o programa com a libcinzart (Q4): no
// Windows, o do MinGW (x86_64-w64-windows-gnu); nos outros, o triple padrão do LLVM
// (x86_64-pc-linux-gnu...).
const std::string& triple() {
    static const std::string t = [] {
#ifdef _WIN32
        return std::string("x86_64-w64-windows-gnu");
#else
        char* p = LLVMGetDefaultTargetTriple();
        std::string r = p;
        LLVMDisposeMessage(p);
        return r;
#endif
    }();
    return t;
}

void inicializaAlvo() {
    static bool feito = false;
    if (feito) return;
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    feito = true;
}

// O deslocamento do vetor de campos num objeto e num struct do runtime (P10: o acesso em
// linha). Os dois tipos não são standard-layout (o offsetof do GCC e do clang vale, com aviso)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
const std::size_t DESLOC_CAMPOS_OBJ    = offsetof(ClassInstance, fields);
const std::size_t DESLOC_CAMPOS_STRUCT = offsetof(StructValue, fields);
#pragma GCC diagnostic pop

// Q5: o deslocamento do vetor de campos fica fixo em 48, em todo sistema — o cinzac e o runtime
// podem ser compilados com bibliotecas C++ diferentes (no Linux, o cinzac com a libstdc++ do LLVM do
// Alpine e o runtime com a libc++), e o acesso em linha aos campos (P10) supõe o mesmo layout dos dois
// lados. (Com um long no GcObject eram 40 no Windows e 48 no Linux.)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(cinza::ClassInstance, fields) == 48 && offsetof(cinza::StructValue, fields) == 48,
              "o vetor de campos mudou de lugar: atualize o caminho rápido dos campos (llvm/DESENHO.md 8.1)");
#pragma GCC diagnostic pop

LLVMTargetMachineRef criaMaquina(bool otimiza, std::string& erro) {
    inicializaAlvo();
    LLVMTargetRef alvo = nullptr;
    char* msg = nullptr;
    if (LLVMGetTargetFromTriple(triple().c_str(), &alvo, &msg)) {
        erro = msg ? msg : "alvo desconhecido";
        LLVMDisposeMessage(msg);
        return nullptr;
    }
    // x86-64 genérico (o executável roda em qualquer máquina da arquitetura); fora do Windows,
    // código independente de posição: o g++ das distribuições liga como PIE
    const bool x86 = triple().rfind("x86_64", 0) == 0;
#ifdef _WIN32
    const LLVMRelocMode reloc = LLVMRelocDefault;
#else
    const LLVMRelocMode reloc = LLVMRelocPIC;
#endif
    return LLVMCreateTargetMachine(alvo, triple().c_str(), x86 ? "x86-64" : "generic", "",
                                   otimiza ? LLVMCodeGenLevelDefault : LLVMCodeGenLevelNone,
                                   reloc, LLVMCodeModelDefault);
}

std::uint8_t kind(Value::Kind k) { return static_cast<std::uint8_t>(k); }

std::uint8_t kindDe(cir::Tipo t) {
    switch (t) {
        case cir::Tipo::I32:  return kind(Value::Kind::I32);
        case cir::Tipo::I64:  return kind(Value::Kind::I64);
        case cir::Tipo::F32:  return kind(Value::Kind::F32);
        case cir::Tipo::F64:  return kind(Value::Kind::F64);
        case cir::Tipo::Bool: return kind(Value::Kind::BOOL);
        case cir::Tipo::Str:   return kind(Value::Kind::STRING);
        case cir::Tipo::Lista: return kind(Value::Kind::LIST);
        case cir::Tipo::Dict:  return kind(Value::Kind::DICT);
        case cir::Tipo::Par:   return kind(Value::Kind::PAIR);
        case cir::Tipo::Struct: return kind(Value::Kind::STRUCT);
        case cir::Tipo::Obj:   return kind(Value::Kind::INSTANCE);
        case cir::Tipo::Enum:  return kind(Value::Kind::ENUM);
        case cir::Tipo::TipoV: return kind(Value::Kind::TYPE);
        case cir::Tipo::Erro:  return kind(Value::Kind::ERROR);
        default:               return kind(Value::Kind::VOID_VAL);
    }
}

// Erro de tradução (CIR que o emissor não sabe traduzir: erro interno do cinzac)
struct ErroEmissor {
    std::string msg;
};

// As funções que podem voltar com erro pendente (P10): uma operação que falha, uma
// chamada a uma função da Cinza (a regra das 2000 chamadas é conferida em quem chama) ou
// a uma oculta que pode falhar. As outras — um getter, um setter sem conta — dispensam a
// conferência do erro depois de chamadas a elas
std::vector<bool> podemFalhar(const cir::Modulo& m) {
    std::vector<bool> r(m.funcoes.size(), false);
    for (bool mudou = true; mudou;) {
        mudou = false;
        for (std::size_t fi = 0; fi < m.funcoes.size(); ++fi) {
            if (r[fi]) continue;
            bool falha = false;
            for (const cir::Bloco& bl : m.funcoes[fi].blocos)
                for (const cir::Instr& in : bl.instr) {
                    if (in.op == cir::Op::Chama) falha = falha || !m.funcoes[in.aux].oculta || r[in.aux];
                    else if (in.op == cir::Op::ChamaIface) falha = true;   // chama uma função não oculta
                    else falha = falha || cir::podeFalhar(in);
                }
            if (falha) { r[fi] = true; mudou = true; }
        }
    }
    return r;
}

}  // namespace

// Instruções da CIR acima das quais uma função não é otimizada (Emissor::Impl::funcao)
constexpr std::size_t LIMITE_OTIMIZA = 3000;
// Soltas acima das quais o refcount da função vai pelo runtime, não em linha
constexpr std::size_t LIMITE_SOLTAS_EM_LINHA = 256;

struct Emissor::Impl {
    bool                 otimiza = false;
    bool                 asan = false;   // sanitize_address em toda função e o passo asan
    bool                 otimizado = false;   // os passes já rodaram
    LLVMContextRef       ctx = nullptr;
    LLVMModuleRef        mod = nullptr;
    LLVMTargetMachineRef tm  = nullptr;
    LLVMTargetDataRef    dl  = nullptr;
    LLVMBuilderRef       b   = nullptr;   // o código
    LLVMBuilderRef       be  = nullptr;   // os alloca, no bloco de entrada

    LLVMTypeRef tI1{}, tI8{}, tI32{}, tI64{}, tF32{}, tF64{}, tPtr{}, tVoid{};
    LLVMTypeRef tValor{};    // CzValor: { i8 tipo, [3 x i8], i32 extra, i64 u }
    LLVMTypeRef tPos{};      // CzPos: { i32 arquivo, i32 linha, i32 coluna }
    LLVMTypeRef tFuncao{};   // CzFuncao: { ptr nome, i32 arquivo, i32 linha, i32 coluna }
    LLVMTypeRef tExec{};     // CzExec: { ptr erro, i32 prof, i32, [N x { ptr f, ptr pos }] }
    LLVMValueRef gExec{};    // @cz_exec

    // A interface do runtime (cinzart.h), declarada no primeiro uso
    struct FnRt { LLVMTypeRef tipo; LLVMValueRef fn; };
    std::map<std::string, FnRt> runtime;

    // @cz.literais: [N x ptr] para as CzString, preenchido no início por uma chamada só
    // (cz_literais, com a tabela constante @cz.literais.tab); literais[i] é o endereço do i-ésimo
    std::vector<LLVMValueRef> literais;
    LLVMValueRef              tabela_literais = nullptr;
    std::vector<LLVMValueRef> nativas;    // @cz.nat.N: ptr para a NativeFn
    std::vector<std::pair<LLVMValueRef, LLVMValueRef>> nomes_nativas;   // módulo, nome
    std::vector<LLVMValueRef> globais;    // os const do nível superior
    std::vector<LLVMValueRef> fns;        // as funções da Cinza
    std::vector<LLVMTypeRef>  tfns;
    std::vector<LLVMValueRef> descs;      // o CzFuncao de cada uma
    std::vector<LLVMValueRef> descs_struct, descs_classe, descs_enum, tipos;   // ptr, preenchidos na partida
    std::vector<LLVMValueRef> tabelas;    // as tabelas de métodos de interface
    std::vector<LLVMValueRef> tabelas_desc;   // o CzFuncao de cada uma (a regra das 2000 chamadas)
    std::vector<bool>         pode_falhar;    // a função pode voltar com erro pendente
    LLVMValueRef quadro_vazio{};          // o quadro da função oculta sem chamador (a dos const)
    std::size_t  vetores = 0;             // os globais dos literais grandes (cz.vetor.N)
    std::unordered_map<std::uint32_t, std::uint32_t> literal_de;   // temporário embrulhado → literal
    std::map<std::tuple<int, int, int>, LLVMValueRef> posicoes;   // os CzPos

    // A função em tradução
    const cir::Modulo* cm = nullptr;
    const cir::Funcao* cf = nullptr;
    LLVMValueRef fn{};
    std::vector<LLVMValueRef> temps, slots;
    std::vector<LLVMBasicBlockRef> blocos;
    LLVMValueRef quadro{};          // o quadro desta função na sombra da pilha
    LLVMBasicBlockRef tratador{};   // o bloco tratador da instrução em tradução
    bool em_linha = false;          // o refcount em linha (-O2, função de tamanho normal)

    // ── utilitários ──────────────────────────────────────────────────────
    void atributo(LLVMValueRef f, const char* nome) {
        const unsigned k = LLVMGetEnumAttributeKindForName(nome, std::strlen(nome));
        LLVMAddAttributeAtIndex(f, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(ctx, k, 0));
    }

    FnRt& rt(const char* nome, LLVMTypeRef ret, std::vector<LLVMTypeRef> params, bool frio = false,
             bool sem_volta = false) {
        auto it = runtime.find(nome);
        if (it != runtime.end()) return it->second;
        LLVMTypeRef t = LLVMFunctionType(ret, params.data(), static_cast<unsigned>(params.size()), 0);
        LLVMValueRef f = LLVMAddFunction(mod, nome, t);
        atributo(f, "nounwind");
        if (frio) atributo(f, "cold");
        if (sem_volta) atributo(f, "noreturn");
        return runtime[nome] = FnRt{t, f};
    }
    LLVMValueRef chamaRt(FnRt& r, std::vector<LLVMValueRef> args) {
        return LLVMBuildCall2(b, r.tipo, r.fn, args.data(), static_cast<unsigned>(args.size()), "");
    }

    LLVMValueRef global(LLVMTypeRef t, const std::string& nome, LLVMValueRef ini, bool constante) {
        LLVMValueRef g = LLVMAddGlobal(mod, t, nome.c_str());
        LLVMSetInitializer(g, ini);
        LLVMSetGlobalConstant(g, constante);
        LLVMSetLinkage(g, constante ? LLVMPrivateLinkage : LLVMInternalLinkage);
        return g;
    }

    // Texto do C terminado em zero
    LLVMValueRef textoC(const std::string& s, const std::string& nome) {
        LLVMValueRef c = LLVMConstStringInContext(ctx, s.c_str(), static_cast<unsigned>(s.size()), 0);
        return global(LLVMTypeOf(c), nome, c, true);
    }

    LLVMValueRef ci32(std::int64_t v) { return LLVMConstInt(tI32, static_cast<std::uint64_t>(v), 1); }
    LLVMValueRef ci64(std::int64_t v) { return LLVMConstInt(tI64, static_cast<std::uint64_t>(v), 1); }

    LLVMTypeRef tipo(cir::Tipo t) {
        switch (t) {
            case cir::Tipo::Void:  return tVoid;
            case cir::Tipo::Bool:  return tI1;
            case cir::Tipo::I32:   return tI32;
            case cir::Tipo::I64:   return tI64;
            case cir::Tipo::F32:   return tF32;
            case cir::Tipo::F64:   return tF64;
            case cir::Tipo::Str:   return tPtr;
            case cir::Tipo::Valor: return tValor;
            case cir::Tipo::Lista: case cir::Tipo::Dict: case cir::Tipo::Par: return tPtr;
            case cir::Tipo::Struct: case cir::Tipo::Obj: case cir::Tipo::Desc: case cir::Tipo::Erro: return tPtr;
            case cir::Tipo::Enum: case cir::Tipo::TipoV: return tValor;
        }
        return tVoid;
    }

    // A posição `tok` como constante CzPos (uma por posição distinta)
    LLVMValueRef posicao(const Token& tok) {
        const auto chave = std::make_tuple(tok.file_id, tok.line, tok.column);
        auto it = posicoes.find(chave);
        if (it != posicoes.end()) return it->second;
        LLVMValueRef campos[] = {ci32(tok.file_id), ci32(tok.line), ci32(tok.column)};
        LLVMValueRef g = global(tPos, "cz.pos." + std::to_string(posicoes.size()),
                                LLVMConstNamedStruct(tPos, campos, 3), true);
        return posicoes[chave] = g;
    }

    // Intrínseca sobrecarregada pelo tipo `t` (llvm.sadd.with.overflow.i32...)
    LLVMValueRef intrinseca(const char* nome, LLVMTypeRef t, std::vector<LLVMValueRef> args) {
        const unsigned id = LLVMLookupIntrinsicID(nome, std::strlen(nome));
        LLVMValueRef f = LLVMGetIntrinsicDeclaration(mod, id, &t, 1);
        LLVMTypeRef tf = LLVMIntrinsicGetType(ctx, id, &t, 1);
        return LLVMBuildCall2(b, tf, f, args.data(), static_cast<unsigned>(args.size()), "");
    }

    bool prepara(std::string& erro) {
        ctx = LLVMContextCreate();
        mod = LLVMModuleCreateWithNameInContext("cinza", ctx);
        tm  = criaMaquina(otimiza, erro);
        if (!tm) return false;
        LLVMSetTarget(mod, triple().c_str());
        dl = LLVMCreateTargetDataLayout(tm);
        LLVMSetModuleDataLayout(mod, dl);
        b  = LLVMCreateBuilderInContext(ctx);
        be = LLVMCreateBuilderInContext(ctx);

        tI1 = LLVMInt1TypeInContext(ctx);
        tI8 = LLVMInt8TypeInContext(ctx);
        tI32 = LLVMInt32TypeInContext(ctx);
        tI64 = LLVMInt64TypeInContext(ctx);
        tF32 = LLVMFloatTypeInContext(ctx);
        tF64 = LLVMDoubleTypeInContext(ctx);
        tPtr = LLVMPointerTypeInContext(ctx, 0);
        tVoid = LLVMVoidTypeInContext(ctx);
        tValor = LLVMStructCreateNamed(ctx, "CzValor");
        LLVMTypeRef cv[] = {tI8, LLVMArrayType2(tI8, 3), tI32, tI64};
        LLVMStructSetBody(tValor, cv, 4, 0);
        tPos = LLVMStructCreateNamed(ctx, "CzPos");
        LLVMTypeRef cp[] = {tI32, tI32, tI32};
        LLVMStructSetBody(tPos, cp, 3, 0);
        tFuncao = LLVMStructCreateNamed(ctx, "CzFuncao");
        LLVMTypeRef cfn[] = {tPtr, tI32, tI32, tI32};
        LLVMStructSetBody(tFuncao, cfn, 4, 0);
        LLVMTypeRef tQuadro = LLVMStructCreateNamed(ctx, "CzQuadro");
        LLVMTypeRef cq[] = {tPtr, tPtr};
        LLVMStructSetBody(tQuadro, cq, 2, 0);
        tExec = LLVMStructCreateNamed(ctx, "CzExec");
        LLVMTypeRef ce[] = {tPtr, tI32, tI32, LLVMArrayType2(tQuadro, CZ_PROFUNDIDADE)};
        LLVMStructSetBody(tExec, ce, 4, 0);
        gExec = LLVMAddGlobal(mod, tExec, "cz_exec");   // definido na libcinzart
        LLVMSetLinkage(gExec, LLVMExternalLinkage);
        return true;
    }

    // ── a função em tradução ─────────────────────────────────────────────
    LLVMBasicBlockRef novoBloco(const char* nome) { return LLVMAppendBasicBlockInContext(ctx, fn, nome); }
    LLVMValueRef reserva(LLVMTypeRef t, const char* nome) { return LLVMBuildAlloca(be, t, nome); }

    cir::Tipo tipoOp(const cir::Operando& o) const {
        switch (o.k) {
            case cir::Operando::K::Temp:    return cf->temps[o.indice];
            case cir::Operando::K::I32:     return cir::Tipo::I32;
            case cir::Operando::K::I64:     return cir::Tipo::I64;
            case cir::Operando::K::F32:     return cir::Tipo::F32;
            case cir::Operando::K::F64:     return cir::Tipo::F64;
            case cir::Operando::K::Bool:    return cir::Tipo::Bool;
            case cir::Operando::K::Literal: return cir::Tipo::Str;
            case cir::Operando::K::Nulo:    return cir::Tipo::Str;
            case cir::Operando::K::Vazio:   return cir::Tipo::Valor;
            case cir::Operando::K::DescStruct: case cir::Operando::K::DescClasse: return cir::Tipo::Desc;
            case cir::Operando::K::Enum:    return cir::Tipo::Enum;
            case cir::Operando::K::TipoLit: return cir::Tipo::TipoV;
        }
        return cir::Tipo::Void;
    }

    LLVMValueRef val(const cir::Operando& o) {
        switch (o.k) {
            case cir::Operando::K::Temp: {
                // o embrulha de um literal só vira código onde é usado: no vetor de um literal
                // grande ele vai como marca constante e não deixa instruções mortas para trás
                if (temps[o.indice]) return temps[o.indice];
                cir::Operando lit;
                lit.k = cir::Operando::K::Literal;
                lit.indice = literal_de.at(o.indice);
                return embrulha(lit);
            }
            case cir::Operando::K::I32:     return ci32(static_cast<std::int32_t>(o.i));
            case cir::Operando::K::I64:     return ci64(o.i);
            case cir::Operando::K::F32:     return LLVMConstReal(tF32, o.f);
            case cir::Operando::K::F64:     return LLVMConstReal(tF64, o.f);
            case cir::Operando::K::Bool:    return LLVMConstInt(tI1, o.i ? 1 : 0, 0);
            case cir::Operando::K::Literal: return LLVMBuildLoad2(b, tPtr, literais[o.indice], "lit");
            case cir::Operando::K::Nulo:    return LLVMConstNull(tPtr);
            case cir::Operando::K::Vazio: {   // o CzValor void: tipo VOID_VAL, o resto zerado
                LLVMValueRef campos[] = {LLVMConstInt(tI8, kind(Value::Kind::VOID_VAL), 0),
                                         LLVMConstNull(LLVMArrayType2(tI8, 3)), ci32(0), ci64(0)};
                return LLVMConstNamedStruct(tValor, campos, 4);
            }
            case cir::Operando::K::DescStruct: return LLVMBuildLoad2(b, tPtr, descs_struct[o.indice], "desc");
            case cir::Operando::K::DescClasse: return LLVMBuildLoad2(b, tPtr, descs_classe[o.indice], "desc");
            case cir::Operando::K::Enum:   // { ENUM, extra = o índice do valor, u = o descritor }
                return valorDireto(kind(Value::Kind::ENUM), static_cast<std::uint32_t>(o.i),
                                   LLVMBuildLoad2(b, tPtr, descs_enum[o.indice], "desc"));
            case cir::Operando::K::TipoLit:   // { TYPE, u = o TypeInfo }
                return valorDireto(kind(Value::Kind::TYPE), 0, LLVMBuildLoad2(b, tPtr, tipos[o.indice], "tipo"));
        }
        return nullptr;
    }

    LLVMValueRef valorDireto(std::uint8_t k, std::uint32_t extra, LLVMValueRef ptr) {
        LLVMValueRef v = LLVMGetUndef(tValor);
        v = LLVMBuildInsertValue(b, v, LLVMConstInt(tI8, k, 0), 0, "");
        v = LLVMBuildInsertValue(b, v, LLVMConstNull(LLVMArrayType2(tI8, 3)), 1, "");
        v = LLVMBuildInsertValue(b, v, LLVMConstInt(tI32, extra, 0), 2, "");
        return LLVMBuildInsertValue(b, v, LLVMBuildPtrToInt(b, ptr, tI64, ""), 3, "");
    }

    // Um valor como os bits do campo `u` do CzValor (os altos zerados, como no Value)
    LLVMValueRef bits(LLVMValueRef v, cir::Tipo t) {
        switch (t) {
            case cir::Tipo::I32:  return LLVMBuildZExt(b, v, tI64, "");
            case cir::Tipo::I64:  return v;
            case cir::Tipo::F32:  return LLVMBuildZExt(b, LLVMBuildBitCast(b, v, tI32, ""), tI64, "");
            case cir::Tipo::F64:  return LLVMBuildBitCast(b, v, tI64, "");
            case cir::Tipo::Bool: return LLVMBuildZExt(b, v, tI64, "");
            case cir::Tipo::Str: case cir::Tipo::Lista: case cir::Tipo::Dict: case cir::Tipo::Par:
            case cir::Tipo::Struct: case cir::Tipo::Obj: case cir::Tipo::Erro:
                return LLVMBuildPtrToInt(b, v, tI64, "");
            default:              throw ErroEmissor{"embrulho de um tipo sem valor"};
        }
    }
    LLVMValueRef deBits(LLVMValueRef u, cir::Tipo t) {
        switch (t) {
            case cir::Tipo::I32:  return LLVMBuildTrunc(b, u, tI32, "");
            case cir::Tipo::I64:  return u;
            case cir::Tipo::F32:  return LLVMBuildBitCast(b, LLVMBuildTrunc(b, u, tI32, ""), tF32, "");
            case cir::Tipo::F64:  return LLVMBuildBitCast(b, u, tF64, "");
            case cir::Tipo::Bool: return LLVMBuildICmp(b, LLVMIntNE, u, ci64(0), "");
            case cir::Tipo::Str: case cir::Tipo::Lista: case cir::Tipo::Dict: case cir::Tipo::Par:
            case cir::Tipo::Struct: case cir::Tipo::Obj: case cir::Tipo::Erro:
                return LLVMBuildIntToPtr(b, u, tPtr, "");
            default:              throw ErroEmissor{"desembrulho para um tipo sem valor"};
        }
    }

    // Desvio frio para o caminho de erro: `falha` chama o runtime (que deixa o erro
    // pendente); depois, o tratador da instrução. O código segue no bloco `ok`.
    template <typename F>
    void seFalha(LLVMValueRef cond, F&& falha) {
        LLVMBasicBlockRef bf = novoBloco("falha"), ok = novoBloco("ok");
        LLVMBuildCondBr(b, cond, bf, ok);
        LLVMPositionBuilderAtEnd(b, bf);
        falha();
        LLVMBuildBr(b, tratador);
        LLVMPositionBuilderAtEnd(b, ok);
    }
    // Depois de uma chamada que pode falhar: erro pendente → o tratador
    void confereErro() {
        LLVMValueRef e = LLVMBuildLoad2(b, tPtr, gExec, "erro");   // o campo 0
        LLVMBasicBlockRef ok = novoBloco("ok");
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, e, LLVMConstNull(tPtr), ""), tratador, ok);
        LLVMPositionBuilderAtEnd(b, ok);
    }
    // -O2: a contagem em linha, como o czRetain/czRelease de gc_object.h — o objeto imortal
    // (literal) não conta; a contagem que chega a zero chama a destruição do runtime. Os
    // deslocamentos (refs no 0, marcas no 5) são os do CzObjeto, conferidos pelo
    // --confere-layout. Em -O0 as funções do runtime fazem isso (e, na libcinzart de
    // depuração, conferem cada operação)
    LLVMValueRef marcasDe(LLVMValueRef p) {
        LLVMValueRef idx[] = {ci64(5)};
        return LLVMBuildLoad2(b, tI8, LLVMBuildInBoundsGEP2(b, tI8, p, idx, 1, ""), "marcas");
    }
    void retemEmLinha(LLVMValueRef p) {
        LLVMValueRef imortal = LLVMBuildICmp(b, LLVMIntNE, LLVMBuildAnd(b, marcasDe(p), LLVMConstInt(tI8, marca::IMORTAL, 0), ""),
                                             LLVMConstInt(tI8, 0, 0), "imortal");
        LLVMBasicBlockRef conta = novoBloco("retem"), fim = novoBloco("reteve");
        LLVMBuildCondBr(b, imortal, fim, conta);
        LLVMPositionBuilderAtEnd(b, conta);
        LLVMValueRef refs = LLVMBuildLoad2(b, tI32, p, "refs");
        LLVMBuildStore(b, LLVMBuildAdd(b, refs, ci32(1), ""), p);
        LLVMBuildBr(b, fim);
        LLVMPositionBuilderAtEnd(b, fim);
    }
    void soltaEmLinha(LLVMValueRef p) {
        LLVMValueRef imortal = LLVMBuildICmp(b, LLVMIntNE, LLVMBuildAnd(b, marcasDe(p), LLVMConstInt(tI8, marca::IMORTAL, 0), ""),
                                             LLVMConstInt(tI8, 0, 0), "imortal");
        LLVMBasicBlockRef conta = novoBloco("conta"), destroi = novoBloco("destroi"), fim = novoBloco("soltou.em.linha");
        LLVMBuildCondBr(b, imortal, fim, conta);
        LLVMPositionBuilderAtEnd(b, conta);
        LLVMValueRef refs = LLVMBuildSub(b, LLVMBuildLoad2(b, tI32, p, "refs"), ci32(1), "");
        LLVMBuildStore(b, refs, p);
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, refs, ci32(0), ""), destroi, fim);
        LLVMPositionBuilderAtEnd(b, destroi);
        chamaRt(rt("cz_destroi", tVoid, {tPtr}), {p});
        LLVMBuildBr(b, fim);
        LLVMPositionBuilderAtEnd(b, fim);
    }

    // A regra das 2000 chamadas, antes de chamar uma função da Cinza (desc: o CzFuncao dela):
    // cheia, o StackOverflowError fica pendente, na posição da declaração chamada, e o fluxo
    // vai para o tratador
    void confereProfundidade(LLVMValueRef desc) {
        LLVMValueRef prof = LLVMBuildLoad2(b, tI32, LLVMBuildStructGEP2(b, tExec, gExec, 1, ""), "prof");
        seFalha(LLVMBuildICmp(b, LLVMIntSGE, prof, ci32(CZ_PROFUNDIDADE), "cheia"), [&] {
            chamaRt(rt("cz_estouro", tVoid, {tPtr}, true), {desc});
        });
    }

    // Volta com o erro pendente: o valor de retorno não importa (o chamador confere antes)
    void retornaComErro() {
        LLVMTypeRef r = LLVMGetReturnType(LLVMGlobalGetValueType(fn));
        if (LLVMGetTypeKind(r) == LLVMVoidTypeKind) LLVMBuildRetVoid(b);
        else LLVMBuildRet(b, LLVMGetUndef(r));
    }

    LLVMValueRef naoFinito(LLVMValueRef r, LLVMTypeRef t) {
        LLVMValueRef abs = intrinseca("llvm.fabs", t, {r});
        return LLVMBuildFCmp(b, LLVMRealUEQ, abs, LLVMConstReal(t, HUGE_VAL), "naofinito");
    }

    void falhaBinaria(std::uint32_t op, cir::Tipo t, LLVMValueRef a, LLVMValueRef x, const Token& pos) {
        chamaRt(rt("cz_falha_binaria", tVoid, {tI32, tI8, tI64, tI64, tPtr}, true),
                {LLVMConstInt(tI32, op, 0), LLVMConstInt(tI8, kindDe(t), 0), bits(a, t), bits(x, t), posicao(pos)});
    }

    LLVMValueRef aritmetica(const cir::Instr& in) {
        const cir::Tipo t = cf->temps[in.dst];
        LLVMTypeRef lt = tipo(t);
        LLVMValueRef a = val(in.args[0]), x = val(in.args[1]);
        const auto op = static_cast<TokenType>(in.aux);
        auto falha = [&] { falhaBinaria(in.aux, t, a, x, in.pos); };
        if (cir::ehInteiro(t)) {
            const bool i32 = t == cir::Tipo::I32;
            const std::int64_t menor = i32 ? INT32_MIN : INT64_MIN;
            LLVMValueRef zero = LLVMConstInt(lt, 0, 0), menos1 = LLVMConstInt(lt, static_cast<std::uint64_t>(-1), 1);
            const char* intr = nullptr;
            switch (op) {
                case TokenType::OP_PLUS:     intr = "llvm.sadd.with.overflow"; break;
                case TokenType::OP_MINUS:    intr = "llvm.ssub.with.overflow"; break;
                case TokenType::OP_MULTIPLY: intr = "llvm.smul.with.overflow"; break;
                case TokenType::OP_DIVIDE: {   // divisor 0 e menor / -1
                    LLVMValueRef ruim = LLVMBuildOr(b, LLVMBuildICmp(b, LLVMIntEQ, x, zero, ""),
                        LLVMBuildAnd(b, LLVMBuildICmp(b, LLVMIntEQ, a, LLVMConstInt(lt, static_cast<std::uint64_t>(menor), 1), ""),
                                     LLVMBuildICmp(b, LLVMIntEQ, x, menos1, ""), ""), "");
                    seFalha(ruim, falha);
                    return LLVMBuildSDiv(b, a, x, "div");
                }
                case TokenType::OP_MODULO: {   // divisor 0; x % -1 é 0 (sem o srem indefinido)
                    seFalha(LLVMBuildICmp(b, LLVMIntEQ, x, zero, ""), falha);
                    LLVMValueRef d = LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntEQ, x, menos1, ""),
                                                     LLVMConstInt(lt, 1, 0), x, "");
                    return LLVMBuildSRem(b, a, d, "rem");
                }
                default: throw ErroEmissor{"operador aritmético desconhecido"};
            }
            LLVMValueRef r = intrinseca(intr, lt, {a, x});
            seFalha(LLVMBuildExtractValue(b, r, 1, "estouro"), falha);
            return LLVMBuildExtractValue(b, r, 0, "r");
        }
        // ponto flutuante: divisão por zero e resultado não finito (infinito e NaN não
        // existem na Cinza); a regra da CVM diz qual dos dois
        LLVMValueRef r = nullptr;
        switch (op) {
            case TokenType::OP_PLUS:     r = LLVMBuildFAdd(b, a, x, "r"); break;
            case TokenType::OP_MINUS:    r = LLVMBuildFSub(b, a, x, "r"); break;
            case TokenType::OP_MULTIPLY: r = LLVMBuildFMul(b, a, x, "r"); break;
            case TokenType::OP_DIVIDE:   r = LLVMBuildFDiv(b, a, x, "r"); break;   // /0: infinito ou NaN
            default: throw ErroEmissor{"operador de ponto flutuante desconhecido"};
        }
        seFalha(naoFinito(r, lt), falha);
        return r;
    }

    LLVMValueRef compara(const cir::Instr& in) {
        const cir::Tipo t = tipoOp(in.args[0]);
        LLVMValueRef a = val(in.args[0]), x = val(in.args[1]);
        const auto op = static_cast<TokenType>(in.aux);
        if (cir::ehFlutuante(t)) {
            LLVMRealPredicate p;
            switch (op) {
                case TokenType::OP_EQUAL:         p = LLVMRealOEQ; break;
                case TokenType::OP_NOT_EQUAL:     p = LLVMRealUNE; break;
                case TokenType::OP_LESS:          p = LLVMRealOLT; break;
                case TokenType::OP_LESS_EQUAL:    p = LLVMRealOLE; break;
                case TokenType::OP_GREATER:       p = LLVMRealOGT; break;
                case TokenType::OP_GREATER_EQUAL: p = LLVMRealOGE; break;
                default: throw ErroEmissor{"comparação desconhecida"};
            }
            return LLVMBuildFCmp(b, p, a, x, "cmp");
        }
        LLVMIntPredicate p;
        switch (op) {
            case TokenType::OP_EQUAL:         p = LLVMIntEQ; break;
            case TokenType::OP_NOT_EQUAL:     p = LLVMIntNE; break;
            case TokenType::OP_LESS:          p = LLVMIntSLT; break;
            case TokenType::OP_LESS_EQUAL:    p = LLVMIntSLE; break;
            case TokenType::OP_GREATER:       p = LLVMIntSGT; break;
            case TokenType::OP_GREATER_EQUAL: p = LLVMIntSGE; break;
            default: throw ErroEmissor{"comparação desconhecida"};
        }
        return LLVMBuildICmp(b, p, a, x, "cmp");
    }

    // cast<T> e as promoções: o caminho rápido confere a faixa; fora dela, a regra da CVM
    LLVMValueRef converte(const cir::Instr& in) {
        const cir::Tipo de = tipoOp(in.args[0]), para = cf->temps[in.dst];
        LLVMValueRef a = val(in.args[0]);
        auto falha = [&] {
            chamaRt(rt("cz_falha_conv", tVoid, {tI8, tI64, tI8, tPtr}, true),
                    {LLVMConstInt(tI8, kindDe(de), 0), bits(a, de), LLVMConstInt(tI8, kindDe(para), 0),
                     posicao(in.pos)});
        };
        if (de == para) return a;
        if (cir::ehInteiro(de) && cir::ehInteiro(para)) {
            if (para == cir::Tipo::I64) return LLVMBuildSExt(b, a, tI64, "");
            LLVMValueRef fora = LLVMBuildOr(b, LLVMBuildICmp(b, LLVMIntSLT, a, ci64(INT32_MIN), ""),
                                            LLVMBuildICmp(b, LLVMIntSGT, a, ci64(INT32_MAX), ""), "fora");
            seFalha(fora, falha);
            return LLVMBuildTrunc(b, a, tI32, "");
        }
        if (cir::ehInteiro(de))   // direto do inteiro: um arredondamento só
            return LLVMBuildSIToFP(b, a, tipo(para), "");
        if (cir::ehFlutuante(para)) {
            if (para == cir::Tipo::F64) return LLVMBuildFPExt(b, a, tF64, "");
            LLVMValueRef r = LLVMBuildFPTrunc(b, a, tF32, "");   // grande demais: infinito
            seFalha(naoFinito(r, tF32), falha);
            return r;
        }
        // flutuante → inteiro: trunca em direção a zero e confere a faixa (em double)
        LLVMValueRef d = de == cir::Tipo::F32 ? LLVMBuildFPExt(b, a, tF64, "") : a;
        LLVMValueRef t = intrinseca("llvm.trunc", tF64, {d});
        LLVMValueRef fora;
        if (para == cir::Tipo::I64)
            fora = LLVMBuildOr(b, LLVMBuildFCmp(b, LLVMRealOLT, t, LLVMConstReal(tF64, -9223372036854775808.0), ""),
                               LLVMBuildFCmp(b, LLVMRealOGE, t, LLVMConstReal(tF64, 9223372036854775808.0), ""), "fora");
        else
            fora = LLVMBuildOr(b, LLVMBuildFCmp(b, LLVMRealOLT, t, LLVMConstReal(tF64, -2147483648.0), ""),
                               LLVMBuildFCmp(b, LLVMRealOGT, t, LLVMConstReal(tF64, 2147483647.0), ""), "fora");
        seFalha(fora, falha);
        return LLVMBuildFPToSI(b, t, tipo(para), "");
    }

    LLVMValueRef embrulha(const cir::Operando& o) {
        const cir::Tipo t = tipoOp(o);
        LLVMValueRef v = LLVMGetUndef(tValor);
        v = LLVMBuildInsertValue(b, v, LLVMConstInt(tI8, kindDe(t), 0), 0, "");
        v = LLVMBuildInsertValue(b, v, LLVMConstNull(LLVMArrayType2(tI8, 3)), 1, "");
        v = LLVMBuildInsertValue(b, v, LLVMConstInt(tI32, 0, 0), 2, "");
        return LLVMBuildInsertValue(b, v, bits(val(o), t), 3, "valor");
    }

    void instrucao(const cir::Instr& in) {
        using cir::Op;
        tratador = blocos[in.erro];
        switch (in.op) {
            case Op::Carrega:
                temps[in.dst] = LLVMBuildLoad2(b, tipo(cf->slots[in.aux]), slots[in.aux], "");
                break;
            case Op::Grava:
                LLVMBuildStore(b, val(in.args[0]), slots[in.aux]);
                break;
            case Op::CarregaG:
                temps[in.dst] = LLVMBuildLoad2(b, tipo(cm->globais[in.aux]), globais[in.aux], "");
                break;
            case Op::GravaG:
                LLVMBuildStore(b, val(in.args[0]), globais[in.aux]);
                break;
            case Op::Arit:    temps[in.dst] = aritmetica(in); break;
            case Op::Compara: temps[in.dst] = compara(in); break;
            case Op::Neg: {
                const cir::Tipo t = cf->temps[in.dst];
                LLVMValueRef a = val(in.args[0]);
                if (cir::ehFlutuante(t)) { temps[in.dst] = LLVMBuildFNeg(b, a, "neg"); break; }
                const std::int64_t menor = t == cir::Tipo::I32 ? INT32_MIN : INT64_MIN;
                seFalha(LLVMBuildICmp(b, LLVMIntEQ, a, LLVMConstInt(tipo(t), static_cast<std::uint64_t>(menor), 1), ""),
                        [&] {
                            chamaRt(rt("cz_falha_neg", tVoid, {tI8, tI64, tPtr}, true),
                                    {LLVMConstInt(tI8, kindDe(t), 0), bits(a, t), posicao(in.pos)});
                        });
                temps[in.dst] = LLVMBuildNSWSub(b, LLVMConstInt(tipo(t), 0, 0), a, "neg");
                break;
            }
            case Op::Nao:
                temps[in.dst] = LLVMBuildXor(b, val(in.args[0]), LLVMConstInt(tI1, 1, 0), "nao");
                break;
            case Op::Converte: temps[in.dst] = converte(in); break;
            case Op::Embrulha:
                if (in.args[0].k == cir::Operando::K::Literal) {   // fica para o uso (val)
                    literal_de[in.dst] = in.args[0].indice;
                    temps[in.dst] = nullptr;
                    break;
                }
                temps[in.dst] = cir::ehValorDireto(tipoOp(in.args[0])) ? val(in.args[0]) : embrulha(in.args[0]);
                break;
            case Op::Desembrulha:   // enum e tipo já são o CzValor inteiro
                temps[in.dst] = cir::ehValorDireto(cf->temps[in.dst])
                                    ? val(in.args[0])
                                    : deBits(LLVMBuildExtractValue(b, val(in.args[0]), 3, ""), cf->temps[in.dst]);
                break;
            case Op::ChamaNativa: {
                LLVMValueRef vetor = LLVMConstNull(tPtr);
                if (!in.args.empty()) {
                    LLVMTypeRef tv = LLVMArrayType2(tValor, in.args.size());
                    vetor = reserva(tv, "args");
                    for (std::size_t i = 0; i < in.args.size(); ++i) {
                        LLVMValueRef idx[] = {ci64(0), ci64(static_cast<std::int64_t>(i))};
                        LLVMBuildStore(b, val(in.args[i]), LLVMBuildInBoundsGEP2(b, tv, vetor, idx, 2, ""));
                    }
                }
                LLVMValueRef res = reserva(tValor, "res");
                LLVMValueRef fnptr = LLVMBuildLoad2(b, tPtr, nativas[in.aux], "nativa");
                chamaRt(rt("cz_nativa", tVoid, {tPtr, tPtr, tI64, tPtr, tPtr}),
                        {fnptr, vetor, ci64(static_cast<std::int64_t>(in.args.size())), res, posicao(in.pos)});
                confereErro();
                temps[in.dst] = LLVMBuildLoad2(b, tValor, res, "resultado");
                break;
            }
            case Op::ConverteValor: {
                LLVMValueRef v = reserva(tValor, "conv");
                LLVMBuildStore(b, val(in.args[0]), v);
                chamaRt(rt("cz_conv_valor", tVoid, {tPtr, tI8, tPtr}),
                        {v, LLVMConstInt(tI8, kindDe(in.tipo), 0), posicao(in.pos)});
                confereErro();
                temps[in.dst] = LLVMBuildLoad2(b, tValor, v, "");
                break;
            }
            case Op::Retem:
                if (em_linha) retemEmLinha(val(in.args[0]));
                else chamaRt(rt("cz_retem", tVoid, {tPtr}), {val(in.args[0])});
                break;
            case Op::Solta: {
                if (cir::ehRef(tipoOp(in.args[0]))) {
                    LLVMValueRef p = val(in.args[0]);
                    if (!em_linha) {   // o cz_solta ignora o nulo (slot vazio)
                        chamaRt(rt("cz_solta", tVoid, {tPtr}), {p});
                        break;
                    }
                    LLVMBasicBlockRef sim = novoBloco("solta"), segue = novoBloco("soltou");
                    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, p, LLVMConstNull(tPtr), ""), sim, segue);
                    LLVMPositionBuilderAtEnd(b, sim);
                    soltaEmLinha(p);
                    LLVMBuildBr(b, segue);
                    LLVMPositionBuilderAtEnd(b, segue);
                    break;
                }
                LLVMValueRef v = reserva(tValor, "solta");
                LLVMBuildStore(b, val(in.args[0]), v);
                chamaRt(rt("cz_solta_valor", tVoid, {tPtr}), {v});
                break;
            }
            case Op::ChamaRt: {
                LLVMValueRef r = chamaRuntime(in);
                if (cir::infoRt(static_cast<cir::Rt>(in.aux)).ret != cir::Tipo::Void) temps[in.dst] = r;
                break;
            }
            case Op::CampoDireto: case Op::GravaCampoDireto: {
                // o CzValor do campo i, direto no vetor de campos do objeto (ClassInstance ou
                // StructValue): o início do std::vector é o primeiro ponteiro dele
                const bool st = tipoOp(in.args[0]) == cir::Tipo::Struct;
                LLVMValueRef o = val(in.args[0]);
                LLVMValueRef off[] = {ci64(static_cast<std::int64_t>(st ? DESLOC_CAMPOS_STRUCT : DESLOC_CAMPOS_OBJ))};
                LLVMValueRef inicio = LLVMBuildLoad2(b, tPtr, LLVMBuildInBoundsGEP2(b, tI8, o, off, 1, ""), "campos");
                LLVMValueRef idx[] = {ci64(static_cast<std::int64_t>(in.aux))};
                LLVMValueRef lugar = LLVMBuildInBoundsGEP2(b, tValor, inicio, idx, 1, "campo");
                if (in.op == Op::CampoDireto) {
                    const cir::Tipo t = cf->temps[in.dst];
                    if (cir::ehValorDireto(t)) {
                        temps[in.dst] = LLVMBuildLoad2(b, tValor, lugar, "");
                    } else {
                        LLVMValueRef pu = LLVMBuildStructGEP2(b, tValor, lugar, 3, "");
                        temps[in.dst] = deBits(LLVMBuildLoad2(b, tI64, pu, ""), t);
                    }
                } else {   // o valor inteiro (o tipo também): o antigo não tem referência
                    LLVMBuildStore(b, cir::ehValorDireto(tipoOp(in.args[1])) ? val(in.args[1]) : embrulha(in.args[1]), lugar);
                }
                break;
            }
            case Op::ChamaIface: {
                // o método pela classe real do objeto: a tabela do método, pelo índice da classe
                const cir::TabelaIface& t = cm->tabelas[in.aux];
                if (quadro) LLVMBuildStore(b, posicao(in.pos), LLVMBuildStructGEP2(b, tQuadro(), quadro, 1, ""));
                std::vector<LLVMValueRef> args;
                for (const auto& a : in.args) args.push_back(val(a));
                LLVMValueRef ci = chamaRt(rt("cz_classe_de", tI32, {tPtr}), {args[0]});
                LLVMTypeRef tt = LLVMArrayType2(tPtr, cm->classes.size());
                LLVMValueRef idx[] = {ci64(0), LLVMBuildSExt(b, ci, tI64, "")};
                LLVMValueRef fp = LLVMBuildLoad2(b, tPtr, LLVMBuildInBoundsGEP2(b, tt, tabelas[in.aux], idx, 2, ""), "metodo");
                confereProfundidade(LLVMBuildLoad2(b, tPtr, LLVMBuildInBoundsGEP2(b, tt, tabelas_desc[in.aux], idx, 2, ""),
                                                   "desc"));
                std::vector<LLVMTypeRef> ps;
                for (cir::Tipo p : t.params) ps.push_back(tipo(p));
                LLVMTypeRef tf = LLVMFunctionType(tipo(t.ret), ps.data(), static_cast<unsigned>(ps.size()), 0);
                LLVMValueRef r = LLVMBuildCall2(b, tf, fp, args.data(), static_cast<unsigned>(args.size()), "");
                if (t.ret != cir::Tipo::Void) temps[in.dst] = r;
                bool falha = false;   // alguma das funções da tabela pode falhar?
                for (int fi : t.funcoes) falha = falha || (fi >= 0 && pode_falhar[static_cast<std::size_t>(fi)]);
                if (falha) confereErro();
                break;
            }
            case Op::Chama: {
                if (quadro)   // a posição da chamada, para o stack trace
                    LLVMBuildStore(b, posicao(in.pos), LLVMBuildStructGEP2(b, tQuadro(), quadro, 1, ""));
                std::vector<LLVMValueRef> args;
                for (const auto& a : in.args) args.push_back(val(a));
                if (!cm->funcoes[in.aux].oculta) confereProfundidade(descs[in.aux]);
                LLVMValueRef r = LLVMBuildCall2(b, tfns[in.aux], fns[in.aux], args.data(),
                                                static_cast<unsigned>(args.size()), "");
                if (cm->funcoes[in.aux].ret != cir::Tipo::Void) temps[in.dst] = r;
                if (pode_falhar[in.aux]) confereErro();   // a função voltou com erro pendente?
                break;
            }
            case Op::ConferePasso:
                seFalha(LLVMBuildICmp(b, LLVMIntEQ, val(in.args[0]), ci64(0), ""), [&] {
                    chamaRt(rt("cz_falha_passo", tVoid, {tPtr}, true), {posicao(in.pos)});
                });
                break;
            case Op::PassoRange: {
                LLVMValueRef s = intrinseca("llvm.sadd.with.overflow", tI64, {val(in.args[0]), val(in.args[1])});
                LLVMValueRef v = LLVMBuildExtractValue(b, s, 0, "");
                LLVMValueRef ultimo = LLVMBuildExtractValue(b, s, 1, "");
                if (in.tipo == cir::Tipo::I32)   // passou da largura do iterador
                    ultimo = LLVMBuildOr(b, ultimo,
                        LLVMBuildOr(b, LLVMBuildICmp(b, LLVMIntSLT, v, ci64(INT32_MIN), ""),
                                    LLVMBuildICmp(b, LLVMIntSGT, v, ci64(INT32_MAX), ""), ""), "");
                temps[in.dst] = LLVMBuildSelect(b, ultimo, val(in.args[2]), v, "proximo");
                break;
            }
            case Op::Br:  LLVMBuildBr(b, blocos[in.alvo]); break;
            case Op::BrC: LLVMBuildCondBr(b, val(in.args[0]), blocos[in.alvo], blocos[in.senao]); break;
            case Op::Ret:
                if (quadro && !cf->oculta) {   // sai da sombra da pilha
                    LLVMValueRef pp = LLVMBuildStructGEP2(b, tExec, gExec, 1, "");
                    LLVMValueRef prof = LLVMBuildLoad2(b, tI32, pp, "prof");
                    LLVMBuildStore(b, LLVMBuildSub(b, prof, ci32(1), ""), pp);
                }
                if (in.args.empty()) LLVMBuildRetVoid(b);
                else LLVMBuildRet(b, val(in.args[0]));
                break;
            case Op::Inalcancavel: LLVMBuildUnreachable(b); break;
            case Op::RetErro:
                if (quadro && !cf->oculta) {   // sai da sombra da pilha também no erro
                    LLVMValueRef pp = LLVMBuildStructGEP2(b, tExec, gExec, 1, "");
                    LLVMValueRef prof = LLVMBuildLoad2(b, tI32, pp, "prof");
                    LLVMBuildStore(b, LLVMBuildSub(b, prof, ci32(1), ""), pp);
                }
                retornaComErro();
                break;
        }
    }

    LLVMTypeRef tQuadro() { return LLVMGetTypeByName2(ctx, "CzQuadro"); }

    // Um alloca no main do C (no começo do bloco de entrada)
    LLVMBasicBlockRef main_entrada{};
    LLVMValueRef reservaMain(LLVMTypeRef t) {
        LLVMBuilderRef x = LLVMCreateBuilderInContext(ctx);
        LLVMValueRef primeira = LLVMGetFirstInstruction(main_entrada);
        if (primeira) LLVMPositionBuilderBefore(x, primeira);
        else LLVMPositionBuilderAtEnd(x, main_entrada);
        LLVMValueRef a = LLVMBuildAlloca(x, t, "params");
        LLVMDisposeBuilder(x);
        return a;
    }

    // call.rt: a assinatura em C sai da tabela da CIR (cir::infoRt) — `valor` por
    // ponteiro, o resultado `valor` por um ponteiro de saída, a posição no fim das que
    // podem falhar; bool volta como int32
    LLVMValueRef chamaRuntime(const cir::Instr& in) {
        const cir::InfoRt& info = cir::infoRt(static_cast<cir::Rt>(in.aux));
        std::vector<LLVMTypeRef> ps;
        std::vector<LLVMValueRef> args;
        for (std::size_t i = 0; i < info.params.size(); ++i) {
            const cir::Tipo t = info.params[i];
            if (cir::ehValorDireto(t)) {   // por ponteiro (const CzValor*)
                LLVMValueRef v = reserva(tValor, "arg");
                LLVMBuildStore(b, val(in.args[i]), v);
                ps.push_back(tPtr);
                args.push_back(v);
            } else {
                ps.push_back(tipo(t));
                args.push_back(val(in.args[i]));
            }
        }
        LLVMValueRef saida = nullptr;
        if (info.variadico) {   // o resto dos argumentos num vetor de CzValor
            const std::size_t n = in.args.size() - info.params.size();
            LLVMValueRef vetor = LLVMConstNull(tPtr);
            if (n > 0) {
                LLVMTypeRef tv = LLVMArrayType2(tValor, n);
                std::vector<LLVMValueRef> vs;
                bool constantes = true;
                for (std::size_t i = 0; i < n; ++i) {
                    const cir::Operando& a = in.args[info.params.size() + i];
                    auto lit = a.k == cir::Operando::K::Temp ? literal_de.find(a.indice) : literal_de.end();
                    if (n >= 8 && lit != literal_de.end()) {   // a string literal como marca constante
                        LLVMValueRef c[] = {LLVMConstInt(tI8, CZ_TIPO_LITERAL, 0), LLVMConstNull(LLVMArrayType2(tI8, 3)),
                                            ci32(lit->second), ci64(0)};
                        vs.push_back(LLVMConstNamedStruct(tValor, c, 4));
                    } else {
                        vs.push_back(val(a));
                    }
                    constantes = constantes && LLVMIsConstant(vs.back());
                }
                if (n >= 8) {
                    // literal grande: os elementos constantes vão num global já pronto (sem
                    // uma instrução por elemento, que o LLVM levaria minutos para otimizar
                    // numa lista de 70 mil); os outros são gravados nele logo antes da
                    // chamada — nenhum código da Cinza roda entre as gravações e a chamada,
                    // que copia os elementos
                    std::vector<LLVMValueRef> ini;
                    for (LLVMValueRef v : vs) ini.push_back(LLVMIsConstant(v) ? v : LLVMConstNull(tValor));
                    LLVMValueRef arr = LLVMConstArray2(tValor, ini.data(), n);
                    vetor = global(LLVMTypeOf(arr), "cz.vetor." + std::to_string(vetores++), arr, constantes);
                } else {
                    vetor = reserva(tv, "vetor");
                }
                for (std::size_t i = 0; i < n; ++i) {
                    if (n >= 8 && LLVMIsConstant(vs[i])) continue;
                    LLVMValueRef idx[] = {ci64(0), ci64(static_cast<std::int64_t>(i))};
                    LLVMBuildStore(b, vs[i], LLVMBuildInBoundsGEP2(b, tv, vetor, idx, 2, ""));
                }
            }
            ps.push_back(tPtr);
            args.push_back(vetor);
            ps.push_back(tI64);
            args.push_back(ci64(static_cast<std::int64_t>(n)));
        }
        if (info.ret == cir::Tipo::Valor) {
            saida = reserva(tValor, "saida");
            ps.push_back(tPtr);
            args.push_back(saida);
        }
        if (info.falha) {
            ps.push_back(tPtr);
            args.push_back(posicao(in.pos));
        }
        LLVMTypeRef ret = info.ret == cir::Tipo::Valor ? tVoid
                        : info.ret == cir::Tipo::Bool  ? tI32 : tipo(info.ret);
        LLVMValueRef r = chamaRt(rt(info.nome, ret, ps), args);
        if (info.falha) confereErro();
        if (info.ret == cir::Tipo::Valor) return LLVMBuildLoad2(b, tValor, saida, "");
        if (info.ret == cir::Tipo::Bool) return LLVMBuildICmp(b, LLVMIntNE, r, ci32(0), "");
        return info.ret == cir::Tipo::Void ? nullptr : r;
    }

    void declaraFuncao(std::size_t i, const cir::Funcao& f) {
        std::vector<LLVMTypeRef> ps;
        for (cir::Tipo t : f.params) ps.push_back(tipo(t));
        LLVMTypeRef tf = LLVMFunctionType(tipo(f.ret), ps.data(), static_cast<unsigned>(ps.size()), 0);
        LLVMValueRef g = LLVMAddFunction(mod, f.nome.c_str(), tf);
        LLVMSetLinkage(g, LLVMInternalLinkage);
        atributo(g, "nounwind");
        fns.push_back(g);
        tfns.push_back(tf);
        // o descritor: nome do trace e posição da declaração
        LLVMValueRef campos[] = {textoC(f.nome_trace, "cz.fn." + std::to_string(i) + ".nome"),
                                 ci32(f.decl.file_id), ci32(f.decl.line), ci32(f.decl.column)};
        descs.push_back(global(tFuncao, "cz.fn." + std::to_string(i), LLVMConstNamedStruct(tFuncao, campos, 4), true));
    }

    void funcao(std::size_t i, const cir::Funcao& f) {
        cf = &f;
        fn = fns[i];
        temps.assign(f.temps.size(), nullptr);
        literal_de.clear();
        slots.clear();
        blocos.clear();
        quadro = nullptr;
        tratador = nullptr;
        // Uma função enorme (o main de um programa gerado, uma tabela escrita à mão) vai sem
        // otimização mesmo em -O2: o alocador de registradores e o machine sinking do LLVM
        // crescem mais que linear com milhares de blocos (o main de 1000 linhas com objetos
        // levava 11 s, uma lista com 400 níveis 11 s, 1000 níveis 114 s). Ela roda o mesmo,
        // só não é otimizada nem expandida nas outras.
        // O refcount em linha põe 5 blocos em cada solta: com centenas delas vivas ao mesmo
        // tempo (os temporários de uma expressão funda) o alocador também cresce mais que
        // linear, e a função fica com as chamadas do runtime.
        std::size_t tamanho = 0, soltas = 0;
        for (const cir::Bloco& bl : f.blocos) {
            tamanho += bl.instr.size();
            for (const cir::Instr& in : bl.instr) soltas += in.op == cir::Op::Solta;
        }
        const bool enorme = tamanho > LIMITE_OTIMIZA;
        em_linha = otimiza && !enorme && soltas <= LIMITE_SOLTAS_EM_LINHA;
        if (otimiza && enorme) {
            atributo(fn, "noinline");
            atributo(fn, "optnone");
        }

        LLVMBasicBlockRef entrada = novoBloco("entrada");
        LLVMPositionBuilderAtEnd(be, entrada);
        for (cir::Tipo t : f.slots) slots.push_back(reserva(tipo(t), "s"));
        LLVMBasicBlockRef prologo = novoBloco("prologo");
        for (const cir::Bloco& bl : f.blocos) blocos.push_back(novoBloco(bl.nome.c_str()));

        LLVMPositionBuilderAtEnd(b, prologo);
        for (std::size_t s = f.params.size(); s < f.slots.size(); ++s)   // os slots com referência começam vazios
            if (cir::ehRef(f.slots[s])) LLVMBuildStore(b, LLVMConstNull(tPtr), slots[s]);
        for (std::size_t p = 0; p < f.params.size(); ++p)
            LLVMBuildStore(b, LLVMGetParam(fn, static_cast<unsigned>(p)), slots[p]);
        if (!f.oculta) {
            // entra na sombra da pilha. A regra das 2000 chamadas (spec 5.8) é conferida por
            // quem chama, antes da chamada, como na CVM (a main conta como a primeira): aqui
            // sempre há lugar
            LLVMValueRef pp = LLVMBuildStructGEP2(b, tExec, gExec, 1, "");
            LLVMValueRef prof = LLVMBuildLoad2(b, tI32, pp, "prof");
            LLVMValueRef idx[] = {ci32(0), ci32(3), prof};
            quadro = LLVMBuildInBoundsGEP2(b, tExec, gExec, idx, 3, "quadro");
            LLVMBuildStore(b, descs[i], LLVMBuildStructGEP2(b, tQuadro(), quadro, 0, ""));
            LLVMBuildStore(b, LLVMConstNull(tPtr), LLVMBuildStructGEP2(b, tQuadro(), quadro, 1, ""));
            LLVMBuildStore(b, LLVMBuildAdd(b, prof, ci32(1), ""), pp);
        }
        if (f.oculta) {
            // a função oculta (inicializador de campos, const) não tem quadro; as chamadas
            // dela gravam a posição no quadro de quem a chamou, como o call_line que a CVM
            // guarda no quadro seguinte ao oculto (sem chamador: num quadro de descarte)
            LLVMValueRef prof = LLVMBuildLoad2(b, tI32, LLVMBuildStructGEP2(b, tExec, gExec, 1, ""), "prof");
            LLVMValueRef topo = LLVMBuildSub(b, prof, ci32(1), "");
            LLVMValueRef idx[] = {ci32(0), ci32(3), LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntSGT, prof, ci32(0), ""),
                                                                    topo, ci32(0), "")};
            LLVMValueRef q = LLVMBuildInBoundsGEP2(b, tExec, gExec, idx, 3, "");
            quadro = LLVMBuildSelect(b, LLVMBuildICmp(b, LLVMIntSGT, prof, ci32(0), ""), q, quadro_vazio, "quadro");
        }
        LLVMBuildBr(b, blocos.front());

        for (std::size_t k = 0; k < f.blocos.size(); ++k) {
            LLVMPositionBuilderAtEnd(b, blocos[k]);
            for (const cir::Instr& in : f.blocos[k].instr) instrucao(in);
        }
        LLVMPositionBuilderAtEnd(be, entrada);
        LLVMBuildBr(be, prologo);
    }

    // int main(int argc, char** argv): partida do runtime, arquivos, literais e nativas,
    // a inicialização dos const, a main da Cinza (cz:::main) e o fim do runtime, que dá o
    // código de saída
    void mainC(const cir::Modulo& m) {
        LLVMTypeRef params[] = {tI32, tPtr};
        LLVMValueRef f = LLVMAddFunction(mod, "main", LLVMFunctionType(tI32, params, 2, 0));
        atributo(f, "nounwind");
        main_entrada = LLVMAppendBasicBlockInContext(ctx, f, "entrada");
        LLVMPositionBuilderAtEnd(b, main_entrada);
        chamaRt(rt("cz_inicio", tVoid, {tI32, tPtr}), {LLVMGetParam(f, 0), LLVMGetParam(f, 1)});

        std::vector<LLVMValueRef> nomes;
        for (std::size_t i = 0; i < m.arquivos.size(); ++i)
            nomes.push_back(textoC(m.arquivos[i], "cz.arq." + std::to_string(i)));
        LLVMValueRef tabela = LLVMConstNull(tPtr);
        if (!nomes.empty()) {
            LLVMValueRef arr = LLVMConstArray2(tPtr, nomes.data(), nomes.size());
            tabela = global(LLVMTypeOf(arr), "cz.arquivos", arr, true);
        }
        chamaRt(rt("cz_arquivos", tVoid, {tPtr, tI64}), {tabela, ci64(static_cast<std::int64_t>(nomes.size()))});

        if (!m.literais.empty()) {   // { bytes, tamanho } de cada literal, na ordem do módulo
            LLVMTypeRef tpar = LLVMStructTypeInContext(ctx, std::vector<LLVMTypeRef>{tPtr, tI64}.data(), 2, 0);
            std::vector<LLVMValueRef> pares;
            for (std::size_t i = 0; i < m.literais.size(); ++i) {
                LLVMValueRef c[] = {textoC(m.literais[i], "cz.lit." + std::to_string(i)),
                                    ci64(static_cast<std::int64_t>(m.literais[i].size()))};
                pares.push_back(LLVMConstStructInContext(ctx, c, 2, 0));
            }
            LLVMValueRef arr = LLVMConstArray2(tpar, pares.data(), pares.size());
            chamaRt(rt("cz_literais", tVoid, {tPtr, tI64, tPtr}),
                    {global(LLVMTypeOf(arr), "cz.literais.tab", arr, true),
                     ci64(static_cast<std::int64_t>(m.literais.size())), tabela_literais});
        }
        for (std::size_t i = 0; i < m.nativas.size(); ++i)
            LLVMBuildStore(b, chamaRt(rt("cz_nativa_resolve", tPtr, {tPtr, tPtr}),
                                      {nomes_nativas[i].first, nomes_nativas[i].second}),
                           nativas[i]);
        // os descritores e os tipos do programa
        auto textos = [&](const std::vector<std::string>& v, const std::string& nome) {
            std::vector<LLVMValueRef> ps;
            for (std::size_t i = 0; i < v.size(); ++i) ps.push_back(textoC(v[i], nome + "." + std::to_string(i)));
            if (ps.empty()) return LLVMConstNull(tPtr);
            LLVMValueRef arr = LLVMConstArray2(tPtr, ps.data(), ps.size());
            return global(LLVMTypeOf(arr), nome, arr, true);
        };
        for (std::size_t i = 0; i < m.structs.size(); ++i) {
            const std::string n = "cz.ds." + std::to_string(i);
            LLVMValueRef d = chamaRt(rt("cz_desc_struct", tPtr, {tPtr, tPtr, tI64}),
                                     {textoC(m.structs[i].nome, n + ".nome"), textos(m.structs[i].campos, n + ".campos"),
                                      ci64(static_cast<std::int64_t>(m.structs[i].campos.size()))});
            LLVMBuildStore(b, d, descs_struct[i]);
        }
        for (std::size_t i = 0; i < m.classes.size(); ++i) {
            LLVMValueRef d = chamaRt(rt("cz_desc_classe", tPtr, {tPtr, tI32, tI32}),
                                     {textoC(m.classes[i].nome, "cz.dc." + std::to_string(i) + ".nome"),
                                      ci32(static_cast<std::int64_t>(i)), ci32(m.classes[i].campos)});
            LLVMBuildStore(b, d, descs_classe[i]);
        }
        for (std::size_t i = 0; i < m.enums.size(); ++i) {
            const std::string n = "cz.de." + std::to_string(i);
            LLVMValueRef d = chamaRt(rt("cz_desc_enum", tPtr, {tPtr, tPtr, tI64}),
                                     {textoC(m.enums[i].nome, n + ".nome"), textos(m.enums[i].valores, n + ".valores"),
                                      ci64(static_cast<std::int64_t>(m.enums[i].valores.size()))});
            LLVMBuildStore(b, d, descs_enum[i]);
        }
        for (std::size_t i = 0; i < m.tipos.size(); ++i) {   // os parâmetros vêm antes na tabela
            const cir::DescTipo& t = m.tipos[i];
            LLVMValueRef params = LLVMConstNull(tPtr);
            if (!t.params.empty()) {
                LLVMTypeRef tv = LLVMArrayType2(tPtr, t.params.size());
                params = reservaMain(tv);
                for (std::size_t k = 0; k < t.params.size(); ++k) {
                    LLVMValueRef idx[] = {ci64(0), ci64(static_cast<std::int64_t>(k))};
                    LLVMBuildStore(b, LLVMBuildLoad2(b, tPtr, tipos[t.params[k]], ""),
                                   LLVMBuildInBoundsGEP2(b, tv, params, idx, 2, ""));
                }
            }
            LLVMValueRef r = chamaRt(rt("cz_tipo", tPtr, {tI8, tPtr, tPtr, tI64}),
                                     {LLVMConstInt(tI8, t.kind, 0), textoC(t.nome, "cz.t." + std::to_string(i) + ".nome"),
                                      params, ci64(static_cast<std::int64_t>(t.params.size()))});
            LLVMBuildStore(b, r, tipos[i]);
        }
        LLVMBasicBlockRef roda = LLVMAppendBasicBlockInContext(ctx, f, "roda");
        LLVMBasicBlockRef termina = LLVMAppendBasicBlockInContext(ctx, f, "termina");
        if (m.init >= 0) {   // erro na inicialização dos const: a main não roda
            LLVMBuildCall2(b, tfns[m.init], fns[m.init], nullptr, 0, "");
            LLVMValueRef e = LLVMBuildLoad2(b, tPtr, gExec, "erro");
            LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, e, LLVMConstNull(tPtr), ""), termina, roda);
        } else {
            LLVMBuildBr(b, roda);
        }
        LLVMPositionBuilderAtEnd(b, roda);
        if (m.funcoes[m.main].params.empty()) {
            LLVMBuildCall2(b, tfns[m.main], fns[m.main], nullptr, 0, "");
        } else {   // main(list<string> args): os argumentos depois do programa, emprestados
            LLVMValueRef args = chamaRt(rt("cz_args", tPtr, {}), {});
            LLVMBuildCall2(b, tfns[m.main], fns[m.main], &args, 1, "");
            chamaRt(rt("cz_solta", tVoid, {tPtr}), {args});
        }
        LLVMBuildBr(b, termina);
        LLVMPositionBuilderAtEnd(b, termina);
        chamaRt(rt("cz_gc_estatisticas", tVoid, {}), {});   // CINZA_GC_STATS=1, como a CVM
        if (m.fim >= 0) LLVMBuildCall2(b, tfns[m.fim], fns[m.fim], nullptr, 0, "");
        LLVMBuildRet(b, chamaRt(rt("cz_fim", tI32, {}), {}));
    }
};

Emissor::Emissor(bool otimiza, bool asan) : impl(std::make_unique<Impl>()) {
    impl->otimiza = otimiza;
    impl->asan = asan;
}

Emissor::~Emissor() {
    if (impl->b) LLVMDisposeBuilder(impl->b);
    if (impl->be) LLVMDisposeBuilder(impl->be);
    if (impl->dl) LLVMDisposeTargetData(impl->dl);
    if (impl->mod) LLVMDisposeModule(impl->mod);
    if (impl->tm) LLVMDisposeTargetMachine(impl->tm);
    if (impl->ctx) LLVMContextDispose(impl->ctx);
}

bool Emissor::gera(const cir::Modulo& m, std::string& erro) {
    Impl& e = *impl;
    if (!e.prepara(erro)) return false;
    if (m.main < 0) { erro = "o programa não tem main"; return false; }
    e.cm = &m;
    try {
        if (!m.literais.empty()) {
            LLVMTypeRef tv = LLVMArrayType2(e.tPtr, m.literais.size());
            e.tabela_literais = e.global(tv, "cz.literais", LLVMConstNull(tv), false);
            for (std::size_t i = 0; i < m.literais.size(); ++i) {
                LLVMValueRef idx[] = {e.ci64(0), e.ci64(static_cast<std::int64_t>(i))};
                e.literais.push_back(LLVMConstInBoundsGEP2(tv, e.tabela_literais, idx, 2));
            }
        }
        for (std::size_t i = 0; i < m.nativas.size(); ++i) {
            const std::string n = std::to_string(i);
            e.nomes_nativas.emplace_back(e.textoC(m.nativas[i].modulo, "cz.nat." + n + ".modulo"),
                                         e.textoC(m.nativas[i].nome, "cz.nat." + n + ".nome"));
            e.nativas.push_back(e.global(e.tPtr, "cz.nat." + n, LLVMConstNull(e.tPtr), false));
        }
        for (std::size_t i = 0; i < m.globais.size(); ++i)
            e.globais.push_back(e.global(e.tipo(m.globais[i]), "cz.global." + std::to_string(i),
                                         LLVMConstNull(e.tipo(m.globais[i])), false));
        auto ptrs = [&](std::vector<LLVMValueRef>& v, std::size_t n, const std::string& nome) {
            for (std::size_t i = 0; i < n; ++i)
                v.push_back(e.global(e.tPtr, nome + std::to_string(i), LLVMConstNull(e.tPtr), false));
        };
        ptrs(e.descs_struct, m.structs.size(), "cz.desc.s.");
        ptrs(e.descs_classe, m.classes.size(), "cz.desc.c.");
        ptrs(e.descs_enum, m.enums.size(), "cz.desc.e.");
        ptrs(e.tipos, m.tipos.size(), "cz.tipo.");
        e.quadro_vazio = e.global(LLVMGetTypeByName2(e.ctx, "CzQuadro"), "cz.quadro.vazio",
                                  LLVMConstNull(LLVMGetTypeByName2(e.ctx, "CzQuadro")), false);
        for (std::size_t i = 0; i < m.funcoes.size(); ++i) e.declaraFuncao(i, m.funcoes[i]);
        for (std::size_t i = 0; i < m.tabelas.size(); ++i) {   // a função de cada classe (ou nulo)
            std::vector<LLVMValueRef> fs;
            for (int fi : m.tabelas[i].funcoes) fs.push_back(fi < 0 ? LLVMConstNull(e.tPtr) : e.fns[fi]);
            LLVMValueRef arr = LLVMConstArray2(e.tPtr, fs.data(), fs.size());
            e.tabelas.push_back(e.global(LLVMTypeOf(arr), "cz.iface." + std::to_string(i), arr, true));
            std::vector<LLVMValueRef> ds;
            for (int fi : m.tabelas[i].funcoes) ds.push_back(fi < 0 ? LLVMConstNull(e.tPtr) : e.descs[fi]);
            LLVMValueRef ad = LLVMConstArray2(e.tPtr, ds.data(), ds.size());
            e.tabelas_desc.push_back(e.global(LLVMTypeOf(ad), "cz.iface." + std::to_string(i) + ".desc", ad, true));
        }
        e.pode_falhar = podemFalhar(m);
        for (std::size_t i = 0; i < m.funcoes.size(); ++i) e.funcao(i, m.funcoes[i]);
        e.mainC(m);
    } catch (const ErroEmissor& x) {
        erro = x.msg;
        return false;
    }

    char* msg = nullptr;
    if (LLVMVerifyModule(e.mod, LLVMReturnStatusAction, &msg)) {
        erro = std::string("IR inválido: ") + (msg ? msg : "");
        LLVMDisposeMessage(msg);
        return false;
    }
    LLVMDisposeMessage(msg);
    return true;
}

bool Emissor::otimiza(std::string& erro) {
    Impl& e = *impl;
    if (e.otimizado) return true;
    e.otimizado = true;
    LLVMPassBuilderOptionsRef opcoes = LLVMCreatePassBuilderOptions();
    if (e.asan) {   // o passo do AddressSanitizer só instrumenta as funções marcadas
        for (LLVMValueRef f = LLVMGetFirstFunction(e.mod); f; f = LLVMGetNextFunction(f))
            if (!LLVMIsDeclaration(f)) e.atributo(f, "sanitize_address");
    }
    const char* passes = e.otimiza ? (e.asan ? "default<O2>,asan" : "default<O2>")
                                   : (e.asan ? "default<O0>,asan" : "default<O0>");
    LLVMErrorRef falha = LLVMRunPasses(e.mod, passes, e.tm, opcoes);
    LLVMDisposePassBuilderOptions(opcoes);
    if (falha) {
        char* msg = LLVMGetErrorMessage(falha);
        erro = std::string("passes do LLVM: ") + msg;
        LLVMDisposeErrorMessage(msg);
        return false;
    }
#ifdef CINZA_CONFERE_REFS   // build de depuração: o IR continua válido depois de otimizar
    char* ver = nullptr;
    if (LLVMVerifyModule(e.mod, LLVMReturnStatusAction, &ver)) {
        erro = std::string("IR inválido depois dos passes: ") + (ver ? ver : "");
        LLVMDisposeMessage(ver);
        return false;
    }
    LLVMDisposeMessage(ver);
#endif
    return true;
}

namespace {

bool gravaObjeto(LLVMTargetMachineRef tm, LLVMModuleRef mod, const std::string& caminho, std::string& erro) {
    char* msg = nullptr;
    std::string arquivo = caminho;   // a llvm-c pede char* (UTF-8)
    if (LLVMTargetMachineEmitToFile(tm, mod, arquivo.data(), LLVMObjectFile, &msg)) {
        erro = std::string("geração do objeto: ") + (msg ? msg : "");
        LLVMDisposeMessage(msg);
        return false;
    }
    return true;
}

bool temAtributo(LLVMValueRef f, const char* nome) {
    const unsigned k = LLVMGetEnumAttributeKindForName(nome, std::strlen(nome));
    return LLVMGetEnumAttributeAtIndex(f, LLVMAttributeFunctionIndex, k) != nullptr;
}

// Tira o corpo da função: ela vira a declaração do que está no outro objeto
void apagaCorpo(LLVMValueRef f) {
    std::vector<LLVMValueRef> instrs;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f); bb; bb = LLVMGetNextBasicBlock(bb))
        for (LLVMValueRef i = LLVMGetFirstInstruction(bb); i; i = LLVMGetNextInstruction(i)) {
            if (LLVMGetTypeKind(LLVMTypeOf(i)) != LLVMVoidTypeKind)
                LLVMReplaceAllUsesWith(i, LLVMGetUndef(LLVMTypeOf(i)));
            instrs.push_back(i);
        }
    for (LLVMValueRef i : instrs) LLVMInstructionEraseFromParent(i);
    while (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(f)) LLVMDeleteBasicBlock(bb);
    LLVMSetLinkage(f, LLVMExternalLinkage);
}

bool local(LLVMValueRef g) {
    const LLVMLinkage l = LLVMGetLinkage(g);
    return l == LLVMInternalLinkage || l == LLVMPrivateLinkage;
}

}  // namespace

// As funções enormes vão com optnone (Impl::funcao), mas a geração de código do -O2 ainda
// roda nelas — e o alocador de registradores dele é quadrático com dezenas de milhares de
// blocos (um main de 50 mil linhas passava de 100 s; em -O0, 14 s). Então, depois dos passes
// (as assinaturas e as convenções de chamada já estão fixadas), o módulo se divide em dois:
// o primeiro fica com tudo menos o corpo das enormes, o segundo só com elas (e as
// declarações do resto), gerado em -O0. Os nomes locais ficam externos para os dois objetos
// se enxergarem — só dentro do executável, que não exporta nada.
bool Emissor::emiteObjeto(const std::string& caminho, std::vector<std::string>& objetos, std::string& erro) {
    Impl& e = *impl;
    if (!otimiza(erro)) return false;
    std::vector<std::string> enormes;
    if (e.otimiza && !e.asan)   // no ASan (depuração) o módulo fica inteiro: ele cria construtores
        for (LLVMValueRef f = LLVMGetFirstFunction(e.mod); f; f = LLVMGetNextFunction(f))
            if (!LLVMIsDeclaration(f) && temAtributo(f, "optnone")) {
                std::size_t n = 0;
                enormes.emplace_back(LLVMGetValueName2(f, &n), n);
            }
    if (enormes.empty()) {
        if (!gravaObjeto(e.tm, e.mod, caminho, erro)) return false;
        objetos.push_back(caminho);
        return true;
    }

    std::size_t sem_nome = 0;
    auto externo = [&](LLVMValueRef g) {
        if (LLVMIsDeclaration(g) || !local(g)) return;
        std::size_t n = 0;
        LLVMGetValueName2(g, &n);
        if (n == 0) {
            const std::string nome = "cz.sem.nome." + std::to_string(sem_nome++);
            LLVMSetValueName2(g, nome.data(), nome.size());
        }
        LLVMSetLinkage(g, LLVMExternalLinkage);
    };
    for (LLVMValueRef g = LLVMGetFirstGlobal(e.mod); g; g = LLVMGetNextGlobal(g)) externo(g);
    for (LLVMValueRef f = LLVMGetFirstFunction(e.mod); f; f = LLVMGetNextFunction(f)) externo(f);

    LLVMModuleRef grandes = LLVMCloneModule(e.mod);
    for (const std::string& nome : enormes) apagaCorpo(LLVMGetNamedFunction(e.mod, nome.c_str()));
    std::vector<LLVMValueRef> outras;
    for (LLVMValueRef f = LLVMGetFirstFunction(grandes); f; f = LLVMGetNextFunction(f))
        if (!LLVMIsDeclaration(f) && !temAtributo(f, "optnone")) outras.push_back(f);
    for (LLVMValueRef f : outras) apagaCorpo(f);
    for (LLVMValueRef g = LLVMGetFirstGlobal(grandes); g; g = LLVMGetNextGlobal(g))
        if (!LLVMIsDeclaration(g)) LLVMSetInitializer(g, nullptr);   // definidos no primeiro

    char* msg = nullptr;
    for (LLVMModuleRef m : {e.mod, grandes})
        if (LLVMVerifyModule(m, LLVMReturnStatusAction, &msg)) {
            erro = std::string("IR inválido ao dividir o módulo: ") + (msg ? msg : "");
            LLVMDisposeMessage(msg);
            LLVMDisposeModule(grandes);
            return false;
        } else {
            LLVMDisposeMessage(msg);
            msg = nullptr;
        }
    LLVMTargetMachineRef tm0 = criaMaquina(false, erro);
    const std::string segundo = caminho.substr(0, caminho.size() - 2) + ".grandes.o";
    const bool ok = tm0 && gravaObjeto(e.tm, e.mod, caminho, erro) && gravaObjeto(tm0, grandes, segundo, erro);
    if (tm0) LLVMDisposeTargetMachine(tm0);
    LLVMDisposeModule(grandes);
    if (!ok) return false;
    objetos.push_back(caminho);
    objetos.push_back(segundo);
    return true;
}

std::string Emissor::textoIR() const {
    char* s = LLVMPrintModuleToString(impl->mod);
    std::string r = s;
    LLVMDisposeMessage(s);
    return r;
}

std::string Emissor::versaoLLVM() {
    unsigned maior = 0, menor = 0, correcao = 0;
    LLVMGetVersion(&maior, &menor, &correcao);
    return std::to_string(maior) + "." + std::to_string(menor) + "." + std::to_string(correcao);
}

std::string Emissor::alvo() { return triple(); }

bool Emissor::confereLayout(std::string& relato) {
    std::string erro;
    LLVMTargetMachineRef tm = criaMaquina(false, erro);
    if (!tm) { relato = erro; return false; }
    LLVMTargetDataRef dl = LLVMCreateTargetDataLayout(tm);
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx), i16 = LLVMInt16TypeInContext(ctx),
                i32 = LLVMInt32TypeInContext(ctx), i64 = LLVMInt64TypeInContext(ctx),
                ptr = LLVMPointerTypeInContext(ctx, 0);
    bool ok = true;
    auto confere = [&](const char* nome, std::vector<LLVMTypeRef> campos, std::size_t tamanho,
                       std::vector<std::size_t> desloc) {
        LLVMTypeRef t = LLVMStructTypeInContext(ctx, campos.data(), static_cast<unsigned>(campos.size()), 0);
        const auto tam = LLVMABISizeOfType(dl, t);
        bool certo = tam == tamanho;
        for (std::size_t i = 0; i < desloc.size(); ++i)
            certo = certo && LLVMOffsetOfElement(dl, t, static_cast<unsigned>(i)) == desloc[i];
        relato += std::string(certo ? "ok    " : "ERRADO ") + nome + ": " + std::to_string(tam) +
                  " bytes no LLVM, " + std::to_string(tamanho) + " no runtime\n";
        ok = ok && certo;
        return t;
    };
    confere("CzValor", {i8, LLVMArrayType2(i8, 3), i32, i64}, sizeof(CzValor),
            {offsetof(CzValor, tipo), offsetof(CzValor, _r), offsetof(CzValor, extra), offsetof(CzValor, u)});
    confere("CzObjeto", {i32, i8, i8, i16, ptr, ptr, ptr}, sizeof(CzObjeto),
            {offsetof(CzObjeto, refs), offsetof(CzObjeto, gc_kind), offsetof(CzObjeto, marcas),
             offsetof(CzObjeto, _reservado), offsetof(CzObjeto, desc), offsetof(CzObjeto, ant),
             offsetof(CzObjeto, prox)});
    confere("CzTextoLiteral", {ptr, i64}, sizeof(CzTextoLiteral),
            {offsetof(CzTextoLiteral, bytes), offsetof(CzTextoLiteral, n)});
    confere("CzPos", {i32, i32, i32}, sizeof(CzPos),
            {offsetof(CzPos, arquivo), offsetof(CzPos, linha), offsetof(CzPos, coluna)});
    confere("CzFuncao", {ptr, i32, i32, i32}, sizeof(CzFuncao),
            {offsetof(CzFuncao, nome), offsetof(CzFuncao, arquivo), offsetof(CzFuncao, linha),
             offsetof(CzFuncao, coluna)});
    LLVMTypeRef quadro = confere("CzQuadro", {ptr, ptr}, sizeof(CzQuadro),
                                 {offsetof(CzQuadro, f), offsetof(CzQuadro, pos)});
    confere("CzExec", {ptr, i32, i32, LLVMArrayType2(quadro, CZ_PROFUNDIDADE)}, sizeof(CzExec),
            {offsetof(CzExec, erro), offsetof(CzExec, prof), offsetof(CzExec, _reservado),
             offsetof(CzExec, quadros)});
    // o acesso em linha aos campos (P10): um Value é o CzValor, e o primeiro ponteiro de um
    // std::vector<Value> é o início dos elementos
    {
        std::vector<Value> v(3);
        Value* const* primeiro = reinterpret_cast<Value* const*>(&v);
        const bool certo = sizeof(Value) == sizeof(CzValor) && *primeiro == v.data();
        relato += std::string(certo ? "ok    " : "ERRADO ") + "campos: std::vector<Value> começa pelo ponteiro dos elementos; "
                  "vetor no deslocamento " + std::to_string(DESLOC_CAMPOS_OBJ) + " (objeto) e " +
                  std::to_string(DESLOC_CAMPOS_STRUCT) + " (struct)\n";
        ok = ok && certo;
    }
    LLVMContextDispose(ctx);
    LLVMDisposeTargetData(dl);
    LLVMDisposeTargetMachine(tm);
    return ok;
}

}  // namespace cinza::llvm
