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
#include <cwchar>

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

bool executa(const std::vector<std::string>& args, int& codigo, std::string* saida) {
    if (args.empty()) return false;
    std::wstring linha;
    for (const auto& a : args) {
        if (!linha.empty()) linha += L' ';
        linha += citado(caminhoUtf8(a).wstring());
    }
    STARTUPINFOW si{};
    si.cb = sizeof si;
    HANDLE ler = nullptr, escrever = nullptr;
    if (saida) {   // a saída padrão por um pipe; só a ponta de escrita é herdada
        SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
        if (!CreatePipe(&ler, &escrever, &sa, 0)) return false;
        SetHandleInformation(ler, HANDLE_FLAG_INHERIT, 0);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = escrever;
        si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    const BOOL iniciou = CreateProcessW(nullptr, linha.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    if (escrever) CloseHandle(escrever);   // senão o ReadFile nunca vê o fim
    if (!iniciou) {
        if (ler) CloseHandle(ler);
        return false;
    }
    if (saida) {
        char buf[65536];
        DWORD n = 0;
        while (ReadFile(ler, buf, sizeof buf, &n, nullptr) && n > 0) saida->append(buf, n);
        CloseHandle(ler);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD c = 1;
    GetExitCodeProcess(pi.hProcess, &c);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    codigo = static_cast<int>(c);
    return true;
}

int executaProcesso(const std::vector<std::string>& args) {
    int codigo = 0;
    return executa(args, codigo) ? codigo : -1;
}

int comPilhaGrande(int (*f)(int, char**), int argc, char** argv) { return f(argc, argv); }

std::vector<std::pair<std::string, std::string>> ambienteTodo() {
    std::vector<std::pair<std::string, std::string>> r;
    wchar_t* bloco = GetEnvironmentStringsW();
    if (!bloco) return r;
    for (const wchar_t* p = bloco; *p; p += std::wcslen(p) + 1) {
        const std::wstring e = p;
        const std::size_t igual = e.find(L'=', 1);   // "=C:=C:\..." são do cmd, sem nome
        if (e[0] == L'=' || igual == std::wstring::npos) continue;
        r.emplace_back(utf8(e.substr(0, igual).c_str()), utf8(e.substr(igual + 1).c_str()));
    }
    FreeEnvironmentStringsW(bloco);
    return r;
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
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace cinza {

std::vector<std::string> argumentosUtf8(int argc, char* argv[]) {
    return std::vector<std::string>(argv, argv + argc);
}

void preparaConsole() {}

bool executa(const std::vector<std::string>& args, int& codigo, std::string* saida) {
    if (args.empty()) return false;
    // falha do execvp: o errno volta por um pipe que o exec fecha (O_CLOEXEC) se der certo
    int aviso[2];
    if (pipe(aviso) < 0) return false;
    fcntl(aviso[0], F_SETFD, FD_CLOEXEC);
    fcntl(aviso[1], F_SETFD, FD_CLOEXEC);
    int saida_fd[2] = {-1, -1};
    if (saida && pipe(saida_fd) < 0) {
        close(aviso[0]);
        close(aviso[1]);
        return false;
    }
    std::vector<char*> v;
    for (const auto& a : args) v.push_back(const_cast<char*>(a.c_str()));
    v.push_back(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        close(aviso[0]);
        if (saida) {
            dup2(saida_fd[1], STDOUT_FILENO);
            close(saida_fd[0]);
            close(saida_fd[1]);
        }
        execvp(v[0], v.data());
        const int e = errno;
        (void)!write(aviso[1], &e, sizeof e);
        _exit(127);
    }
    close(aviso[1]);
    if (saida) close(saida_fd[1]);
    if (pid < 0) {
        close(aviso[0]);
        if (saida) close(saida_fd[0]);
        return false;
    }
    int e = 0;
    const ssize_t lido = read(aviso[0], &e, sizeof e);
    close(aviso[0]);
    if (saida) {
        char buf[65536];
        for (ssize_t n; (n = read(saida_fd[0], buf, sizeof buf)) > 0;) saida->append(buf, static_cast<std::size_t>(n));
        close(saida_fd[0]);
    }
    int estado = 0;
    if (waitpid(pid, &estado, 0) < 0) return false;
    if (lido == static_cast<ssize_t>(sizeof e)) return false;   // o exec falhou
    codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : 128 + WTERMSIG(estado);
    return true;
}

int executaProcesso(const std::vector<std::string>& args) {
    int codigo = 0;
    return executa(args, codigo) ? codigo : -1;
}

namespace {
struct Chamada {
    int (*f)(int, char**);
    int    argc;
    char** argv;
    int    resultado;
};
void* rodaChamada(void* p) {
    auto* c = static_cast<Chamada*>(p);
    c->resultado = c->f(c->argc, c->argv);
    return nullptr;
}
}  // namespace

int comPilhaGrande(int (*f)(int, char**), int argc, char** argv) {
    pthread_attr_t atributos;
    pthread_attr_init(&atributos);
    pthread_attr_setstacksize(&atributos, std::size_t{64} << 20);
    Chamada c{f, argc, argv, 0};
    pthread_t t;
    const bool criou = pthread_create(&t, &atributos, rodaChamada, &c) == 0;
    pthread_attr_destroy(&atributos);
    if (!criou) return f(argc, argv);   // sem a thread, na pilha que houver
    pthread_join(t, nullptr);
    return c.resultado;
}

std::vector<std::pair<std::string, std::string>> ambienteTodo() {
    std::vector<std::pair<std::string, std::string>> r;
    for (char** p = environ; p && *p; ++p) {
        const std::string e = *p;
        const std::size_t igual = e.find('=');
        if (igual == std::string::npos || igual == 0) continue;
        r.emplace_back(e.substr(0, igual), e.substr(igual + 1));
    }
    return r;
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
