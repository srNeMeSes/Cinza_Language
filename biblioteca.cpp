#include "biblioteca.h"
#include <map>

namespace cinza {

namespace {

// #embed é do C++26; o g++ 15 e o clang o aceitam antes, como extensão
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wc23-extensions"
#else
#pragma GCC diagnostic ignored "-Wc++26-extensions"
#endif
const unsigned char JSON[] = {
#embed "biblioteca/Json.cinza"
};
#pragma GCC diagnostic pop

const std::map<std::string, std::string>& fontes() {
    static const std::map<std::string, std::string> f{
        {"Json", std::string(reinterpret_cast<const char*>(JSON), sizeof JSON)},
    };
    return f;
}

} // namespace

const std::string* fonteDaBiblioteca(const std::string& nome) {
    auto it = fontes().find(nome);
    return it == fontes().end() ? nullptr : &it->second;
}

std::string caminhoDaBiblioteca(const std::string& nome) { return "<biblioteca>/" + nome + ".cinza"; }

} // namespace cinza
