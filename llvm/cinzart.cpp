// Interface C do runtime (libcinzart): o que o código nativo do cinzac chama.
// Convenções e contrato em cinzart.h e em llvm/DESENHO.md, seção 7.
#include "cinzart.h"
#include "../gc.h"
#include "../natives.h"
#include "../plataforma.h"
#include "../operacoes.h"
#include "../runtime_error.h"
#include "../source_files.h"
#include "../utf8.h"
#include <cstddef>

// Q5: o deslocamento do vetor de campos fica fixo em 40 — o cinzac e o runtime podem ser compilados
// com bibliotecas C++ diferentes (no Linux, o cinzac com a libstdc++ do LLVM do Alpine e o runtime
// com a libc++), e o acesso em linha aos campos (P10) supõe o mesmo layout dos dois lados
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(cinza::ClassInstance, fields) == 40 && offsetof(cinza::StructValue, fields) == 40,
              "o vetor de campos mudou de lugar: atualize o caminho rápido dos campos (llvm/DESENHO.md 8.1)");
#pragma GCC diagnostic pop
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

extern "C" CzObjeto* cz_args() noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {   // como o cinza: o que vem depois do arquivo (aqui, depois do executável)
        std::vector<Value> elems;
        for (std::size_t i = 1; i < argumentos.size(); ++i) elems.emplace_back(argumentos[i]);
        r = makeList(std::move(elems)).entrega().u.obj;
    });
    return r;
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

extern "C" void cz_literais(const CzTextoLiteral* tabela, std::uint64_t n, CzObjeto** destino) noexcept {
    fronteira([&] {
        literais.reserve(literais.size() + static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i) {
            auto s = novo<CzString>(std::string(tabela[i].bytes, static_cast<std::size_t>(tabela[i].n)));
            s->marcas |= marca::IMORTAL;
            literais.push_back(s.get());
            destino[i] = s.solta();
        }
    });
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

// ── Erros ─────────────────────────────────────────────────────────────────

extern "C" CzObjeto* cz_erro_novo(CzObjeto* tipo, CzObjeto* msg, std::int32_t arquivo, std::int32_t linha,
                                  std::int32_t coluna) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {   // o NEWERROR da CVM: na posição da criação, sem stack trace
        auto ev     = novo<ErrorValue>();
        ev->kind    = static_cast<CzString*>(tipo)->texto;
        ev->message = msg ? static_cast<CzString*>(msg)->texto : std::string();
        ev->line    = linha;
        ev->column  = coluna;
        ev->file_id = arquivo;
        r = ev.solta();
    });
    return r;
}

extern "C" void cz_erro_campo(CzObjeto* e, std::int32_t qual, CzValor* r) noexcept {
    *r = Value().entrega();
    fronteira([&] {   // o ERRFIELD da CVM
        const auto* ev = static_cast<const ErrorValue*>(e);
        switch (qual) {
            case 0:  *r = Value(displayName(ev->kind)).entrega(); break;
            case 1:  *r = Value(ev->message).entrega(); break;
            case 2:  *r = Value::i32(static_cast<std::int32_t>(ev->line)).entrega(); break;
            default: *r = Value::i32(static_cast<std::int32_t>(ev->column)).entrega(); break;
        }
    });
}

extern "C" std::int32_t cz_erro_casa(CzObjeto* tipo) noexcept {
    const auto* e = static_cast<const ErrorValue*>(cz_exec.erro);
    if (!e) return 0;
    const std::string& t = static_cast<CzString*>(tipo)->texto;
    return t == e->kind || (t == "Error" && e->kind != EXIT_KIND) ? 1 : 0;
}

extern "C" CzObjeto* cz_pega_erro() noexcept {
    CzObjeto* e = cz_exec.erro;
    cz_exec.erro = nullptr;
    return e;
}

