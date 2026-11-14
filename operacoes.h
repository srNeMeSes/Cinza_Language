#ifndef CINZA_OPERACOES_H
#define CINZA_OPERACOES_H

#include "lexer.h"
#include "value.h"
#include "types.h"
#include <span>
#include <variant>
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

// a op b, com op aritmético, de comparação ou de igualdade. Números só da
// mesma família (inteiro com inteiro, ponto flutuante com ponto flutuante: o
// semântico garante); + concatena quando um lado é string
Value applyBinaryOp(TokenType op, const Value& left, const Value& right);

// -x (inteiro com OverflowError no menor valor da largura; ponto flutuante IEEE)
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

// ── Conversões numéricas (seção K) ────────────────────────────────────────

// cast<T>(v) e as promoções implícitas inseridas pelo semântico (i32 → i64,
// f32 → f64). T é i32, i64, f32, f64 ou string. Inteiro de destino: fora da
// faixa → OverflowError; NaN/infinito → ValueError; flutuante trunca em direção
// a zero. f64 → f32 segue o IEEE (pode dar infinito). Texto → número: o formato
// estrito ([sinal] dígitos [. dígitos [e [sinal] dígitos]], ou inf/nan para
// flutuante); inválido → ValueError, fora da faixa → OverflowError.
Value convertValue(const Value& v, TypeRef to);

// double → float pelo IEEE (arredonda para o mais próximo; além de FLT_MAX +
// meia unidade vira infinito), sem o comportamento indefinido do static_cast
float doubleToF32(double d);

// Valor de um literal numérico/texto/bool, na largura que o semântico decidiu
// (resolved_type: i32/i64, f32/f64)
Value literalValue(const std::variant<std::int64_t, double, std::string, bool>& v, TypeRef t);

// Nativa genérica (Lists.sum de lista vazia devolve 0 inteiro de 64 bits): o
// resultado ganha a largura do tipo estático `t` da chamada
inline Value nativeResult(Value r, TypeRef t) {
    if (t && t->isNumeric() && r.isNumber() && (r.isInteger() != t->isInteger() ||
                                                 r.kind() != (t->is(TypeInfo::Kind::I32) ? Value::Kind::I32 :
                                                              t->is(TypeInfo::Kind::I64) ? Value::Kind::I64 :
                                                              t->is(TypeInfo::Kind::F32) ? Value::Kind::F32 :
                                                                                           Value::Kind::F64)))
        return convertValue(r, t);
    return r;
}

// ── interfaces e type() ──────────────────────────────────────────────────

// Tipo real de um valor cujo tipo estático é `st` (spec 3.8): a classe de um
// objeto guardado como interface, o tipo de um erro guardado como Error
TypeRef runtimeType(const Value& v, TypeRef st);

} // namespace cinza

#endif // CINZA_OPERACOES_H
