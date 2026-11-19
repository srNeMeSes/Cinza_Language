#ifndef CINZA_BIBLIOTECA_H
#define CINZA_BIBLIOTECA_H

#include <string>

namespace cinza {

// ============================================================================
// MÓDULOS DA BIBLIOTECA ESCRITOS EM CINZA (Q3)
//
// A fonte de biblioteca/Nome.cinza vai embutida no executável (#embed, em
// biblioteca.cpp): `import Json;` a carrega como um módulo comum, com o nome
// "<biblioteca>/Json.cinza" nas mensagens e nos traces. Como os nativos, o nome
// é reservado: um arquivo do programa com ele é erro.
// ============================================================================

// A fonte do módulo, ou nulo se não houver um com esse nome
const std::string* fonteDaBiblioteca(const std::string& nome);

// O caminho mostrado do módulo ("<biblioteca>/Json.cinza")
std::string caminhoDaBiblioteca(const std::string& nome);

} // namespace cinza

#endif // CINZA_BIBLIOTECA_H
