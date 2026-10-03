#pragma once
#include <cstdint>
#include <filesystem>

namespace cinza {

// Instante da última modificação do arquivo, em segundos desde 1970 UTC;
// false se não conseguir ler
bool dataModificacao(const std::filesystem::path& p, std::int64_t& segundos);

} // namespace cinza
