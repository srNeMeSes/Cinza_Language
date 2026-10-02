#ifndef CINZA_DIAGNOSTICS_H
#define CINZA_DIAGNOSTICS_H

#include "source_files.h"
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace cinza {

// ============================================================================
// DIAGNÓSTICOS UNIFICADOS (B6)
//
// Todo erro mostrado ao usuário — léxico, de sintaxe, de import, semântico e
// de runtime — sai no mesmo formato:
//
//     arquivo:linha:coluna: Tipo: mensagem
//
// Sem posição (ex.: erro no arquivo como um todo): "arquivo: Tipo: mensagem".
// Sem arquivo conhecido: "Tipo: mensagem". Os tipos: LexicalError,
// SyntaxError, ImportError, SemanticError e os de runtime (ValueError,
// IndexError, ..., ou um `error Nome;` do programa).
// ============================================================================

struct SourceLocation {
    int file_id = 0;   // índice em sourceFiles()
    int line    = 0;   // 0: sem posição
    int column  = 0;
};

// "arquivo:linha" (usado no stack trace) ou "linha N" sem arquivo
inline std::string formatPosition(const SourceLocation& loc) {
    const std::string& arquivo = fileName(loc.file_id);
    if (arquivo.empty()) return "linha " + std::to_string(loc.line);
    return arquivo + ":" + std::to_string(loc.line);
}

inline std::string formatDiagnostic(const std::string& kind, const std::string& message,
                                    const SourceLocation& loc) {
    std::string prefixo = fileName(loc.file_id);
    if (!prefixo.empty() && loc.line > 0)
        prefixo += ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    if (!prefixo.empty()) prefixo += ": ";
    return prefixo + displayName(kind) + ": " + message;
}

// Coleta os diagnósticos de todas as fases e os imprime juntos, no formato
// acima; `notes` são linhas extras, recuadas (o stack trace de um erro de
// runtime).
class DiagnosticEngine {
public:
    struct Diagnostic {
        std::string              kind;
        std::string              message;
        SourceLocation           loc;
        std::vector<std::string> notes;
    };

    void report(std::string kind, std::string message, SourceLocation loc = {},
                std::vector<std::string> notes = {}) {
        items.push_back({std::move(kind), std::move(message), loc, std::move(notes)});
    }

    bool   hasErrors() const { return !items.empty(); }
    size_t count()     const { return items.size(); }
    const std::vector<Diagnostic>& all() const { return items; }

    // Imprime tudo o que foi reportado e esvazia a lista
    void flush(std::ostream& out) {
        for (const auto& d : items) {
            out << formatDiagnostic(d.kind, d.message, d.loc) << "\n";
            for (const auto& nota : d.notes) out << "    " << nota << "\n";
        }
        items.clear();
    }

private:
    std::vector<Diagnostic> items;
};

// Instância única do processo, compartilhada pelas fases
inline DiagnosticEngine& diagnostics() {
    static DiagnosticEngine engine;
    return engine;
}

} // namespace cinza

#endif // CINZA_DIAGNOSTICS_H
