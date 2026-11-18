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

void preparaConsole() {
    SetConsoleOutputCP(CP_UTF8);   // força UTF-8 no terminal Windows
    SetConsoleCP(CP_UTF8);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

namespace {
// Um argumento na linha de comando do Windows, com as regras do CommandLineToArgvW:
// entre aspas, e as barras antes de uma aspa (ou do fim) dobradas
std::wstring citado(const std::wstring& a) {
    std::wstring r = L"\"";
    std::size_t barras = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++barras; continue; }
        if (c == L'"') r.append(barras * 2 + 1, L'\\');
        else r.append(barras, L'\\');
        barras = 0;
        r += c;
    }
    r.append(barras * 2, L'\\');
    return r + L"\"";
}
}  // namespace

int executaProcesso(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
    std::wstring linha;
    for (const auto& a : args) {
        if (!linha.empty()) linha += L' ';
        linha += citado(caminhoUtf8(a).wstring());
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, linha.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD codigo = 1;
    GetExitCodeProcess(pi.hProcess, &codigo);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(codigo);
}

std::string caminhoDoExecutavel() {
    std::wstring w(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, w.data(), static_cast<DWORD>(w.size()));
    w.resize(n);
    return utf8(w.c_str());
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
#include <climits>
#include <sys/wait.h>
#include <unistd.h>

namespace cinza {

std::vector<std::string> argumentosUtf8(int argc, char* argv[]) {
    return std::vector<std::string>(argv, argv + argc);
}

void preparaConsole() {}

int executaProcesso(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        std::vector<char*> v;
        for (const auto& a : args) v.push_back(const_cast<char*>(a.c_str()));
        v.push_back(nullptr);
        execvp(v[0], v.data());
        _exit(127);
    }
    int estado = 0;
    if (waitpid(pid, &estado, 0) < 0) return -1;
    return WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
}

std::string caminhoDoExecutavel() {
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string();
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
