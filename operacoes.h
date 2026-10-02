#ifndef CINZA_OPERACOES_H
#define CINZA_OPERACOES_H

#include "lexer.h"
#include "value.h"

namespace cinza {

// ============================================================================
// OPERAÇÕES COMPARTILHADAS (interpretador e CVM)
//
// As regras de aritmética, comparação e concatenação ficam num lugar só, para
// as duas implementações darem exatamente os mesmos resultados e as mesmas
// mensagens de erro (spec 5.3). Os erros são lançados como RuntimeError sem
// posição; quem chama completa com a posição da expressão.
// ============================================================================

// a op b, com op aritmético, de comparação ou de igualdade (int/decimal se
// misturam; + concatena quando um lado é string)
Value applyBinaryOp(TokenType op, const Value& left, const Value& right);

// -x (int com OverflowError no menor int, ou decimal)
Value negateOp(const Value& v);

} // namespace cinza

#endif // CINZA_OPERACOES_H
