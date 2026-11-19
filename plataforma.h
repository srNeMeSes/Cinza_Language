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

// Console em UTF-8 e, no Windows, sem a janela de relatório de erro (P2): um
// processo que cai termina na hora, em vez de ficar preso segurando o executável.
// Usado pelo cinza e pelos programas gerados pelo cinzac (cz_inicio).
void preparaConsole();

// Roda um programa (args[0], procurado no PATH) com os argumentos, sem shell, esperando o
// fim; devolve o código de saída, ou -1 se não conseguiu iniciar. Argumentos em UTF-8.
int executaProcesso(const std::vector<std::string>& args);

// O mesmo, com o código em `codigo` (no POSIX, morto por um sinal: 128 + o sinal) e false
// só se não conseguiu iniciar o programa. Com `saida`, a saída padrão dele vem para ela, em
// bytes (a entrada e a saída de erro continuam as do terminal). Usado por Process (Q3).
bool executa(const std::vector<std::string>& args, int& codigo, std::string* saida = nullptr);

// Todas as variáveis de ambiente, (nome, valor) em UTF-8 (Env.all)
std::vector<std::pair<std::string, std::string>> ambienteTodo();

// Caminho do executável em execução, em UTF-8 (o cinzac acha a libcinzart ao lado)
std::string caminhoDoExecutavel();

// Instante da última modificação do arquivo, em segundos desde 1970 UTC;
// false se não conseguir ler
bool dataModificacao(const std::filesystem::path& p, std::int64_t& segundos);

} // namespace cinza
