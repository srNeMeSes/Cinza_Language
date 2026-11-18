// CIR → LLVM pela llvm-c (LLVM 20.1.8, fixo: llvm/DESENHO.md 1 e 8.4). A sequência:
// alvo X86 → TargetMachine → triple e DataLayout do módulo → IR → verificação →
// passes → objeto.
#include "emissor.h"
#include "cinzart.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <cstring>
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

}  // namespace

struct Emissor::Impl {
    bool                 otimiza = false;
    bool                 otimizado = false;   // os passes já rodaram
    LLVMContextRef       ctx = nullptr;
    LLVMModuleRef        mod = nullptr;
    LLVMTargetMachineRef tm  = nullptr;
    LLVMTargetDataRef    dl  = nullptr;
    LLVMBuilderRef       b   = nullptr;

    LLVMTypeRef tI8 = nullptr, tI32 = nullptr, tI64 = nullptr, tPtr = nullptr, tVoid = nullptr;
    LLVMTypeRef tValor = nullptr;   // CzValor: { i8 tipo, [3 x i8], i32 extra, i64 u }

    // A interface do runtime (cinzart.h)
    LLVMTypeRef  fInicio{}, fFim{}, fLiteral{}, fResolve{}, fNativa{}, fSolta{};
    LLVMValueRef czInicio{}, czFim{}, czLiteral{}, czResolve{}, czNativa{}, czSolta{};

    std::vector<LLVMValueRef> literais;   // @cz.lit.N: ptr para a CzString
    std::vector<LLVMValueRef> bytes;      // @cz.lit.N.bytes
    std::vector<LLVMValueRef> nativas;    // @cz.nat.N: ptr para a NativeFn
    std::vector<std::pair<LLVMValueRef, LLVMValueRef>> nomes_nativas;   // módulo, nome

    LLVMValueRef declara(const char* nome, LLVMTypeRef& tipo, LLVMTypeRef ret,
                         std::vector<LLVMTypeRef> params) {
        tipo = LLVMFunctionType(ret, params.data(), static_cast<unsigned>(params.size()), 0);
        LLVMValueRef f = LLVMAddFunction(mod, nome, tipo);
        nounwind(f);
        return f;
    }

    void nounwind(LLVMValueRef f) {
        const unsigned k = LLVMGetEnumAttributeKindForName("nounwind", 8);
        LLVMAddAttributeAtIndex(f, LLVMAttributeFunctionIndex, LLVMCreateEnumAttribute(ctx, k, 0));
    }

    LLVMValueRef global(LLVMTypeRef t, const std::string& nome, LLVMValueRef ini, bool constante) {
        LLVMValueRef g = LLVMAddGlobal(mod, t, nome.c_str());
        LLVMSetInitializer(g, ini);
        LLVMSetGlobalConstant(g, constante);
        LLVMSetLinkage(g, constante ? LLVMPrivateLinkage : LLVMInternalLinkage);
        return g;
    }

    LLVMValueRef textoC(const std::string& s, const std::string& nome) {
        LLVMValueRef c = LLVMConstStringInContext(ctx, s.c_str(), static_cast<unsigned>(s.size()), 0);
        return global(LLVMTypeOf(c), nome, c, true);
    }

    bool prepara(std::string& erro) {
        ctx = LLVMContextCreate();
        mod = LLVMModuleCreateWithNameInContext("cinza", ctx);
        tm  = criaMaquina(otimiza, erro);
        if (!tm) return false;
        LLVMSetTarget(mod, TRIPLE);
        dl = LLVMCreateTargetDataLayout(tm);
        LLVMSetModuleDataLayout(mod, dl);
        b = LLVMCreateBuilderInContext(ctx);

        tI8 = LLVMInt8TypeInContext(ctx);
        tI32 = LLVMInt32TypeInContext(ctx);
        tI64 = LLVMInt64TypeInContext(ctx);
        tPtr = LLVMPointerTypeInContext(ctx, 0);
        tVoid = LLVMVoidTypeInContext(ctx);
        tValor = LLVMStructCreateNamed(ctx, "CzValor");
        LLVMTypeRef campos[] = {tI8, LLVMArrayType2(tI8, 3), tI32, tI64};
        LLVMStructSetBody(tValor, campos, 4, 0);

        czInicio  = declara("cz_inicio", fInicio, tVoid, {tI32, tPtr});
        czFim     = declara("cz_fim", fFim, tI32, {});
        czLiteral = declara("cz_str_literal", fLiteral, tPtr, {tPtr, tI64});
        czResolve = declara("cz_nativa_resolve", fResolve, tPtr, {tPtr, tPtr});
        czNativa  = declara("cz_nativa", fNativa, tVoid, {tPtr, tPtr, tI64, tPtr});
        czSolta   = declara("cz_solta_valor", fSolta, tVoid, {tPtr});
        return true;
    }