extern "C" void cz_lanca(CzObjeto* e, const CzPos* p) noexcept {
    fronteira([&] {   // o THROW da CVM: relançar preserva tipo, posição e stack trace
        const auto* ev = static_cast<const ErrorValue*>(e);
        RuntimeError err(ev->kind, ev->message, static_cast<int>(ev->line), static_cast<int>(ev->column),
                         ev->file_id);
        err.trace = ev->trace;   // vazio num erro novo: o trace de agora
        throw err;
    }, p);
}

extern "C" void cz_relanca(CzObjeto* e, const CzPos*) noexcept {
    if (cz_exec.erro) czRelease(cz_exec.erro);
    cz_exec.erro = e;   // a posse volta ao estado
}

// ── Posse ─────────────────────────────────────────────────────────────────

extern "C" void cz_retem(CzObjeto* o) noexcept {
    if (o) czRetain(o);
}

extern "C" void cz_solta(CzObjeto* o) noexcept {
    if (o) czRelease(o);   // a destruição (czDestroi) não lança
}

extern "C" void cz_destroi(CzObjeto* o) noexcept {
    czDestroi(o);
}

// ── Strings ───────────────────────────────────────────────────────────────

namespace {

const std::string& textoDe(CzObjeto* s) { return static_cast<CzString*>(s)->texto; }

CzObjeto* novaString(std::string&& t) { return novo<CzString>(std::move(t)).solta(); }

// Um Value que é dono de uma string, para as regras da CVM que recebem Value
Value valorDeTexto(CzObjeto* s) {
    CzValor v{};
    v.tipo = static_cast<std::uint8_t>(Value::Kind::STRING);
    v.u.obj = s;
    return Value::empresta(v);
}

void metodo(std::int32_t b, const CzValor* o, std::initializer_list<const CzValor*> args, CzValor* r,
            const CzPos* p) {
    *r = Value().entrega();
    fronteira([&] {
        Value obj = Value::empresta(*o);
        Value vals[2];   // no máximo dois argumentos (cz_metodo0..2): sem alocar
        std::size_t n = 0;
        for (const CzValor* a : args) vals[n++] = Value::empresta(*a);
        *r = callBuiltin(static_cast<Builtin>(b), obj, std::span<const Value>(vals, n)).entrega();
    }, p);
}

}  // namespace

extern "C" CzObjeto* cz_concat(const CzValor* a, const CzValor* b) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {   // o CONCAT da CVM: string como está, o resto em texto
        const Value x = Value::empresta(*a), y = Value::empresta(*b);
        std::string s = x.kind() == Value::Kind::STRING ? x.asString() : x.toString();
        s += y.kind() == Value::Kind::STRING ? y.asString() : y.toString();
        r = novaString(std::move(s));
    });
    return r;
}

extern "C" CzObjeto* cz_texto(const CzValor* v) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {   // cast<string>: a regra comum (convertValue)
        // a string que a regra criou passa direto, com a posse
        r = convertValue(Value::empresta(*v), TypeContext::instance().stringType()).entrega().u.obj;
    });
    return r;
}

extern "C" std::int32_t cz_str_igual(CzObjeto* a, CzObjeto* b) noexcept {
    return textoDe(a) == textoDe(b) ? 1 : 0;
}

extern "C" std::int32_t cz_str_compara(CzObjeto* a, CzObjeto* b) noexcept {
    const std::string &x = textoDe(a), &y = textoDe(b);   // a ordem do LT_S/LE_S (std::string)
    return x < y ? -1 : (x == y ? 0 : 1);
}

extern "C" std::int64_t cz_str_bytes(CzObjeto* s) noexcept {
    return static_cast<std::int64_t>(textoDe(s).size());
}

extern "C" CzObjeto* cz_str_caractere(CzObjeto* s, std::int64_t pos) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {   // o FORNEXT da CVM: um caractere UTF-8 por vez
        const std::string& t = textoDe(s);
        const auto i = static_cast<std::size_t>(pos);
        r = novaString(t.substr(i, utf8::bytesDoCaractere(t, i)));
    });
    return r;
}

