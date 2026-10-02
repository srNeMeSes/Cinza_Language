#ifndef CINZA_OPERACOES_H
#define CINZA_OPERACOES_H

#include "lexer.h"
#include "value.h"
#include <span>
#include <string>

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

// ── Coleções ──────────────────────────────────────────────────────────────

// Leitura obj[idx] (list: IndexError; dict: KeyError)
Value indexGet(const Value& obj, const Value& idx);

// Lugar obj[idx] para gravar (B5): list exige índice válido; dict só
// ATUALIZA chave existente (para inserir, .add)
Value* indexPlace(Value& obj, const Value& key);

// Métodos embutidos de list, dict e string (spec 5.7)
enum class Builtin : std::uint16_t {
    ListAdd, ListSize, ListHas, ListRemove,
    DictAdd, DictHas, DictRemove, DictSize, DictKeys, DictValues,
    StrSize,
    COUNT
};
// Método `name` do tipo de valor `kind`; lança se não existir
Builtin     builtinFor(Value::Kind kind, const std::string& name);
const char* builtinName(Builtin b);
Value       callBuiltin(Builtin b, Value& obj, std::span<const Value> args);

} // namespace cinza

#endif // CINZA_OPERACOES_H
