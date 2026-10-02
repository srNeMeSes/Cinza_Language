#ifndef CINZA_SOURCE_FILES_H
#define CINZA_SOURCE_FILES_H

#include <cctype>
#include <string>
#include <vector>

namespace cinza {

// ============================================================================
// ARQUIVOS-FONTE (C5)
//
// Cada arquivo carregado recebe um id; os tokens guardam o id do arquivo de
// onde vieram. O id 0 é o arquivo principal. Os diagnósticos (B6,
// diagnostics.h) mostram o nome registrado aqui: "util/texto.cinza:3:5: ...".
// ============================================================================

inline std::vector<std::string>& sourceFiles() {
    static std::vector<std::string> files{""};   // 0: principal (setMainFile)
    return files;
}

inline void setMainFile(const std::string& display) {
    sourceFiles()[0] = display;
}

inline int registerSourceFile(const std::string& display) {
    sourceFiles().push_back(display);
    return static_cast<int>(sourceFiles().size()) - 1;
}

// Nome do arquivo para as mensagens ("" se o id não for conhecido)
inline const std::string& fileName(int file_id) {
    static const std::string vazio;
    if (file_id < 0 || file_id >= static_cast<int>(sourceFiles().size())) return vazio;
    return sourceFiles()[static_cast<size_t>(file_id)];
}

// Nomes globais de módulos importados levam o prefixo do módulo
// ("modulos.geom::Ponto"); para o usuário, só o nome declarado aparece.
inline std::string displayName(const std::string& name) {
    const auto p = name.rfind("::");
    return p == std::string::npos ? name : name.substr(p + 2);
}

// Tira todos os prefixos de módulo de um texto gerado pelo compilador
// ("esperado 'list<modulos.geom::Ponto>'" → "esperado 'list<Ponto>'")
inline std::string stripModulePrefixes(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 2, "::") == 0) {
            size_t k = out.size();
            while (k > 0) {
                const char c = out[k - 1];
                const bool nome = std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
                                  c == '.' || c == '#' || (static_cast<unsigned char>(c) & 0x80);
                if (!nome) break;
                --k;
            }
            out.resize(k);
            ++i;   // pula o segundo ':'
            continue;
        }
        out += text[i];
    }
    return out;
}

} // namespace cinza

#endif // CINZA_SOURCE_FILES_H