extern "C" std::int64_t cz_str_tam_caractere(CzObjeto* s, std::int64_t pos) noexcept {
    return static_cast<std::int64_t>(utf8::bytesDoCaractere(textoDe(s), static_cast<std::size_t>(pos)));
}

extern "C" CzObjeto* cz_formata(const CzValor* v, std::int32_t largura, std::int32_t casas) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] { r = novaString(formatPart(Value::empresta(*v), largura, casas)); });
    return r;
}

extern "C" void cz_de_texto(CzObjeto* s, std::int32_t kind, CzValor* r, const CzPos* p) noexcept {
    *r = Value().entrega();
    fronteira([&] {
        *r = convertValue(valorDeTexto(s), tipoDoKind(static_cast<std::uint8_t>(kind))).entrega();
    }, p);
}

extern "C" void cz_indice(const CzValor* o, const CzValor* i, CzValor* r, const CzPos* p) noexcept {
    *r = Value().entrega();
    fronteira([&] { *r = indexGet(Value::empresta(*o), Value::empresta(*i)).entrega(); }, p);
}

extern "C" void cz_fatia(const CzValor* s, const CzValor* ini, const CzValor* fim, const CzValor* passo,
                         CzValor* r, const CzPos* p) noexcept {
    *r = Value().entrega();
    fronteira([&] {
        *r = sliceGet(Value::empresta(*s), Value::empresta(*ini), Value::empresta(*fim),
                      Value::empresta(*passo)).entrega();
    }, p);
}

// ── list, dict e pair ─────────────────────────────────────────────────────

namespace {

// Um elemento de um vetor do código gerado: a marca CZ_TIPO_LITERAL é a string literal do
// programa com aquele índice (os literais estão em `literais`, na ordem do módulo)
Value elemento(const CzValor& c) {
    if (c.tipo != CZ_TIPO_LITERAL) return Value::empresta(c);
    CzValor v{};
    v.tipo = static_cast<std::uint8_t>(Value::Kind::STRING);
    v.u.obj = literais.at(c.extra);
    return Value::empresta(v);
}

}  // namespace

extern "C" CzObjeto* cz_lista_nova(const CzValor* elems, std::uint64_t n) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {
        std::vector<Value> v;
        v.reserve(static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i) v.push_back(elemento(elems[i]));
        r = makeList(std::move(v)).entrega().u.obj;
    });
    return r;
}

extern "C" CzObjeto* cz_dict_novo(const CzValor* kv, std::uint64_t n) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {   // como o NEWDICT: na ordem, a última chave repetida vence
        Value d = makeDict();
        auto& entries = d.asDict()->entries;
        for (std::uint64_t i = 0; i + 1 < n; i += 2) entries[elemento(kv[i])] = elemento(kv[i + 1]);
        r = d.entrega().u.obj;
    });
    return r;
}

extern "C" CzObjeto* cz_par_novo(const CzValor* a, const CzValor* b) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] { r = makePair(Value::empresta(*a), Value::empresta(*b)).entrega().u.obj; });
    return r;
}

extern "C" void cz_par_campo(const CzValor* p, std::int32_t qual, CzValor* r) noexcept {
    *r = Value().entrega();
    fronteira([&] {
        const Value par = Value::empresta(*p);
        *r = Value(qual == 0 ? par.asPair()->first : par.asPair()->second).entrega();
    });
}

extern "C" std::int32_t cz_igual(const CzValor* a, const CzValor* b) noexcept {
    std::int32_t r = 0;
    fronteira([&] { r = Value::empresta(*a) == Value::empresta(*b) ? 1 : 0; });
    return r;
}

extern "C" CzObjeto* cz_elementos(const CzValor* col, const CzPos* p) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] { r = makeList(forElements(Value::empresta(*col))).entrega().u.obj; }, p);
    return r;
}

