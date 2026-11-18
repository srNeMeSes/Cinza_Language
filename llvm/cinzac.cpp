// cinzac — o compilador nativo da Cinza (llvm/DESENHO.md, seção 9):
//
//   cinzac prog.cinza -o prog.exe          compila e liga
//   cinzac -O2 prog.cinza -o prog.exe      com otimização
//   cinzac --emit-cir prog.cinza           mostra a CIR
//   cinzac --emit-llvm prog.cinza [-o p.ll] grava (ou mostra) o IR do LLVM
//   cinzac --confere-layout                confere as estruturas da interface (LLVM x GCC)
//
// O front-end é o mesmo do cinza (mesmos diagnósticos). Códigos de saída: 0 certo,
// 1 erro do programa ou da ligação, 75 recurso que o cinzac ainda não compila.
#include "../ast.h"
#include "../diagnostics.h"
#include "../module_loader.h"
#include "../plataforma.h"
#include "../semantic.h"
#include "../source_files.h"
#include "emissor.h"
#include "gera_cir.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace cinza;

namespace {

constexpr int NAO_SUPORTADO = 75;

void ajuda() {
    std::cout << "cinzac — compilador nativo da Cinza (LLVM)\n\n"
                 "Uso: cinzac [opções] arquivo.cinza [-o saída]\n\n"
                 "  -o ARQUIVO        executável (padrão: o nome do programa com .exe)\n"
                 "  -O2               com otimização\n"
                 "  --emit-cir        mostra a CIR e para\n"
                 "  --emit-llvm       grava o IR do LLVM (em -o, ou na saída) e para\n"
                 "  --manter-objeto   não apaga o .o depois de ligar\n"
                 "  --confere-layout  confere as estruturas da interface com o runtime\n";
}

bool leArquivo(const std::string& nome, std::string& conteudo) {
    std::ifstream f(caminhoUtf8(nome), std::ios::binary);
    if (!f) return false;
    std::ostringstream s;
    s << f.rdbuf();
    conteudo = s.str();
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    preparaConsole();
    const std::vector<std::string> args = argumentosUtf8(argc, argv);

    std::string arquivo, saida;
    bool otimiza = false, emit_cir = false, emit_llvm = false, manter_objeto = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-h" || a == "--help") { ajuda(); return 0; }
        if (a == "--confere-layout") {
            std::string relato;
            const bool ok = llvm::Emissor::confereLayout(relato);
            std::cout << relato;
            return ok ? 0 : 1;
        }
        if (a == "-o" && i + 1 < args.size()) { saida = args[++i]; continue; }
        if (a == "-O2") { otimiza = true; continue; }
        if (a == "-O0") { otimiza = false; continue; }
        if (a == "--emit-cir") { emit_cir = true; continue; }
        if (a == "--emit-llvm") { emit_llvm = true; continue; }
        if (a == "--manter-objeto") { manter_objeto = true; continue; }
        if (!a.empty() && a[0] == '-') {
            std::cerr << "Erro: opção desconhecida '" << a << "'.\n";
            ajuda();
            return 1;
        }
        if (!arquivo.empty()) {
            std::cerr << "Erro: mais de um arquivo ('" << arquivo << "' e '" << a << "').\n";
            return 1;
        }
        arquivo = a;
    }
    if (arquivo.empty()) { ajuda(); return 1; }

    // O front-end, como no cinza: o arquivo principal aparece como foi informado
    arquivo = textoCaminho(caminhoUtf8(arquivo));
    setMainFile(arquivo);
    std::string codigo;
    if (!leArquivo(arquivo, codigo)) {
        diagnostics().report("IOError", "não foi possível abrir o arquivo", SourceLocation{0, 0, 0});
        diagnostics().flush(std::cerr);
        return 1;
    }
    ModuleLoader carregador;
    if (!carregador.load(arquivo, codigo)) {
        diagnostics().flush(std::cerr);
        return 1;
    }
    std::vector<Module>& modulos = carregador.ordered();
    std::vector<ModuleUnit> unidades;
    for (auto& m : modulos) unidades.push_back(ModuleUnit{m.program.get(), m.prefix, m.imports, m.native});
    SemanticAnalyzer analisador;
    try {
        analisador.analyze(unidades);
    } catch (const SemanticError& e) {
        diagnostics().report("SemanticError", e.message, e.loc, e.notes);
        diagnostics().flush(std::cerr);
        return 1;
    }

    // AST → CIR
    cir::Modulo modulo;
    try {
        modulo = cir::gera(modulos);
    } catch (const cir::NaoSuportado& n) {
        std::cerr << (n.tok.line ? formatPosition(n.tok.loc()) + ": " : std::string())
                  << "[cinzac] ainda não suportado: " << n.o_que << "\n";
        return NAO_SUPORTADO;
    }
    if (emit_cir) {
        std::cout << cir::texto(modulo);
        return 0;
    }

    // CIR → LLVM → objeto
    llvm::Emissor emissor(otimiza);
    std::string erro;
    if (!emissor.gera(modulo, erro)) {
        std::cerr << "Erro interno do cinzac: " << erro << "\n";
        return 1;
    }
    if (emit_llvm) {   // o IR depois dos passes (o de -O2, com -O2)
        if (!emissor.otimiza(erro)) {
            std::cerr << "Erro interno do cinzac: " << erro << "\n";
            return 1;
        }
        if (saida.empty()) { std::cout << emissor.textoIR(); return 0; }
        std::ofstream f(caminhoUtf8(saida), std::ios::binary);
        f << emissor.textoIR();
        return f ? 0 : 1;
    }
    if (saida.empty()) saida = textoCaminho(caminhoUtf8(arquivo).replace_extension(".exe"));
    const std::string objeto = saida + ".o";
    if (!emissor.emiteObjeto(objeto, erro)) {
        std::cerr << "Erro interno do cinzac: " << erro << "\n";
        return 1;
    }

    // Ligação pelo g++ (o mesmo que compilou o runtime, por causa da libstdc++): a
    // libcinzart.a fica ao lado do cinzac. CINZA_GXX troca o g++.
    const std::filesystem::path pasta = caminhoUtf8(caminhoDoExecutavel()).parent_path();
    std::string gxx = "g++";
    ambienteUtf8("CINZA_GXX", gxx);
    const int ligou = executaProcesso({gxx, objeto, textoCaminho(pasta / "libcinzart.a"), "-o", saida,
                                       "-static-libstdc++", "-static-libgcc",
                                       "-Wl,--stack,67108864"});
    if (!manter_objeto) {
        std::error_code ec;
        std::filesystem::remove(caminhoUtf8(objeto), ec);
    }
    if (ligou != 0) {
        std::cerr << "Erro: a ligação com o " << gxx << " falhou"
                  << (ligou < 0 ? " (não foi possível executá-lo)" : "") << ".\n";
        return 1;
    }
    return 0;
}
