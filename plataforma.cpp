#include "plataforma.h"

// ============================================================================
// Pedaços dependentes do sistema operacional, isolados aqui para que o
// windows.h (e as macros dele: ERROR, min, max...) não chegue ao resto.
// ============================================================================

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace cinza {

namespace {
std::string utf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
}  // namespace

std::vector<std::string> argumentosUtf8(int argc, char* argv[]) {
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    std::vector<std::string> args;
    if (!w || n != argc) {   // não deveria acontecer; fica com o que veio
        for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    } else {
        for (int i = 0; i < n; ++i) args.push_back(utf8(w[i]));
    }
    if (w) LocalFree(w);
    return args;
}

bool ambienteUtf8(const char* nome, std::string& valor) {
    const std::wstring wnome = caminhoUtf8(nome).wstring();
    const DWORD n = GetEnvironmentVariableW(wnome.c_str(), nullptr, 0);
    if (n == 0) return false;
    std::wstring w(n, L'\0');
    const DWORD lidos = GetEnvironmentVariableW(wnome.c_str(), w.data(), n);
    w.resize(lidos);
    valor = utf8(w.c_str());
    return true;
}

// No Windows o _wstat (usado pelo std::filesystem do MinGW) converte a data do
// arquivo pelo horário local e respeita a variável TZ, enquanto o relógio do
// sistema não: com TZ definida a data saía deslocada. FILETIME já é UTC.
bool dataModificacao(const std::filesystem::path& p, std::int64_t& segundos) {
    WIN32_FILE_ATTRIBUTE_DATA dados;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &dados)) return false;
    ULARGE_INTEGER ft;
    ft.LowPart  = dados.ftLastWriteTime.dwLowDateTime;
    ft.HighPart = dados.ftLastWriteTime.dwHighDateTime;
    // FILETIME: intervalos de 100 ns desde 1601-01-01 UTC
    constexpr std::int64_t DE_1601_A_1970 = 11644473600LL;
    segundos = static_cast<std::int64_t>(ft.QuadPart / 10000000ULL) - DE_1601_A_1970;
    return true;
}

} // namespace cinza

#else
#include <chrono>
#include <cstdlib>

namespace cinza {

std::vector<std::string> argumentosUtf8(int argc, char* argv[]) {
    return std::vector<std::string>(argv, argv + argc);
}

bool ambienteUtf8(const char* nome, std::string& valor) {
    const char* v = std::getenv(nome);
    if (!v) return false;
    valor = v;
    return true;
}

bool dataModificacao(const std::filesystem::path& p, std::int64_t& segundos) {
    std::error_code ec;
    const auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) return false;
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    segundos = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    return true;
}

} // namespace cinza
#endif

namespace cinza {

std::filesystem::path caminhoUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string textoCaminho(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

} // namespace cinza
