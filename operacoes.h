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

// Leitura obj[idx] (list: IndexError; dict: KeyError; string: o caractere,
// índice negativo conta do fim)
Value indexGet(const Value& obj, const Value& idx);

// Elementos de um for, copiados no início (spec 5.5): list (os elementos),
// dict (pares chave/valor, em ordem de chave), string (um caractere por vez);
// outro tipo lança
std::vector<Value> forElements(const Value& col);

// Fatia s[ini:fim:passo] de uma string; parte omitida chega como void
Value sliceGet(const Value& s, const Value& ini, const Value& fim, const Value& passo);

// ── printf / format ───────────────────────────────────────────────────────

// Um {expr[:formato]}: o valor como texto (forma da seção 5.4), com `casas`
// decimais fixas (>= 0) e largura mínima (números à direita, o resto à esquerda)
std::string formatPart(const Value& v, int largura, int casas);

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

// ── op<...>, interfaces e type() ──────────────────────────────────────────

// Tipo real de um valor cujo tipo estático é `st` (spec 3.9): o tipo do op que
// o valor guarda, a classe de um objeto guardado como interface, o tipo de erro
TypeRef runtimeType(const Value& v, TypeRef st);

// Conversão inserida pelo semântico de/para op<...>: confere o tipo (TypeError,
// que não ocorre num programa aceito) e converte int → decimal se o destino pede
Value   narrowValue(Value v, TypeRef from, TypeRef to);

// Atribuição a quem já travou num tipo desconhecido: mantém o tipo travado
// (spec 3.7 — v = 5 guarda 5.0 se v travou em decimal)
Value   keepLock(const Value& atual, Value novo);

} // namespace cinza

#endif // CINZA_OPERACOES_H
