#include "natives.h"
#include "runtime_error.h"
#include <iostream>
#include <unordered_map>

namespace cinza {

// ============================================================================
// PRELUDE (C6)
// ============================================================================

// print(any...) -> void: argumentos separados por espaço, e fim de linha
static Value nativePrint(Executor&, std::span<const Value> args) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) std::cout << " ";
        std::cout << args[i].toString();
    }
    std::cout << "\n";
    return Value();
}

// range(int inicio, int fim[, int passo]) -> list<int>: de inicio (inclusive)
// até fim (exclusive). Passo negativo conta para baixo; passo 0 é ValueError.
static Value nativeRange(Executor&, std::span<const Value> args) {
    const std::int64_t inicio = args[0].asInt();
    const std::int64_t fim    = args[1].asInt();
    const std::int64_t passo  = args.size() == 3 ? args[2].asInt() : 1;

    if (passo == 0)
        throw RuntimeError("ValueError: o passo de 'range' não pode ser 0");

    std::vector<Value> elems;
    for (std::int64_t i = inicio; passo > 0 ? i < fim : i > fim;) {
        elems.emplace_back(i);
        if (__builtin_add_overflow(i, passo, &i)) break;   // chegou ao limite de int
    }
    return makeList(std::move(elems));
}

// input(string prompt) -> string: mostra o prompt (sem pular linha) e lê uma
// linha da entrada, sem o fim de linha. Fim da entrada é IOError.
static Value nativeInput(Executor&, std::span<const Value> args) {
    std::cout << args[0].asString() << std::flush;
    std::string linha;
    if (!std::getline(std::cin, linha))
        throw RuntimeError("IOError: fim da entrada em 'input'");
    if (!linha.empty() && linha.back() == '\r') linha.pop_back();
    return Value(std::move(linha));
}

const NativeFn* findPrelude(const std::string& name) {
    auto& t = TypeContext::instance();
    static const std::unordered_map<std::string, NativeFn> prelude = {
        {"print", {"print", {t.typeVar("any")}, t.voidType(), nativePrint, 0, true}},
        {"range", {"range", {t.intType(), t.intType(), t.intType()},
                   t.list(t.intType()), nativeRange, 2, false}},
        {"input", {"input", {t.stringType()}, t.stringType(), nativeInput}},
    };
    auto it = prelude.find(name);
    return it != prelude.end() ? &it->second : nullptr;
}

} // namespace cinza
