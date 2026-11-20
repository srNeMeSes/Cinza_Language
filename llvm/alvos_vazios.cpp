// Só o alvo x86-64 dentro do cinzac (Q5, com o LLVM estático). O driver do lld inicializa todos os
// alvos do LLVM (InitializeAllTargets...), por causa do LTO; o cinzac nunca lhe passa bitcode, só os
// objetos x86-64 que ele mesmo gerou. Com estas inicializações vazias, o código dos outros alvos
// (ARM, AMDGPU, RISC-V...) fica fora do executável: ~60 MB a menos. A lista é a de
// `llvm-config --targets-built` do LLVM do MSYS2, menos o X86.
#define CINZA_ALVO_VAZIO(T)                              \
    extern "C" void LLVMInitialize##T##TargetInfo() {}   \
    extern "C" void LLVMInitialize##T##Target() {}       \
    extern "C" void LLVMInitialize##T##TargetMC() {}     \
    extern "C" void LLVMInitialize##T##AsmParser() {}    \
    extern "C" void LLVMInitialize##T##AsmPrinter() {}   \
    extern "C" void LLVMInitialize##T##Disassembler() {} \
    extern "C" void LLVMInitialize##T##TargetMCA() {}

CINZA_ALVO_VAZIO(AArch64)
CINZA_ALVO_VAZIO(AMDGPU)
CINZA_ALVO_VAZIO(ARM)
CINZA_ALVO_VAZIO(AVR)
CINZA_ALVO_VAZIO(BPF)
CINZA_ALVO_VAZIO(Hexagon)
CINZA_ALVO_VAZIO(Lanai)
CINZA_ALVO_VAZIO(LoongArch)
CINZA_ALVO_VAZIO(Mips)
CINZA_ALVO_VAZIO(MSP430)
CINZA_ALVO_VAZIO(NVPTX)
CINZA_ALVO_VAZIO(PowerPC)
CINZA_ALVO_VAZIO(RISCV)
CINZA_ALVO_VAZIO(Sparc)
CINZA_ALVO_VAZIO(SPIRV)
CINZA_ALVO_VAZIO(SystemZ)
CINZA_ALVO_VAZIO(VE)
CINZA_ALVO_VAZIO(WebAssembly)
CINZA_ALVO_VAZIO(XCore)