    // Um operando como o i64 do campo `u` do CzValor, e o tipo (Value::Kind) dele
    std::pair<std::uint8_t, LLVMValueRef> carga(const cir::Operando& o) {
        switch (o.k) {
            case cir::Operando::K::Literal: {
                LLVMValueRef p = LLVMBuildLoad2(b, tPtr, literais[o.indice], "lit");
                return {kind(Value::Kind::STRING), LLVMBuildPtrToInt(b, p, tI64, "")};
            }
            case cir::Operando::K::I32:   // os 32 bits baixos; os altos zerados, como o Value::i32
                return {kind(Value::Kind::I32),
                        LLVMConstInt(tI64, static_cast<std::uint32_t>(static_cast<std::int32_t>(o.i)), 0)};
            case cir::Operando::K::I64:
                return {kind(Value::Kind::I64), LLVMConstInt(tI64, static_cast<std::uint64_t>(o.i), 1)};
            case cir::Operando::K::F32: {
                const float f = static_cast<float>(o.f);
                std::uint32_t bits;
                std::memcpy(&bits, &f, sizeof bits);
                return {kind(Value::Kind::F32), LLVMConstInt(tI64, bits, 0)};
            }
            case cir::Operando::K::F64: {
                std::uint64_t bits;
                std::memcpy(&bits, &o.f, sizeof bits);
                return {kind(Value::Kind::F64), LLVMConstInt(tI64, bits, 0)};
            }
            case cir::Operando::K::Bool:
                return {kind(Value::Kind::BOOL), LLVMConstInt(tI64, o.i ? 1 : 0, 0)};
            case cir::Operando::K::Temp:
                break;
        }
        return {kind(Value::Kind::VOID_VAL), LLVMConstInt(tI64, 0, 0)};
    }

    void funcao(const cir::Funcao& f) {
        LLVMTypeRef tf = LLVMFunctionType(tVoid, nullptr, 0, 0);
        LLVMValueRef fn = LLVMAddFunction(mod, f.nome.c_str(), tf);
        LLVMSetLinkage(fn, LLVMInternalLinkage);
        nounwind(fn);

        // Entrada: um alloca por temporário e um vetor de argumentos por chamada nativa
        LLVMBasicBlockRef entrada = LLVMAppendBasicBlockInContext(ctx, fn, "entrada");
        LLVMPositionBuilderAtEnd(b, entrada);
        std::vector<LLVMValueRef> temps;
        for (cir::Tipo t : f.temps) {
            if (t != cir::Tipo::Valor) temps.push_back(LLVMBuildAlloca(b, tI64, "t"));
            else temps.push_back(LLVMBuildAlloca(b, tValor, "t"));
        }
        std::vector<LLVMBasicBlockRef> blocos;
        for (const cir::Bloco& bl : f.blocos)
            blocos.push_back(LLVMAppendBasicBlockInContext(ctx, fn, bl.nome.c_str()));
        // Os vetores de argumentos também ficam na entrada (fora de laços)
        std::vector<LLVMValueRef> vetores;
        for (const cir::Bloco& bl : f.blocos)
            for (const cir::Instr& in : bl.instr)
                if (in.op == cir::Op::ChamaNativa && !in.args.empty())
                    vetores.push_back(LLVMBuildAlloca(b, LLVMArrayType2(tValor, in.args.size()), "args"));
        LLVMBuildBr(b, blocos.front());

        std::size_t proximo_vetor = 0;
        for (std::size_t k = 0; k < f.blocos.size(); ++k) {
            LLVMPositionBuilderAtEnd(b, blocos[k]);
            for (const cir::Instr& in : f.blocos[k].instr) {
                switch (in.op) {
                    case cir::Op::Embrulha: {
                        auto [tipo, u] = carga(in.args[0]);
                        LLVMValueRef v = LLVMGetUndef(tValor);
                        v = LLVMBuildInsertValue(b, v, LLVMConstInt(tI8, tipo, 0), 0, "");
                        v = LLVMBuildInsertValue(b, v, LLVMConstNull(LLVMArrayType2(tI8, 3)), 1, "");
                        v = LLVMBuildInsertValue(b, v, LLVMConstInt(tI32, 0, 0), 2, "");
                        v = LLVMBuildInsertValue(b, v, u, 3, "");
                        LLVMBuildStore(b, v, temps[in.dst]);
                        break;
                    }
                    case cir::Op::ChamaNativa: {
                        LLVMValueRef vetor = LLVMConstNull(tPtr);
                        if (!in.args.empty()) {
                            vetor = vetores[proximo_vetor++];
                            LLVMTypeRef tv = LLVMArrayType2(tValor, in.args.size());
                            for (std::size_t a = 0; a < in.args.size(); ++a) {
                                LLVMValueRef idx[] = {LLVMConstInt(tI64, 0, 0), LLVMConstInt(tI64, a, 0)};
                                LLVMValueRef dest = LLVMBuildInBoundsGEP2(b, tv, vetor, idx, 2, "");
                                LLVMValueRef v = LLVMBuildLoad2(b, tValor, temps[in.args[a].indice], "");
                                LLVMBuildStore(b, v, dest);
                            }
                        }
                        LLVMValueRef fnptr = LLVMBuildLoad2(b, tPtr, nativas[in.nativa], "fn");
                        LLVMValueRef args[] = {fnptr, vetor, LLVMConstInt(tI64, in.args.size(), 0),
                                               temps[in.dst]};
                        LLVMBuildCall2(b, fNativa, czNativa, args, 4, "");
                        break;
                    }
                    case cir::Op::Solta: {
                        LLVMValueRef arg = temps[in.args[0].indice];
                        LLVMBuildCall2(b, fSolta, czSolta, &arg, 1, "");
                        break;
                    }
                    case cir::Op::Ret:
                        LLVMBuildRetVoid(b);
                        break;
                }
            }
        }
    }

