// Interface C do runtime (libcinzart): o que o código nativo do cinzac chama.
// Convenções e contrato em cinzart.h e em llvm/DESENHO.md, seção 7.
#include "cinzart.h"
#include "../gc.h"
#include "../natives.h"
#include "../plataforma.h"
#include "../operacoes.h"
#include "../runtime_error.h"
#include "../source_files.h"
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

// O stack trace da sombra da pilha, no formato da CVM (VM::trace): do quadro mais
// interno para fora; o mais interno com a linha do erro, cada um de fora com a linha
// da chamada em andamento nele
std::vector<std::string> traceAtual(int linha_erro) {
    std::vector<std::string> t;
    for (std::int32_t k = cz_exec.prof; k-- > 0;) {
        const CzQuadro& q = cz_exec.quadros[k];
        const int linha = k == cz_exec.prof - 1 ? linha_erro : (q.pos ? q.pos->linha : 0);
        t.push_back("em " + std::string(q.f->nome) + " (" +
                    formatPosition({q.f->arquivo, linha, 0}) + ")");
    }
    constexpr std::size_t mostrar = 10;
    if (t.size() > mostrar + 1) {
        const std::size_t omitidas = t.size() - mostrar - 1;
        std::vector<std::string> curto(t.begin(), t.begin() + mostrar);
        curto.push_back("... (" + std::to_string(omitidas) + " chamadas omitidas)");
        curto.push_back(t.back());
        t = std::move(curto);
    }
    return t;
}

// RuntimeError → o erro pendente (substitui um que já estivesse pendente). Como o
// VM::fail: sem posição, ganha a da operação (`p`); sem trace, o da sombra da pilha
void defineErro(const RuntimeError& e, const CzPos* p) {
    auto ev     = novo<ErrorValue>();
    ev->kind    = e.kind;
    ev->message = e.message;
    ev->line    = e.line;
    ev->column  = e.column;
    ev->file_id = e.file_id;
    if (e.line == 0 && p) {
        ev->line    = p->linha;
        ev->column  = p->coluna;
        ev->file_id = p->arquivo;
    }
    ev->trace = e.trace.empty() ? traceAtual(static_cast<int>(ev->line)) : e.trace;
    if (cz_exec.erro) czRelease(cz_exec.erro);
    cz_exec.erro = ev.solta();
}

// Toda função cz_* roda o corpo aqui dentro: nenhuma exceção do C++ escapa
// (llvm/DESENHO.md 5.3). `p`: a posição de um erro que vier sem ela
template <typename F>
void fronteira(F&& corpo, const CzPos* p = nullptr) noexcept {
    try {
        try {
            corpo();
        } catch (const RuntimeError& e) {
            defineErro(e, p);   // pode alocar: um bad_alloc aqui cai no bloco de fora
        }
    } catch (const std::bad_alloc&) {
        fatalMemoria();
    } catch (const std::exception& e) {
        fatalInterno(e.what());
    } catch (...) {
        fatalInterno("exceção desconhecida");
    }
}

// Os bits do campo `u` de um CzValor como Value do tipo `kind`
Value valorDeBits(std::uint8_t kind, std::uint64_t bits) {
    CzValor v{};
    v.tipo = kind;
    v.u.i64 = static_cast<std::int64_t>(bits);
    return Value::empresta(v);   // números: nada a reter
}

TypeRef tipoDoKind(std::uint8_t kind) {
    auto& t = TypeContext::instance();
    switch (static_cast<Value::Kind>(kind)) {
        case Value::Kind::I32: return t.i32Type();
        case Value::Kind::I64: return t.i64Type();
        case Value::Kind::F32: return t.f32Type();
        case Value::Kind::F64: return t.f64Type();
        default: fatalInterno("conversão para um tipo que não é número");
    }
}

// A operação do caminho de erro deveria ter falhado (o código gerado só a chama quando
// o caminho rápido detectou o erro)
[[noreturn]] void naoFalhou(const char* o_que) { fatalInterno(o_que); }

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
                          CzValor* resultado, const CzPos* p) noexcept {
    *resultado = Value().entrega();
    fronteira([&] {
        // Os argumentos viram Value (cópia com retain, llvm/DESENHO.md 2.3): a NativeFn de
        // hoje é chamada sem mudança
        std::vector<Value> vals;
        vals.reserve(static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i) vals.push_back(Value::empresta(args[i]));
        Value r = static_cast<const NativeFn*>(fn)->impl(std::span<const Value>(vals.data(), vals.size()));
        *resultado = r.entrega();
    }, p);
}

extern "C" void cz_solta_valor(CzValor* v) noexcept {
    fronteira([&] {
        Value morto = Value::toma(*v);
        *v = Value().entrega();
    });
}

extern "C" void cz_arquivos(const char* const* nomes, std::uint64_t n) noexcept {
    fronteira([&] {
        if (n == 0) return;
        setMainFile(nomes[0]);
        for (std::uint64_t i = 1; i < n; ++i) registerSourceFile(nomes[i]);
    });
}

extern "C" void cz_falha_binaria(std::uint32_t op, std::uint8_t kind, std::uint64_t a, std::uint64_t b,
                                 const CzPos* p) noexcept {
    fronteira([&] {
        applyBinaryOp(static_cast<TokenType>(op), valorDeBits(kind, a), valorDeBits(kind, b));
        naoFalhou("a conta do caminho de erro não falhou");
    }, p);
}

extern "C" void cz_falha_neg(std::uint8_t kind, std::uint64_t a, const CzPos* p) noexcept {
    fronteira([&] {
        negateOp(valorDeBits(kind, a));
        naoFalhou("a negação do caminho de erro não falhou");
    }, p);
}

extern "C" void cz_falha_conv(std::uint8_t de, std::uint64_t a, std::uint8_t para, const CzPos* p) noexcept {
    fronteira([&] {
        convertValue(valorDeBits(de, a), tipoDoKind(para));
        naoFalhou("a conversão do caminho de erro não falhou");
    }, p);
}

extern "C" void cz_falha_passo(const CzPos* p) noexcept {
    fronteira([&] {   // a mesma mensagem da nativa range e do RANGEPREP da CVM
        throw RuntimeError("ValueError: o passo de 'range' não pode ser 0");
    }, p);
}

extern "C" void cz_estouro(const CzFuncao* f) noexcept {
    fronteira([&] {   // como a CVM: na posição da declaração chamada
        throw RuntimeError("StackOverflowError",
                           "profundidade máxima de chamadas (" + std::to_string(CZ_PROFUNDIDADE) +
                               ") excedida ao chamar '" + f->nome + "'",
                           f->linha, f->coluna, f->arquivo);
    });
}

extern "C" void cz_conv_valor(CzValor* v, std::uint8_t kind, const CzPos* p) noexcept {
    fronteira([&] {
        Value atual = Value::toma(*v);
        *v = Value().entrega();
        *v = convertValue(atual, tipoDoKind(kind)).entrega();
    }, p);
}

extern "C" void cz_morre() noexcept {
    std::exit(cz_fim());
}
