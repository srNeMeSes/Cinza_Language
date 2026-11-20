#ifndef CINZA_LSP_SERVIDOR_H
#define CINZA_LSP_SERVIDOR_H

namespace cinza::lsp {

// ============================================================================
// SERVIDOR DE LINGUAGEM (Q6) — `cinza --lsp`
//
// O protocolo LSP pela entrada e saída padrão (JSON-RPC com Content-Length), com
// o front-end da linguagem: a cada arquivo aberto ou mudado, o léxico, a sintaxe,
// os imports e a semântica rodam sobre o texto do editor (sem executar nada), e
// os erros vão para o editor — os mesmos da compilação, com a mesma mensagem. A
// formatação do documento é o `cinza fmt`.
// ============================================================================

// Atende até o "exit"; devolve o código de saída
int rodaServidor();

} // namespace cinza::lsp

#endif // CINZA_LSP_SERVIDOR_H