    // int main(int argc, char** argv): partida do runtime, literais e nativas, a main da
    // Cinza (cz:::main) e o fim do runtime, que dá o código de saída
    void mainC(const cir::Modulo& m) {
        LLVMTypeRef params[] = {tI32, tPtr};
        LLVMValueRef fn = LLVMAddFunction(mod, "main", LLVMFunctionType(tI32, params, 2, 0));
        nounwind(fn);
        LLVMPositionBuilderAtEnd(b, LLVMAppendBasicBlockInContext(ctx, fn, "entrada"));
        LLVMValueRef argv[] = {LLVMGetParam(fn, 0), LLVMGetParam(fn, 1)};
        LLVMBuildCall2(b, fInicio, czInicio, argv, 2, "");
        for (std::size_t i = 0; i < m.literais.size(); ++i) {
            LLVMValueRef args[] = {bytes[i], LLVMConstInt(tI64, m.literais[i].size(), 0)};
            LLVMBuildStore(b, LLVMBuildCall2(b, fLiteral, czLiteral, args, 2, "s"), literais[i]);
        }
        for (std::size_t i = 0; i < m.nativas.size(); ++i) {
            LLVMValueRef args[] = {nomes_nativas[i].first, nomes_nativas[i].second};
            LLVMBuildStore(b, LLVMBuildCall2(b, fResolve, czResolve, args, 2, "n"), nativas[i]);
        }
        LLVMValueRef czMain = LLVMGetNamedFunction(mod, "cz:::main");
        LLVMBuildCall2(b, LLVMFunctionType(tVoid, nullptr, 0, 0), czMain, nullptr, 0, "");
        LLVMBuildRet(b, LLVMBuildCall2(b, fFim, czFim, nullptr, 0, "codigo"));
    }
};

Emissor::Emissor(bool otimiza) : impl(std::make_unique<Impl>()) { impl->otimiza = otimiza; }

Emissor::~Emissor() {
    if (impl->b) LLVMDisposeBuilder(impl->b);
    if (impl->dl) LLVMDisposeTargetData(impl->dl);
    if (impl->mod) LLVMDisposeModule(impl->mod);
    if (impl->tm) LLVMDisposeTargetMachine(impl->tm);
    if (impl->ctx) LLVMContextDispose(impl->ctx);
}

bool Emissor::gera(const cir::Modulo& m, std::string& erro) {
    Impl& e = *impl;
    if (!e.prepara(erro)) return false;
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
    for (const cir::Funcao& f : m.funcoes) e.funcao(f);
    e.mainC(m);

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
    };
    confere("CzValor", {i8, LLVMArrayType2(i8, 3), i32, i64}, sizeof(CzValor),
            {offsetof(CzValor, tipo), offsetof(CzValor, _r), offsetof(CzValor, extra), offsetof(CzValor, u)});
    confere("CzObjeto", {i32, i8, i8, i16, ptr, ptr, ptr}, sizeof(CzObjeto),
            {offsetof(CzObjeto, refs), offsetof(CzObjeto, gc_kind), offsetof(CzObjeto, marcas),
             offsetof(CzObjeto, _reservado), offsetof(CzObjeto, desc), offsetof(CzObjeto, ant),
             offsetof(CzObjeto, prox)});
    confere("CzExec", {ptr}, sizeof(CzExec), {offsetof(CzExec, erro)});
    LLVMContextDispose(ctx);
    LLVMDisposeTargetData(dl);
    LLVMDisposeTargetMachine(tm);
    return ok;
}

}  // namespace cinza::llvm
