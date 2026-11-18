// Interface C do runtime (libcinzart): o que o código nativo do cinzac chama.
// Convenções e contrato em cinzart.h e em llvm/DESENHO.md, seção 7.
#include "cinzart.h"
#include "../gc.h"
#include "../natives.h"
#include "../plataforma.h"
#include "../runtime_error.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

using namespace cinza;

cinza::CzExec cz_exec{};

namespace {

std::vector<CzString*>   literais;     // imortais; soltos no cz_fim
std::vector<std::string> argumentos;   // main(list<string> args): P9

// Falta de memória: erro fatal, como na CVM, com texto fixo e sem alocar
[[noreturn]] void fatalMemoria() noexcept {
    std::fflush(stdout);
    std::fputs("Erro fatal: memória insuficiente para continuar o programa\n", stderr);
    std::_Exit(1);
}

[[noreturn]] void fatalInterno(const char* o_que) noexcept {
    std::fflush(stdout);
    std::fprintf(stderr, "Erro interno do runtime: %s\n", o_que);
    std::_Exit(1);
}

// RuntimeError → o erro pendente (substitui um que já estivesse pendente)
void defineErro(const RuntimeError& e) {
    auto ev     = novo<ErrorValue>();
    ev->kind    = e.kind;
    ev->message = e.message;
    ev->line    = e.line;
    ev->column  = e.column;
    ev->trace   = e.trace;
    ev->file_id = e.file_id;
    if (cz_exec.erro) czRelease(cz_exec.erro);
    cz_exec.erro = ev.solta();
}

// Toda função cz_* roda o corpo aqui dentro: nenhuma exceção do C++ escapa
// (llvm/DESENHO.md 5.3)
template <typename F>
void fronteira(F&& corpo) noexcept {
    try {
        try {
            corpo();
        } catch (const RuntimeError& e) {
            defineErro(e);   // pode alocar: um bad_alloc aqui cai no bloco de fora
        }
    } catch (const std::bad_alloc&) {
        fatalMemoria();
    } catch (const std::exception& e) {
        fatalInterno(e.what());
    } catch (...) {
        fatalInterno("exceção desconhecida");
    }
}

}  // namespace

extern "C" void cz_inicio(int argc, char** argv) noexcept {
    fronteira([&] {
        preparaConsole();
        std::ios::sync_with_stdio(false);   // como o cinza: a saída vai pelo std::cout
        argumentos = argumentosUtf8(argc, argv);
    });
}

extern "C" int cz_fim() noexcept {
    int codigo = 0;
    fronteira([&] {
        std::cout.flush();
        if (cz_exec.erro) {
            auto* e = static_cast<ErrorValue*>(cz_exec.erro);
            if (e->kind == EXIT_KIND) {
                codigo = std::stoi(e->message);
            } else {   // erro não tratado: o mesmo relato da CVM (tipo, mensagem, stack trace)
                const int linha = static_cast<int>(e->line);
                diagnostics().report(e->kind, e->message,
                                     SourceLocation{linha ? e->file_id : -1, linha,
                                                    static_cast<int>(e->column)},
                                     e->trace);
                diagnostics().flush(std::cerr);
                codigo = 1;
            }
            czRelease(cz_exec.erro);
            cz_exec.erro = nullptr;
        }
        // Ciclos que sobraram primeiro (podem apontar para literais), depois os literais
        gcCollect();
        for (CzString* s : literais) {
            s->marcas &= static_cast<std::uint8_t>(~marca::IMORTAL);
            czRelease(s);
        }
        literais.clear();
        // CINZA_VAZAMENTOS=1: nenhum objeto do runtime pode sobrar (llvm/DESENHO.md 2.6)
        if (const char* v = std::getenv("CINZA_VAZAMENTOS"); v && *v && gc_registro.objetos != 0) {
            std::cout.flush();
            std::cerr << "[vazamentos] " << gc_registro.objetos
                      << " objeto(s) do runtime ainda vivo(s) ao sair:";
            for (std::size_t k = 0; k < 7; ++k)
                if (gc_registro.por_tipo[k])
                    std::cerr << " " << nomeGcKind(static_cast<GcKind>(k)) << " "
                              << gc_registro.por_tipo[k];
            std::cerr << "\n";
            codigo = 70;
        }
    });
    std::cout.flush();
    return codigo;
}

extern "C" CzObjeto* cz_str_literal(const char* bytes, std::uint64_t n) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {
        auto s = novo<CzString>(std::string(bytes, static_cast<std::size_t>(n)));
        s->marcas |= marca::IMORTAL;
        literais.push_back(s.get());
        r = s.solta();
    });
    return r;
}

extern "C" const void* cz_nativa_resolve(const char* modulo, const char* nome) noexcept {
    const void* r = nullptr;
    fronteira([&] {
        if (!*modulo) {
            r = findPrelude(nome);
        } else if (const NativeModule* m = findNativeModule(modulo)) {
            for (const NativeFn& f : m->fns)
                if (f.name == nome) r = &f;
        }
    });
    return r;
}

extern "C" void cz_nativa(const void* fn, const CzValor* args, std::uint64_t n,
                          CzValor* resultado) noexcept {
    *resultado = Value().entrega();
    fronteira([&] {
        // Os argumentos viram Value (cópia com retain, llvm/DESENHO.md 2.3): a NativeFn de
        // hoje é chamada sem mudança
        std::vector<Value> vals;
        vals.reserve(static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i) vals.push_back(Value::empresta(args[i]));
        Value r = static_cast<const NativeFn*>(fn)->impl(std::span<const Value>(vals.data(), vals.size()));
        *resultado = r.entrega();
    });
}

extern "C" void cz_solta_valor(CzValor* v) noexcept {
    fronteira([&] {
        Value morto = Value::toma(*v);
        *v = Value().entrega();
    });
}
