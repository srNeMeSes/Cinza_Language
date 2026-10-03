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

namespace cinza {

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

namespace cinza {

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