extern "C" std::int64_t cz_lista_tam(CzObjeto* l) noexcept {
    return static_cast<std::int64_t>(static_cast<CinzaList*>(l)->elements.size());
}

extern "C" void cz_lista_elem(CzObjeto* l, std::int64_t i, CzValor* r) noexcept {
    *r = Value(static_cast<CinzaList*>(l)->elements[static_cast<std::size_t>(i)]).entrega();
}

extern "C" void cz_indice_lugar(const CzValor* o, const CzValor* k, CzValor* r, const CzPos* p) noexcept {
    *r = Value().entrega();
    fronteira([&] {
        Value obj = Value::empresta(*o);   // o mesmo objeto (compartilhado)
        *r = Value(*indexPlace(obj, Value::empresta(*k))).entrega();
    }, p);
}

extern "C" void cz_grava_indice(const CzValor* o, const CzValor* k, const CzValor* v, const CzPos* p) noexcept {
    fronteira([&] {
        Value obj = Value::empresta(*o);
        *indexPlace(obj, Value::empresta(*k)) = Value::empresta(*v);
    }, p);
}

// ── Structs, objetos, enums e tipos ───────────────────────────────────────

namespace {

std::vector<std::unique_ptr<CzStructDesc>> descs_struct;   // vivem até o fim do processo
std::vector<std::unique_ptr<CzClasseDesc>> descs_classe;
std::vector<std::unique_ptr<CzEnumDesc>>   descs_enum;

// Os campos de um objeto ou struct, sem copiar o dono (um struct não é clonado)
std::vector<Value>& camposDe(const CzValor* o) {
    if (o->tipo == static_cast<std::uint8_t>(Value::Kind::STRUCT)) return static_cast<StructValue*>(o->u.obj)->fields;
    return static_cast<ClassInstance*>(o->u.obj)->fields;
}

}  // namespace

extern "C" const void* cz_desc_struct(const char* nome, const char* const* campos, std::uint64_t n) noexcept {
    const void* r = nullptr;
    fronteira([&] {
        auto d = std::make_unique<CzStructDesc>();
        d->nome = nome;
        for (std::uint64_t i = 0; i < n; ++i) d->campos.emplace_back(campos[i]);
        r = d.get();
        descs_struct.push_back(std::move(d));
    });
    return r;
}

extern "C" const void* cz_desc_classe(const char* nome, std::uint32_t indice, std::uint32_t campos) noexcept {
    const void* r = nullptr;
    fronteira([&] {
        descs_classe.push_back(std::make_unique<CzClasseDesc>(CzClasseDesc{nome, indice, campos}));
        r = descs_classe.back().get();
    });
    return r;
}

extern "C" const void* cz_desc_enum(const char* nome, const char* const* valores, std::uint64_t n) noexcept {
    const void* r = nullptr;
    fronteira([&] {
        auto d = std::make_unique<CzEnumDesc>();
        d->nome = nome;
        for (std::uint64_t i = 0; i < n; ++i) d->valores.emplace_back(valores[i]);
        r = d.get();
        descs_enum.push_back(std::move(d));
    });
    return r;
}

