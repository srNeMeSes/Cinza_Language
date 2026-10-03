#ifndef CINZA_NATIVES_H
#define CINZA_NATIVES_H

#include "value.h"
#include "types.h"
#include <span>
#include <string>
#include <vector>

namespace cinza {

// ============================================================================
// FUNÇÕES NATIVAS (C6)
//
// Funções da linguagem implementadas em C++. O semântico checa a chamada pela
// assinatura (`params`/`ret`) e o executor chama `impl` com os argumentos já
// avaliados.
//
// Variáveis de tipo (TypeInfo::Kind::TypeVar) só existem aqui, nas
// assinaturas nativas: "T" é fixada pelo primeiro argumento que a usa e os
// demais precisam ser compatíveis (unificação simples); "any" aceita qualquer
// tipo sem se fixar. Duas variáveis têm restrição (Fase 7): "número" só se
// fixa em int ou decimal, e "comparável" em int, decimal ou string.
//
// Erros: `impl` lança RuntimeError("ValueError: ...") sem linha; o executor
// completa com a posição da chamada e o stack trace.
// ============================================================================

struct NativeFn {
    std::string          name;
    std::vector<TypeRef> params;
    TypeRef              ret;
    Value (*impl)(std::span<const Value>);

    // Parâmetros obrigatórios (os demais, no fim, são opcionais: range(a, b[, p]));
    // 0 = todos obrigatórios; SEM_OBRIGATORIOS = todos opcionais (exit([código]))
    static constexpr size_t SEM_OBRIGATORIOS = static_cast<size_t>(-1);
    size_t min_params = 0;
    // O último parâmetro se repete (print(a, b, c, ...))
    bool   variadic   = false;
    // Argumentos que a função altera, um bit por posição (bit 0 = 1º argumento:
    // Lists.sort; Lists.sort_by altera os dois primeiros): proibido se for const
    unsigned mutates = 0;
};

// x com exatamente `casas` casas decimais, arredondado como Math.round_to
// (Strings.fixed e o {x:.2f} do printf)
std::string fixedText(double x, std::int64_t casas);

// Constante de um módulo nativo (Math.pi)
struct NativeConst {
    std::string name;
    TypeRef     type;
    Value       value;
};

// Módulo nativo da biblioteca padrão (Fase 7): `import Math;` → Math.sqrt(2.0)
struct NativeModule {
    std::string              name;
    std::vector<NativeFn>    fns;
    std::vector<NativeConst> consts;
};

// Prelude: nativas visíveis em qualquer arquivo sem import (print, range).
// Uma função do usuário com o mesmo nome tem precedência.
const NativeFn* findPrelude(const std::string& name);

// Módulos nativos: Strings, Files, Math, Random, Convert, Lists (stdlib.cpp)
const std::vector<NativeModule>& nativeModules();
const NativeModule*              findNativeModule(const std::string& name);

} // namespace cinza

#endif // CINZA_NATIVES_H
