// CIR → LLVM pela llvm-c (LLVM 20.1.8, fixo: llvm/DESENHO.md 1 e 8.4). A sequência:
// alvo X86 → TargetMachine → triple e DataLayout do módulo → IR → verificação →
// passes → objeto.
//
// A tradução é mecânica: slots viram alloca (o mem2reg do -O2 os leva a registradores),
// temporários viram valores SSA, os const globais viram globais internos. Cada operação
// que pode falhar tem o caminho rápido em linha e, no desvio frio, a chamada ao runtime
// que refaz a operação pela regra da CVM (a mesma mensagem) e o cz_morre (P4: todo erro
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
#include <tuple>
#include <vector>

namespace cinza::llvm {

namespace {

constexpr const char* TRIPLE = "x86_64-w64-windows-gnu";   // o do GCC do projeto

void inicializaAlvo() {
    static bool feito = false;
    if (feito) return;
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();
    feito = true;
}

LLVMTargetMachineRef criaMaquina(bool otimiza, std::string& erro) {
    inicializaAlvo();
    LLVMTargetRef alvo = nullptr;
    char* msg = nullptr;
    if (LLVMGetTargetFromTriple(TRIPLE, &alvo, &msg)) {
        erro = msg ? msg : "alvo desconhecido";
        LLVMDisposeMessage(msg);
        return nullptr;
    }
    return LLVMCreateTargetMachine(alvo, TRIPLE, "x86-64", "",
                                   otimiza ? LLVMCodeGenLevelDefault : LLVMCodeGenLevelNone,
                                   LLVMRelocDefault, LLVMCodeModelDefault);
}

std::uint8_t kind(Value::Kind k) { return static_cast<std::uint8_t>(k); }

std::uint8_t kindDe(cir::Tipo t) {
    switch (t) {
        case cir::Tipo::I32:  return kind(Value::Kind::I32);
        case cir::Tipo::I64:  return kind(Value::Kind::I64);
        case cir::Tipo::F32:  return kind(Value::Kind::F32);
        case cir::Tipo::F64:  return kind(Value::Kind::F64);
        case cir::Tipo::Bool: return kind(Value::Kind::BOOL);
        case cir::Tipo::Str:  return kind(Value::Kind::STRING);
        default:              return kind(Value::Kind::VOID_VAL);
    }
}

// Erro de tradução (CIR que o emissor não sabe traduzir: erro interno do cinzac)
struct ErroEmissor {
    std::string msg;
};

}  // namespace

struct Emissor::Impl {
    bool                 otimiza = false;
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

    std::vector<LLVMValueRef> literais;   // @cz.lit.N: ptr para a CzString
    std::vector<LLVMValueRef> bytes;      // @cz.lit.N.bytes
    std::vector<LLVMValueRef> nativas;    // @cz.nat.N: ptr para a NativeFn
    std::vector<std::pair<LLVMValueRef, LLVMValueRef>> nomes_nativas;   // módulo, nome
    std::vector<LLVMValueRef> globais;    // os const do nível superior
    std::vector<LLVMValueRef> fns;        // as funções da Cinza
    std::vector<LLVMTypeRef>  tfns;
    std::vector<LLVMValueRef> descs;      // o CzFuncao de cada uma
    std::map<std::tuple<int, int, int>, LLVMValueRef> posicoes;   // os CzPos

