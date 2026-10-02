#ifndef CINZA_NATIVES_H
#define CINZA_NATIVES_H

#include "value.h"
#include "types.h"
#include <span>
#include <string>
#include <vector>

namespace cinza {

class Executor;

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
    Value (*impl)(Executor&, std::span<const Value>);

    // Parâmetros obrigatórios (os demais, no fim, são opcionais: range(a, b[, p]))
    size_t min_params = 0;
    // O último parâmetro se repete (print(a, b, c, ...))
    bool   variadic   = false;
    // Altera o primeiro argumento (Lists.sort): proibido se ele for const
    bool   mutates_first = false;
};

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
