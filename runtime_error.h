#ifndef CINZA_RUNTIME_ERROR_H
#define CINZA_RUNTIME_ERROR_H

#include "value.h"
#include "diagnostics.h"
#include <stdexcept>
#include <string>
#include <vector>

namespace cinza {

// ============================================================================
// TIPOS DE ERRO EMBUTIDOS (C4)
//
// Error é a raiz: `except (Error e)` captura qualquer erro. Os demais são
// específicos; erros de runtime sem categoria própria têm tipo Error.
// `error Nome;` no nível superior declara novos tipos (também filhos de Error).
// ============================================================================

inline const std::vector<std::string>& builtinErrorKinds() {
    static const std::vector<std::string> kinds = {
        "Error", "ValueError", "IndexError", "KeyError", "ZeroDivisionError",
        "OverflowError", "IOError", "StackOverflowError", "TypeError"
    };
    return kinds;
}

// ============================================================================
// RUNTIME ERROR
//
// Erro da linguagem em tempo de execução (o SemanticAnalyzer não pode
// prevê-lo): IndexError, KeyError, ZeroDivisionError, OverflowError,
// StackOverflowError, ValueError... e os lançados com `throw` (C4).
// É capturável por try/except.
//
//   kind    → tipo do erro ("ZeroDivisionError", "SaldoInsuficiente", "Error")
//   message → texto sem o prefixo do tipo
//   trace   → pilha de chamadas no momento do erro, da mais interna para fora
//             ("em f (linha 3)"); preenchida pelo Executor
//
// Construído só com a mensagem, o tipo é lido do prefixo "Tipo: " quando ele
// é um tipo embutido ("IndexError: índice 5 ..."); senão o tipo é Error.
// ============================================================================

class RuntimeError : public std::runtime_error {
public:
    std::string              kind;
    std::string              message;
    int                      line;
    int                      column;
    std::vector<std::string> trace;

    int                      file_id = 0;   // C5: arquivo do erro (0 = principal)

    // B6: posição (line 0 = sem posição)
    SourceLocation location() const { return {line ? file_id : -1, line, column}; }

    RuntimeError(const std::string& msg, int ln = 0, int col = 0, int file = 0)
        : RuntimeError(splitKind(msg), ln, col, file) {}

    RuntimeError(const std::string& k, const std::string& msg, int ln, int col, int file = 0)
        : std::runtime_error(buildMessage(k, msg, ln, col, file)),
          kind(k), message(msg), line(ln), column(col), file_id(file) {}

private:
    struct KindAndMessage { std::string kind, message; };

    RuntimeError(KindAndMessage km, int ln, int col, int file)
        : RuntimeError(km.kind, km.message, ln, col, file) {}

    static KindAndMessage splitKind(const std::string& msg) {
        for (const auto& k : builtinErrorKinds()) {
            const std::string prefixo = k + ": ";
            if (msg.compare(0, prefixo.size(), prefixo) == 0)
                return {k, msg.substr(prefixo.size())};
        }
        return {"Error", msg};
    }

    // B6: "arquivo:linha:coluna: Tipo: mensagem" (sem posição: "Tipo: mensagem")
    static std::string buildMessage(const std::string& k, const std::string& msg,
                                    int ln, int col, int file) {
        if (ln == 0) return formatDiagnostic(k, msg, SourceLocation{-1, 0, 0});
        return formatDiagnostic(k, msg, SourceLocation{file, ln, col});
    }
};

// exit(código): um sinal com este tipo desce a pilha como um erro, rodando os
// finally pendentes, mas nenhum except o captura (nem `except (Error e)`); no
// topo, o programa encerra com o código (guardado na mensagem). O nome não é um
// identificador válido, então nenhum programa declara um erro com ele.
inline const std::string EXIT_KIND = "<exit>";

inline bool isExit(const RuntimeError& e) { return e.kind == EXIT_KIND; }

} // namespace cinza

#endif // CINZA_RUNTIME_ERROR_H