    // A função em tradução
    const cir::Modulo* cm = nullptr;
    const cir::Funcao* cf = nullptr;
    LLVMValueRef fn{};
    std::vector<LLVMValueRef> temps, slots;
    std::vector<LLVMBasicBlockRef> blocos;
    LLVMValueRef quadro{};          // o quadro desta função na sombra da pilha
    LLVMBasicBlockRef morre{};      // erro pendente depois de uma nativa: cz_morre

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
        LLVMSetTarget(mod, TRIPLE);
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
        }
        return cir::Tipo::Void;
    }

    LLVMValueRef val(const cir::Operando& o) {
        switch (o.k) {
            case cir::Operando::K::Temp:    return temps[o.indice];
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
        }
        return nullptr;
    }

    // Um valor como os bits do campo `u` do CzValor (os altos zerados, como no Value)
    LLVMValueRef bits(LLVMValueRef v, cir::Tipo t) {
        switch (t) {
            case cir::Tipo::I32:  return LLVMBuildZExt(b, v, tI64, "");
            case cir::Tipo::I64:  return v;
            case cir::Tipo::F32:  return LLVMBuildZExt(b, LLVMBuildBitCast(b, v, tI32, ""), tI64, "");
            case cir::Tipo::F64:  return LLVMBuildBitCast(b, v, tI64, "");
            case cir::Tipo::Bool: return LLVMBuildZExt(b, v, tI64, "");
            case cir::Tipo::Str:  return LLVMBuildPtrToInt(b, v, tI64, "");
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
            case cir::Tipo::Str:  return LLVMBuildIntToPtr(b, u, tPtr, "");
            default:              throw ErroEmissor{"desembrulho para um tipo sem valor"};
        }
    }

    // Desvio frio para o caminho de erro: `falha` chama o runtime (que deixa o erro
    // pendente); depois, cz_morre. O código segue no bloco `ok`.
    template <typename F>
    void seFalha(LLVMValueRef cond, F&& falha) {
        LLVMBasicBlockRef bf = novoBloco("falha"), ok = novoBloco("ok");
        LLVMBuildCondBr(b, cond, bf, ok);
        LLVMPositionBuilderAtEnd(b, bf);
        falha();
        chamaRt(rt("cz_morre", tVoid, {}, true, true), {});
        LLVMBuildUnreachable(b);
        LLVMPositionBuilderAtEnd(b, ok);
    }
    // Depois de uma chamada ao runtime que pode falhar: erro pendente → cz_morre
    void confereErro() {
        if (!morre) {
            LLVMBasicBlockRef aqui = LLVMGetInsertBlock(b);
            morre = novoBloco("morre");
            LLVMPositionBuilderAtEnd(b, morre);
            chamaRt(rt("cz_morre", tVoid, {}, true, true), {});
            LLVMBuildUnreachable(b);
            LLVMPositionBuilderAtEnd(b, aqui);
        }
        LLVMValueRef e = LLVMBuildLoad2(b, tPtr, gExec, "erro");   // o campo 0
        LLVMBasicBlockRef ok = novoBloco("ok");
        LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, e, LLVMConstNull(tPtr), ""), morre, ok);
        LLVMPositionBuilderAtEnd(b, ok);
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
            case Op::Embrulha: temps[in.dst] = embrulha(in.args[0]); break;
            case Op::Desembrulha:
                temps[in.dst] = deBits(LLVMBuildExtractValue(b, val(in.args[0]), 3, ""), cf->temps[in.dst]);
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
                chamaRt(rt("cz_retem", tVoid, {tPtr}), {val(in.args[0])});
                break;
            case Op::Solta: {
                if (tipoOp(in.args[0]) == cir::Tipo::Str) {   // nulo (slot vazio): nada a soltar
                    LLVMValueRef p = val(in.args[0]);
                    LLVMBasicBlockRef sim = novoBloco("solta"), segue = novoBloco("soltou");
                    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, p, LLVMConstNull(tPtr), ""), sim, segue);
                    LLVMPositionBuilderAtEnd(b, sim);
                    chamaRt(rt("cz_solta", tVoid, {tPtr}), {p});
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
            case Op::Chama: {
                if (quadro)   // a posição da chamada, para o stack trace
                    LLVMBuildStore(b, posicao(in.pos), LLVMBuildStructGEP2(b, tQuadro(), quadro, 1, ""));
                std::vector<LLVMValueRef> args;
                for (const auto& a : in.args) args.push_back(val(a));
                LLVMValueRef r = LLVMBuildCall2(b, tfns[in.aux], fns[in.aux], args.data(),
                                                static_cast<unsigned>(args.size()), "");
                if (cm->funcoes[in.aux].ret != cir::Tipo::Void) temps[in.dst] = r;
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
                if (quadro) {   // sai da sombra da pilha
                    LLVMValueRef pp = LLVMBuildStructGEP2(b, tExec, gExec, 1, "");
                    LLVMValueRef prof = LLVMBuildLoad2(b, tI32, pp, "prof");
                    LLVMBuildStore(b, LLVMBuildSub(b, prof, ci32(1), ""), pp);
                }
                if (in.args.empty()) LLVMBuildRetVoid(b);
                else LLVMBuildRet(b, val(in.args[0]));
                break;
            case Op::Inalcancavel: LLVMBuildUnreachable(b); break;
        }
    }

    LLVMTypeRef tQuadro() { return LLVMGetTypeByName2(ctx, "CzQuadro"); }

    // call.rt: a assinatura em C sai da tabela da CIR (cir::infoRt) — `valor` por
    // ponteiro, o resultado `valor` por um ponteiro de saída, a posição no fim das que
    // podem falhar; bool volta como int32
    LLVMValueRef chamaRuntime(const cir::Instr& in) {
        const cir::InfoRt& info = cir::infoRt(static_cast<cir::Rt>(in.aux));
        std::vector<LLVMTypeRef> ps;
        std::vector<LLVMValueRef> args;
        for (std::size_t i = 0; i < info.params.size(); ++i) {
            const cir::Tipo t = info.params[i];
            if (t == cir::Tipo::Valor) {
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
        slots.clear();
        blocos.clear();
        quadro = nullptr;
        morre = nullptr;

        LLVMBasicBlockRef entrada = novoBloco("entrada");
        LLVMPositionBuilderAtEnd(be, entrada);
        for (cir::Tipo t : f.slots) slots.push_back(reserva(tipo(t), "s"));
        LLVMBasicBlockRef prologo = novoBloco("prologo");
        for (const cir::Bloco& bl : f.blocos) blocos.push_back(novoBloco(bl.nome.c_str()));

        LLVMPositionBuilderAtEnd(b, prologo);
        for (std::size_t s = f.params.size(); s < f.slots.size(); ++s)   // os slots com referência começam vazios
            if (f.slots[s] == cir::Tipo::Str) LLVMBuildStore(b, LLVMConstNull(tPtr), slots[s]);
        for (std::size_t p = 0; p < f.params.size(); ++p)
            LLVMBuildStore(b, LLVMGetParam(fn, static_cast<unsigned>(p)), slots[p]);
        if (!f.oculta) {
            // entra na sombra da pilha: a regra das 2000 chamadas (spec 5.8), conferida na
            // chamada como na CVM (a main conta como a primeira)
            LLVMValueRef pp = LLVMBuildStructGEP2(b, tExec, gExec, 1, "");
            LLVMValueRef prof = LLVMBuildLoad2(b, tI32, pp, "prof");
            seFalha(LLVMBuildICmp(b, LLVMIntSGE, prof, ci32(CZ_PROFUNDIDADE), "cheia"), [&] {
                chamaRt(rt("cz_estouro", tVoid, {tPtr}, true), {descs[i]});
            });
            LLVMValueRef idx[] = {ci32(0), ci32(3), prof};
            quadro = LLVMBuildInBoundsGEP2(b, tExec, gExec, idx, 3, "quadro");
            LLVMBuildStore(b, descs[i], LLVMBuildStructGEP2(b, tQuadro(), quadro, 0, ""));
            LLVMBuildStore(b, LLVMConstNull(tPtr), LLVMBuildStructGEP2(b, tQuadro(), quadro, 1, ""));
            LLVMBuildStore(b, LLVMBuildAdd(b, prof, ci32(1), ""), pp);
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
        LLVMPositionBuilderAtEnd(b, LLVMAppendBasicBlockInContext(ctx, f, "entrada"));
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

        for (std::size_t i = 0; i < m.literais.size(); ++i)
            LLVMBuildStore(b, chamaRt(rt("cz_str_literal", tPtr, {tPtr, tI64}),
                                      {bytes[i], ci64(static_cast<std::int64_t>(m.literais[i].size()))}),
                           literais[i]);
        for (std::size_t i = 0; i < m.nativas.size(); ++i)
            LLVMBuildStore(b, chamaRt(rt("cz_nativa_resolve", tPtr, {tPtr, tPtr}),
                                      {nomes_nativas[i].first, nomes_nativas[i].second}),
                           nativas[i]);
        if (m.init >= 0) LLVMBuildCall2(b, tfns[m.init], fns[m.init], nullptr, 0, "");
        LLVMBuildCall2(b, tfns[m.main], fns[m.main], nullptr, 0, "");
        if (m.fim >= 0) LLVMBuildCall2(b, tfns[m.fim], fns[m.fim], nullptr, 0, "");
        LLVMBuildRet(b, chamaRt(rt("cz_fim", tI32, {}), {}));
    }
};

Emissor::Emissor(bool otimiza) : impl(std::make_unique<Impl>()) { impl->otimiza = otimiza; }

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
        for (std::size_t i = 0; i < m.literais.size(); ++i) {
            e.bytes.push_back(e.textoC(m.literais[i], "cz.lit." + std::to_string(i) + ".bytes"));
            e.literais.push_back(e.global(e.tPtr, "cz.lit." + std::to_string(i), LLVMConstNull(e.tPtr), false));
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
        for (std::size_t i = 0; i < m.funcoes.size(); ++i) e.declaraFuncao(i, m.funcoes[i]);
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
    LLVMErrorRef falha = LLVMRunPasses(e.mod, e.otimiza ? "default<O2>" : "default<O0>", e.tm, opcoes);
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

bool Emissor::emiteObjeto(const std::string& caminho, std::string& erro) {
    Impl& e = *impl;
    if (!otimiza(erro)) return false;
    char* msg = nullptr;
    std::string arquivo = caminho;   // a llvm-c pede char* (UTF-8)
    if (LLVMTargetMachineEmitToFile(e.tm, e.mod, arquivo.data(), LLVMObjectFile, &msg)) {
        erro = std::string("geração do objeto: ") + (msg ? msg : "");
        LLVMDisposeMessage(msg);
        return false;
    }
    return true;
}

std::string Emissor::textoIR() const {
    char* s = LLVMPrintModuleToString(impl->mod);
    std::string r = s;
    LLVMDisposeMessage(s);
    return r;
}

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
                  " bytes no LLVM, " + std::to_string(tamanho) + " no GCC\n";
        ok = ok && certo;
        return t;
    };
    confere("CzValor", {i8, LLVMArrayType2(i8, 3), i32, i64}, sizeof(CzValor),
            {offsetof(CzValor, tipo), offsetof(CzValor, _r), offsetof(CzValor, extra), offsetof(CzValor, u)});
    confere("CzObjeto", {i32, i8, i8, i16, ptr, ptr, ptr}, sizeof(CzObjeto),
            {offsetof(CzObjeto, refs), offsetof(CzObjeto, gc_kind), offsetof(CzObjeto, marcas),
             offsetof(CzObjeto, _reservado), offsetof(CzObjeto, desc), offsetof(CzObjeto, ant),
             offsetof(CzObjeto, prox)});
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
    LLVMContextDispose(ctx);
    LLVMDisposeTargetData(dl);
    LLVMDisposeTargetMachine(tm);
    return ok;
}

}  // namespace cinza::llvm
