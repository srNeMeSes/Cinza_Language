#ifndef CINZA_RUNTIME_ERROR_H
#define CINZA_RUNTIME_ERROR_H

#include "value.h"
#include <stdexcept>
#include <string>

namespace cinza {

// ============================================================================
// RUNTIME ERROR
//
// Erros detectáveis apenas em tempo de execução.
// O SemanticAnalyzer não pode prevê-los estaticamente:
//
//   IndexError      → lista[i] fora dos limites
//   KeyError        → dict["x"] com chave inexistente
//   DivisionByZero  → divisão ou módulo por zero
// ============================================================================

class RuntimeError : public std::runtime_error {
public:
    int line;
    int column;

    RuntimeError(const std::string& msg, int ln = 0, int col = 0)
        : std::runtime_error(buildMessage(msg, ln, col)),
          line(ln), column(col) {}

private:
    static std::string buildMessage(const std::string& msg, int ln, int col) {
        if (ln == 0) return "RuntimeError: " + msg;
        return "RuntimeError [linha " + std::to_string(ln) +
               ", col "  + std::to_string(col) + "]: " + msg;
    }
};

// ============================================================================
// RETURN SIGNAL
//
// Não é um erro — é controle de fluxo puro.
//
// Lançado pelo executor ao encontrar um ReturnStmt.
// Capturado pelo executor de chamadas de função (executeFunction /
// executeMethodCall) para extrair o valor de retorno e continuá-lo
// normalmente para o chamador.
//
// Esse padrão evita passar o valor de retorno através de parâmetros de
// saída por toda a árvore de chamadas.  É a abordagem canônica de
// tree-walk interpreters (cf. Crafting Interpreters, cap. 10).
// ============================================================================

struct ReturnSignal {
    Value value;

    ReturnSignal() = default;
    explicit ReturnSignal(Value v) : value(std::move(v)) {}
};

} // namespace cinza

#endif // CINZA_RUNTIME_ERROR_H