extern "C" const void* cz_tipo(std::uint8_t kind, const char* nome, const void* const* params, std::uint64_t n) noexcept {
    const void* r = nullptr;
    fronteira([&] {
        using K = TypeInfo::Kind;
        auto& t = TypeContext::instance();
        auto p = [&](std::uint64_t i) { return static_cast<TypeRef>(params[i]); };
        const std::string nm = nome;
        switch (static_cast<K>(kind)) {
            case K::I64:       r = t.i64Type(); break;
            case K::F64:       r = t.f64Type(); break;
            case K::I32:       r = t.i32Type(); break;
            case K::F32:       r = t.f32Type(); break;
            case K::String:    r = t.stringType(); break;
            case K::Bool:      r = t.boolType(); break;
            case K::Void:      r = t.voidType(); break;
            case K::Var:       r = t.varType(); break;
            case K::List:      r = t.list(p(0)); break;
            case K::Dict:      r = t.dict(p(0), p(1)); break;
            case K::Pair:      r = t.pair(p(0), p(1)); break;
            case K::Class:     r = t.classType(nm); break;
            case K::Struct:    r = t.structType(nm); break;
            case K::Error:     r = t.errorType(nm); break;
            case K::TypeVar:   r = t.typeVar(nm); break;
            case K::Type:      r = t.typeType(); break;
            case K::Enum:      r = t.enumType(nm); break;
            case K::Interface: r = t.interfaceType(nm); break;
        }
        (void)n;
    });
    return r;
}

extern "C" CzObjeto* cz_struct_novo(const void* desc, const CzValor* campos, std::uint64_t n) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {
        gcMaybeCollect();   // ponto seguro da coleta, como o NEWSTRUCT
        auto sv = novo<StructValue>(static_cast<const CzStructDesc*>(desc));
        sv->fields.reserve(static_cast<std::size_t>(n));
        for (std::uint64_t i = 0; i < n; ++i) sv->fields.push_back(elemento(campos[i]));
        r = sv.solta();
    });
    return r;
}

extern "C" CzObjeto* cz_obj_novo(const void* desc) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] {
        gcMaybeCollect();   // como o NEWOBJ
        r = makeInstance(static_cast<const CzClasseDesc*>(desc)).entrega().u.obj;
    });
    return r;
}

extern "C" void cz_campo(const CzValor* o, std::int32_t i, CzValor* r) noexcept {
    *r = Value().entrega();
    fronteira([&] { *r = Value(camposDe(o)[static_cast<std::size_t>(i)]).entrega(); });
}

extern "C" void cz_grava_campo(const CzValor* o, std::int32_t i, const CzValor* v) noexcept {
    fronteira([&] { camposDe(o)[static_cast<std::size_t>(i)] = Value::empresta(*v); });
}

extern "C" CzObjeto* cz_struct_copia(CzObjeto* s) noexcept {
    CzObjeto* r = nullptr;
    fronteira([&] { r = novo<StructValue>(*static_cast<StructValue*>(s)).solta(); });
    return r;
}

extern "C" std::int32_t cz_classe_de(CzObjeto* o) noexcept {
    return static_cast<std::int32_t>(static_cast<ClassInstance*>(o)->classe()->indice);
}

extern "C" void cz_tipo_de(const CzValor* x, const CzValor* estatico, CzValor* r) noexcept {
    *r = Value().entrega();
    fronteira([&] {
        const TypeRef st = static_cast<TypeRef>(estatico->u.ptr);
        *r = Value(runtimeType(Value::empresta(*x), st)).entrega();
    });
}

extern "C" void cz_gc_estatisticas() noexcept {
    if (cz_exec.erro) return;   // como a CVM: o resumo só sai se a main terminou
    fronteira([&] {
        if (const char* e = std::getenv("CINZA_GC_STATS"); e && *e) {
            gcCollect();
            std::cout << "[gc] liberados em ciclos: " << gc_stats.liberados
                      << ", contêineres vivos: " << gc_registro.vivos << "\n";
        }
    });
}

extern "C" void cz_metodo0(std::int32_t b, const CzValor* o, CzValor* r, const CzPos* p) noexcept {
    metodo(b, o, {}, r, p);
}

extern "C" void cz_metodo1(std::int32_t b, const CzValor* o, const CzValor* a, CzValor* r,
                           const CzPos* p) noexcept {
    metodo(b, o, {a}, r, p);
}

extern "C" void cz_metodo2(std::int32_t b, const CzValor* o, const CzValor* a, const CzValor* x, CzValor* r,
                           const CzPos* p) noexcept {
    metodo(b, o, {a, x}, r, p);
}
