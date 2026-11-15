#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cinza {

// Todo texto da Cinza é UTF-8 (nomes de arquivo inclusive). No Windows o
// std::filesystem trabalha em UTF-16 e a conversão de um std::string comum usa a
// página de código do sistema: um nome com acento quebrava. Estas duas passam
// sempre por UTF-8.
std::filesystem::path caminhoUtf8(const std::string& s);
std::string           textoCaminho(const std::filesystem::path& p);   // com '/'

// Argumentos da linha de comando em UTF-8 (no Windows, argv vem na página de
// código do sistema, e um acento virava bytes inválidos)
std::vector<std::string> argumentosUtf8(int argc, char* argv[]);

// Variável de ambiente em UTF-8; false se não existir
bool ambienteUtf8(const char* nome, std::string& valor);

// Instante da última modificação do arquivo, em segundos desde 1970 UTC;
// false se não conseguir ler
bool dataModificacao(const std::filesystem::path& p, std::int64_t& segundos);

} // namespace cinza
