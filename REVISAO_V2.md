# Cinza v2 — Revisão e Checklist de Implementação

> **Para o Claude Code:** este arquivo é a fonte de verdade da v2.
> - Trabalhe **uma fase por vez**, na ordem da seção 0.
> - Ao concluir um item, troque `- [ ]` por `- [x]` e acrescente ao final da linha: `— feito em <commit/data>`.
> - Todo bug corrigido precisa de um teste em `tests/` que falhava antes e passa depois.
> - Rode `make test` (e `make debug` para os itens de memória/UB) antes de marcar qualquer item.
> - Não altere a semântica da linguagem além do que está descrito aqui sem perguntar.
> - Se um item se mostrar errado ou incompleto, **não o marque**: adicione uma nota `> ⚠️ Nota:` logo abaixo dele.

Legenda de prioridade: 🔴 crítico · 🟠 importante · 🟢 melhoria

---

## 0. Ordem das fases

- [x] **Fase 1** — Infraestrutura de testes + alvo `make debug` (seção 1) — feito em 2026-09-26
- [x] **Fase 2** — Correção dos bugs da seção A — feito em 2026-09-27
  > Nota: todos os itens de A1–A12 feitos, exceto `self` (A10), que depende de B5 e foi para a Fase 4.
  > `make test`: 14 testes de unidade + 69 `.cinza`, todos verdes, também sob ASan/UBSan (MSYS2 CLANG64).
- [x] **Fase 2.5** — `main` obrigatória e nível superior declarativo (seção 2.5) — feito em 2026-09-27
  > Nota: pendentes só o que depende de fora desta fase: `main` em módulo importado (C5) e o Apêndice A
  > do livro (fora do repositório). `make test`: 14 testes de unidade + 89 `.cinza`, também sob ASan/UBSan.
- [x] **Fase 3** — Refatorações sem mudança de comportamento: B1, B3, B4 — feito em 2026-09-27
  > Nota: `make test` = 2 testes de unidade (`Value`, `TypeContext`) + 92 `.cinza`, também sob ASan/UBSan.
- [x] **Fase 4** — B5 (lvalue genérico) → `struct`, operadores compostos, `and`/`or`/`not`, `break`/`continue` — feito em 2026-09-28
  > Nota: B5, `self`, C1, C2 e C3 feitos. Pendentes só os dois itens de documentação do livro (C1), que
  > não está no repositório. `make test` = 2 testes de unidade + 148 `.cinza`, também sob ASan/UBSan.
- [x] **Fase 5** — Exceções: `try` / `except` / `finally` / `throw` — feito em 2026-09-28
  > Nota: `make test` = 2 testes de unidade + 166 `.cinza`, também sob ASan/UBSan.
- [x] **Fase 6** — Módulos, `import` e interface de funções nativas — feito em 2026-09-28
  > Nota: C5 e a primeira parte de C6 (`NativeFn`, variáveis de tipo, prelude com `print`/`range`). `make test`
  > = 2 testes de unidade + 188 `.cinza` (18 `c5_*` com módulos auxiliares em `tests/modulos/`), também sob
  > ASan/UBSan.
- [x] **Fase 7** — Biblioteca padrão — feito em 2026-09-29
  > Nota: `input` no prelude e seis módulos nativos (`Strings`, `Files`, `Math`, `Random`, `Convert`, `Lists`)
  > em `stdlib.cpp`. `make test` = 2 testes de unidade + 233 `.cinza` (45 `f7_*`), também sob ASan/UBSan.
- [x] **Fase 8** — B2 (resolução de nomes em slots) — ponte para a futura CVM — feito em 2026-09-29
  > Nota: `make test` = 2 testes de unidade + 234 `.cinza`, também sob ASan/UBSan. Ganho de 1,7× a 2,5×
  > nos benchmarks de `bench/` (ver B2).

---

## 1. Infraestrutura (Fase 1)

- [x] 🔴 Criar a pasta `tests/`. Cada teste é um arquivo `.cinza` com a saída esperada em comentários: — feito em 2026-09-26
  ```
  decimal d = 5;
  print(d / 2);
  // expect: 2.5
  ```
  Para testes que devem falhar, usar `// expect-error: <trecho da mensagem>`.
- [x] 🔴 Criar `tests/run_tests.sh` (ou `.py`): para cada `.cinza`, executa `./cinza`, compara stdout/stderr com os `expect`, imprime `PASS`/`FAIL` e retorna código ≠ 0 se algo falhar. — feito em 2026-09-26
- [x] 🔴 Fazer `make test` rodar o script acima (hoje ele roda um único `teste.cinza`). — feito em 2026-09-26
- [x] 🟠 Adicionar ao Makefile: — feito em 2026-09-26
  ```make
  debug: CXXFLAGS = -std=c++20 -O0 -g -Wall -Wextra -fsanitize=address,undefined
  debug: clean $(TARGET)
  ```
  > ⚠️ Nota: o MinGW-w64 (g++) não traz `libasan`/`libubsan`. No Windows, `make debug` roda no terminal
  > **MSYS2 CLANG64** (o Makefile troca para `clang++` sozinho). Lá não há Python: use
  > `make test PYTHON=/c/Python313/python` ou instale `mingw-w64-clang-x86_64-python`.
  > Ao voltar para o PowerShell, rode `mingw32-make clean` antes (não misturar `.o` do clang e do g++).
  > Achados da primeira execução: A5 é UB de verdade (`executor.cpp:331`, conversão `double` → `int`
  > fora da faixa); A6 aparece como `AddressSanitizer: stack-overflow`.
- [x] 🟢 Trocar a dependência de headers do Makefile por `-MMD -MP` (dependências automáticas). — feito em 2026-09-26
- [x] 🔴 Adicionar como testes de regressão **todos** os programas de reprodução da seção A (eles devem falhar antes das correções). — feito em 2026-09-26
  > Nota: 10 testes (A1–A8), todos falhando com os sintomas descritos; `tests/basico_print.cinza` é o teste de sanidade que passa.
  > A9–A12 não têm programa de reprodução na revisão; os testes deles entram junto com cada correção.

---

## A. Bugs confirmados (Fase 2)

### A1. 🔴 Escopo dinâmico no Environment

As funções enxergam e alteram as variáveis locais de quem chamou, e os métodos leem locais do chamador no lugar dos seus campos.

Reprodução 1 (`tests/a1_escopo_global.cinza`):
```
int count = 0;
fn inc() { count = count + 1; }
fn main() -> int { int count = 100; inc(); print(count); return 0; }
int r = main();
print(count);
// expect: 100
// expect: 1
```
Hoje a saída é `101` e `0`.

Reprodução 2 (`tests/a1_metodo_campo.cinza`):
```
class P { string nome = "campo"; pub { fn get() -> string { return nome; } } }
fn f() -> string { string nome = "LOCAL"; P p = new P(); return p.get(); }
print(f());
// expect: campo
```

- [x] Reestruturar `Environment` em **globais + pilha de frames**, onde cada frame é uma pilha de escopos de bloco. — feito em 2026-09-27
- [x] Chamada de função, método ou construtor abre um frame novo (`pushFrame`/`popFrame` via guard RAII). — feito em 2026-09-27
  > Nota: os inicializadores de campo no `new` também rodam num frame próprio.
- [x] Ordem de busca de nomes: escopos do frame atual → campos de `current_instance` → globais. **Nunca** o frame do chamador. — feito em 2026-09-27
  > Nota: centralizada em `Executor::lookupVariable`; o semântico já resolvia nomes lexicalmente.
- [x] `lookup` passa a retornar `Value*`, eliminando a cópia a cada leitura de variável. — feito em 2026-09-27
  > Nota: some a cópia intermediária e o get+assign de `x[i] = v`; `evalExpr` ainda devolve `Value`
  > por valor (uma cópia por leitura), o que só muda com B2.
- [x] Os dois testes acima passam. — feito em 2026-09-27
  > Nota: mais dois testes: `a1_metodo_atribui_campo` (falhava: o método sobrescrevia o local do
  > chamador) e `a1_regressao_chamadas` (recursão, método→método, construtor, `for`). Limpo sob ASan/UBSan.

### A2. 🔴 A promoção `int → decimal` não é materializada

```
decimal d = 5;
print(d / 2);
// expect: 2.5
```
Hoje imprime `2`.

- [x] Criar o nó `CastExpr` (alvo em `resolved_type`). — feito em 2026-09-27
- [x] Criar `SemanticAnalyzer::coerceInPlace(ExprPtr& slot, target)` para inserir o cast. — feito em 2026-09-27
  > Nota: em literais de list/dict/pair ele desce nos elementos e reanota o literal com o tipo alvo.
  > Literais mistos (`[1, 2.5]`, `{{"a", 1}, {"b", 2.5}}`) também convertem os elementos `int`.
- [x] Aplicar a coerção em: declaração, atribuição, argumentos de função/método/construtor, `return`, elementos de literal de list/dict/pair, inicializador de campo, `[]=`, `dict.add`, `list.add`. — feito em 2026-09-27
  > Nota: também na chamada implícita de método da própria classe e no iterador de
  > `for (decimal x in list<int>)` (este no executor). Hoje `pair<string, int>` não é atribuível a
  > `pair<string, decimal>`, então a coerção em literal de pair e em `dict.add({"a", 1})` só fica
  > alcançável quando A3 fizer os literais usarem o tipo esperado. A reanálise de funções com
  > parâmetro `var` (removida em A7) reconhece o `CastExpr` já inserido.
- [x] O executor avalia `CastExpr`, convertendo INT para DECIMAL. — feito em 2026-09-27
- [x] Testes cobrindo cada um dos pontos de coerção acima. — feito em 2026-09-27
  > Nota: `tests/a2_coercoes.cinza` cobre 15 pontos (todos davam `2` antes) e
  > `tests/a2_divisao_inteira.cinza` garante que `int / int` continua inteiro. `dict.add` e literal
  > de pair ficaram em `tests/a3_literal_promovido.cinza`, porque só passaram a ser aceitos com A3.

### A3. 🔴 A covariância de coleções quebra a segurança de tipos

```
list<int> a = [1];
list<decimal> b = a;
// expect-error: incompatível
```
Hoje isso é aceito e permite que `int x = a[1]` guarde `2.5`.

- [x] Genéricos (`list`, `dict`, `pair`) passam a ser **invariantes** em `isAssignable`. — feito em 2026-09-27
  > Nota: `list<var>`/`dict<var, var>` (literal vazio sem contexto) ainda casa com qualquer
  > parâmetro. Hoje o parser já proíbe `[]`/`{}` como inicializador.
- [x] Implementar checagem bidirecional: `analyzeExpr(expr, expected)`. Literais usam o tipo esperado para se promover (`list<decimal> l = [1, 2];` continua válido). — feito em 2026-09-27
  > Nota: o tipo esperado vem de declaração, atribuição, `[]=`, `return`, argumentos (função,
  > método, construtor, chamada implícita), inicializador de campo, `list.add` e `dict.add`, e desce
  > em literais aninhados. Efeito colateral desejado: `pair<string, decimal> p = {"a", 1};` e
  > `d.add({"a", 1})` em `dict<string, decimal>`, antes rejeitados, agora são aceitos.
- [x] Testes: variável → variável é rejeitado; literal → variável é aceito. — feito em 2026-09-27
  > Nota: `a3_covariancia_{lista,dict,argumento,retorno}` (rejeição) e `a3_literal_promovido`
  > (13 contextos de literal). Antes, argumento `list<int>` em `list<decimal>` fazia `1 / 2` dar `0`.

### A4. 🔴 Nome de classe contendo "void" é rejeitado

```
class Avoider { int v = 1; }
Avoider q = new Avoider();
print(q);
// expect: <Avoider object>
```

- [x] Substituir `declared_type.find("void")` por uma verificação recursiva na árvore `Type`. — feito em 2026-09-27
- [x] `void` também proibido em parâmetros (função, método, construtor), campos e como parâmetro de tipo
      no retorno (`-> list<void>`); `-> void` continua válido. — feito em 2026-09-27
  > Nota: item acrescentado. O comentário do semântico dizia que parâmetros eram barrados, mas
  > `fn f(void x)`, `fn f(list<void> x)` e `class C { list<void> xs; }` eram aceitos. Testes `a4_*`
  > (7 arquivos, todos falhavam antes).

### A5. 🔴 Overflow de inteiros

```
int z = 99999999999999999999;
// expect-error: fora do intervalo
```
```
int big = 9223372036854775807;
print(big + 1);
// expect-error: OverflowError
```
> Nota: exemplos atualizados para `int` de 64 bits (decisão abaixo); os originais usavam os limites de 32 bits.

- [x] No lexer, usar `std::from_chars` em `int64_t` e gerar um token de erro para literais fora da faixa. Isso remove o `std::stoi`, que derruba o programa com "Erro fatal: stoi". — feito em 2026-09-27
  > Nota: literais decimais também passaram de `std::stod` para `from_chars` ("fora do intervalo de
  > decimal"). `9223372036854775808` (2^63) passa pelo lexer marcado e só é aceito com `-` na frente.
- [x] No parser, combinar `-` unário com literal numérico antes da checagem de faixa, para que `-2147483648` seja válido. — feito em 2026-09-27
  > Nota: com 64 bits o caso-limite é `-9223372036854775808`. Só literais inteiros são combinados.
- [x] No executor, fazer aritmética inteira nativa com checagem de overflow (`__builtin_add_overflow`, `__builtin_sub_overflow`, `__builtin_mul_overflow`), sem passar por `double`. — feito em 2026-09-27
  > Nota: some o UB que o UBSan acusava (conversão `double` → `int` fora da faixa). `decimal % 0`
  > (divisor int) continua dando "Modulo por zero"; divisor `0.0` segue no A10.
- [x] Cobrir os casos `-INT_MIN`, `INT_MIN / -1` e `INT_MIN % -1`, que devem gerar `OverflowError`. — feito em 2026-09-27
  > Nota: revisto em 2026-10-03 (seção I): `INT_MIN % -1` dá `0` (o resultado cabe em `int`); `-INT_MIN`
  > e `INT_MIN / -1` continuam lançando `OverflowError`.
  > Nota: testes `a5_*` (10 arquivos): os três casos acima, soma, subtração, multiplicação, literal
  > fora da faixa (int e decimal), `2^63` sem `-` e os limites válidos (`3000000000 * 3` funciona).
- [x] 🟢 **Decidir e registrar aqui:** `int` passa a ter 64 bits na v2? Decisão: **sim, 64 bits (`int64_t`)** — feito em 2026-09-27
  > Nota: `Value`, `LiteralExpr` e `Token` usam `int64_t`; os dois testes originais foram reescritos.

### A6. 🔴 Recursão infinita causa segmentation fault

```
fn r(int n) -> int { return r(n + 1); }
print(r(0));
// expect-error: StackOverflowError
```

- [x] Adicionar um contador de profundidade de chamadas com guard RAII, com limite configurável (sugestão: 2000). — feito em 2026-09-27
  > Nota: `CallDepthGuard` em função, método e construtor; limite via `Executor(int max_depth = 2000)`.
- [x] Ao exceder o limite, lançar `RuntimeError` do tipo `StackOverflowError`. — feito em 2026-09-27
- [x] Pilha nativa grande o bastante para o limite: o Makefile linka com `-Wl,--stack,67108864` (64 MB)
      no Windows. — feito em 2026-09-27
  > Nota: item acrescentado. Com a pilha padrão de 1 MB a recursão estourava em ~1007 níveis, antes do
  > limite. Com 64 MB, 1999 níveis funcionam também no build com ASan. No Linux/macOS a pilha do
  > processo vem do `ulimit -s` (em geral 8 MB); se for preciso, rodar o executor numa thread com pilha
  > própria. Testes `a6_*`: recursão infinita em função, mútua, método e construtor, mais recursão
  > válida com 1990 níveis.

### A7. 🔴 `var` em parâmetros e retorno

Problemas: `return` aninhado falha com `__var_infer__`; o corpo é reanalisado com o escopo do chamador (vazamento de variáveis); as anotações da AST são sobrescritas a cada chamada; e funções nunca chamadas não são verificadas.

```
fn g(var x) -> int { return x + segredo; }
fn h() -> int { int segredo = 42; return g(1); }
// expect-error: segredo
```
> Nota: com `var` proibido em parâmetros, este programa para no parser ("'var' não é permitido em
> parâmetros"); o teste `a7_var_parametro` espera essa mensagem. A versão com `int x`, que verifica que o
> corpo não enxerga `segredo`, ficou em `a7_corpo_verificado`.

- [x] Remover `var` de parâmetros e de tipo de retorno. O parser gera erro com a sugestão de usar tipo explícito. — feito em 2026-09-27
  > Nota: também dentro de tipos compostos (`list<var> x`) e em campos de classe, que eram aceitos.
- [x] Remover do semântico o caminho de reanálise por ponto de chamada (`decl_ptr`, `__var_infer__`). — feito em 2026-09-27
- [x] Manter `var` apenas em declaração local e no iterador do `for`. — feito em 2026-09-27
  > Nota: testes `a7_*` (6 arquivos): 4 rejeições que falhavam antes, mais `a7_var_local_valido` e
  > `a7_corpo_verificado` como regressão.
- [x] 🟢 Registrar como trabalho futuro: genéricos explícitos `fn max<T>(T a, T b) -> T`. — feito em 2026-09-27
  > Nota: registrado na seção D, junto de "Genéricos do usuário com monomorfização".

### A8. 🟠 `fn` aninhada é aceita, mas nunca registrada

```
fn f() -> int { fn inner() -> int { return 1; } return inner(); }
// expect-error: nível superior
```

- [x] `fn`, `class` e `struct` só são permitidos no nível superior; `fn` também dentro de `class`. Gerar erro no parser. — feito em 2026-09-27
  > Nota: `Parser::block_depth` (guard RAII em `parseBlockStatement`). `struct` entra na regra quando existir
  > (C3). O primeiro erro é o certo, mas ainda vêm erros em cascata depois dele; some com o item do
  > `synchronize()` em A9. Testes `a8_*`: fn e class aninhadas, fn em bloco do nível superior, e regressão
  > de métodos.

### A9. 🟠 Parser e lexer

- [x] `parseBlockStatement` usar `consume(LBRACE, "Esperado '{' ...")` em vez de `advance()`. — feito em 2026-09-27
  > Nota: antes `fn f() -> int return 1;` engolia o `return` como `{`. Teste `a9_bloco_sem_chave`.
- [x] `synchronize()` parar também em `RBRACE`, sem consumi-lo, para evitar erros em cascata. — feito em 2026-09-27
  > Nota: via `Parser::brace_depth` (blocos, corpo de classe e `pub { }`); no nível superior o `}` avulso
  > é consumido para garantir progresso. A `fn`/`class` aninhada (A8) agora é lida e descartada, gerando
  > um único erro. `run_tests.py` ganhou `// expect-not:` para testar a ausência de cascata
  > (`a9_sincroniza_chave`, `a8_sem_cascata`). Chaves de literal de dict/pair não entram na conta.
- [x] `nextToken()` definir `start = current` antes de gerar `END_OF_FILE`, para não produzir lexema lixo. — feito em 2026-09-27
  > Nota: o lexema lixo era o resto do arquivo; em testes com `// expect-error:` ele chegava a ecoar o
  > texto esperado e fazer o teste passar por acidente. Teste `a9_eof_lexema`.
- [x] Converter para `unsigned char` todas as chamadas de `std::isalpha`, `std::isdigit` e `std::isalnum`. — feito em 2026-09-27
  > Nota: substituídas por testes ASCII explícitos (`isAsciiAlpha`/`isAsciiDigit`/`isAsciiAlnum`), sem UB
  > e sem depender de locale; não sobrou nenhum `std::is*` no projeto.
- [x] Ignorar o BOM UTF-8 (`EF BB BF`) no início do arquivo. Hoje gera 3 `LexicalError` com
      arquivos salvos por editores do Windows (ex.: `Set-Content -Encoding utf8` no PowerShell 5.1). — feito em 2026-09-27
  > Nota: teste `a9_bom`.
- [x] Identificadores só em ASCII, com mensagem clara ao encontrar acentos (ex.: "Identificadores não podem conter 'ç'"). — feito em 2026-09-27
  > Nota: a palavra inteira é consumida (um erro só): "Identificadores não podem conter 'ç' (em 'ação')".
  > Acentos continuam válidos em strings e comentários (`a9_acento_em_string`).
- [x] Escape desconhecido em string (`"\q"`) passa a ser erro léxico. — feito em 2026-09-27
  > Nota: lista os escapes válidos na mensagem. Testes `a9_escape_desconhecido` e `a9_escapes_validos`.
- [x] String com várias linhas: o token deve registrar a linha de **início**. — feito em 2026-09-27
  > Nota: vale também para o erro "String não terminada". Teste `a9_string_multilinha`.
- [x] `list`, `dict` e `pair` exigem `<...>` (proibir `list x;`). — feito em 2026-09-27
  > Nota: mensagens com exemplo (`list<int>`, `dict<string, int>`, `pair<string, int>`). Testes `a9_*_sem_tipo`.

### A10. 🟠 Semântico e executor

- [x] Dentro de uma classe, um **método da própria classe tem precedência** sobre uma função global de mesmo nome (semântico e executor). — feito em 2026-09-27
  > Nota: vale também para a checagem de aridade e tipos (usa a assinatura do método). Testes
  > `a10_metodo_precede_global` e `a10_metodo_precede_assinatura`.
- [x] Adicionar `self` para acessar campos e métodos quando houver ambiguidade (ex.: `Pessoa(string nome) { self.nome = nome; }`). Depende de B5. — feito em 2026-09-28
  > Nota: implementado junto com B5 (Fase 4).
  > Nota: adiado para a Fase 4, junto com B5, como a própria dependência indica.
- [x] Permitir `return;` sem valor em função void, para saída antecipada. `return <expr>;` em void continua sendo erro. — feito em 2026-09-27
  > Nota: vale também em construtor. Testes `a10_return_vazio` e `a10_return_valor_em_void`.
- [x] `%` com divisor decimal `0.0` gera `ZeroDivisionError`. — feito em 2026-09-27
  > Nota: as mensagens de `/` e `%` por zero (int e decimal) passaram a começar com `ZeroDivisionError:`,
  > alinhadas com os tipos de erro de C4. Testes `a10_modulo_zero_decimal`, `a10_divisao_zero_int`,
  > `a10_modulo_zero_int`.
- [x] Proibir `decimal` como chave de `dict`. — feito em 2026-09-27
  > Nota: checagem recursiva na árvore do tipo em variável, parâmetro, retorno, campo e construtor
  > (`list<dict<decimal, T>>` também é pego), e na chave inferida de literal. Testes `a10_dict_chave_decimal*`.
- [x] 🟢 **Decidir e registrar aqui:** `string.size()` conta bytes ou code points? Decisão: **conta caracteres (code points UTF-8)** — decidido em 2026-09-27
- [x] Implementar `string.size()` contando code points UTF-8 (hoje conta bytes: `"ção".size()` dá 5). — feito em 2026-09-27
  > Nota: antes `"ação".size()` dava 6. Teste `a10_string_size`.
- [x] Nenhuma exceção C++ interna (`bad_variant_access`, `out_of_range`, ...) pode chegar ao usuário. `main` deve capturá-las como "erro interno do compilador" com instrução para reportar. — feito em 2026-09-27
  > Nota: `reportInternalError` em `main.cpp` (inclusive `catch (...)`); arquivo inexistente continua sendo
  > erro comum do usuário. Nenhum programa conhecido aciona a rede, então não há teste em `tests/`:
  > verificado com uma cópia do código com exceção injetada (fora do repositório).

### A11. 🟠 Armadilha de C++ em `value.h`

- [x] Adicionar `explicit Value(const char* s)`. Sem ele, `Value("x")` vira `BOOL true`. — feito em 2026-09-27
  > Nota: sem linguagem que reproduza, o teste é em C++: `tests/unit_value.cpp`, compilado e rodado por
  > `make test` antes da suíte `.cinza`. Com o `value.h` antigo ele falha (`Value("x")` dá BOOL).
- [x] Remover `Value::kind` (redundante com `data.index()`) ou garantir por construção que os dois nunca divergem. — feito em 2026-09-27
  > Nota: o campo virou `kind()`, derivado de `data.index()`; `static_assert`s em `value.h` garantem que a
  > ordem do variant casa com a do enum. Teste de unidade cobre `kind()` após reatribuir `data`.

### A12. 🟢 `main.cpp`

- [x] `readFile` usar `std::ifstream` + `rdbuf()` em vez de ler linha por linha. — feito em 2026-09-27
  > Nota: melhoria sem mudança de comportamento; coberta pela suíte inteira (69 testes).
- [x] Chamar `std::ios::sync_with_stdio(false)` no início do programa. — feito em 2026-09-27
  > Nota: o compilador não usa `printf`; `cerr` continua ligado (tie) a `cout`, então a saída do programa
  > sai antes das mensagens de erro.

---

### Fase 2.5 — `main` obrigatória e nível superior declarativo

- [x] Nível superior aceita só `fn`, `class`, `struct`, `error`, `import` e `const`
      com expressão constante. Comandos soltos geram erro com a dica
      "mova para `fn main()`". — feito em 2026-09-27
  > Nota: checado no parser (`Parser::parse`): variável não-const vira "Variável global 'x' precisa ser
  > 'const'", demais comandos viram "Comando solto no nível superior ... Mova-o para dentro de 'fn main()'".
  > Nota: `struct`, `error` e `import` só existem a partir das Fases 4–6; na Fase 2.5 o nível
  > superior aceita `fn`, `class` e `const`, e os demais entram quando forem implementados.
  > Não há variáveis globais mutáveis (ver Registro de decisões).
- [x] Arquivo principal exige exatamente uma `main`, em uma destas formas:
      `fn main() { }` ou `fn main(list<string> args) { }`. — feito em 2026-09-27
  > Nota: ausência → "Função 'main' não encontrada"; duplicata → "Declaração duplicada" (já existente).
- [x] `main` sempre é void. Formas aceitas:
      `fn main()`, `fn main() -> void`,
      `fn main(list<string> args)`, `fn main(list<string> args) -> void`.
      Único parâmetro permitido: `list<string>` (nome livre).
      Qualquer outro tipo de retorno ou outros parâmetros → erro semântico
      listando as formas aceitas. — feito em 2026-09-27
- [x] `args` recebe os argumentos da linha de comando que vêm depois do nome
      do arquivo (`cinza prog.cinza a b` → `["a", "b"]`). — feito em 2026-09-27
  > Nota: por consequência, as opções do interpretador (`--tokens`, `--ast`, `-h`) passam a vir ANTES do
  > arquivo (`cinza --ast prog.cinza`); tudo depois dele vai para `args`, mesmo `--ast` (ver Registro de
  > decisões). Makefile (`test-tokens`/`test-ast`) e a ajuda do programa foram atualizados.
- [x] `main` em módulo importado é erro semântico. — feito em 2026-09-28
  > Nota: feito em C5 (Fase 6); teste `c5_main_em_modulo`.
- [x] `main` não pode ser chamada pelo programa (`main();` é erro semântico). — feito em 2026-09-27
  > Nota: um método de classe chamado `main` continua permitido (a precedência de A10 o resolve antes).
- [x] Código de saída do interpretador: 0 em execução normal; 1 em erro de
      compilação ou erro de runtime não tratado. — feito em 2026-09-27
  > Nota: já era assim; `run_tests.py` agora exige código 1 em todo teste com `expect-error` (crash ou
  > outro código conta como falha).
- [x] Semântico verifica que inicializadores de `const` global são constantes.
      Expressão constante = literais (`int`, `decimal`, `string`, `bool`), operadores
      aritméticos, de comparação e lógicos, parênteses e outros `const` globais já declarados.
      Proibidos: chamadas de função, `new`, literais de `list`/`dict`/`pair`. — feito em 2026-09-27
  > Nota: `SemanticAnalyzer::isConstantExpr`. Erro de runtime na avaliação (ex.: overflow) acontece antes
  > da `main` e sai com código 1.
- [x] Executor: pré-registro → avaliação dos `const` → chamada da `main`. — feito em 2026-09-27
- [x] Converter todos os testes de `tests/` para o formato com `main`. — feito em 2026-09-27
  > Nota: conversão por script (declarações ficam fora, comandos vão para `fn main()`), mais três ajustes à
  > mão: `a1_escopo_global` (reescrito com `const` global homônimo de um local), `a9_eof_lexema` (erro
  > continua no fim do arquivo) e `a9_string_multilinha` (linha/coluna esperadas mudaram).
  > Nota: `a1_escopo_global.cinza` usa global mutável e uma `fn main() -> int` chamada pelo
  > programa; deve ser reescrito para testar que uma função não enxerga nem altera os locais
  > de quem a chamou (ex.: `fn inc(int count)` alterando só a cópia). `a7`/`a8` ganham uma
  > `main` vazia para que o erro testado continue sendo o primeiro.
- [x] `tests/run_tests.py` aceita a diretiva `// args: a b c` e testes cobrindo `args`,
      as formas inválidas de `main`, `main();` e a ausência de `main`. — feito em 2026-09-27
  > Nota: 20 testes `f25_*` (formas válidas e inválidas de `main`, `args`, comando solto, global mutável,
  > regras de `const`).
- [x] Converter `program.cinza` e os exemplos das seções C3 e C4 para o formato com `main`. — feito em 2026-09-27
- [ ] Atualizar o Apêndice A do livro (EBNF: `program = declaration*`).
  > Nota (2026-09-29): adiado — o livro será reescrito no futuro; o conteúdo entra na nova versão.
  > ⚠️ Nota: o livro não está neste repositório; fica para quem o mantém.


## B. Arquitetura do front-end (Fases 3 e 8)

### B1. 🔴 Tipos como estrutura (`TypeRef`), não como string

- [x] Criar `TypeInfo { Kind kind; std::vector<TypeRef> params; const void* decl; }` e `using TypeRef = const TypeInfo*;`. — feito em 2026-09-27
  > Nota: em `types.h`. Classes são identificadas por `name` (não por `decl`): há um só espaço de nomes
  > até os módulos (C5), quando o nome qualificado entra. `TypeInfo` guarda também o texto canônico
  > (`str()`), montado uma vez, usado só em mensagens de erro.
- [x] Criar `TypeContext` com hash-consing (cada tipo existe uma só vez; igualdade = comparação de ponteiros). — feito em 2026-09-27
  > Nota: instância única do processo (`TypeContext::instance()`); os tipos vivem até o fim porque o
  > executor lê `resolved_type` dos `CastExpr`. Teste de unidade `tests/unit_types.cpp`.
- [x] `Expr::resolved_type`, `Symbol`, `FieldInfo` e `MethodInfo` passam a usar `TypeRef`. — feito em 2026-09-27
  > Nota: também `ClassInfo::ctor_param_types` e o tipo de retorno da função em análise. Saíram APIs
  > sem uso baseadas em string (`resolveMethod`, `resolveField`, `existsInCurrentScope`).
- [x] Eliminar `extractTypeParams`, `substr(0,5) == "list<"` e `trim`. — feito em 2026-09-27
- [x] `make test` 100% verde, sem mudança de comportamento. — feito em 2026-09-27
  > Nota: as mensagens de erro mantêm o mesmo texto de tipo; testes de regressão
  > `b1_mensagens_tipos` e `b1_metodo_tipo_primitivo`.

### B2. 🟠 Resolução de nomes em slots (Fase 8)

- [x] O semântico anota cada `IdentifierExpr` com `Resolution { Local | Field | Global, slot }`. — feito em 2026-09-29
  > Nota: há também `Self` (o objeto atual), para `self` não depender de comparar o nome. As declarações
  > recebem o slot (`VarDeclStmt::res`, `ForStmt::iter_slot`, `ExceptClause::var_slot`; parâmetros ocupam
  > 0..n-1). Cada declaração tem um slot próprio na função (sem reaproveitar entre blocos), então sombra e
  > redeclaração em laço funcionam sem escopos em runtime. `MemberAccessExpr::field_index` dá o índice do
  > campo de objeto ou struct.
- [x] Cada função guarda seu número de slots locais; o frame vira um `std::vector<Value>`. — feito em 2026-09-29
  > Nota: `FunctionDecl::num_slots` e `Constructor::num_slots`. Os campos de `ClassInstance` também viraram
  > `std::vector<Value>` (na ordem da declaração), e os `const` globais de todos os módulos (e os de módulos
  > nativos, como `Math.pi`) ficam num vetor de slots (`GlobalLayout`). `environment.h` foi removido.
- [x] O executor acessa variáveis por índice, sem hash de string. — feito em 2026-09-29
  > Nota: `Executor::slotOf`. `locals` aponta para o buffer do frame atual, que não muda de lugar enquanto
  > o frame vive. Teste de regressão `b2_slots`. Continuam por nome, fora do escopo do B2: funções, classes
  > e métodos (registros do executor).
- [x] Benchmark antes e depois registrado aqui: — feito em 2026-09-29
  > Nota: `bench/medir.ps1` (melhor de 5 execuções, build `-O3`, mesma máquina):
  >
  > | Programa | Antes | Depois | Ganho |
  > |---|---|---|---|
  > | `fib.cinza` (recursão, `fib(27)`) | 298 ms | 174 ms | 1,7× |
  > | `laco.cinza` (laço com locais, 2 milhões de voltas) | 829 ms | 329 ms | 2,5× |
  > | `objetos.cinza` (métodos com campos, 500 mil chamadas) | 387 ms | 213 ms | 1,8× |

### B3. 🟠 Despacho por `switch` em vez de `dynamic_cast`

- [x] Adicionar `enum class NodeKind` em `Expr` e `Stmt`, preenchido pelos construtores. — feito em 2026-09-27
  > Nota: um único `NodeKind` para expressões e statements, no membro `node_kind`.
- [x] Trocar as cadeias de `dynamic_cast` do semântico, do executor e de `allPathsReturn` por `switch` + `static_cast`. — feito em 2026-09-27
  > Nota: restam `dynamic_cast` pontuais no parser e no executor (checagens de um tipo só, não
  > despacho). `hasAnyReturn` foi removido: estava sem uso desde A10.

### B4. 🔴 Controle de fluxo sem exceções

- [x] Criar `enum class Flow { Normal, Return, Break, Continue }`; `executeStmt` retorna `Flow`. — feito em 2026-09-27
- [x] O valor de retorno fica em um membro do executor (`return_value`). — feito em 2026-09-27
  > Nota: consumido por `Executor::executeBody`, que substitui os três `try/catch` de função, método e
  > construtor.
- [x] Remover `ReturnSignal`. — feito em 2026-09-27
- [x] Laços e blocos propagam `Flow` corretamente. — feito em 2026-09-27
  > Nota: `while`/`for` já tratam `Break`/`Continue`, prontos para a Fase 4. Teste de regressão
  > `b4_fluxo_retorno` (return atravessando if, while, for, bloco, método e recursão), escrito e
  > verificado antes da mudança.

### B5. 🔴 Atribuição com lvalue genérico

- [x] Criar `AssignStmt { ExprPtr target; TokenType op; ExprPtr value; }`, substituindo `AssignmentStmt` e `IndexAssignmentStmt`. — feito em 2026-09-28
  > Nota: `NodeKind::Assign` substitui `Assignment` e `IndexAssignment`.
- [x] No parser: analisar a expressão; se vier um operador de atribuição, validar com `isLValue` (`Identifier`, `MemberAccess` ou `IndexAccess`). — feito em 2026-09-28
  > Nota: alvo inválido (ex.: `f() = 3;`) → "O lado esquerdo de '=' não é atribuível".
- [x] No semântico: checar `const` na raiz do lvalue e visibilidade de campos. — feito em 2026-09-28
  > Nota: `SemanticAnalyzer::constRoot` sobe do recipiente até a variável de origem e para ao passar
  > por objeto de classe (C3). A mesma função passou a guardar `.add`/`.remove` (antes só a variável
  > direta era checada). A leitura do alvo reaproveita `analyzeMemberAccess`/`analyzeIndexAccess`.
- [x] No executor: resolver o "lugar" uma única vez e só então ler ou escrever. — feito em 2026-09-28
  > Nota: duas etapas (`collectPlace` → valor → `walkPlace`): índices do alvo avaliados uma vez, da
  > esquerda para a direita, antes do valor; o percurso até o lugar não executa código do usuário, então
  > o valor pode alterar a coleção sem invalidar ponteiros (`b5_valor_altera_lista`). Limpo sob ASan.
- [x] Testes: `obj.campo = x`, `m[i][j] = x`, `obj.lista[0] = x`, `p.first = x` (deve ser erro: pair é imutável). — feito em 2026-09-28
  > Nota: `b5_*` (14 testes; 12 falhavam antes). Como todos os campos de classe são privados, `obj.campo = x`
  > é testado de dentro da classe e barrado de fora (`b5_campo_privado_fora`).
- [x] Implementar `self` (item adiado de A10), que depende do lvalue genérico: `self.nome = nome;`. — feito em 2026-09-28
  > Nota: palavra reservada; vale em métodos, construtor e inicializadores de campo; pode ser lido,
  > passado e retornado (`return self;`), mas não reatribuído. O executor guarda o objeto atual também
  > como `Value` (`current_self`). Testes `b5_self*`.

### B6. 🟢 Diagnósticos unificados

- [x] Criar `SourceLocation { file_id, line, col }` no `Token`. — feito em 2026-09-29
  > Nota: em `diagnostics.h`. O `Token` ganhou `loc()`, que devolve o `SourceLocation`; os campos `line`,
  > `column` e `file_id` continuam no token, para não mexer nas centenas de usos que já existem.
- [x] Criar um `DiagnosticEngine` único para lexer, parser, semântico e runtime, com formato `arquivo:linha:coluna: Tipo: mensagem`. — feito em 2026-09-29
  > Nota: `diagnostics()` (instância única) recebe os erros de todas as fases, e o `main.cpp` os imprime em
  > stderr com `flush`. Tipos: `LexicalError`, `SyntaxError`, `ImportError`, `SemanticError` e os de
  > runtime. O arquivo principal também aparece pelo nome (antes só os módulos), com `/` como separador.
  > Sem posição: `arquivo: Tipo: mensagem` (ex.: arquivo que não abre). O stack trace segue o mesmo padrão:
  > `em f (prog.cinza:3)`. Saíram o cabeçalho "Erros encontrados durante o parsing" e o aviso "Compilação
  > interrompida". `SemanticError::what()` e `RuntimeError::what()` usam o mesmo formato. Testes `b6_*`
  > (léxico, sintaxe com dois erros, semântico, runtime com trace); 7 testes antigos atualizados.
- [x] Erros de runtime exibem stack trace a partir dos frames (A1). — feito em 2026-09-28
  > Nota: feito com C4 — `Executor::call_stack` (nome e linha da chamada), anexado a todo `RuntimeError`
  > por `Executor::raise`.

### B7. 🟢 Custo das chamadas (melhoria, 2026-09-30)

- [x] O semântico resolve o alvo de cada chamada (`CallExpr::target`/`implicit_method`,
      `MethodCallExpr::target`); o executor não procura mais a função pelo nome. — feito em 2026-09-30
  > Nota: antes, toda chamada feita dentro de um método procurava o nome, com `dynamic_cast`, entre todos os
  > métodos da classe (mesmo para funções globais), e depois consultava o registro de funções por hash.
- [x] Nome do stack trace montado uma vez (`FunctionDecl::trace_name`, `Constructor::trace_name`); a pilha de
      chamadas guarda só o ponteiro. — feito em 2026-09-30
- [x] Argumentos avaliados direto nos slots do frame novo (`Executor::evalArgs`), sem vetor intermediário;
      `executeBody` sem `dynamic_cast`; `InstanceGuard` move o `self` anterior em vez de copiá-lo. — feito em 2026-09-30
  > Nota: `bench/medir.ps1`, melhor de 5: `fib` 174 → 128 ms, `objetos` 213 → 146 ms, `laco` sem mudança
  > (329 ms, não faz chamadas). Python 3.13 na mesma máquina: 59, 85 e 251 ms. `make test` verde, também sob
  > ASan/UBSan.

---

## C. Features da v2

### C1. Operadores (Fase 4)

- [x] Lexer: tokens `+=`, `-=`, `*=`, `/=`, `%=`. — feito em 2026-09-28
- [x] Lexer: palavras-chave `and`, `or`, `not`. — feito em 2026-09-28
  > Nota: geram os mesmos tokens de `&&`, `||` e `!` (mesma precedência); as mensagens de erro mostram
  > o operador como foi escrito (`c1_and_mensagem`, `c1_not_mensagem`).
- [x] 🟢 **Decidir e registrar aqui:** manter `&&`, `||` e `!` como sinônimos, ou removê-los com erro e dica? Decisão: **manter como sinônimos de `and`, `or`, `not`** — decidido em 2026-09-27
- [x] `a op= b` avalia o alvo **uma única vez**. Teste com `lista[f()] += 1`, em que `f` imprime algo. — feito em 2026-09-28
  > Nota: reaproveita o lugar em duas etapas do B5; a aritmética saiu de `evalBinary` para
  > `Executor::applyBinary`, usada pelos dois. Testes `c1_composto_*`.
- [x] Regra de tipo: o resultado de `a op b` deve ser atribuível ao tipo de `a` (`int x = 1; x += 1.5;` é erro). — feito em 2026-09-28
  > Nota: `c1_composto_tipo`; `const`, overflow e operador inválido também testados.
- [ ] Documentar no livro/spec: `/` inteiro trunca em direção a zero; o sinal de `%` segue o dividendo.
  > Nota (2026-09-29): adiado — o livro será reescrito no futuro; o conteúdo entra na nova versão.
  > ⚠️ Nota: o livro/spec não está neste repositório. O comportamento já é esse e está coberto por
  > `a5_limites_validos` (`7 / -2` = -3, `7 % -2` = 1, `-7 % 2` = -1).
- [ ] Atualizar a tabela de precedência (Apêndice A) com `not`, `and`, `or`.
  > Nota (2026-09-29): adiado — o livro será reescrito no futuro; o conteúdo entra na nova versão.
  > ⚠️ Nota: o livro não está neste repositório. Para constar: `not` tem a precedência do `!` (unário),
  > `and` a do `&&` e `or` a do `||`.

### C2. Controle de fluxo (Fase 4)

- [x] `break` e `continue`, com `loop_depth` no semântico (erro fora de laço). — feito em 2026-09-28
  > Nota: `loop_depth` é zerado ao entrar numa função, então o laço de quem chama não vale dentro dela
  > (`c2_continue_em_funcao`). O executor já tratava `Flow::Break/Continue` desde B4.
- [x] `for` sobre `dict<K,V>`, com iterador `pair<K,V>`. — feito em 2026-09-28
  > Nota: em ordem crescente de chave (o dict é um `std::map`); iterador `var` também funciona.
- [x] `for` sobre `string`, com iterador `string` de um caractere. — feito em 2026-09-28
  > Nota: caractere = code point UTF-8, coerente com `string.size()` (A10).
- [x] Função `range(a, b)` e `range(a, b, passo)`, com passo 0 gerando erro. — feito em 2026-09-28
  > Nota: embutida (uma `fn range` do usuário tem precedência); devolve um `list<int>` completo, então
  > intervalos enormes ocupam memória proporcional. Passo 0 → "ValueError: o passo de 'range' não pode
  > ser 0" (runtime); argumentos precisam ser `int`.

### C3. Struct (Fase 4)

Sintaxe alvo:
```
struct NameObject {
    string name = "<nome>";
    int value = 0;
    decimal values_d = 0.0;
    str others = "<others>";
}
fn main() {
    NameObject obj = new NameObject("Niel", 23, 1.65, "Analista de dados");
    obj.name = "Evan";
    print(obj.name);
}
// expect: Evan
```

- [x] Lexer: palavra-chave `struct`. — feito em 2026-09-28
- [x] AST: `StructDecl { name, std::vector<Field> }`, reaproveitando `ClassDecl::Field`. — feito em 2026-09-28
  > Nota: a leitura de campo saiu de `parseClassDeclaration` para `Parser::parseFieldDeclaration`,
  > usada por class e struct. No sistema de tipos, struct é `TypeInfo::Kind::Struct` (separado de
  > Class), para o `const` atravessar struct e parar em objeto.
- [x] Todos os campos são públicos e têm inicializador obrigatório (exceto `list`/`dict`). Sem métodos, sem bloco `pub`. — feito em 2026-09-28
  > Nota: `fn`/`pub` dentro de struct → erro de parser; chamada de método em struct → erro semântico.
  > Inicializadores rodam no escopo global (não enxergam outros campos). Struct só no nível superior (A8).
- [x] Construtor automático: `new S()` usa os valores padrão; `new S(a, b, ...)` exige **todos** os campos, em ordem. Número parcial de argumentos é erro. — feito em 2026-09-28
  > Nota: argumentos com tipo esperado e coerção `int → decimal` (A2/A3), como qualquer atribuição.
- [x] **Semântica de valor:** atribuir ou passar um struct copia o struct. Campos `list`, `dict` e classe continuam compartilhados (documentar essa regra). — feito em 2026-09-28
  > Nota: implementada no construtor de cópia de `Value` (clona o `StructValue`; os campos list/dict/
  > objeto copiam só o ponteiro). `l[0].x = v` altera no lugar (B5); o iterador do `for` é cópia.
  > Testes `c3_struct_copia`, `c3_struct_lugar` e checagens em `tests/unit_value.cpp`.
  > Decisão (2026-09-27): **cópia rasa**, confirmada. Implementar a cópia no próprio `Value` (toda
  > atribuição, argumento, `return` e `.add()` copia o struct automaticamente). Cuidados: `l[0].x = 1`
  > altera o struct dentro da lista (via B5); em `for (S s in l)` o iterador é cópia; `const` é raso
  > (`t.alunos.add(...)` passa) — decidido: `const` fecha o recipiente e para no objeto de classe (ver itens abaixo).
- [x] `==` estrutural, campo a campo. — feito em 2026-09-28
- [x] `const S o` proíbe `o.campo = ...`. — feito em 2026-09-28
- [x] `const` fecha o **recipiente** inteiro, em qualquer profundidade de `struct`/`list`/`dict`/`pair`:
      `o.campo = x`, `o.lista.add(x)`, `o.lista.remove(i)`, `o.lista[0] = x`, `o.interno.campo = x` são erro.
      Vale igual fora de struct (`const list<list<int>> m; m[0].add(1)` é erro; hoje só `.add` direto na
      variável é barrado). Checagem pela raiz do lvalue (B5). — feito em 2026-09-28
  > Nota: struct incluído (`c3_struct_const_*`).
  > Nota: para list/dict já feito em B5 (`b5_const_aninhado`, `b5_const_metodo_aninhado`); falta struct.
- [x] O `const` **para no objeto de classe**: em `const list<Pessoa> l = [...]` a lista não ganha, perde
      nem troca elementos, mas `l[0].nome = "x"` e `l[0].metodo()` são permitidos — o objeto não vira
      constante por estar num recipiente const. Idem para campo de classe dentro de struct const
      (`t.obj = outro` é erro; `t.obj.campo = x` é permitido). — feito em 2026-09-28
  > Nota: `constRoot` para ao encontrar um valor de classe no caminho (`b5_const_para_no_objeto`).
- [x] `const` diretamente num tipo de classe é erro semântico: `const Pessoa p = new Pessoa();` (variável
      local ou parâmetro `const Pessoa p`), com mensagem explicando que objetos de classe não podem ser const. — feito em 2026-09-27
  > Nota: feito antes da Fase 4, a pedido. Vale para variável local, global, parâmetro de função e de
  > construtor. Testes `c3_const_classe_*` e `c3_const_lista_de_objetos` (lista const de objetos continua
  > válida e os objetos seguem alteráveis). `f25_const_new` passou a usar `const int X = new C().get();`,
  > porque `const C c = new C();` agora é barrado antes pela regra nova.
- [x] Struct que contém a si mesmo por valor, direta ou indiretamente, é erro semântico. — feito em 2026-09-28
  > Nota: `pair` conta como valor; `list`/`dict` não (podem ficar vazios). A mensagem mostra o ciclo
  > ("A → B → A").
- [x] `print(obj)` gera `NameObject{name: "Evan", value: 23, values_d: 1.65, others: "Analista de dados"}`. — feito em 2026-09-28
- [x] 🟢 **Decidir e registrar aqui:** manter `str` como sinônimo de `string`? Decisão: **sim, `str` continua sinônimo de `string`** — decidido em 2026-09-27

### C4. Exceções (Fase 5)

Sintaxe alvo:
```
error SaldoInsuficiente;

fn main() {
    try {
        int n = convert.to_int(texto);
    } except (ValueError e) {
        print(e.message);
    } except (Error e) {
        print(e.kind, e.message, e.line);
    } finally {
        print("sempre roda");
    }
    throw SaldoInsuficiente("saldo: " + saldo);
}
```

- [x] Lexer: `try`, `except`, `finally`, `throw`, `error`. — feito em 2026-09-28
- [x] Tipo embutido `Error` com os campos `kind`, `message`, `line`, `column`. — feito em 2026-09-28
  > Nota: `Error` é a raiz (decisão: sem `Exception`); erros de runtime sem tipo próprio têm kind
  > `Error`. Campos somente leitura; erro não tem métodos. `print(e)` mostra "Tipo: mensagem".
- [x] Tipos de erro embutidos: `ValueError`, `IndexError`, `KeyError`, `ZeroDivisionError`, `OverflowError`, `IOError`, `StackOverflowError`. — feito em 2026-09-28
  > Nota: `builtinErrorKinds()` em `runtime_error.h`. `RuntimeError` agora carrega `kind`, `message` e
  > `trace`; mensagens do executor que começam com "Tipo: " viram esse tipo. `IOError` ainda não tem
  > quem o lance (entra com `io`, C6).
- [x] Declaração de nível superior `error Nome;` registra um novo tipo de erro. — feito em 2026-09-28
  > Nota: filho de Error; `Nome("mensagem")` cria o erro (vale para os embutidos também).
  > Só no nível superior (A8/Fase 2.5).
- [x] Semântico: `e` visível só no bloco `except`. — feito em 2026-09-28
- [x] Semântico: `except (Error e)` precisa ser o último (senão os seguintes ficam inalcançáveis — erro). — feito em 2026-09-28
  > Nota: `except` repetido para o mesmo tipo também é erro (o segundo nunca seria alcançado).
- [x] Semântico: `return`, `break` e `continue` proibidos dentro de `finally`. — feito em 2026-09-28
  > Nota: laços dentro do finally continuam aceitando break/continue; só sair do finally é proibido.
- [x] Executor: `finally` roda em todos os casos, inclusive quando o próprio `except` lança (usar `std::exception_ptr`). — feito em 2026-09-28
  > Nota: `Executor::executeTry`; um `return` pendente no try é preservado mesmo se o finally chamar
  > funções. Erros internos do C++ não são capturáveis, mas o finally roda antes deles subirem.
- [x] Erros de runtime da linguagem viram exceções capturáveis. — feito em 2026-09-28
  > Nota: IndexError, KeyError (inclusive `d[k] = v` com chave nova), ZeroDivisionError, OverflowError,
  > StackOverflowError e ValueError (`range` com passo 0) — `c4_captura_generica`.
- [x] Erro não tratado imprime tipo, mensagem e stack trace. — feito em 2026-09-28
  > Nota: "Tipo [linha L, col C]: mensagem" e uma linha "em função (linha N)" por chamada; pilhas
  > fundas mostram as 10 mais internas, o total omitido e a mais externa. `throw e;` preserva o trace de
  > origem.
- [x] Testes: captura específica, captura genérica, relançamento, `finally` após `return` no `try`, exceção dentro do `except`. — feito em 2026-09-28
  > Nota: `c4_*` (18 testes).

### C5. Módulos (Fase 6)

Sintaxe alvo:
```
import math;
import util.texto as tx;

pub fn soma(int a, int b) -> int { return a + b; }
fn auxiliar() -> int { return 0; }
```

- [x] Lexer: `import`, `as`. `pub` passa a ser válido no nível superior. — feito em 2026-09-28
  > Nota: `pub` no nível superior só antes de `fn`/`class`/`struct`/`error`/`const`; `import` só no início do
  > arquivo (depois de uma declaração é erro de sintaxe); apelido repetido é erro.
- [x] `ModuleLoader`: resolve o caminho relativo ao arquivo que importa e depois `CINZA_PATH`; mantém cache por caminho canônico. — feito em 2026-09-28
  > Nota: `module_loader.h/.cpp`. `import util.texto;` procura `util/texto.cinza`; `CINZA_PATH` separa as
  > pastas com `;` no Windows e `:` nos demais. Módulo não achado: `ImportError` listando os locais
  > procurados. `run_tests.py` ganhou a diretiva `// env: NOME=valor` para testar o `CINZA_PATH`.
- [x] Detecção de importação circular (DFS com três cores), com a mensagem mostrando o ciclo completo. — feito em 2026-09-28
  > Nota: `ImportError ...: import circular: a.cinza → b.cinza → a.cinza`.
- [x] Cada módulo tem seu próprio escopo global; `import` declara um `ModuleSymbol`. — feito em 2026-09-28
  > Nota: em vez de um `ModuleSymbol`, cada módulo tem um `ModuleScope` (nomes globais, exportados e
  > apelidos → módulo). Os nomes de nível superior dos módulos importados são reescritos no AST como
  > `prefixo::nome` (`modulos.geom::area`), então o executor segue com um só espaço de nomes; nas mensagens
  > o prefixo é removido. Nome de outro módulo sem o apelido não é visível; variável ou parâmetro com o
  > nome de um apelido é erro semântico.
- [x] Nomes sem `pub` são privados ao módulo. — feito em 2026-09-28
- [x] O semântico reescreve `mod.f(...)` como chamada qualificada. — feito em 2026-09-28
  > Nota: o parser já reconhece `apelido.nome` (só para apelidos importados no arquivo) em chamada,
  > identificador, `new`, tipo e `except`; o semântico troca pelo nome completo.
- [x] `parseType` aceita `IDENT ('.' IDENT)*`. — feito em 2026-09-28
  > Nota: aceita `apelido.Nome` (um nível, que é o que o apelido permite). O nó `Type` guarda o token onde
  > foi escrito, para a posição das mensagens.
- [x] O código de nível superior de cada módulo roda uma vez, em ordem topológica. — feito em 2026-09-28
  > ⚠️ Nota: com a Fase 2.5 o nível superior é declarativo; o que roda em ordem topológica
  > é só a avaliação dos `const` de cada módulo.
- [x] As ASTs de todos os módulos ficam vivas até o fim da execução (o executor guarda ponteiros para elas). — feito em 2026-09-28
  > Nota: o `ModuleLoader` é dono dos `Program` e vive em `main.cpp` até o fim da execução.
- [x] Mensagens de erro incluem o nome do arquivo (depende de B6). — feito em 2026-09-28
  > Nota: cada token guarda o `file_id` do arquivo. Erros léxicos, de sintaxe, semânticos, de runtime e o
  > stack trace mostram o arquivo quando ele é um módulo (`[tests/modulos/falha.cinza, linha 3, col 12]`);
  > as do arquivo principal mantêm o formato antigo. O formato unificado fica com o B6.
- [x] `main` em módulo importado é erro semântico (item vindo da Fase 2.5). — feito em 2026-09-28

### C6. Biblioteca padrão (Fases 6 e 7)

- [x] Interface de funções nativas: — feito em 2026-09-28
  ```cpp
  struct NativeFn {
      std::string name;
      std::vector<TypeRef> params;
      TypeRef ret;
      Value (*impl)(Executor&, std::span<const Value>);
  };
  ```
  > Nota: `natives.h/.cpp`; a struct ganhou `min_params` (parâmetros opcionais, ex.: o passo de `range`) e
  > `variadic` (`print`). Erro de uma nativa leva a posição da chamada.
- [x] Variáveis de tipo (`T`) permitidas somente em assinaturas nativas, com unificação simples. — feito em 2026-09-28
- [x] `print` deixa de ser `KW_PRINT` e vira função nativa de `io`, exposta globalmente por um prelude. — feito em 2026-09-28
  > Nota: `print` e `range` estão no prelude; uma função do usuário com o mesmo nome tem precedência. O
  > namespace `io` em si entra com a Fase 7.

Namespaces (decisões de 2026-09-29, ver Registro de decisões): nomes com inicial maiúscula, usados
com import (`import Strings as st;` → `st.upper(nome)`), e nomes curtos dentro deles (`Files.read`,
não `Files.read_file`). Não há namespace `io`.

- [x] Embutidos no prelude, sem import: `print` (já feito) e `input(prompt) -> string`. `println` não
      existe (`print` já pula linha). — feito em 2026-09-29
  > Nota: fim da entrada em `input` é `IOError`. `run_tests.py` ganhou a diretiva `// stdin: linha`.
- [x] Módulos nativos no sistema de módulos da C5: `import Math;` acha o módulo nativo (escrito em C++),
      com `as`, nomes qualificados e mensagens de erro iguais aos dos módulos `.cinza`. — feito em 2026-09-29
  > Nota: `NativeModule` (funções + constantes, como `Math.pi`) em `natives.h`. O semântico anota a
  > `CallExpr` com a nativa resolvida, e o executor a chama direto. Variáveis de tipo com restrição:
  > `número` (int/decimal) e `comparável` (int/decimal/string). Nativas que alteram a lista (`Lists.sort`,
  > `Lists.reverse`) não aceitam lista const.
- [x] Nomes reservados: um arquivo do usuário com o nome de um módulo nativo (`Strings.cinza`,
      `Math.cinza`, ...) é erro na hora, em qualquer arquivo carregado. — feito em 2026-09-29
  > Nota: vale para o arquivo principal, para módulos importados e para um arquivo de mesmo nome achado no
  > caminho de busca de `import Math;`. O caso do arquivo principal não tem teste automático: um
  > `tests/Files.cinza` colidiria com o `import Files;` dos outros testes da pasta (a própria regra).
- [x] `Files`: `read`, `write` (lançam `IOError`) — feito em 2026-09-29
- [x] `Math`: `pi`, `e`, `sqrt`, `pow`, `abs`, `floor`, `ceil`, `round`, `min`, `max`, `sin`, `cos`, `log` — feito em 2026-09-29
- [x] `Strings`: `upper`, `lower`, `trim`, `split -> list<string>`, `join`, `contains`, `replace`, `substr`, `find`, `starts_with` — feito em 2026-09-29
- [x] `Convert`: `to_int(string)` (lança `ValueError`), `to_decimal`, `to_string`, `to_bool` — feito em 2026-09-29
- [x] `Lists`: `sort`, `reverse`, `contains`, `index_of`, `slice`, `sum` — feito em 2026-09-29
- [x] `Random`: `seed`, `int(a, b)`, `decimal()`, `choice` (estado em `std::mt19937_64`) — feito em 2026-09-29
- [x] Cada função com pelo menos um teste de sucesso e um de erro. — feito em 2026-09-29
  > Nota: sucesso em `f7_<módulo>`; erros de runtime em `f7_<módulo>_erros` e, para as funções que não
  > falham em runtime, erro de tipo ou de aridade em `f7_erro_<módulo>_<função>`. Decimais com valor
  > inteiro continuam impressos sem `.0` (`Math.sqrt(16.0)` → `4`), como no resto da linguagem.
- [x] Parser aceita palavra-chave logo depois de `apelido.` (`Random.int(1, 6)`, `Random.decimal()`). — feito em 2026-09-29

---

## D. Trabalho futuro (fora da v2)

- [x] Tipo `op<T1, T2, ...>`: o usuário escolhe quais tipos um lugar aceita (`op<decimal, int> n = 1;`,
      `op<string, bool, int> status = "ok";`). Substitui o `option<T>` planejado (decisão de 2026-09-30).
  > Regras (2026-09-30): pelo menos 2 tipos; sem repetir tipo; a ordem não importa; sem `void` e sem `var`;
  > `op` dentro de `op` é erro (em qualquer profundidade); `op` não pode ser chave de `dict`; um literal usa o
  > tipo exato se ele estiver na lista; aceita primitivos, `list`, `dict`, `struct` e classes; regra 9: no máximo
  > um `list`, um `dict` e um `pair` por `op`. O tipo atual é descoberto com a nativa `type()`.
  > Revisto no mesmo dia (primeiro foi feito com uso livre e `TypeError` em runtime; o autor decidiu que isso
  > contraria a filosofia da Cinza). **Nada é dinâmico: o tipo trava na inicialização e nunca muda.**
  > - Local ou `const` com valor de tipo conhecido vira esse tipo: `op<decimal, int, string> coisa = 1;` é `int`
  >   para o compilador (`coisa = "x";` é erro de compilação). O valor inicial é obrigatório, inclusive em
  >   coleções de op e em campos.
  > - Onde o compilador não sabe o tipo travado (parâmetro, retorno, campo, iterador), toda operação precisa
  >   valer para **todos** os tipos do op (`TypeChecker::expand` lista os tipos concretos possíveis);
  >   `if (type(x) == T) { ... }` estreita `x` para `T` dentro do bloco (variável, parâmetro ou campo; é seguro
  >   porque o tipo travado nunca muda). Método, campo, `[]` e `for` direto num op pedem esse estreitamento.
  > - `list<op<A, B>>` (e dict/pair): todos os elementos do mesmo tipo — é uma `list<A>` ou uma `list<B>`. Uma
  >   coleção de op com tipo travado desconhecido (ex.: recebida por parâmetro) é somente leitura (`add`,
  >   `remove`, `[] =`, `Lists.sort`...), para não quebrar o tipo da coleção original. `type()` não vale numa
  >   coleção de op (ela não guarda o tipo dos elementos em runtime).
  > - Trocar o valor de quem travou num tipo desconhecido só com um valor que sirva para todos os tipos; o tipo
  >   travado se mantém (`v = 5;` guarda 5.0 se `v` travou em decimal — `AssignStmt::keep_lock`).
  > - Nenhuma conferência de tipo em runtime; a única conversão é int → decimal. `TypeError` continua como
  >   erro embutido (decisão do autor), mas o op não o lança.
  > - Estruturas recursivas (lista ligada, árvore) usam `list<No>`: vazia = sem próximo (teste
  >   `lista_ligada_list`). O `op<No, bool>` não serve mais para isso: o campo travaria em `bool`.
  > - Campo de classe com op trava no inicializador do campo; o construtor não escolhe o tipo. Em `struct`, os
  >   valores do `new S(...)` são a inicialização, então travam no tipo passado.
  >   Mantido de propósito (decisão do autor, 2026-09-30): nada de tipo escolhido depois da inicialização.
  > - Testes `op_*` (32) e `lista_ligada_list`; teste de unidade da ordem canônica em `unit_types`. `make test`
  >   verde, também sob ASan/UBSan.
- [x] `enum` (decisões de 2026-09-30): `enum Cor { Vermelho, Verde, Azul }` no nível superior (`pub` para
      exportar). — feito em 2026-09-30
  > Nota: valores sempre qualificados (`Cor.Verde`; de módulo, `cr.Cor.Verde`); são só nomes, sem int por trás
  > (`Cor c = 1;` e `c + 1` são erros); só `==` e `!=`; pode ser chave de dict (e chave repetida num literal é
  > erro); o `print` mostra `Cor.Verde`. Vale em `struct`, `const`, `op<...>` e com `type()`. Em runtime é um
  > `Value::Kind::ENUM` (declaração + índice do valor). Testes `enum_*` (15).
- [x] Interfaces (decisões de 2026-09-30): `interface Forma { fn area() -> decimal; }` no nível superior (`pub`
      para exportar); `class Circulo : Forma, Desenhavel { ... }`. — feito em 2026-09-30
  > Nota: só assinaturas de métodos (sem código e sem campos: não é herança); só `class` cumpre (struct não tem
  > métodos); uma classe pode cumprir várias. Tudo conferido na compilação: a classe precisa ter cada método em
  > `pub { }` com exatamente a mesma assinatura; por um valor do tipo interface só se chamam os métodos dela, e
  > ele nunca volta a ser tratado como a classe (decisão do autor). `list<Circulo>` não vira `list<Forma>`
  > (invariância, A3); um literal `[new Circulo(...), new Retangulo(...)]` numa `list<Forma>` vira. `type(f)`
  > dá a classe real. `op` não aceita interface. Em runtime, o semântico monta para cada classe a tabela dos
  > métodos de cada interface (`ClassDecl::itables`), e a chamada pela interface escolhe o método por ela.
  > Correção junto: o iterador do `for` não aceitava tipo de módulo (`for (fm.Forma f in l)`); coberto por
  > `iface_modulo`. Testes `iface_*` (18).
- [x] Especificação da linguagem em `spec/` (Markdown, 7 capítulos: léxico, gramática em EBNF, tipos,
      declarações, execução, biblioteca padrão, erros e diagnósticos). — feito em 2026-10-02
  > Nota: os blocos ` ```cinza ` com `// expect:`/`// expect-error:` são exemplos executáveis — o
  > `run_tests.py` os extrai para `tests/spec_exemplos/` (fora do git) e os roda junto com a suíte (56
  > exemplos). Toda afirmação foi conferida no código e nos testes; quem mudar a linguagem atualiza a spec.
- [x] ~~Genéricos do usuário~~ — descartado em 2026-09-30: a Cinza não terá genéricos (decisão do autor).
- [x] Coleta de ciclos: `shared_ptr` vaza memória em ciclos como `class A { list<A> xs; }` com `a.xs.add(a)`. — feito em 2026-09-30
  > Nota: `gc_object.h` e `gc.h`. Algoritmo "trial deletion", como no CPython: todo contêiner (list, dict,
  > pair, objeto, struct) herda `GcObject` (herança só na implementação em C++) e fica numa lista global. A
  > coleta parte das contagens do `shared_ptr`, desconta as referências internas, marca o que é alcançável a
  > partir do que sobrou (frames, globais, temporários do executor) e esvazia o resto, que a contagem então
  > libera. Não precisa enumerar temporários do C++. Disparada em `evalNew` (todo ciclo passa por objeto;
  > ponto seguro, sem ponteiro cru para dentro de contêiner), a cada 10 mil contêineres novos ou tantos
  > quantos sobreviveram na última coleta (custo amortizado constante). A liberação normal continua sendo a
  > contagem de referências. `CINZA_GC_STATS=1` faz uma coleta final e mostra quantos contêineres foram
  > liberados em ciclos (usado pelos testes). Testes `gc_*` (5: ciclo simples, várias coletas preservando
  > ciclos alcançáveis, coleta dentro de construtor, ciclos por op/dict, sem ciclo) e `tests/unit_gc.cpp`.
  > `bench/ciclos.cinza` (300 mil pares em ciclo): pico de 30,7 MB, contra 225,8 MB com os mesmos pares
  > presos; sem mudança nos outros benchmarks. `make test` verde, também sob ASan/UBSan.
- [ ] CVM: máquina virtual própria (AST → bytecode → laço de execução em C++), partindo dos slots de B2 e
      do `Flow` de B4 e reaproveitando o runtime atual (`Value`, coleções, `stdlib.cpp`, erros). O interpretador
      atual fica como implementação de referência: os testes rodam nos dois modos (decisão de 2026-09-29,
      revista no mesmo dia: substitui o backend LLVM por ser bem mais simples de implementar).

---

## E. Revisão bruta (2026-10-01)

~150 programas-sonda cobrindo léxico, precedência, fluxo, funções, classes, struct, coleções, const,
exceções, `op`, `enum`, interfaces, nativas e nível superior; mais leitura das mensagens do executor.

Defeitos corrigidos (cada um com teste `rev_*`):

- [x] `decimal` impresso com só 6 dígitos significativos (`123456.789` → `123457`, `1234567.5` →
      `1.23457e+06`); `Convert.to_string` não voltava ao mesmo número. Agora a menor forma que volta ao
      mesmo número (`std::to_chars`); valor inteiro continua sem `.0`. — feito em 2026-10-01
- [x] Método declarado duas vezes na classe (inclusive dentro e fora de `pub { }`) era aceito e o segundo
      sumia em silêncio. Agora é erro. — feito em 2026-10-01
- [x] Nome de função usado como valor (`print(f)`) compilava e falhava em runtime ("Variavel nao
      encontrada"). Agora é erro de compilação. — feito em 2026-10-01
- [x] Expressão `void` como argumento de nativa (`print(f())`) imprimia `void`. Agora é erro. — feito em 2026-10-01
- [x] Atribuição a campo de struct temporário (`mk().x = 5;`) compilava e se perdia numa cópia. Agora é
      erro com explicação. — feito em 2026-10-01
- [x] `while (true) { ... return ...; }` sem `break` dava "não retorna em todos os caminhos". Agora conta
      como retorno (um `break` que saia do laço continua exigindo o retorno). — feito em 2026-10-01
- [x] `throw Falhou;` dizia "recebeu 'type'"; agora explica que é preciso criar o erro. — feito em 2026-10-01
- [x] Mensagens sem acento ("indice", "nao", "dicionario", "Funcao", "Variavel"...) no executor, no parser e
      no semântico. — feito em 2026-10-01
- [x] Mensagens de atribuição mostravam `(expressão)` para chamadas e `new`; agora `mk().x`. — feito em 2026-10-01

Pontos que mudam a linguagem (aguardam decisão do autor):

- [x] Código depois de `return`/`break`/`continue`/`throw` no mesmo bloco é aceito em silêncio (código
      morto). Tornar erro de compilação? — feito em 2026-10-01
  > Decidido (2026-10-01): sim, erro de compilação. Também conta como saída um `if`/`else` com os dois ramos
  > saindo, um `while (true)` sem `break` e um `try` com o bloco e todos os `except` saindo
  > (`SemanticAnalyzer::analyzeStatements`). Testes `rev_codigo_morto_*` e `rev_codigo_alcancavel`.
- [ ] Campo e método com o mesmo nome (`int x` e `fn x()`), e método com o nome da classe (`fn A()` ao
      lado do construtor `A()`), são aceitos. Proibir?
  > Decidido (2026-10-01): continuam aceitos — campo e método são usados de formas diferentes (`x` e `x()`).
- [ ] `dict.add({k, v})` com chave existente substitui em silêncio, enquanto `d[k] = v` com chave inexistente
      é erro. Tornar `add` em chave existente um `KeyError` (simétrico)?
  > Decidido (2026-10-01): mantém como está (`add` substitui).
- [ ] Comentário de bloco `/* ... */` não existe (só `//`). Acrescentar?
  > Decidido (2026-10-01): não, por enquanto.
- [ ] Decimal muito grande ou muito pequeno sai em notação científica (`1e+24`), que não é um literal válido
      da Cinza (o lexer não aceita expoente; `Convert.to_decimal` aceita). Aceitar expoente nos literais?
  > Decidido (2026-10-01): não aceitar expoente nos literais.
- [x] Overflow de `decimal` dá `inf` e operações seguintes dão `nan` (`print` mostra `inf -nan`), sem erro —
      enquanto `int` lança `OverflowError` (A5) e o NaN silencioso do `%` já foi corrigido (A10). Lançar
      `OverflowError` quando uma operação de `decimal` der infinito?
  > Decidido (2026-10-01): sim. Feito em 2026-10-01: `+ - * /` de decimal (e `Lists.sum`) que dão infinito
  > lançam `OverflowError: ... excede a faixa de decimal`; com isso o `nan` também não aparece mais. Literal
  > gigante já era barrado pelo lexer. Teste `rev_decimal_overflow`.
- [ ] Para registro, sem proposta de mudança: `decimal x = 7 / 2;` dá `3` (divisão inteira antes da
      conversão, como em C e Java).

---

## F. CVM — máquina virtual da Cinza (alinhamento de 2026-10-02)

Regras:

1. A especificação (`spec/`) é a lei: mesmas saídas, erros, mensagens e stack trace do interpretador.
   Diferença de comportamento entre os dois é defeito.
2. O `make test` roda a suíte inteira (testes e exemplos da spec) no interpretador e na CVM; uma
   etapa só fecha com tudo verde nos dois, também sob ASan/UBSan.
3. Nada dinâmico: a CVM confia no semântico; instruções específicas por tipo; em runtime só as
   conferências que a spec manda (overflow, índice, chave, divisão por zero, recursão).
4. Reaproveita lexer, parser, semântico, `Value`, coleções, biblioteca padrão, coleta de ciclos e
   diagnósticos; o novo é o compilador AST → bytecode e o laço de execução.
5. Etapas pequenas, cada uma com testes, benchmark (`bench/`, contra o interpretador e o Python) e commit.
6. Meta: pelo menos 2× mais rápida que o interpretador; mirar alcançar ou passar o Python em `fib` e no laço.

Decisões do autor:

- Arquitetura de **registradores** (como a VM do Lua 5), aproveitando os slots do B2.
- A CVM vira o modo padrão quando estiver completa; o interpretador fica como **referência**
  (opção `--interp`), e os testes rodam nos dois.
- Bytecode **só em memória** por ora (formato de arquivo fica para quando estiver estável).
- **Desmontador**: opção `--bytecode` mostra as instruções de cada função.

Etapas:

- [x] 1. Desenho do conjunto de instruções e do formato das funções compiladas (registradores,
      constantes, tabela de linhas para diagnósticos). — feito em 2026-10-02 (cvm/DESENHO.md, revisado)
- [x] 2. Funções, chamadas, variáveis locais, aritmética e comparação; `--bytecode`. — feito em 2026-10-02
  > Nota: `cvm/bytecode.h`, `compiler.cpp`, `vm.cpp`, `disasm.cpp`. Opções `--cvm` e `--bytecode` (o padrão
  > continua o interpretador até a etapa 9); `make test-cvm` / `run_tests.py --cvm`. A aritmética saiu do
  > `Executor` para `operacoes.cpp` (`applyBinaryOp`, `negateOp`), compartilhada com a VM: mesmas regras e
  > mensagens. As nativas não recebem mais o `Executor&` (nenhuma o usava). Recurso ainda não compilado vira
  > `CVMError: ... ainda não é suportado pela CVM (etapa N)`. Suíte no modo CVM: 258 de 387; as 129 falhas são
  > todas recursos de etapas seguintes — nenhuma divergência. Limpo sob ASan/UBSan nos dois modos.
- [x] 3. Controle de fluxo: `if`, `while`, `for`, `break`, `continue`, `return`. — feito em 2026-10-02
  > Nota: `FORPREP`/`FORNEXT`/`FORNEXT_D`; o `R[a+2]` do laço guarda o número do slot do iterador (decidido
  > pelo semântico, não pode ser forçado a `a+2`). `while (true)` sem teste. Testes `cvm_fluxo_*` e
  > `cvm_recursao` (stack trace do estouro idêntico nos dois modos). Suíte na CVM: 266 de 391, sem
  > divergência. Benchmark (`bench/medir.ps1`, agora nos dois modos): `fib` 122 → 50 ms (2,4×), `laco`
  > 315 → 89 ms (3,5×); Python 3.13: 59 e 251 ms. Limpo sob ASan/UBSan.
- [x] 4. Coleções: `list`, `dict`, `pair`, literais, índices, métodos embutidos, `const`. — feito em 2026-10-02
  > Nota: índices e métodos embutidos saíram do `Executor` para `operacoes.cpp` (`indexGet`, `indexPlace`,
  > `builtinFor`/`callBuiltin`), compartilhados com a VM. Instruções `NEWLIST`, `NEWDICT`, `NEWPAIR`,
  > `GETINDEX`, `INDEXPLACE` (caminho até o lugar, com as mensagens de gravação), `SETINDEX`, `CALLBUILTIN`,
  > `GETFIRST`/`GETSECOND`. Atribuição a índice na ordem do interpretador (base, índices, valor, caminho,
  > gravação). `const` é só do semântico. Testes `cvm_colecoes*`. Suíte na CVM: 294 de 393, sem divergência
  > (o resto é das etapas 5 a 7). Limpo sob ASan/UBSan.
- [x] 5. `struct`, classes, `self`, campos, métodos, construtores. — feito em 2026-10-02
  > Nota: métodos e construtores são protótipos com o `self` em `r0` (slots do B2 deslocados de 1); chamada
  > de método é um `CALL` com o objeto no começo da janela. `new Classe(args)` na ordem do interpretador:
  > argumentos, `NEWOBJ`, inicializador de campos (protótipo oculto: fora do stack trace e do limite de 2000,
  > como no interpretador), construtor. `new Struct()` calcula os valores padrão no próprio chamador
  > (`NEWSTRUCT`). Gravação em lugar com campos e índices (`assignPlace`): cada nível que é struct é lido,
  > alterado e devolvido, depois do valor (desenho, seção 7) — testes `cvm_struct_lugar_*`. Coleta de ciclos
  > no `NEWOBJ`/`NEWSTRUCT`, e `CINZA_GC_STATS` com as mesmas contagens nos dois modos. Suíte na CVM: 338 de
  > 393, sem divergência (o resto é das etapas 6 e 7). Benchmark: `objetos` 176 → 128 ms (1,4×), `ciclos`
  > 929 → 796 ms — o custo está no mecanismo da chamada de método (frame, limpeza da janela, cópia do
  > `shared_ptr`), não no bytecode; fica para as otimizações (desenho, seção 11). Limpo sob ASan/UBSan.
- [x] 6. Interfaces, `enum`, `op<...>`, `type()`. — feito em 2026-10-02
  > Nota: `runtimeType`, `narrowValue` e `keepLock` saíram do `Executor` para `operacoes.cpp`, compartilhados.
  > `CALLIFACE` escolhe o protótipo pela tabela de interface da classe real do objeto; valores de enum e tipos
  > escritos como valor são constantes (`LOADK`); `TYPEOF`, `CAST` (conversões de/para op), `KEEPLOCK` e as
  > operações genéricas `ADD`...`LE`/`NEG` só para operandos op de tipo travado desconhecido. Suíte na CVM:
  > 359 de 393, sem divergência — as 34 restantes são todas exceções (etapa 7). Limpo sob ASan/UBSan.
- [x] 7. Exceções: `try`/`except`/`finally`/`throw`, stack trace, `StackOverflowError`. — feito em 2026-10-02
  > Nota: tabela de tratadores por protótipo (mais interno primeiro); um erro procura tratador do frame do erro
  > para fora, limpando a janela de cada frame desempilhado. `finally` compilado UMA vez, com ação e valor
  > pendentes e despacho no fim (decisão de 2026-10-02, desenho seção 6, no lugar da cópia com intervalos
  > partidos). `NEWERROR`, `ERRFIELD`, `THROW` (relançar preserva tipo, posição e trace). O laço de despacho
  > foi para `VM::dispatch`, separado do `try`/`catch` (`VM::handle`): sem isso as variáveis quentes iam para
  > a memória e o `laco` piorava ~30%. Teste `cvm_finally` (try aninhados, break/continue/return por dois
  > finally, erro no finally e no except, relançamento com trace). **Suíte inteira na CVM: 394 de 394**, sem
  > divergência. Limpo sob ASan/UBSan.
- [x] 8. Módulos, `const` globais, funções nativas e biblioteca padrão. — feito em 2026-10-02
  > Nota: já funcionavam desde as etapas 2 a 7 (const de cada módulo num protótipo oculto, em ordem
  > topológica; nativas por `CALLNATIVE`, constantes nativas já nos slots). A etapa fechou com a prova:
  > **teste diferencial** (`run_tests.py --diff` / `make test-diff`) que roda cada programa nos dois modos e
  > compara código de saída, stdout e stderr byte a byte — 397 de 397 idênticos, também sob ASan/UBSan — e as
  > ~170 sondas da revisão bruta mais sondas novas de módulos e da biblioteca padrão, todas idênticas. Opção
  > `--interp` já aceita. Testes `cvm_modulo*` (módulos encadeados, erro em método de outro módulo, erro na
  > avaliação de const de módulo).
- [x] 9. CVM como padrão, `--interp` para o interpretador; suíte nos dois modos; benchmark final. — feito em 2026-10-02
  > Nota: `cinza prog.cinza` executa na CVM; `--interp` usa o interpretador de referência; `--cvm` continua
  > aceito. `make test` = testes de unidade + suíte na CVM + suíte no interpretador + diferencial (397/397/397
  > idênticos, também sob ASan/UBSan); `make test-interp` e `make test-diff` à parte. Spec 7.4, CLAUDE.md e
  > README atualizados.
  >
  > Benchmark final (`bench/medir.ps1`, melhor de 5, mesma rodada; Python 3.13 na mesma máquina):
  >
  > | Programa | Interpretador | CVM | Ganho | Python |
  > |---|---|---|---|---|
  > | `fib` (recursão) | 131 ms | 58 ms | 2,3× | 61 ms |
  > | `laco` (laço com locais) | 368 ms | 115 ms | 3,2× | 276 ms |
  > | `objetos` (métodos e campos) | 160 ms | 119 ms | 1,3× | 90 ms |
  > | `ciclos` (criação de objetos) | 852 ms | 723 ms | 1,2× | — |
  >
  > Meta (≥2×) atingida em `fib` e `laco`, onde a CVM também passa o Python; não atingida nos programas com
  > muitos objetos — o custo está no mecanismo da chamada de método e na criação de objetos, alvo das
  > otimizações da seção 11 do desenho.
- [x] 10. Otimizações da seção 11 do desenho. — feito em 2026-10-03
  > Nota: cada uma entrou só com ganho medido e a suíte idêntica nos dois modos (401/401/401, também sob
  > ASan/UBSan). Acessores do `Value` por referência (a cópia do `shared_ptr` mexia no contador atômico a
  > cada leitura de campo); `for` sobre `range` embutido sem criar a lista (`RANGEPREP`/`FORRANGE`); `for`
  > sobre `string` sem a lista (um caractere UTF-8 por vez, ~2× num laço de caracteres); instruções int
  > com constante embutida (`ADDK_I`…`GEK_I`); compara-e-salta no `if`/`while` (`JLT_I`…`JGEK_I` + o `JMP`
  > seguinte num só despacho). O *computed goto* foi implementado e medido lado a lado com o `switch`: sem
  > ganho e com a armadilha de não destruir locais — descartado (registrado no desenho). `Value` sem
  > embrulho nos registradores ficou de fora: as medições não apontam o `Value` como gargalo. Testes
  > `cvm_for_range`, `cvm_constante_embutida`, `cvm_compara_salta`, `cvm_for_string`.
  >
  > | Programa | Interpretador | CVM antes | CVM agora | Ganho sobre o interpretador | Python |
  > |---|---|---|---|---|---|
  > | `fib` | 148 ms | 58 ms | 48 ms | 3,1× | 61 ms |
  > | `laco` | 386 ms | 115 ms | 94 ms | 4,1× | 276 ms |
  > | `objetos` | 169 ms | 119 ms | 77 ms | 2,2× | 90 ms |
  > | `ciclos` | 890 ms | 723 ms | 549 ms | 1,6× | — |
  >
  > A CVM agora passa o Python também em `objetos`. Em `ciclos` o custo restante é a coleta de ciclos e a
  > criação de objetos (runtime compartilhado), não o bytecode.

---

## G. Biblioteca padrão rica (2026-10-03)

Enriquecer os módulos nativos, um por vez, nesta ordem; cada um com spec (capítulo 6), testes e as
decisões de nome e comportamento tomadas pelo autor. Implementação única em `stdlib.cpp` (a CVM e o
interpretador chamam as mesmas nativas).

- [x] 1. `Files` — feito em 2026-10-03
  > Nota: de 2 para 23 funções: `lines`, `append`, `write_lines`, `append_line`, `exists`, `is_file`,
  > `is_dir`, `size` (bytes), `copy`/`move` (com `bool substituir`), `delete`, `make_dir`, `list_dir`,
  > `delete_dir` (só vazia), `delete_tree`, `current_dir` e os caminhos `join`, `name`, `extension`,
  > `parent`, `absolute`. Sobre `std::filesystem`; caminhos convertidos de/para UTF-8 explicitamente
  > (no Windows o `std::string` virava caminho pela página de código ANSI — nomes acentuados falhavam
  > também no `read`/`write` antigos); separador `/` na saída. Testes `files_texto`, `files_pastas`,
  > `files_caminhos`, `files_erros_novos`. 406/406/406, limpo sob ASan/UBSan.
- [x] 2. `Math` — feito em 2026-10-03
  > Nota: de 11 para 38 funções e 3 constantes: `tau`; `tan`, `asin`, `acos`, `atan`, `atan2`, `degrees`,
  > `radians`, `sinh`, `cosh`, `tanh`; `cbrt`, `exp`, `log10`, `log2`, `log_base`, `hypot`; `trunc`,
  > `round_to`; `gcd`, `lcm`, `is_even`, `is_odd`, `sign`, `clamp`, `factorial`, `is_prime`
  > (Miller-Rabin determinístico, exato nos 64 bits); `is_close`. Nenhuma produz infinito nem NaN.
  > `round_to` arredonda a forma decimal mais curta (a do `print`): `round_to(2.675, 2)` = `2.68`.
  > Testes `math_completo`, `math_erros`. 408/408/408, limpo sob ASan/UBSan.
- [x] 3. `Strings` — feito em 2026-10-03
  > Nota: de 10 para 44 funções: `ends_with`, `find_last`, `find_from`, `count`; `char_at`, `left`,
  > `right`, `slice`, `chars`, `lines`, `words`; `trim_start`, `trim_end`, `replace_first`, `remove`;
  > `repeat`, `reverse`, `pad_left`/`pad_right`/`center` (preenchimento opcional, padrão espaço),
  > `fixed` (casas fixas: `fixed(2.5, 2)` = `"2.50"`, mesmo arredondamento do `Math.round_to`, agora num
  > auxiliar comum); `capitalize`, `title`; `is_empty`, `is_digit`, `is_alpha`, `is_alnum`, `is_space`,
  > `is_upper`, `is_lower`; `ord`, `chr`; `equals_ignore_case`, `compare`. Índices em caracteres.
  > Testes `strings_completo`, `strings_erros`. 410/410/410, limpo sob ASan/UBSan.
- [x] 4. `Convert` — feito em 2026-10-03
  > Nota: de 4 para 15 funções: `is_int`, `is_decimal`, `is_bool`; `to_int_or`, `to_decimal_or`,
  > `to_bool_or`; `to_base`, `from_base` (bases 2 a 36), `to_hex`, `to_binary`, `to_octal`. A leitura
  > estrita virou auxiliar comum (`lerInt`/`lerDecimal`/`lerBool`), então `is_*`, `to_*_or` e `to_*`
  > aceitam exatamente os mesmos textos. **Defeito corrigido:** `to_decimal("1e999")` devolvia `0` em
  > silêncio (`from_chars` não toca no valor quando estoura); agora lança `ValueError`, e o pequeno
  > demais (`"1e-999"`) vira `0`. Testes `convert_completo`, `convert_erros`, `convert_decimal_faixa`.
  > 413/413/413, limpo sob ASan/UBSan.
- [x] 5. `Random` — feito em 2026-10-03
  > Nota: de 4 para 11 funções: `decimal_range`, `bool`, `chance`, `gauss`, `choices` (com repetição),
  > `sample` (posições distintas), `shuffle` (no lugar; proibido em `const`, como `Lists.sort`).
  > `k` = 0 dá `[]` mesmo com lista vazia. Testes por propriedade (a sequência não é especificada):
  > `random_completo`, `random_erros`, `random_shuffle_const`. 416/416/416, limpo sob ASan/UBSan
  > (que usa outra biblioteca C++ e outra sequência — os testes não dependem dela).
- [x] 6. `Lists` — feito em 2026-10-03
  > Nota: de 6 para 29 funções: `sort_desc`, `sort_by` (ordena por lista de chaves paralela — a forma
  > estática de ordenar objetos e structs, sem closures), `sorted`, `reversed`; `min`, `max`,
  > `average`, `product`; `count`, `last_index_of`, `first`, `last`, `is_empty`; `insert`, `pop`,
  > `remove_value`, `clear`, `extend`, `swap`; `concat`, `unique`, `repeat`, `copy`. O campo
  > `mutates_first` das nativas virou a máscara `mutates` (um bit por argumento), porque `sort_by` altera
  > também as chaves: `const` e coleção de `op` desconhecido são barrados em cada argumento alterado.
  > Testes `lists_completo`, `lists_erros`, `lists_sort_by_const`, `lists_insert_const`. 420/420/420,
  > limpo sob ASan/UBSan.
- [x] 7. `Time` (módulo novo; data de modificação de arquivo entra aqui) — feito em 2026-10-03
  > Nota: 33 funções. Instante = `int` (segundos desde 1970 UTC), faixa dos anos 1 a 9999. Relógio:
  > `now`, `clock` (ms, monotônico), `sleep`; `make` (horas opcionais) e as partes `year`, `month`, `day`,
  > `hour`, `minute`, `second`, `weekday` (ISO, 1 = segunda), `day_of_year` — horário local, mais as
  > variantes `_utc`; `format`/`parse` com `dd/MM/yyyy HH:mm:ss` (e `_utc`); `add_days`/`add_hours`/
  > `add_minutes`/`add_seconds`, `days_between` (dias de calendário), `is_leap_year`, `days_in_month`;
  > e `Files.modified`. Calendário calculado com os algoritmos civis de H. Hinnant (exatos em toda a
  > faixa, conferidos com o Python); do sistema só vem o deslocamento do fuso. Novo `plataforma.cpp`
  > isola o `windows.h`. **Dois defeitos de plataforma achados e contornados:** (1) o `_wstat` do CRT,
  > usado pelo `std::filesystem` do MinGW, aplica a variável `TZ` à data do arquivo — `Files.modified` lê
  > o `FILETIME` direto; (2) o `sleep_for` do MinGW às vezes acorda antes do prazo — `sleep` repete até o
  > relógio monotônico alcançar o prazo. Testes `time_utc` (valores exatos), `time_local` (propriedades),
  > `time_fuso` (`TZ=JST-9`, exato), `time_erros`. 425/425/425, limpo sob ASan/UBSan.

---

## H. Ergonomia de texto (2026-10-03)

- [x] 1. Interpolação: `printf("Olá, {nome}!")` e `format(...)` — feito em 2026-10-03
  > Nota: o texto precisa ser literal; o semântico o divide em trechos e `{expr[:formato]}`, e cada
  > expressão passa pelo próprio parser (`Parser::parseEmbedded`, com a posição dentro do texto) e pela
  > análise normal — nome inexistente ou tipo errado é erro de compilação. Formatos `{x:.2f}` (como
  > `Strings.fixed`, agora `fixedText` comum) e `{x:8}` (números à direita, texto à esquerda);
  > `{{`/`}}` literais; `{n:05}` recusado (seria zero à esquerda no Python). `printf` = `print` do
  > texto. Uma função do usuário com o nome tem precedência, como `print`. CVM: instrução `FMT`
  > (formato num `K`) + `CONCAT`; regra de formatação única em `operacoes.cpp` (`formatPart`).
  > Testes `printf_basico`, `printf_sombra` e 11 de erro de compilação (`printf_erro_*`, `format_erro_tipo`).
- [x] 2. `s[i]` e fatias `s[ini:fim:passo]` em string — feito em 2026-10-03
  > Nota: em caracteres UTF-8; negativo conta do fim; fatias **estritas** (limite fora lança
  > `IndexError`, sem o ajuste silencioso do Python); passo negativo como no Python (`s[::-1]`); passo 0
  > lança `ValueError`. Strings imutáveis: `s[i] = ...` é erro de compilação. Só string — listas
  > continuam sem índice negativo, com `Lists.slice`. Novo nó `SliceExpr`, instrução `SLICE` na CVM,
  > regra única `sliceGet` em `operacoes.cpp` (o `s[i]` entrou no `indexGet` comum). Testes
  > `str_indice`, `str_indice_erros`, `str_erro_*`. 445/445/445, limpo sob ASan/UBSan.
- [x] 3. `exit([código])` embutido — feito em 2026-10-03
  > Nota: código opcional (padrão 0), de 0 a 255 (senão `ValueError`). É um sinal (`EXIT_KIND`) que
  > desce a pilha pelo mecanismo de erros: roda os `finally` pendentes, mas nenhum `except` o
  > captura — na CVM, `except (Error e)` passou a ter o tipo "Error" na tabela (pega tudo, menos o
  > exit), e o tipo vazio ficou só para o pega-tudo do `finally`; no topo, `main.cpp` encerra com o
  > código. Para a análise de fluxo, `exit(...)` encerra o caminho (função com retorno pode terminar
  > nele; código depois é inalcançável). `NativeFn::SEM_OBRIGATORIOS` para nativa só com opcionais.
  > O runner ganhou `// expect-exit: N` (código de saída exato). Testes `exit_codigo`, `exit_padrao`,
  > `exit_finally`, `exit_fluxo`, `exit_codigo_morto`, `exit_sombra`. 454/454/454, limpo sob ASan/UBSan.

---

## I. Revisão geral (2026-10-03)

Código, testes e biblioteca padrão, com ~210 sondas de borda rodadas nos dois modos (e sob ASan).

- [x] 1. Defeitos corrigidos — feito em 2026-10-03
  > `Files.move(a, a, true)` apagava o arquivo (perda de dados); fatia com passo enorme derrubava o
  > programa com "Erro interno" (estouro do int64); fatia com passo negativo não aceitava o fim antes
  > do primeiro caractere (assimétrico ao passo positivo); código aninhado demais derrubava o processo
  > sem mensagem (agora `SyntaxError` acima de 2000 níveis, relatado uma vez); lista/dict literal com
  > mais de 65535 elementos rodava no interpretador e era recusado pela CVM (agora em blocos, `APPEND`/
  > `DICTADD`; `LOADK` com índice de 32 bits); `Time.make` de hora inexistente (horário de verão)
  > voltava uma hora em vez de adiantar; `Strings.is_upper("ÿ")` dava `true`; `Math.pow(0, -1)` dizia
  > "grande demais" (agora `ZeroDivisionError`); `Strings.repeat("", n)` dava n voltas vazias.
- [x] 2. Redundâncias removidas — feito em 2026-10-03
  > `utf8.h` (regras de UTF-8 que estavam copiadas em 4 arquivos); `Value::asNumber()` (6 cópias);
  > `forElements` (retrato do `for`, no interpretador e na VM); `dividirLinhas` (`Strings.lines` e
  > `Files.lines`); VM usa o resultado da regra compartilhada nas bordas de `/` e `%`; nomes que
  > escondiam outros (`-Wshadow`, um deles o ponteiro do frame na VM).
- [x] 3. Documentação — feito em 2026-10-03
  > `cvm/DESENHO.md` seção 5 reescrita a partir do `bytecode.h` (descrevia instruções planejadas que não
  > existem e omitia 21 reais); `Time` nas listas de módulos nativos; limite de aninhamento (spec 7.2);
  > regra simétrica das fatias (spec 5.7). 461/461/461, suíte e sondas limpas sob ASan/UBSan.
- [x] 4. Pontos levados ao autor — decididos em 2026-10-03
  > `menor_int % -1` passou a dar `0` (antes `OverflowError`, decisão A5): o resultado cabe em `int`,
  > como no Python; teste `a5_overflow_modulo` atualizado (registrador, constante embutida e `%=`).
  > `-0.0` continua aparecendo como `-0`, e decimais muito grandes ou pequenos continuam em notação
  > científica (`1e+21`) — mantidos por decisão do autor.

## J. Programa real: rede neural (2026-10-04)

`exemplos/rede_neural/` — uma MLP completa em Cinza (CSV → treino com Adam → modelo num formato
binário próprio, `.czr` → previsão sem retreinar), ~1000 linhas em 6 arquivos, usada como teste da
linguagem num programa de verdade. Roda igual na CVM e no interpretador.

- [x] 1. 🟠 `{Modulo.funcao(...)}` dentro de `printf`/`format` dava "Variável 'Math' não declarada" —
  corrigido em 2026-10-04
  > A expressão de dentro do texto era lida por um parser novo, sem os apelidos dos `import` do
  > arquivo; a spec (6.1) promete "qualquer expressão". `Parser::parseEmbedded` recebe os apelidos
  > do módulo atual. Testes `printf_modulo` (nativo, `as`, módulo do usuário) e
  > `printf_modulo_privado` (nome sem `pub` continua inacessível).
- [x] 2. 🟢 Não havia como gravar bytes de 128 a 255 — feito em 2026-10-04
  > Strings são UTF-8 e `Files` só lia/gravava texto, então um formato binário feito em Cinza ficava
  > restrito a bytes 0–127 (o `.czr` usa 7 bits por byte por isso). Implementados, por decisão do autor,
  > `Files.read_bytes(caminho) -> list<int>` e `Files.write_bytes(caminho, list<int>)` (bytes de 0 a
  > 255; fora disso, `ValueError`). Spec 6.2; teste `files_bytes`. O exemplo `.czr` continua com 7 bits
  > (funciona e documenta o contorno; pode passar a 8 bits quando for revisto).

---

## K. Reestruturação do sistema numérico (2026-11-14)

Especificação do autor: quatro tipos numéricos em duas famílias que não se misturam — inteiros
`i32`/`i64` e ponto flutuante `f32`/`f64` — no lugar de `int`/`decimal`. Dentro da família a
promoção para o mais largo é automática; entre famílias só com `cast<T>(expr)`. Sai o `op<...>`.

**Regras** (as centrais ficam num lugar só: `TypeChecker` no semântico e `operacoes.cpp` no runtime,
comuns ao interpretador e à CVM):

- Literais: `1` é `i32` (ou `i64` se não couber em `i32`); `1.f`/`1.5f` é `f32`; `1.ff`/`1.5ff` é
  `f64`; literal com ponto **sem sufixo é erro**. Sem notação científica (continua como estava).
- Operadores: `i32 op i64` → `i64`; `f32 op f64` → `f64`; mesma largura fica na largura; o operando
  mais estreito é promovido **antes** da conta. Inteiro com flutuante é erro de compilação. `%` só
  entre inteiros. Comparações seguem a mesma regra.
- Atribuição, argumento e retorno: só o mesmo tipo ou a promoção dentro da família (`i32` → `i64`,
  `f32` → `f64`). O caminho inverso e a troca de família exigem `cast`.
- Inteiros: overflow, divisão por zero e `menor / -1` lançam erro (em 32 e em 64 bits); `x % -1` é 0.
- Flutuantes: IEEE 754 em tudo — `inf`, `-inf`, `NaN` e `-0` são valores, inclusive na `Math`
  (`sqrt(-1.ff)` é `NaN`); entram `Math.inf`, `Math.nan`, `Math.is_nan`, `Math.is_inf`.
- `cast<T>(expr)`: entre `i32`, `i64`, `f32`, `f64` e `string` (não `bool`). Fora da faixa →
  `OverflowError`; `NaN`/infinito para inteiro e texto inválido → `ValueError`; flutuante → inteiro
  trunca em direção a zero; `f64` → `f32` segue o IEEE (pode dar infinito).
- Tamanhos e posições (`size()`, `find`, `index_of`, `count`...) são `i32`; grandezas que pedem 64 bits
  (`Files.size`, `Time.*`) são `i64`. Índice aceita qualquer inteiro, nunca flutuante.
- `Convert` fica só com o que o `cast` não faz: `is_i32`/`is_i64`/`is_f32`/`is_f64`/`is_bool`,
  `to_bool`, os `to_*_or` e as bases. Saem `to_int`, `to_decimal`, `to_string`.
- `type(x)` continua (classe real atrás de uma interface), sem estreitamento.
- Backend: a CVM continua sendo o runtime; o mapeamento para LLVM fica documentado.

**Etapas** (cada uma com a suíte verde nos dois modos e um commit):

- [x] 1. Remover `op<...>` (tipo, estreitamento com `type()`, instruções `CAST`/`KEEPLOCK`/genéricas). — feito em 2026-11-14
  > Nota: saíram o tipo `Op` (`types.h`, `ast.h`), as regras de "todos os tipos" e o travamento
  > (`TypeChecker::containsOp`/`expand`/`reassignable`, `checkOpType`, `lockCollectionLiteral`,
  > `narrowingOf`, `keep_lock`), `narrowValue`/`keepLock` do runtime e, na CVM, `CAST`, `KEEPLOCK` e as
  > oito instruções genéricas (`ADD`…`NEG`). `op` continua palavra reservada só para o erro
  > "O tipo 'op<...>' foi removido da linguagem", relatado uma vez (o parser pula o `<...>`).
  > `type(x)` fica, sem estreitar. Spec: sai a seção 3.7 (3.8–3.10 viram 3.7–3.9). Testes: saíram 30
  > `op_*` e `iface_op`; `type_valores` e `erro_typeerror_embutido` renomeados (cobrem o que continua);
  > novos `op_removido`, `op_removido_parametro`. 429/429/429, limpo sob ASan/UBSan.
- [x] 2. `i64`/`f64` no lugar de `int`/`decimal`: troca de nomes e migração mecânica dos testes, sem
  mudar comportamento. — feito em 2026-11-14
  > Nota: palavras-chave `i64` e `f64`; `int` e `decimal` ficam reservadas só para o erro "O tipo 'int'
  > não existe mais: use i32 (32 bits) ou i64 (64 bits)" (e o equivalente para `decimal`), relatado uma
  > vez — o parser segue com o tipo de 64 bits, sem cascata. Nomes internos do sistema de tipos
  > renomeados (`TK::I64`/`F64`, `TYPE_I64`/`F64`, `Type::Kind::I64`/`F64`, `i64Type()`/`f64Type()`);
  > os do runtime (`Value::Kind::INT`, `asInt`) mudam na etapa 3, junto com os tipos de 32 bits.
  > Mensagens de erro passam a citar `i64`/`f64`. Migrados 264 programas (testes, módulos de teste e o
  > exemplo da rede neural) e os blocos de código da spec; o texto corrido da spec é reescrito na etapa 6.
  > `Random.int`/`Random.decimal` e `Convert.to_int`/`to_decimal` ficam para a etapa 5. Testes novos
  > `tipo_int_removido`, `tipo_decimal_removido`. 431/431/431, limpo sob ASan/UBSan (uma falha passageira
  > de `files_pastas` sob ASan, por trava do Windows na pasta temporária, não se repetiu em 4 execuções).
- [x] 3. `i32`/`f32`, literais com sufixo, famílias e promoção, IEEE nos flutuantes, `%` só inteiro
  (semântico, `operacoes.cpp`, interpretador e CVM). — feito em 2026-11-14, junto com a etapa 4
- [x] 4. `cast<T>(expr)`. — feito em 2026-11-14 (com a 3: sem `cast` os testes não migram)
  > Nota: **tipos** — `TK::I32`/`F32` com `isInteger()`/`isFloat()`; a regra central é
  > `TypeChecker::numericResult` (mesma família → o mais largo; famílias diferentes → erro com a
  > sugestão do `cast`) e `TypeChecker::widens` (só `i32` → `i64` e `f32` → `f64`), usadas por
  > `isAssignable`, `checkBinaryOp`, a unificação de literais e as variáveis de tipo das nativas
  > (`Math.max(i32, i64)` alarga para `i64`). `analyzeBinary` promove os dois lados ao tipo comum
  > **antes** da conta (literal muda só o tipo; o resto ganha um `CastExpr`), então o runtime sempre
  > recebe operandos do mesmo tipo; o mesmo na atribuição composta e no `for (i64 x in list<i32>)`
  > (`ForStmt::widen_to`). **Literais** — o lexer exige o sufixo (`1.5f`, `1.5ff`, `1.f`, `1.ff`;
  > `1.5`, `1f` e `1.5fff` têm erro próprio) e lê `f32` direto como `float`; inteiro é `i32` se couber.
  > Literal inteiro onde se espera ponto flutuante tem erro que mostra a correção ("Escreva 2.ff").
  > **cast** — palavra reservada; `cast<T>(valor)` com `T` em `i32`/`i64`/`f32`/`f64`/`string` e valor
  > número ou string (`bool` e coleções têm erro próprio). A regra mora em `convertValue`
  > (`operacoes.cpp`), comum ao interpretador, à CVM (`CONV`) e às promoções: trunca em direção a zero,
  > confere a faixa antes de converter (sem comportamento indefinido do C++), `f64` → `f32` pelo IEEE
  > com a borda do infinito tratada à parte, texto no formato estrito (`inf`/`nan` para flutuante).
  > **Runtime** — `Value` ganha `I32`/`F32`; `applyBinaryOp` conta inteiros em 64 bits e confere a
  > faixa de 32 quando é `i32`; ponto flutuante é IEEE puro (f32 em `float`): a conta de `f64` que
  > estoura **deixa de lançar** `OverflowError` (revê a revisão I) e dá `inf`, como `Lists.sum`/`product`.
  > `literalValue` e `nativeResult` (nativa genérica devolve na largura do tipo estático) ficam em
  > `operacoes`. **CVM** — instruções por tipo `ADD_I64`…`DIV_F32` (sem `MOD` de flutuante),
  > `ADDK_I64`/`ADDK_I32`…, `NEG_*`, `LOADI32`, `CONV` (no lugar de `I2D`), `LT_F`/`LE_F`, `FORNEXT_W`
  > (alarga o elemento); sai `FORRANGE_D`. Índices, fatias e chaves de dict aceitam `i32` e `i64`.
  > A biblioteca continua com as assinaturas de 64 bits (etapa 5). **Testes** — literais migrados por
  > script (sufixo `ff` em 59 arquivos; 149 literais inteiros em contexto de ponto flutuante, pelas
  > posições que um modo temporário do compilador registrou, já removido); o resto à mão, com `cast`
  > (testes, módulos de teste, exemplo da rede neural, blocos da spec 3.5, 5.3, 5.4 e 6.x, e as frases
  > de regra mudadas junto). 29 testes novos `k3_*`. 460/460/460.
- [x] 5. Biblioteca padrão com os tipos novos (`Math` IEEE, `Convert` enxuto, tamanhos `i32`). — feito em 2026-11-14
  > Decisões do autor (2026-11-14): `Random` com o nome do tipo (`i32(a, b)`, `i64(a, b)`, `f64()`,
  > `f64_range(a, b)`); `range` segue os argumentos; a `Math` de ponto flutuante segue a largura do
  > argumento. Nota: duas variáveis de tipo novas nas assinaturas nativas, `inteiro` (i32/i64) e
  > `flutuante` (f32/f64); a mensagem de argumento errado mostra os tipos aceitos ("esperado 'f32' ou
  > 'f64'"). **Math** — sqrt, pow, trigonométricas, exp, log* e hypot são `flutuante → flutuante`
  > (conta em double, um arredondamento para f32); IEEE em tudo: fora do domínio dá `nan`, polo dá
  > `±inf`, grande demais dá `inf` (saem os `ValueError`/`OverflowError` da G2). Entram `Math.inf`,
  > `Math.nan`, `is_nan`, `is_inf`. `floor`/`ceil`/`round`/`trunc` continuam `i64` (nan/inf → `ValueError`).
  > `round_to` e `Strings.fixed` com f32 arredondam a forma decimal do float. `is_close` em f32 tem
  > tolerância de f32 (1e-5 relativa). `gcd`/`lcm`/`is_even`/`is_odd` aceitam i32 e i64 (resultado na
  > largura); `sign` é i32; `factorial`/`is_prime` ficam em i64. **Tamanhos e posições i32** — `size()`
  > de list/dict/string, `Strings.find`/`find_last`/`find_from`/`count`/`compare`/`ord`,
  > `Lists.index_of`/`last_index_of`/`count`, `e.line`/`e.column`; os parâmetros de posição continuam
  > `i64` (aceitam os dois). `Files.read_bytes` devolve `list<i32>`; `write_bytes` aceita list de
  > qualquer inteiro. `Files.size`/`modified` e `Time.*` ficam i64. **range** — `range(0, 10)` é
  > `list<i32>`, com i64 é `list<i64>`; a CVM mantém a largura no `FORRANGE` e alarga o contador
  > (`CONV`) quando o iterador é mais largo. **Convert** — saem `to_int`, `to_decimal`, `to_string`
  > (e `is_int`, `is_decimal`, `to_int_or`, `to_decimal_or`); entram `is_i32`/`is_i64`/`is_f32`/`is_f64`,
  > `to_i32_or`/`to_i64_or`/`to_f32_or`/`to_f64_or`, que leem o texto pela mesma regra do `cast`
  > (`convertValue`). Os nomes antigos de `Random`/`Convert` têm erro que aponta o substituto. Testes
  > migrados por script (nomes) e à mão (mensagens: `Math` agora devolve nan/inf; texto que não cabe é
  > `OverflowError`, como no `cast`). 12 testes novos `k5_*`. 472/472/472.
- [x] 6. Spec, desenho da CVM e o mapeamento para LLVM. — feito em 2026-11-14 (com L1 e L2)
  > Nota: o DESENHO foi atualizado em L1 (instruções da seção K e seção 12, LLVM). Spec: 1.4/1.5
  > (palavras `cast`, `i32`, `f32`; literais inteiros i32/i64 e de ponto flutuante com sufixo), 2
  > (gramática com `cast<T>(...)` e os tipos novos), 3.1 (os quatro tipos, as famílias e a tabela do
  > `cast<T>`), 3.3/3.5, 5.3 (aritmética, comparação e igualdade por família; IEEE), 5.4 (forma
  > textual de f32/f64), 5.6/5.7 (tamanhos e posições i32), 6 (variáveis de tipo `inteiro` e
  > `flutuante`, `range`, `Math` IEEE, `Random`, `Convert`, ordem com nan em `Lists`) e 7. Versão 3.

## L. Só a CVM, sem compatibilidade antiga, e varredura (2026-11-14)

Pedido do autor: apagar o interpretador ("vamos trabalhar só com a CVM e futuramente integrar tudo
ao LLVM"), não manter suporte a versões anteriores da linguagem ("nenhum programa no mundo usa
ela") e fazer uma varredura completa atrás de bugs e inconsistências.

- [x] L1. Interpretador removido e compatibilidade antiga removida. — feito em 2026-11-14
  > Nota: saíram `executor.cpp`/`executor.h`, as opções `--interp` e `--cvm` (opção desconhecida
  > agora é erro, em vez de ser ignorada), os modos `--interp`/`--diff` do `run_tests.py` e os alvos
  > `test-interp`/`test-diff`; `make test` roda a suíte na CVM. Saíram também `int`, `decimal` e `op`
  > como palavras reservadas (com os erros "não existe mais"/"foi removido": agora `int` é só um nome
  > de tipo desconhecido) e o mapa de nomes antigos de `Random`/`Convert`, com os 6 testes que
  > existiam para essas mensagens. `bench/` migrado (ainda usava `int`) e `medir.ps1` só mede a CVM.
  > Comentários, CLAUDE.md, README, spec e cvm/DESENHO.md atualizados; o DESENHO ganhou as
  > instruções da seção K (que estavam desatualizadas) e a seção 12, o mapeamento para LLVM.
  > Extensão do VS Code (`C:\cinza-vscode`, 0.3.0): tipos `i32`/`i64`/`f32`/`f64`, `cast`, literais
  > `1.5f`/`1.5ff` (com ponto e sem sufixo aparece como inválido), módulo `Time`, `printf`/`format`/
  > `exit`, diretivas de teste; saem `int`, `decimal` e `op`. 466/466.
- [x] L2. Varredura: ~90 programas de sondagem (números e `cast` em todos os pontos de passagem,
  campos, `const`, interfaces, módulos, exceções e `finally`, coleções, strings Unicode, fatias,
  `Time`, `Convert`, limites da CVM) e revisão textual do código. — feito em 2026-11-14
  > Corrigido (cada um com teste `l_*`):
  > - **nan quebrava a ordenação** (`Lists.sort`/`sorted`/`sort_desc`/`sort_by`): o `<` do IEEE não é
  >   uma ordem válida, e a lista saía fora de ordem (`sorted([2, nan, 1])` voltava igual; no C++ é
  >   comportamento indefinido). Agora a ordem é total, com nan depois de todos (inclusive de inf).
  > - **`Math.min`/`max` com nan dependiam da ordem**: `min(nan, 1)` dava nan e `min(1, nan)` dava 1.
  >   Agora nan se propaga dos dois lados; `Lists.min`/`max` também.
  > - **`Lists.unique` perdia elementos com nan**: o `std::set` tratava nan como igual a tudo
  >   (`unique([nan, 1.ff])` dava `[nan]`). nan agora usa o caminho do `==` (cada nan fica); `i32`/`f32`
  >   passaram a usar o caminho rápido.
  > - **`Strings.pad_left`/`pad_right`/`center` com largura enorme** esgotavam a memória com "erro
  >   interno do compilador"; agora `ValueError` acima de 2²⁸ caracteres, como `repeat`.
  > - **`range` enorme como lista** (`range(0, 1000000000)`) esgotava a memória; agora `ValueError`
  >   acima de 100 milhões de elementos, como `Lists.repeat` (o `for` sobre `range` não cria a lista e
  >   continua sem limite). E memória esgotada em geral sai como "memória insuficiente", não como bug.
  > - **`Convert.from_base` fora da faixa** lançava `ValueError`; agora `OverflowError`, como o `cast`
  >   e a regra da seção K.
  > - A mensagem de `var` em campo sugeria `'int x = 0;'` (tipo que não existe mais); agora `i32`.
  >   Acesso a campo num primitivo não fala mais em "classe conhecida".
  > Limpeza: `nativeResult` (só o interpretador usava) saiu; comentários e testes ainda citavam
  > `int`/`decimal`, o interpretador ou "os dois modos". 476/476.

## M. Sem infinito nem NaN (2026-11-14)

Decisão do autor, revendo a resposta "IEEE em tudo" da seção K: "nan e inf não devem existir na
linguagem CINZA, vai contra a filosofia da linguagem, isso deve gerar erro e não permitir".
`TypeError` continua embutido (para os genéricos do futuro).

- [x] M1. Toda conta que produziria infinito ou NaN é erro. — feito em 2026-11-14
  > Regras (as de antes da seção K, agora nos quatro tipos): divisão de ponto flutuante por zero,
  > inclusive `0 / 0`, lança `ZeroDivisionError`; resultado além do maior valor finito do tipo
  > (`f64` ou `f32`) lança `OverflowError`; fora do domínio (`sqrt` de negativo, `log` de 0, `asin(2)`,
  > `pow` sem resultado real) lança `ValueError`, e `pow(0, negativo)`, `ZeroDivisionError`. O que é
  > pequeno demais vira 0 (ou subnormal) e `-0` continua existindo (I4).
  > Nota: `contaFlutuante` (operacoes.cpp) e as instruções `ADD_F64`…`DIV_F32` da CVM conferem
  > divisor zero e resultado não finito; `cast<f32>` de um `f64` grande demais e de texto acima da
  > faixa lançam `OverflowError`; `"inf"`/`"nan"` deixam de ser texto numérico (`ValueError`; `is_f64`
  > dá `false`). Na `Math` voltam as mensagens de domínio da G2 e `naLargura` confere o resultado
  > (inclusive "não cabe em f32"); `Lists.sum`/`product` voltam a lançar `OverflowError` em ponto
  > flutuante; `average` soma em `long double`, então só a média precisa caber. Saem `Math.inf`,
  > `Math.nan`, `Math.is_nan`, `Math.is_inf`, o texto `nan`/`inf` do `print`/`fixed` e todo o
  > tratamento de NaN da varredura L2 (ordem total no `sort`, propagação em `min`/`max`, exceção no
  > `unique`), que perdeu o motivo. Spec 3.1, 5.3, 5.4, 6 (`Math`, `Lists`) e 7, e o DESENHO (5.2 e o
  > mapeamento para LLVM: teste de divisor e de resultado não finito depois de cada conta).
  > Testes: saem `k3_ieee`, `k5_math_ieee`, `l_nan_ordenacao`, `l_unique_nan`; voltam `math_erros`,
  > `f7_math_erros`, `stdlib_revisao` e `rev_decimal_overflow` no formato de erro (com f32); novos
  > `m_sem_infinito`, `m_cast_sem_infinito`, `m_math_sem_constantes`, `m_math_sem_is_nan`,
  > `m_average_sem_estouro`, `l_unique_largura`.
- [x] M2. Segunda varredura (antes do push). — feito em 2026-11-14
  > ~35 programas de sondagem: o padrão do autor (`printf` com `self.campo.metodo()` dentro de método,
  > `for (pair<str, i32> p in dict)`), interpolação (escapes, `{{`, formatos, erros), `throw` de
  > novo e erro dentro do `finally`, métodos privados, `self` fora de classe, `const` que estoura na
  > inicialização, structs recursivos, parâmetros `const`, 300 parênteses aninhados, `print`
  > redefinido, módulos (`pub`, apelido, inexistente), alteração da coleção dentro do `for`, código
  > inalcançável, chaves duplicadas, interfaces, hierarquia de erros, Unicode com índice negativo.
  > Tudo de acordo com a spec. Uma inconsistência corrigida: as operações entre famílias sugeriam o
  > `cast<T>`, mas declaração, atribuição, retorno e argumentos (função, método, construtor, nativa,
  > `list.add`) só diziam "incompatível"; agora todos terminam com "Converta com cast<T>(valor)"
  > quando o `cast` faria a conversão (números entre si e com `string`; `bool` não). Testes `n_*`.
  > 483/483, limpo sob ASan/UBSan.

## N. Funções genéricas (2026-11-15)

Decisões do autor (revê "A Cinza não terá genéricos", D, 2026-09-30), depois de comparar
monomorfização, apagamento de tipo e dicionários, e o modelo "confere no uso" (C++/Zig) com o de
contrato declarado (Rust/Go):

1. **Monomorfização**: cada combinação de tipos usada vira uma função comum. Execução sem custo;
   a CVM não muda (nenhuma instrução nova), e o LLVM futuro recebe só tipos concretos.
2. **Conferência no uso, como no Zig**: o corpo só é analisado com o tipo concreto; `T` aceita os
   campos, métodos e operadores que o tipo real tiver, sem restrições declaradas. Genérico não
   usado só passa pelo parser.
3. **Inferência + explícito opcional**: os tipos saem dos argumentos (literal se adapta, promoção
   dentro da família, coleções invariantes); `f[i32](x)` sempre vale e é obrigatório quando `T` não
   aparece nos parâmetros.
4. **Colchetes são a sintaxe da função genérica, e só dela**: `fn f[T, U](...)` traz apenas os
   parâmetros de tipo e `f[i32, string](...)`, apenas os tipos; `list<T>`, `dict<K, V>`,
   `pair<A, B>` e `cast<T>` continuam com `<>`. Proposta do autor: elimina a ambiguidade do `<`
   (com `<`, `f(a < b, c > (d))` viraria chamada genérica). Com `[`, não há exceção: funções não
   são valores, então `x[...](` nunca teve outro significado.
5. **Erros com rastro** (`em f[i32], usada em main.cinza:9`) e limite de 64 cópias encadeadas.

Especificação: capítulo 8 (`spec/08-genericos.md`), escrito antes do código; os exemplos dele são
o alvo das etapas e só passam quando a N3 terminar. Até lá o capítulo traz a linha
`<!-- exemplos-pendentes: ... -->`, que o `run_tests.py` respeita (avisa com `PEND` e não roda os
exemplos); ela saiu na N3. Testes com prefixo `gen_`.

**Etapas** (cada uma com a suíte verde e um commit):

- [x] N1. Parser e AST. — feito em 2026-11-15
  - `FunctionDecl` ganha os parâmetros de tipo e a faixa de tokens da declaração (para gerar as
    cópias); `CallExpr` e a chamada de função de módulo ganham os argumentos de tipo.
  - Na declaração, `fn nome[` abre os parâmetros de tipo. Na chamada, depois de `nome` ou
    `apelido.nome`, procurar o `]` que fecha o `[` (contando aninhamento): se o token seguinte é
    `(`, o conteúdo é lido como `tipo {, tipo}`; senão é índice ou fatia, como hoje.
  - Erros de declaração: parâmetros de tipo em método, construtor, assinatura de interface,
    `struct`, `class`, `enum` e `error` ("métodos não podem ser genéricos", "struct não aceita
    parâmetros de tipo"...).
  - O semântico ainda recusa toda função genérica com um erro único, para a suíte ficar verde.
  > Nota: `FunctionDecl::type_params`/`fonte` (os tokens de `fn` ao `}`, só na genérica) e
  > `CallExpr::type_args`. No parser, `parseTypeParams`, `checkGenericCall` (procura o `]`
  > correspondente; `;`, `{`, `}` e o fim encerram a procura), `parseGenericCall` e
  > `recusaParametrosTipo`. O conteúdo dos colchetes é lido como tipos em modo silencioso: se não
  > for (`l[0](2)`), um erro só, "O resultado de um índice não pode ser chamado: funções não são
  > valores...", em vez de "Esperado tipo". `fn f[]`, `f[](x)` e `fn f[i32]` têm mensagem própria.
  > Método, construtor e assinatura de interface: "Métodos/Construtores não podem ser genéricos";
  > `struct`/`class`/`enum`/`interface`/`error`: "X não aceita parâmetros de tipo". Os dois casos
  > leem e descartam a lista, sem cascata. Semântico: "Funções genéricas ainda não estão
  > implementadas" na declaração (provisório, sai na N2) e "'f' não é uma função genérica" na
  > chamada com colchetes. 18 testes `gen_*` (um deles, `gen_indices_intactos`, confere índices,
  > fatias, chaves e índices dentro do `printf`). 501/501, limpo sob ASan/UBSan.
- [x] N2. Semântico: declaração, dedução e cópias. — feito em 2026-11-15, junto com a N3
  - Regras da 8.1: nomes distintos, sem colidir com tipos do módulo, `main` não genérica, sem
    sobrecarga. O corpo de uma genérica não é analisado.
  - Dedução (8.3): unificação dos tipos dos parâmetros com os dos argumentos, reaproveitando a das
    variáveis de tipo das nativas (`TypeChecker::widens`, literais pela regra de `coerceInPlace`);
    erros "Não é possível deduzir o tipo 'T'" e "tipos conflitantes para 'T'". Com tipos
    explícitos, o número precisa bater e os argumentos seguem as regras normais.
  - Cópia: chave (declaração, tipos concretos) num cache. A cópia nasce reparseando a faixa de
    tokens da declaração (cada cópia tem a própria AST, onde o semântico grava os tipos), com o nome
    `f[i32]` e os parâmetros de tipo ligados aos tipos concretos na resolução de tipos. A
    assinatura concreta tipa a chamada na hora; o corpo entra numa fila e é analisado depois da
    função atual, no escopo do módulo da declaração.
  - Rastro (8.4): cada cópia guarda o uso que a criou (posição e cópia de origem). Um erro durante
    a análise do corpo ganha as linhas "em f[i32], usada em ...", abreviadas como o stack trace.
    Profundidade acima de 64 é erro. `x[...](...)` com `x` que não é função genérica: "'x' não é
    uma função genérica".
  > Nota: a substituição é **nos tokens**, antes do reparse: cada `T` (que não venha depois de
  > `.`) vira os tokens do tipo concreto (`list<i32>` vira `list`, `<`, `i32`, `>`). Assim todo
  > lugar onde um tipo aparece — declaração, `cast<T>`, `new T()`, `type(x) == T`, `for (T x in
  > ...)` — sai certo sem percorrer a AST, e um corpo que deixa de ser sintaxe válida (`new i32()`)
  > vira "Com T = i32, o corpo de 'cria' não é válido: ..." com o rastro. Um tipo do principal
  > usado por uma genérica de outro módulo vai como `::Nome` (`qualifyName`/`findSymbol` aceitam:
  > o principal não tem prefixo), para não ser qualificado no módulo da genérica. Dedução por
  > `Ligacao` (tipo, fixo): fora de coleções, números da mesma família combinam no mais largo;
  > dentro, o tipo é exato e fixa a ligação (um `i32` de fora ainda é promovido a ele).
  > Os argumentos usados na dedução são analisados uma vez, sem tipo esperado, e depois
  > conferidos e convertidos (`coerceInPlace`) contra a assinatura concreta; os outros, com ela
  > como tipo esperado. Fila de corpos (`analisaInstancias`) esvaziada depois de cada declaração
  > do nível superior, no módulo da genérica; `SemanticError::notes` leva o rastro até o
  > `main.cpp`. Nomes de cópia longos (tipos que crescem) são encurtados nas mensagens. Erros
  > novos: argumento de tipo `void`/`type`/desconhecido, quantidade de tipos explícitos, forma
  > errada (`list<T>` com `i32`), parâmetro de tipo repetido ou com nome de tipo, `main` genérica.
- [x] N3. CVM: as cópias são compiladas como funções comuns do módulo da declaração, com o nome
  `f[i32]` na tabela de funções, no stack trace e no `--disasm`. DESENHO.md: nota de que os
  genéricos não chegam à VM (nem ao LLVM). Os exemplos do capítulo 8 passam. — feito em 2026-11-15
  > Nota: feita junto com a N2 (sem ela, nenhum programa com genérica rodava, e os testes da N2
  > precisavam rodar). `Program::instancias` guarda as cópias; o compilador pula a declaração
  > genérica e compila as cópias como funções comuns (o `--bytecode` mostra `fn par[string]`).
  > Sai a linha `exemplos-pendentes` do capítulo 8: os 17 exemplos passam. Testes: sai o
  > provisório `gen_n1_declaracao_lida`; 36 novos `gen_*` (dedução, tipos explícitos, invariância,
  > struct/classe/interface como `T`, `new T()`, `const`, `printf`, recursão e cache, rastro
  > encadeado e abreviado, módulo com `pub`, privado e erro com rastro — módulo de teste
  > `tests/modulos/genericos.cinza`). 553/553, limpo sob ASan/UBSan.
- [x] N4. Varredura e acabamento: genéricos entre módulos (`pub`, apelido, tipo do módulo de quem
  usa), recursão com e sem crescimento de tipo, interface como `T`, `const`, `var`, `printf`/
  `format` no corpo, coleções aninhadas, colchetes de tipo ao lado de índices e fatias
  (`f[i32](l[0])`, `l[f[i32](x)]`); ASan/UBSan. Extensão do VS Code (`fn nome[T]` e chamadas com
  tipos explícitos). Spec versão 4. — feito em 2026-11-15
  > ~40 programas de sondagem: colchetes de tipo com índices, fatias e índices encadeados; tipos de
  > módulo como `T` (deduzidos e explícitos, `g.cria[geo.Ponto]()`), duas genéricas de módulos
  > diferentes; `var`, `dict<T, ...>` com chave inválida, retorno em todos os caminhos, `throw T(...)`
  > e `except (T e)`, enum como `T`, genérica em inicializador de campo (class e struct), nativas
  > genéricas no corpo (`Math.max`, `Lists.sorted`), 12 cópias de uma função, estouro de pilha e
  > exceção atravessando uma cópia, parâmetro de tipo sem uso, `str`, literais de pair e de lista
  > com tipos explícitos, campo privado, igualdade de struct, formatos `{x:8}`/`{x:.2f}`, genérica
  > declarada depois da `main`, chamada dentro de método que cumpre interface e com `self`.
  > **Corrigido**: o `T` dentro do texto de `printf`/`format` não era trocado (as expressões
  > `{...}` só viram tokens na análise): `printf("{cast<T>(x)}")` dava "cast<T> não existe". Agora
  > `parseEmbedded` recebe a mesma troca de tokens da cópia (`trocaParametros`). Spec 8.1: a lista
  > de lugares onde `T` vale (inclusive `new T()`, `except (T e)` e o texto do `printf`) e a
  > regra do `x.T`. Extensão (`C:\cinza-vscode`, 0.5.0): `fn nome[T, U]` com os parâmetros de
  > tipo destacados como tipos, `nome[tipos](...)` e `apelido.f[tipos](...)` como chamada de
  > função (só quando o `]` é seguido de `(`; índices e fatias não mudam). Spec versão 4.
  > 16 testes `gen_*` novos. 569/569, limpo sob ASan/UBSan.

- [x] N5. Structs genéricos, campos obrigatórios e criação pelo nome (spec 4.7 e 8.6). — feito
  em 2026-11-15
  Decisões do autor: o problema era o campo `T` sem valor inicial possível. (1) Em **todos** os
  structs, um campo pode ficar sem valor inicial: `list`/`dict` nascem vazios, os demais são
  obrigatórios na criação (nenhum struct existe com campo sem valor). (2) Criação também pelo
  nome, `new A[Ponto](ct: ponto)`, omitindo os campos com valor inicial. (3) No uso do struct
  genérico os tipos são sempre escritos (`A[Ponto]`), sem inferência: com `A[T, K]` e campos
  omitidos ela não teria de onde sair, e a verbosidade fica restrita aos genéricos.
  > Nota: mesmo mecanismo das funções. `StructDecl::type_params`/`fonte`; no parser,
  > `Nome[tipos]` num tipo (`Type::type_params` em `CUSTOM`), `checkCustomTypeDecl` (no começo
  > de uma instrução, `Nome[...]` seguido de um nome é declaração; senão, índice), `new
  > Nome[tipos](...)` e argumentos `campo: valor` (`NewExpr::arg_names`). No semântico, o
  > genérico fica em `structs_genericos` (não é um tipo sozinho); `typeOf` e `analyzeNew` trocam
  > `Par[i32, string]` pelo nome da cópia, um struct comum: `instanciaStruct` reparseia os tokens
  > com os tipos trocados, registra os campos na hora (o tipo é preciso já, até no pré-registro;
  > `list<No[T]>` dentro de `No[T]` acha a cópia, declarada antes dos campos) e deixa os valores
  > iniciais e a conferência dos tipos para `analisaInstancias`, no escopo global. Erros da cópia
  > com o rastro; mais de 64 cópias de struct encadeadas (`Cresce[list<T>]`) é erro. A dedução
  > das funções genéricas entra em `Par[A, B]` com tipos exatos (`copia_struct`). `type(x) == T`
  > com `T` = cópia de struct é o tipo da cópia. Um campo do tipo `T` não pode ter valor inicial
  > (conferido mesmo sem uso). CVM: as cópias entram na tabela de structs; a criação pelo nome
  > avalia os informados na ordem escrita direto no registro do campo, depois os valores
  > iniciais dos omitidos (`NewExpr::arg_fields`). `displayName` passa a tirar todos os prefixos
  > (`Par[string, Conta[i32]]`, com `Conta` de outro módulo, saía `Conta[i32]]`). Na classe nada
  > muda: campo sem valor inicial continua erro (classes genéricas ainda em discussão). Spec
  > versão 5. Módulo de teste `tests/modulos/structs_genericos.cinza`; 27 testes `gen_struct*`
  > (um deles, `gen_struct`, reescrito: antes conferia o erro "struct não aceita parâmetros de
  > tipo") e 14 `n5_*` em `structs/`. 627/627.
- [x] N6. Varredura dos structs genéricos. — feito em 2026-11-15
  > ~50 programas de sondagem (struct genérico em interface, campo de classe, coleções e
  > structs aninhados, `enum`/interface/erro como `T`, `const`, `var`, módulos com `T` do
  > principal, `new T(campo: v)` numa genérica, tipos explícitos com `Par[i32, string]` entre os
  > colchetes, recursão mútua por `list`, `--bytecode`, aninhamento profundo). **Corrigido**:
  > (1) ciclo por valor entre cópias (`A[T] { B[T] b; }` e `B[T] { A[T] a; }`) só aparecia como
  > erro de construtor no uso — agora `checkStructCycles` roda quando a cópia de fora termina de
  > nascer; (2) campo ou parâmetro com o nome de um parâmetro de tipo (`struct S[T] { T T; }`,
  > `fn f[T](T T)`) dava "Esperado nome do campo" na cópia — agora erro na declaração;
  > (3) faltava escrever o struct genérico como valor (`type(p) == Par[i32, string]`, que vale
  > para `Ponto` e `list<i64>`): o parser lê `Nome[tipos]` numa expressão como tipo quando há mais
  > de um tipo ou um tipo que não é nome, e guarda as duas leituras de `Caixa[Ponto]`
  > (`IndexAccessExpr::como_tipo`), que o semântico escolhe pelo que o nome é (struct genérico, e
  > não variável local); `l[i32]` numa lista tem mensagem própria; (4) a leitura especulativa
  > do parser preserva o primeiro erro guardado (numa cópia genérica, um erro posterior saía com a
  > mensagem da especulação); (5) "class não aceita parâmetros de tipo" dizia que só funções são
  > genéricas, e `struct S[]` sugeria `fn nome[T]`; (6) `--ast` mostra os parâmetros de tipo e
  > os nomes dos campos no `new`. Extensão do VS Code 0.6.0: `struct Nome[T, U]` com os
  > parâmetros destacados e `campo:` na criação pelo nome (só depois de `(` ou `,`; fatias não
  > mudam). 10 testes `gen_*` novos.

**Correções gerais da mesma varredura** (testes `rev_*`):
- Nome de arquivo com acento derrubava o `cinza.exe` (`std::filesystem::filesystem_error` sem
  captura, processo preso no relatório de erro do Windows) e argumentos com acento chegavam a
  `main(args)` como bytes inválidos: no Windows o `argv` vem na página de código do sistema.
  Agora `argumentosUtf8` (linha de comando em UTF-16, convertida) e `caminhoUtf8`/`textoCaminho`
  (`plataforma.h`) em todo caminho do `main.cpp` e do `ModuleLoader`, também no `CINZA_PATH`
  (`ambienteUtf8`). Teste `rev_nome_com_acentuação` (com `// args: ação ñ`).
- O arquivo principal era lido em modo texto (o Windows tira o `\r`) e os módulos em binário: um
  texto de várias linhas num arquivo `\r\n` saía `"a\nb"` no principal e `"a\r\nb"` num módulo.
  Pela spec 1.1, `\r\n` é um fim de linha só: o léxico agora o grava como `\n` dentro do texto, e
  o principal também é lido em binário. Teste `rev_texto_crlf` (ele e o módulo em CRLF).
- A sugestão do `KeyError` (`d[k] = v` com chave nova) punha a chave sempre entre aspas
  (`.add({"Cor.G", valor})`); agora só texto vai entre aspas. Teste `rev_keyerror_sugestao`.
  640/640.

**Fora desta seção** (em discussão com o autor, continua aberto): `class` genérica — falta
decidir como o construtor garante o campo `T` (proposta: atribuição obrigatória no nível de cima
do construtor) e se a classe inteira é conferida a cada tipo —, métodos genéricos, interfaces
genéricas, restrições opcionais (`T: Forma`) e especialização.

## O. Imports a partir da raiz do projeto (2026-11-15)

Problema apontado pelo autor: um projeto organizado em pastas não conseguia importar entre
pastas irmãs. O `import a.b;` procurava primeiro na pasta do arquivo que importa (e depois no
`CINZA_PATH`), sem como subir uma pasta: `servicos/cadastro.cinza` não achava
`modelos/usuario.cinza`. Também dava ao mesmo arquivo nomes diferentes conforme quem importava
(`usuario` para o vizinho, `modelos.usuario` para o principal), ruim para os nomes de símbolo
estáveis que o backend LLVM vai precisar (compilação separada, cópias de genéricos).

Decisão do autor: **híbrido com o marcador `czm.mod` (vazio)**. A raiz do projeto é a pasta do
`czm.mod` mais próximo (na pasta do arquivo principal ou acima); sem ele, a pasta do principal.
O import parte sempre da raiz, depois do `CINZA_PATH` (bibliotecas fora do projeto); sai a busca
na pasta de quem importa (sem import relativo nem `..`).

- [x] O1. Raiz do projeto, busca raiz + `CINZA_PATH`, ambiguidade. — feito em 2026-11-15
  > Nota: `ModuleLoader::projectRoot` sobe as pastas atrás do `czm.mod` (raiz relativa à pasta
  > atual quando o caminho do principal é relativo, para as mensagens continuarem
  > `tests/modulos/geom.cinza`); `resolve` devolve todos os arquivos achados, sem repetir o mesmo
  > arquivo (a raiz também pode estar no `CINZA_PATH`). Mais de um é `ImportError` ("o módulo
  > 'x' existe em mais de um lugar: A e B"). "Não encontrado" explica que o caminho parte da raiz.
  > O conteúdo do `czm.mod` é ignorado por enquanto (lugar futuro da configuração de build).
  > Spec 4.11 (raiz, exemplo de projeto, ambiguidade) e 7.2. `tests/czm.mod` faz de `tests/` um
  > projeto: o `run_tests.py` deixa de passar `CINZA_PATH=tests`, e os módulos de apoio que
  > importavam vizinhos (`usa_geom`, `cvm_usa`, `ciclo_a`/`ciclo_b`) usam o caminho a partir da
  > raiz. `exemplos/rede_neural/czm.mod` marca o exemplo como projeto. Testes `o_pasta_irma`,
  > `o_sem_busca_relativa`, `o_mesmo_caminho_em_dois_lugares`, `o_raiz_no_cinza_path`; sem
  > `czm.mod` (raiz na pasta do principal) e rodar um arquivo de subpasta conferidos à mão, num
  > projeto temporário (a suíte inteira vive sob `tests/czm.mod`). 573/573, limpo sob ASan/UBSan.

## P. Backend nativo com LLVM (2026-11-18)

Desenho completo em `llvm/DESENHO.md` (escrito antes do código, para revisão). Decisões do autor,
depois de um debate com duas revisões externas:

1. **IR pela API do LLVM**, a versão em C (`llvm-c`, LLVM 20.1.8 fixo): liga com o GCC do
   projeto sem depender da ABI de C++ do LLVM (compilado com `libc++` no CLANG64).
2. **CIR**: uma IR intermediária tipada e simples (blocos básicos, slots, posse e erros
   explícitos) entre a AST e o LLVM. A CVM continua compilando da AST: duas implementações
   independentes da mesma semântica.
3. **Memória**: contagem de referências (não atômica, sem threads) + a coleta de ciclos do
   `gc.h`, com a contagem embutida no objeto no lugar do `shared_ptr`. Posse +0/+1 (parâmetro
   emprestado, retorno com posse); temporários com liberação explícita na CIR, no fim da
   instrução.
4. **Erros por retorno explícito**, um mecanismo só (erros do runtime, `throw` e o sinal de
   `exit`): o erro pendente num estado de execução encapsulado pelo runtime (`cz_exec`, global
   sem threads), testado depois de cada chamada que pode falhar. Sem *unwinding*.
5. **CVM mantida até o `cinzac` passar a suíte inteira**; depois se decide o papel dela.
6. **Dois executáveis**: `cinza prog.cinza` (CVM, sem LLVM) e `cinzac prog.cinza -o prog.exe`.
7. O `StackOverflowError` da spec 5.8 é a regra das 2000 chamadas, não o estouro da pilha do
   sistema: o código nativo conta como a CVM, numa sombra da pilha que também dá o stack trace.

**Etapas** (cada uma com a suíte verde, ASan/UBSan e um commit):

- [ ] P0. Revisão do desenho (`llvm/DESENHO.md`) pelo autor. Spec 5.6: erro lançado no `finally`
  substitui o fluxo pendente, e o `exit` segue a mesma regra (o que a CVM já faz; exemplos
  executáveis na spec).
  > Revisão 2 (2026-11-18), depois de uma revisão externa: `CzValor` (C), `Value` (C++, dono) e
  > `ValorRef` (vista sem posse) separados — mesmo layout não torna os tipos intercambiáveis, e
  > as `NativeFn` passam a receber vistas; inicialização do alvo X86, triple e `DataLayout` do
  > módulo, tratamento do `LLVMErrorRef`, verificação antes e depois da otimização, teste de
  > layout contra os `static_assert` do runtime; nenhuma exceção do C++ escapa de `cz_*`
  > (`RuntimeError` vira erro pendente, `bad_alloc` é fatal como na CVM, o resto é erro interno);
  > orçamento de quadro por função calculado pela CIR e pilha dimensionada para o programa, com
  > rede de segurança que para com erro interno em vez de `0xC00000FD`; `borrow`/`copy`/`move`
  > e `store.own`/`store.copy` na CIR, slots começando vazios (sem liberação dupla nos caminhos
  > de erro); fila de destruição intrusiva (o campo `prox`, livre depois de o objeto sair do
  > registro da coleta), objetos nascendo com `refs = 1`, testes de contagem no build de
  > depuração; `CzString` sem array flexível (não é C++ padrão); o estado de erro descrito como
  > contrato de layout, não como opaco; a tabela de saídas do `finally` (`return`, `break` e
  > `continue` já eram erro de compilação); cabeçalhos e biblioteca do LLVM sempre da mesma
  > versão.
- [x] P1. Migração do runtime, conferida pela CVM: cabeçalho `CzObjeto` com contagem embutida
  não atômica (objetos nascem com `refs = 1`), destruição iterativa (corrige P1a), strings
  imutáveis com contagem (hoje o `std::string` é copiado a cada cópia do `Value`), `CzValor` de
  16 bytes com `Value` por cima, erros como objetos, descritores de classe/struct/enum sem
  ponteiro para a AST, contador de objetos vivos e o modo `CINZA_VAZAMENTOS=1`, testes de
  ciclos e cadeias em `tests/gc/`. Medição de velocidade antes e depois. — feito em 2026-11-18
  > Nota: em cinco passos, cada um com a suíte verde. (1) `gc_object.h`: `CzObjeto` (32 bytes,
  > `static_assert`), `GcObject` = cabeçalho + o que a coleta usa (`gc_refs`, `gc_marca`), `Ref<T>`
  > intrusivo no lugar do `shared_ptr` (`adota`, `empresta`, `solta`; atribuição com retain antes
  > do release), `novo<T>()`; `czDestroi` em `value.h` com a fila intrusiva pelo campo `prox`;
  > `gc.h` lê `refs` e segura o lixo com `czRetain`. (2) `CzString`: o texto imutável num
  > `std::string` interno, para `asString()` continuar `const std::string&` nos ~135 usos; os
  > bytes na mesma alocação ficam como otimização (o código nativo só lê por funções).
  > (3) `Value` = um `CzValor` (tipo, extra, união de 8 bytes) no lugar do `std::variant` de 40
  > bytes; acessores conferem o tipo (falha fria, `gnu::cold`) e devolvem os objetos como ponteiro
  > sem posse — nenhum código guardava cópia com posse do resultado; atribuição por cópia com
  > caminho rápido para valores sem objeto. (4) Descritores `CzClasseDesc`/`CzStructDesc`/
  > `CzEnumDesc` no campo `desc` do cabeçalho, montados pelo compilador da CVM (a imagem é a
  > dona); some o `class_name` copiado em cada objeto; a chamada por interface vai direto pelo
  > índice da classe (sem o mapa `class_index`); o `value.h` não inclui mais a AST; o tipo de
  > `type()` continua o `TypeInfo` (não depende da AST). (5) Modo sem vazamento: o `main` solta a
  > VM e a imagem, coleta e conta (por tipo); **ligado por padrão no `run_tests.py`**
  > (`--sem-vazamentos` desliga), então todo teste é teste de vazamento — a detecção foi
  > conferida com um objeto esquecido de propósito (código 70, "lista 1"). Conferência de
  > contagem (`CINZA_CONFERE_REFS`: liberação dupla, estouro) no `make debug`. Desvios do
  > desenho, registrados nele: o `ValorRef` das nativas virou cópia para `Value` no `cz_nativa`
  > (P3), sem mudar as ~200 funções da biblioteca; a fronteira `noexcept` entra com a interface C
  > (P3). Velocidade (bench/medir.ps1, ms, melhor de 5): `ciclos` 615 → 416, `fib` 54 → 40,
  > `laco` 123 → 108, `objetos` 100 → 62. Testes: `tests/gc/rev_cadeia_longa` e cinco `p1_*`
  > (construtor que falha, temporários de expressões que falham, autoatribuição, ciclos por tipos
  > variados, strings compartilhadas), mais conferências em `unit_gc` e `unit_value`. 649/649,
  > todos no modo sem vazamento; limpo sob ASan/UBSan com `CINZA_CONFERE_REFS`.
  - [x] P1a. **Defeito encontrado na CVM**: soltar uma cadeia longa (400 mil nós em
    `class No { list<No> f; }`) estoura a pilha do sistema (`0xC00000FD`), porque o
    `shared_ptr` destrói os nós em recursão; a spec 5.9 promete memória automática. Corrige com
    a destruição iterativa do P1 (o *trashcan* do CPython). Teste obrigatório da migração, na
    suíte normal e no modo sem vazamento: `tests/gc/rev_cadeia_longa`. — feito em 2026-11-18
    > Nota: 400 mil nós soltos em 0,45 s e 3 milhões em 3,1 s, sem estourar; `unit_gc` solta uma
    > cadeia de 1 milhão de listas. Os processos que travaram nas sondagens deste defeito ficaram
    > presos no relatório de erro do Windows e seguraram o `cinza.exe` (sem permissão para
    > encerrá-los); por isso as sondagens de travamento passam a rodar numa cópia do executável.
  - [x] P1b. **Defeito encontrado no P2, também na CVM de hoje**: as funções recursivas do
    runtime em C++ (`toString`, `==`, `<`) sobre dados aninhados sem limite estouram a pilha do
    sistema. Reprodução: `struct No { list<No> f; }` encadeado 200 mil vezes e impresso
    (`format("{n}")`) derruba o `cinza.exe` com `0xC00000FD`. Medido: 848 bytes de pilha por
    nível no `toString` (≈ 75 mil níveis com 64 MB). A correção pede uma decisão do autor:
    percorrer sem recursão (uma pilha explícita, como a destruição do P1) ou lançar um erro
    capturável acima de uma profundidade (como o `RecursionError` do `repr` no Python).
    — feito em 2026-11-18
    > Decisão do autor: **percorrer sem recursão** (a linguagem não ganha regra nova; a spec 5.9
    > promete a memória automática). Nota: só o `Value::toString` e o `Value::operator==`
    > desciam pelos dados (a biblioteca e o `operacoes.cpp` só fazem operações rasas; o `<` não
    > desce em contêineres). Os dois agora usam uma pilha explícita no heap: a igualdade empilha
    > pares de filhos a comparar (`detail::igualRaso`, sem alocar nada no caso comum de números e
    > strings); a conversão em texto empilha pedaços — valores e trechos fixos — em ordem
    > inversa. Formato da saída e regras de igualdade idênticos (a suíte inteira confere). 200 mil
    > níveis impressos e comparados em 1,3 s e 1 milhão em 7,2 s. Teste
    > `tests/gc/rev_aninhamento_profundo` (100 mil níveis, a diferença no nó mais fundo): o
    > executável de antes cai nele com `0xC00000FD`. Sem perda nos benchmarks. 650/650, limpo
    > sob ASan/UBSan.
- [x] P2. Pilha física: o orçamento de quadro por função, a reserva de pilha calculada e a rede
  de segurança (`llvm/DESENHO.md`, 6.3), conferidos com programas de 2000 níveis com dezenas de
  structs grandes por valor e chamadas ao runtime em cada nível. — feito em 2026-11-18
  > Nota: como a CIR ainda não existe (P4), o mecanismo foi conferido **antes** de ser
  > construído, com o protótipo `llvm/prototipos/pilha/` (`python llvm/prototipos/pilha/pilha.py`):
  > o "código gerado" é C compilado pelo clang 20.1.8 do CLANG64 (o mesmo LLVM do `cinzac`), o
  > runtime é C++ com o `value.h` de verdade compilado pelo GCC do projeto, e a ligação é pelo
  > `g++`. (1) O orçamento `(slots + cópias por valor) × 2 + 4 KB` cobre os quadros reais do LLVM
  > (`-fstack-usage`) em seis casos, em -O0 e -O2, com folga mínima de 2×; o LLVM não expõe o
  > tamanho do quadro para COFF, então a estimativa pela CIR é mesmo o caminho. (2) Um `print`
  > típico gasta ~4 KB de pilha no topo. (3) 2000 níveis do quadro extremo (dez structs de 8 KB,
  > vinte chamadas por valor) terminam em -O0 e -O2 com a reserva da fórmula. (4) Com a reserva
  > fixa de 64 MB, a rede de segurança para com "Erro interno: pilha do sistema esgotada", e o
  > controle sem a rede estoura com `0xC00000FD`. **Corrigido no desenho**: a reserva precisa
  > incluir a margem da rede (`2000 × orçamento + margem + 8 MB`), senão um programa válido com um
  > quadro maior que 8 MB bateria na rede. **Também**: o `cinza.exe` passa a chamar
  > `SetErrorMode` na partida (sem a janela de relatório de erro do Windows), e um processo que
  > cai termina na hora (conferido: 0,7 s no caso do P1b) em vez de ficar preso segurando o
  > executável, como os três do P1. Sem teste automático para isso: a suíte não tem programas que
  > derrubam o processo (o do P1b, que servia de exemplo, foi corrigido logo depois).
- [x] P3. `libcinzart` (interface C, `cinzart.h`), build do `cinzac` com a `llvm-c`, "olá
  mundo" nativo ligado pelo `g++`, `run_tests.py --cinzac` e `make test-llvm`. Antes: instalar
  os cabeçalhos (`mingw-w64-clang-x86_64-llvm`) **na mesma versão da `libLLVM` instalada**
  (`pacman -S` sem `-y`), ou atualizar o conjunto inteiro para uma versão nova e fixá-la.
  — feito em 2026-11-18
  > Nota: cabeçalhos instalados com autorização do autor (`pacman -S`, sem atualizar o banco):
  > `mingw-w64-clang-x86_64-llvm 20.1.8-2`, a mesma versão da `llvm-libs`, sem mexer em outro
  > pacote. **Problemas resolvidos no caminho**: (1) o GCC do projeto liga a `llvm-c` do CLANG64
  > (confirmado num teste: módulo, triple, DataLayout, `default<O2>`, objeto), mas com
  > `-idirafter` — senão os cabeçalhos do CLANG64 passariam na frente dos do GCC; (2) o
  > `cinzac.exe` caía com `0xC0000139` porque o `C:\MinGW\bin` do `PATH` tem outras versões de
  > `zlib1`, `libzstd` e `libxml2-16`, que a `libLLVM` também usa: o `make cinzac` copia a
  > `libLLVM-20.dll` e as 7 dependências (fecho calculado pelo `llvm-objdump`, ~145 MB) para
  > `llvm/bin/`, ao lado do `cinzac.exe` — o Windows procura ali antes do `PATH` (`llvm/bin/` fica
  > fora do git); (3) o `copy` do `cmd` lê o `+` de `libc++.dll` como concatenação: caminhos
  > entre aspas. **O que entrou**: `llvm/cinzart.{h,cpp}` (`cz_inicio`, `cz_fim` — relata o erro
  > pendente como a CVM, solta os literais e confere vazamentos —, `cz_str_literal` imortal,
  > `cz_nativa_resolve`/`cz_nativa` com os argumentos copiados para `Value`, `cz_solta_valor`;
  > toda função passa por uma fronteira que transforma `RuntimeError` em erro pendente e trata
  > `bad_alloc` e o resto como fatais); `Value::empresta`/`toma`/`entrega`; a CIR mínima
  > (`llvm/cir.*`: embrulha, call.nativa, solta, ret; `--emit-cir`) e a tradução da AST
  > (`llvm/gera_cir.*`: a `main` com `print` de literais, inclusive `-literal`; o resto é
  > "ainda não suportado", código 75, com a posição); o emissor (`llvm/emissor.*`, o único que
  > inclui `llvm-c`, com a sequência do desenho e o `--confere-layout`: `CzValor`, `CzObjeto` e
  > `CzExec` com o mesmo tamanho e deslocamentos no LLVM e no GCC); `llvm/cinzac.cpp` (front-end
  > igual ao do `cinza`, CIR, LLVM, ligação pelo `g++` com a `libcinzart.a` ao lado, `-O2`,
  > `--emit-llvm` com o IR já otimizado, `--manter-objeto`); `preparaConsole`,
  > `executaProcesso` e `caminhoDoExecutavel` em `plataforma.*` (o `cinza` passa a usar o mesmo
  > `preparaConsole`); o `operacoes.cpp` deixou de incluir o `semantic.h` (sobrava), então o
  > runtime não depende do front-end. **Testes**: `run_tests.py --cinzac` (compila, roda o
  > executável com os mesmos args/stdin/env, conta "ainda não suportado" à parte e resume os
  > motivos; diz quantos rodaram como executável nativo — os demais que passam são erros de
  > compilação do front-end comum) e `make test-llvm` (layout + suíte); teste
  > `tests/nativo/p3_ola` (literais de cada tipo, acentos, `print()` vazio) nos dois backends.
  > Resultado: 651/651 na CVM; pelo `cinzac`, 0 falhas, 341 passaram (8 como executável
  > nativo), 310 ainda não suportados; a CVM limpa sob ASan/UBSan. O `cinzac` sob ASan fica para quando a
  > CIR crescer (o build de depuração é pelo clang do CLANG64, que liga com `libc++`).
- [x] P4. CIR e backend para números, `bool`, funções, `if`/`while`/`for range`, `print`,
  `const` globais; `--emit-cir` e `--emit-llvm`. — feito em 2026-11-18
  > Nota: a CIR (`llvm/cir.h`) ganhou slots, globais, contas, comparações, conversões, chamadas e
  > fluxo; a tradução (`llvm/gera_cir.cpp`) segue a ordem de avaliação e os tokens de posição do
  > compilador da CVM, operação por operação; o emissor (`llvm/emissor.cpp`) põe o caminho rápido
  > em linha (`llvm.s*.with.overflow`, divisor zero e `menor / -1`, `x % -1` = 0 sem o `srem`
  > indefinido, resultado não finito no ponto flutuante, faixa nas conversões) e, no desvio frio, a
  > função do runtime que refaz a conta pela regra da CVM — a mensagem é a mesma por construção.
  > **Duas decisões tomadas na etapa**: (1) a **sombra da pilha entrou já no P4**, e não no P8:
  > sem ela, a regra das 2000 chamadas e o stack trace dos erros de conta não seriam os da CVM; a
  > posição vai como argumento ao caminho de erro, e só a chamada de função grava a posição no
  > quadro (DESENHO 6.2); (2) **até o P8 todo erro é fatal** (`cz_morre`: o relato de um erro não
  > capturado, ou o código do `exit`), por isso programas com `try` continuam "não suportados".
  > Também: as cópias de funções genéricas (N2) já compilam, porque são funções comuns; `for range`
  > conta em i64 com o iterador na largura do fim, como o `FORRANGE`. **Defeitos encontrados no
  > caminho, no cinzac**: uma referência a instrução guardada antes de gerar o valor de um `const`
  > (memória corrompida no `cinzac`), e o trace com a linha do quadro errado ("em main (a.cinza:0)")
  > — os dois apareceram na comparação com a CVM e foram corrigidos; o segundo tem teste próprio
  > (`tests/nativo/p4_erro_trace`). **Conferência**: 22 programas de erro (estouro em cada largura,
  > divisão e módulo por zero, `menor / -1`, `-menor`, f32 além da faixa, `cast` fora da faixa,
  > passo 0, 2000 chamadas, erro em `const`, erro de nativa, `exit`) com stdout, stderr e código de
  > saída idênticos aos da CVM, em -O0 e -O2. **Testes**: nove `tests/nativo/p4_*` nos dois
  > backends. 660/660 na CVM; pelo `cinzac` 0 falhas, 388 passaram (55 como executável nativo,
  > eram 8), 272 ainda não suportados (os motivos mais comuns: struct 49, string 47, class 37,
  > try 36, import 23). Velocidade: `fib(32)` mais um laço de 100 milhões em 137 ms com `-O2`
  > (1,1 s em -O0; 3,9 s na CVM).
- [x] P5. Strings e as regras de posse (temporários, `return` antecipado, parâmetros
  reatribuídos), com o modo sem vazamento. — feito em 2026-11-18
  > Nota: string em variáveis, parâmetros, retornos e `const`; `+` com string (o outro lado em
  > texto, o `CONCAT` da CVM), `+=`, as seis comparações, índice, fatia, `size()`, `for` em
  > string, `cast<string>` e `cast<T>(texto)`, `printf`/`format` e as nativas que recebem ou
  > devolvem string. A posse segue o DESENHO 4.3 (nota do P5 lá): slots donos que começam
  > vazios, parâmetros retidos na entrada, uma saída única por função que solta os slots,
  > temporários da instrução soltos no fim dela; a tradução escreve cada `retem`/`solta` e o
  > emissor não decide nada. As operações de string chamam o runtime (`call.rt`, por tabela)
  > com as mesmas regras da CVM (`operacoes.cpp`). **Decisão**: até o P8 o erro fatal não solta os
  > slots das funções em andamento, e o `cz_morre` desliga a conferência de vazamentos — os
  > testes de erro voltam a conferir no P8. **Conferências**: a suíte nativa inteira passa no
  > modo sem vazamento e também com uma `libcinzart` montada com `CINZA_CONFERE_REFS` (nenhuma
  > liberação dupla); uma sabotagem de propósito (`cz_solta` sem soltar) faz o
  > `tests/nativo/p5_posse` sair com 70 e "string 40046", então a conferência de fato pega a falta
  > de um `solta`. **Testes**: `tests/nativo/p5_strings`, `p5_posse` (return de dentro de laços com
  > locais vivas, parâmetro reatribuído, `s = s`, resultado descartado, temporários do lado
  > direito do `and`/`or`, 20 mil strings num laço, `for` aninhado em string com
  > `break`/`continue`) e três de erro (`IndexError`, `cast` inválido, passo 0 na fatia), nos dois
  > backends. 665/665 na CVM; pelo `cinzac` 0 falhas, 420 passaram (87 como executável nativo,
  > eram 55), 245 ainda não suportados (struct 49, try 46, class 37, import 23, `type()` 20,
  > list 40). Velocidade: 2 milhões de `cast<string>(i) + ";"` em 452 ms (580 na CVM) — o tempo é
  > do runtime comum aos dois; o `cz_texto` deixou de copiar a string que o `convertValue` cria.
- [x] P6. `list`, `dict`, `pair` e a coleta de ciclos. — feito em 2026-11-18
  > Nota: os três como ponteiros para os objetos do runtime de hoje, com a posse do P5 (DESENHO
  > 4.4, nota do P6): literais, `first`/`second`, métodos embutidos, leitura e gravação por
  > índice (aninhadas e compostas, com o caminho e as mensagens do `assignPlace`/`indexPlace` da
  > CVM), `==` que desce nos elementos, `for` sobre a cópia dos elementos com o alargamento
  > `i32 → i64`, nativas que recebem e devolvem listas (inclusive as que mudam a lista e as
  > genéricas, como `Lists.first`), `range` como lista e `list`/`dict` declarados vazios.
  > **Coleta de ciclos**: sem class ou struct não se forma ciclo na Cinza (nenhum tipo contém a
  > si mesmo), então ela só tem trabalho a partir do P7; o algoritmo só lê as contagens e os
  > filhos, e as referências dos slots nativos entram na contagem — nada a mudar nele. **Testes**:
  > `tests/nativo/p6_colecoes`, `p6_posse` (compartilhamento da spec 5.1, a cópia do `for` da
  > spec 5.5, return de dentro de um `for` em lista, 5 mil listas/dicts/pares por volta, temporários
  > aninhados) e dois de erro (`IndexError` na gravação, `KeyError` na gravação de chave nova),
  > nos dois backends, no modo sem vazamento e com a `libcinzart` de `CINZA_CONFERE_REFS`.
  > 669/669 na CVM; pelo `cinzac` 0 falhas, 462 passaram (129 como executável nativo, eram 87), 207
  > ainda não suportados (try 54, struct 49, class 37, `type()` 26, import 23). Velocidade: 2
  > milhões de `add`, um `for` e 2 milhões de leituras por índice em 184 ms (244 na CVM), depois de
  > tirar a alocação de um vetor por chamada de método (era 286 ms).
- [x] P7. Structs, classes, interfaces e enums. — feito em 2026-11-18
  > Nota: também o `type()` e os tipos escritos como valor (26 testes dependiam dele), e os
  > structs genéricos (as cópias do N5, structs comuns). O desenho está no DESENHO 4.4 (nota do
  > P7): struct como ponteiro com a regra de valor feita pela posse — cada slot tem o seu, guardar
  > um emprestado clona —, objeto e interface como ponteiro compartilhado, métodos com o self
  > primeiro, chamada por interface pela tabela do método, enum e tipo como `CzValor` sem
  > referência, os `TypeInfo` refeitos na partida pelo `TypeContext`. **Encontrado no caminho**: a
  > posição de uma chamada feita de dentro do inicializador de campos (oculto) precisa ir para o
  > quadro de quem fez o `new`, senão o trace mostra a linha do `new` em vez da linha da chamada,
  > que é a da CVM (teste `p7_erro_init`). **Testes**: `tests/nativo/p7_structs` (cópias em
  > atribuição, parâmetro, retorno e lista; caminhos com níveis de struct, campo de objeto que é
  > struct, criação pelo nome, genérico), `p7_classes` (inicializador antes do construtor,
  > métodos públicos, privados e sem o objeto, `self`, compartilhamento, interface com a classe
  > real), `p7_enum_tipo`, `p7_ciclos` (3 mil ciclos de objetos com struct, lista e string dentro:
  > o mesmo resumo do `CINZA_GC_STATS` da CVM) e dois de erro (`p7_erro_init`, `p7_erro_metodo`),
  > nos dois backends, no modo sem vazamento e com a `libcinzart` de `CINZA_CONFERE_REFS`. Os testes
  > `tests/gc/` que não usam `try` rodam como executável nativo. 675/675 na CVM; pelo `cinzac` 0
  > falhas, 577 passaram (244 como executável nativo, eram 129), 98 ainda não suportados (`try` 67,
  > `import` 23, `throw` 4, `main(args)` 4). Velocidade (bench/, -O2, melhor de 3): `fib` 23 ms
  > (CVM 61), `laco` 23 (153), `objetos` 41 (86), `ciclos` 570 (620).
- [x] P8. Erros completos: `throw`, `try`/`except`/`finally`, erros de runtime, stack trace,
  profundidade de 2000, `exit`. — feito em 2026-11-18
  > Nota: a sombra da pilha, o stack trace e a regra das 2000 chamadas já tinham entrado no P4.
  > Agora: cada operação que pode falhar leva o bloco tratador (DESENHO 5.3, nota do P8) — o
  > `except` de um `try` da função ou a saída de erro, que solta os slots e volta ao chamador com
  > o erro pendente; o chamador confere depois de toda chamada. `try`/`except`/`finally` com a
  > ordem e o pega-tudo do `tryStmt` da CVM, as saídas `return`/`break`/`continue` pelo
  > `finally`, `throw`, relançar com a mesma posição e trace, os tipos de `error`, `Tipo("msg")`,
  > os campos `kind`/`message`/`line`/`column`, o `exit` que roda os `finally` e o erro na
  > inicialização dos `const`. O `cz_morre` saiu, e com ele a exceção da conferência de
  > vazamentos: os testes de erro voltam a conferir que nada sobra. **Testes**:
  > `tests/nativo/p8_try`, `p8_posse_erro` (2000 voltas com erros no meio de expressões, em
  > funções aninhadas com locais e structs vivos e num construtor), `p8_finally` (as regras da
  > spec 5.6) e `p8_relanca_trace`, nos dois backends, no modo sem vazamento e com a `libcinzart`
  > de `CINZA_CONFERE_REFS`; os 22 programas de erro do P4 continuam idênticos à CVM em -O0 e
  > -O2. 679/679 na CVM; pelo `cinzac` 0 falhas, 652 passaram (319 como executável nativo, eram
  > 244), 27 ainda não suportados (todos do P9: `import` 23, `main(args)` 4). Velocidade com as
  > conferências depois das chamadas (bench/, -O2): `fib` 16 ms (CVM 41), `laco` 16 (111),
  > `objetos` 33 (63), `ciclos` 330 (376).
- [x] P9. Módulos, genéricos (as cópias), biblioteca nativa, `input`/`args`/`env`: a suíte
  inteira passa nos dois backends, sem vazamento. — feito em 2026-11-19
  > Nota: os genéricos (as cópias do N2 e do N5) e a biblioteca nativa já compilavam desde o P4–P7;
  > faltavam os módulos da Cinza e `main(args)`. Os módulos entram todos no mesmo módulo LLVM, na
  > ordem topológica, com os nomes completos que o semântico já dá (DESENHO 4.4, nota do P9). **A
  > suíte inteira passa pelo `cinzac`**: 681/681, nenhum "ainda não suportado", 348 como executável
  > nativo (os demais são erros de compilação, do front-end comum), no modo sem vazamento, em -O0
  > e em -O2 (`run_tests.py --cinzac --O2`, opção nova) e com a `libcinzart` de
  > `CINZA_CONFERE_REFS`. **Encontrado no caminho**: um literal de lista com 67 mil números levava
  > 9 s para compilar em -O0 e passava de 60 s em -O2 (uma instrução por elemento); os elementos
  > constantes agora vão num global já pronto, e os dois ficam em cerca de 1 s (teste que já
  > existia: `tests/cvm/cvm_literal_grande`, agora também em -O2). **Testes**:
  > `tests/nativo/p9_modulos` (dois módulos encadeados com const, enum, error, classe, struct
  > genérico e função genérica, `except` de um erro de outro módulo, stack trace atravessando os
  > arquivos) e `p9_args` (`main(list<string> args)` com acento, `input`), nos dois backends.
- [x] P10. Otimização: `-O2`, eliminar `retain`/`release` desnecessários, não testar erro depois
  de funções que não falham, campos e listas com tipos nativos. Medição contra a CVM e decisão
  sobre o papel dela. — feito em 2026-11-19
  > Nota (DESENHO 4.4, nota do P10): parâmetros que não mudam ficam emprestados, `return` de uma
  > local move, a contagem em linha no -O2, a regra das 2000 chamadas em quem chama e a conferência
  > do erro só depois de funções que podem falhar, e os campos sem referência lidos e gravados em
  > linha. **Decisões do autor**: (1) campos com o **caminho rápido**, sem mudar a representação do
  > runtime — a representação nativa (campos tipados, listas especializadas) fica de fora por
  > enquanto (DESENHO 8.1); (2) **as duas ficam**: `cinza` (CVM) roda na hora e `cinzac` gera o
  > executável; toda mudança da linguagem entra nos dois e a suíte roda nos dois. **Testes**:
  > `tests/nativo/p10_posse_otimizada`, `p10_profundidade_iface` e `p10_campos`; 684/684 na CVM e
  > pelo `cinzac` em -O0 e -O2, sem vazamento e com a `libcinzart` de `CINZA_CONFERE_REFS`; os 22
  > programas de erro do P4 continuam idênticos à CVM. **Medição** (-O2, melhor de 5, ms, nativo /
  > CVM, com o ganho do P10 entre parênteses): `ciclos` 334 / 407, `fib` 17 / 45, `laco` 17 / 119,
  > `objetos` 16 / 68 (era 32), e três novos — `strings` (concatenação e parâmetros) 461 / 909,
  > `structs` (structs por valor) 154 / 1055 (era 581), `metodos` (getters e um setter) 36 / 580
  > (era 110). O que sobra no `strings` e no `ciclos` é do runtime comum aos dois (alocação,
  > conversão em texto, a coleta). Compilar leva 0,4–0,55 s (a maior parte é a ligação pelo `g++`;
  > gerar o IR, 80 ms); o executável tem 4 MB.

**Fora desta seção**: informação de depuração, compilação separada por módulo, ABI estável,
threads, `cinzac` sem `g++` (com o `lld`), distribuição. (As funções recursivas do runtime sobre
dados aninhados, que estavam aqui como risco, viraram o P1b e foram corrigidas.)

---

## Q. Roteiro até a 1.0 (2026-11-19)

**Onde a Cinza está.** Especificação escrita (`spec/`, oito capítulos, com os exemplos executados
pela suíte); duas implementações permanentes com a mesma semântica — a CVM (`cinza`) e o
compilador nativo pelo LLVM (`cinzac`) —, as 684 provas da suíte passando nas duas, sem
vazamento; tipos numéricos rigorosos, structs, classes, interfaces, enums, erros com
`try`/`finally`, genéricos em funções e structs, módulos, sete módulos nativos; a extensão do VS
Code no repositório, com teste da gramática.

**O que "1.0" quer dizer aqui: a promessa de estabilidade.** Um programa válido na 1.0 continua
válido e faz a mesma coisa em toda a 1.x. Por isso a 1.0 vem depois de fechar o núcleo da
linguagem, e não antes. Até lá continua valendo a regra atual (sem compatibilidade com versões
antigas: o que muda, muda de vez); a última etapa (Q9) é a que a troca.

**Regras das etapas**, as mesmas de sempre: a suíte verde nos dois backends (`make test`, `make
test-llvm`, `run_tests.py --cinzac --O2`), um teste para cada correção, a spec atualizada quando a
linguagem muda. Os pontos **[decidir]** são do autor e vêm antes do código.

- [ ] Q1. Fechar o núcleo da linguagem. **[decidir]**
  - Q1a. **Classes e interfaces genéricas** (pausadas em 2026-11-15, seção N). Já decidido: tipos
    sempre escritos (`Pilha[i32]`; `No[T]` dentro da própria classe; o construtor com o nome puro);
    campo sem valor inicial, em toda classe, obriga um construtor que o atribua no nível de cima
    antes de ler o campo, chamar método ou usar `self`; só os métodos chamados são conferidos
    (modelo C++/Zig); `type(p) == Pilha[i32]`; interfaces genéricas entram (`interface
    Comparavel[T]`); métodos genéricos e restrições (`T: Forma`) ficam fora; struct nunca cumpre
    interface. Falta responder as oito incoerências levantadas: (1) o que conta como método
    "usado" — chamado, exigido por uma interface, chamado por um usado; (2) assinaturas
    conferidas já e corpos só se usados; (3) o parser aceitar qualquer tipo em `new`, `except` e
    `throw`; (4) valores iniciais sem `self` quando há campo obrigatório; (5) a lista completa da
    regra do construtor; (6) tirar dos structs a proibição de valor inicial num campo `T`; (7) no
    máximo uma cópia de cada interface genérica por classe; (8) classe com campo obrigatório do
    próprio tipo (depende de Q1b).
    > **Decidido (2026-11-23), as oito:** (1) um método é **usado** se é chamado de código conferido,
    > se é exigido por uma interface que a cópia cumpre, ou se é chamado por outro método usado;
    > (2) as **assinaturas** de todos são conferidas já na cópia, os **corpos** só dos usados; (3)
    > `new` aceita uma classe ou um struct genérico com os tipos (`new No[T](v)`) e o próprio `T`
    > (conferido em cada cópia: `new T()` com `T = i32` é erro com o rastro); `except` e `throw` só
    > tipos de erro, que nunca são genéricos; (4) com um campo obrigatório na classe, os valores
    > iniciais dos outros campos não usam `self` nem chamam métodos; (5) o construtor atribui cada
    > campo obrigatório **no nível de cima** do corpo (fora de if, laço e try), antes de ler um
    > campo, chamar um método, usar `self` ou retornar; (6) o campo `T` de struct continua sem
    > valor inicial (obrigatório) — o "pode faltar" é `T?`; (7) no máximo uma cópia de cada
    > interface genérica por classe (sem sobrecarga, duas exigiriam métodos com o mesmo nome); (8)
    > um campo obrigatório do próprio tipo é erro de compilação (o primeiro objeto nunca poderia
    > ser criado), com a dica `No? prox`.
    > **Feito (2026-11-23), no ramo `q1-classes-genericas`, em fatias, nos dois backends:**
    > 1. **Campos obrigatórios em toda classe.** O parser aceita campo sem valor inicial; o
    >    semântico (`confereCamposObrigatorios`) exige o construtor, confere as instruções do nível
    >    de cima dele até todos os obrigatórios estarem atribuídos (nada de ler um campo, chamar um
    >    método, usar `self` ou retornar antes), recusa `self`/campos/métodos nos valores iniciais
    >    dos outros campos e (`checkRequiredFieldCycles`) um ciclo de campos obrigatórios entre
    >    classes, com o caminho. O inicializador de campos dos dois backends deixa o obrigatório
    >    vazio para o construtor.
    > 2. **Classes genéricas.** `class Pilha[T]` guarda os próprios tokens, como o struct; cada uso
    >    (`Pilha[i32]`, também em `new`, no tipo escrito como valor e dentro da própria classe)
    >    gera uma cópia registrada com `registerClass` (sem símbolo global). Os campos, os valores
    >    iniciais e as interfaces da cópia são conferidos na fila das cópias; o construtor e o corpo
    >    de cada método, só quando usados (`usaCorpo`: chamada, `new`, interface, e o que um corpo
    >    usado chama), com o rastro da cópia nos erros. `FunctionDecl::corpo_analisado` (e o do
    >    construtor) diz aos backends o que gerar: a CVM e o `cinzac` compilam só os corpos
    >    conferidos, e o índice do LSP só os indexa. `new T()` vale quando `T` é uma classe ou um
    >    struct; com `T = i32` a cópia não é válida, com o rastro.
    > 3. **Interfaces genéricas.** `interface Comparavel[T]`; a lista de interfaces de uma classe
    >    aceita os tipos (`: Comparavel[Nota]`, `: Colecao[T]` numa classe genérica); cada uso é uma
    >    cópia registrada com `registerInterface`; uma classe cumpre no máximo uma cópia de cada.
    >
    > Spec: 3.6 e 4.6 (os campos obrigatórios), 8.7 (classes e interfaces genéricas, com exemplos);
    > testes em `tests/classes/q1a_*` e `tests/genericos/q1a_*`.
  - [x] Q1b. **Ausência de valor.** A Cinza não tem `null` (filosofia); opcionais (`T?`) foram
    discutidos em 2026-11-15, sem decisão. Entram na 1.0, e como? Ou ficam fora de vez (e uma
    lista vazia continua sendo o jeito de dizer "nenhum", como em `list<No>`)?
    > Decidido: `T?` e `Option` ficam fora de vez (Q1c). **Levantamento (2026-11-23)** de todo lugar
    > onde hoje se diz "nenhum", para o autor decidir um por um:
    > 1. **Buscas com `-1`**: `Strings.find`/`find_last`, `Lists.index_of`/`last_index_of`,
    >    `Regex.find`. O `-1` é um `i32` válido, um null com outro nome: `st.slice(s, i + 1, 3)`
    >    com `i = -1` dá o texto inteiro, sem erro nenhum.
    > 2. **Leituras que podem faltar**: `d[k]` (`KeyError`), `Dicts.get_or`, `Env.get`/`get_or`/`has`,
    >    `Json.Value.get`/`get_or`/`has`, `l[i]` com `l.has(i)`. Já seguem um padrão — lança, ou
    >    `_or(padrão)`, ou pergunta antes —, mas ele não está escrito como regra da biblioteca.
    > 3. **Coleção vazia como "nenhum"**: `find_all` → `[]`, `Regex.groups` → `[]`, `Regex.named` →
    >    `{}`, `keys_of` → `[]`. Natural. Exceção: um grupo do `Regex` que não participou vira `""`,
    >    igual a um grupo que casou o vazio.
    > 4. **Estruturas recursivas**: `list<No>` serve bem a 0 ou mais (filhos, vizinhos), mal a 0 ou 1
    >    (o próximo, o pai). **Bug confirmado:** um campo que constrói a própria classe
    >    (`No prox = new No();`) derruba as duas implementações — a CVM sai com "memória
    >    insuficiente", o executável do `cinzac` cai com estouro da pilha do sistema (0xC00000FD,
    >    sem mensagem) e o caso mútuo (`A` cria `B`, `B` cria `A`) trava a CVM consumindo memória. O
    >    struct já recusa conter a si mesmo; a classe, não. Liga-se ao item (8) do Q1a (campo
    >    obrigatório do próprio tipo: nunca dá para criar o primeiro objeto).
    > 5. **Funções que podem não ter resultado** (código do usuário): hoje, lançar um erro próprio,
    >    devolver `list<T>` de 0 ou 1, ou `pair<bool, T>` (que exige um `T` de mentira quando não há).
    > 6. **A entrada que acaba**: `input()` no fim da entrada lança `IOError`; ler até o fim exige o
    >    `try`/`except` como controle do laço.
    > 7. **Variável que ainda não tem valor**: toda local precisa de valor na declaração (3.6), então
    >    `i32 x; if (c) { x = 1; } else { x = 2; }` não existe; escreve-se um valor de mentira antes.
    > 8. **O `null` do JSON**: um `Json.Value` de `Kind.Null`, um dado como outro. Sem problema.
    >
    > **Decidido e feito (2026-11-23)**, item por item:
    > 1. As buscas que não acham **lançam `ValueError`** (`'find': "z" não aparece no texto`,
    >    `'index_of': 3 não está na lista`, `'find': o padrão "x" não casa com o texto`): `Strings.find`,
    >    `find_last`, `find_from`, `Lists.index_of`, `last_index_of`, `Regex.find`. Quem não sabe
    >    pergunta antes com `contains`. Para percorrer, entra `Strings.positions(s, t) -> list<i32>`
    >    (as ocorrências do `count`, sem sobreposição; nenhuma: `[]`).
    > 2. A regra escrita no capítulo 6 ("O que pode faltar"): lança, pergunta antes, `get_or` onde um
    >    padrão é comum, lista vazia para zero ou mais. Nada novo além do item 1.
    > 3. O `""` do grupo que não participou fica, escrito como indistinguível do vazio.
    > 4. **Corrigido:** um `new` nos valores iniciais dos campos (ou nos padrões de um struct) que
    >    volta ao próprio tipo é erro de compilação, com o caminho (`No → No`, `A → B → A`,
    >    `Caixa → No → Caixa`), no semântico (`checkConstructionCycles`, depois de todos os
    >    módulos): vale para os dois backends. É o único caminho sem chamada no meio; por um
    >    construtor, um método ou uma função a criação sem fim já parava no `StackOverflowError`
    >    (conferido nos dois). `list<No>` continua o jeito de 0 ou mais.
    > 5. Funções do usuário: os mesmos idiomas da biblioteca, na nova seção 3.10 da spec ("Sem
    >    `null`: como dizer nenhum", com exemplo executado: perguntar antes, lista, erro próprio e o
    >    objeto vazio que cumpre a interface). Sem recurso novo.
    > 6. `input()` no fim continua lançando `IOError` (sem `has_input`).
    > 7. A 3.6 fica: todo valor na declaração (sem atribuição definida).
    >
    > **Achado no caminho, corrigido:** todo erro de runtime num executável nativo do Windows,
    > mesmo capturado, escrevia `libunwind: pc not in table` na saída de erro — as funções geradas
    > não tinham tabela de desempilhamento (`.pdata`), e o libunwind, ao tratar a exceção do
    > runtime, não achava o quadro de quem o chamou. Agora o emissor marca as funções geradas com
    > `uwtable`. A suíte não via porque não olhava a saída de erro de quem termina bem: o
    > `run_tests.py` passa a exigir a saída de erro vazia num programa que termina com código 0
    > (em todos os modos). Testes: `tests/classes/q1b_criacao_*`,
    > `tests/biblioteca/q1b_buscas_sem_menos_um.cinza`, os exemplos novos da spec (3.10 e 4.6).
    >
    > **Reaberto pelo autor (2026-11-23): o opcional `T?` com `null`.** Depois de conversar com
    > outra IA, o autor quer a ausência dentro da linguagem, de forma segura: um tipo que a admite e
    > o compilador exigindo que ela seja resolvida antes do uso. Substitui os itens 1, 3, 4 (o
    > `list<No>` como "zero ou um"), 5 e 6 acima e as entradas "null e opcional" do Q1c; o erro de
    > ciclo de criação e o `uwtable` ficam. **Desenho** (decidido com o autor, falta implementar):
    > 1. **O tipo.** `T?` admite `null`; `T` nunca. Vale para todo tipo menos `void`, também dentro
    >    de `list`, `dict` (só no valor: uma chave não pode ser opcional) e `pair`. `T??` é erro; numa
    >    cópia de genérico, `T?` com `T` já opcional é erro com o rastro.
    > 2. **`null`** ("ausência de um valor válido ou conhecido") é palavra reservada e só entra onde
    >    se espera um `T?` (`var x = null;` é erro: sem tipo). `T` vira `T?` sozinho; `T?` nunca vira
    >    `T` sem resolver.
    > 3. **Sem valor inicial**, uma variável ou um campo `T?` nasce `null`, como `list` e `dict`
    >    nascem vazios; num struct, o campo `T?` não é obrigatório no `new`. Assim o campo de um
    >    genérico (`T? ct;`) e o "próximo" de uma estrutura (`No? prox;`) não precisam de valor de
    >    mentira — resolve os itens (6) e (8) do Q1a.
    > 4. **Um `T?` não se usa como `T`**: operadores, métodos, campos, índices e `for` sobre ele são
    >    erro de compilação ("valor opcional: resolva com else ou com if (T v = ...)"). Só `==` e `!=`
    >    valem: `x == null`; dois opcionais iguais se os dois são `null` ou têm valores iguais.
    > 5. **`expr else alternativa`**: o valor de `expr` (um `T?`), ou a alternativa (um `T`) se for
    >    `null`; a alternativa só é avaliada na ausência. A precedência é a mais baixa dos operadores;
    >    `a else b else c` encadeia (`b` pode ser `T?`).
    > 6. **`expr else { ... }`**: todo caminho do bloco termina com `yield valor` (o valor que a
    >    expressão recebe) ou sai do fluxo (`return`, `throw`, `break`, `continue`); chegar ao `}` é
    >    erro de compilação (a mesma análise do "retorna em todos os caminhos"). `return` continua
    >    sempre "sair da função"; `yield` fora de um bloco de `else` é erro.
    > 7. **`if (T nome = expr) { ... } else { ... }`** e **`while (T nome = expr) { ... }`** (também
    >    com `var`): o bloco roda quando há valor, com `nome` do tipo `T`, que só existe nele; `expr`
    >    precisa ser opcional. Sem estreitamento: `x != null` não muda o tipo de `x` (com campos ele
    >    não seria seguro).
    > 8. **Na execução**: `print` e a interpolação mostram `null`; `type()` de um `string?` é
    >    `string?` (também escrito como valor); `const T?` vale.
    > 9. **Biblioteca** (migra já; sem compatibilidade). Regra: o que quem chama controla lança (`l[i]`,
    >    `d[k]`, `first`/`last`/`pop`/`min`/`max` e `Random.choice` de lista vazia); o que pode não
    >    existir de verdade devolve `T?`, e somem os atalhos redundantes:
    >    - `Strings.find`, `find_last`, `find_from` (o início inválido continua `IndexError`),
    >      `Lists.index_of`, `last_index_of`, `Regex.find` → `i32?` (desfaz o `ValueError` do item 1);
    >    - `Dicts.get(d, k) -> V?` entra e `Dicts.get_or` sai (`d.has(k)` fica: é da coleção);
    >    - `Env.get -> string?`; saem `Env.get_or` e `Env.has`;
    >    - `Json.Value.get -> Value?`; saem `get_or` e `has` do objeto (o `Kind.Null` fica: é dado);
    >    - `Convert.to_i32`/`to_i64`/`to_f32`/`to_f64`/`to_bool(s) -> T?`; saem os `is_*` e os `*_or`
    >      (o `cast<T>(s)`, que lança, fica);
    >    - `Regex.groups -> list<string?>` e `named -> dict<string, string?>`: o grupo que não
    >      participou é `null`, o que casou o vazio é `""`;
    >    - `input() -> string?`: `null` no fim da entrada (`while (string l = input("")) { ... }`).
    > 10. **Spec**: a 3.10 reescrita (a ausência com `T?`), a gramática (o sufixo `?`, `null`, `else`
    >    numa expressão, `yield`, a declaração no `if`/`while`), os capítulos 3, 5 e 6, a filosofia e
    >    "O que fica fora" do README (sai "null e opcional").
    > 11. **Implementação**: nos dois backends (na CVM um valor marcado; no `cinzac` um `T?` vai como
    >    `CzValor`, com a marca, inclusive `i32?`), no LSP, no formatador e na gramática do VS Code.
    > 12. Decidido depois: o valor de um bloco de else é dado por **`yield`** (o `return` continua
    >    sendo sempre "sair da função"); todo caminho do bloco termina com `yield` ou saindo.
    >
    > **Feito (2026-11-23), no ramo `q1-opcional`, em fatias — cada uma no front-end e nos dois
    > backends, com a suíte e o ASan verdes antes da próxima:**
    > 1. **O tipo, `null`, `==`/`!=` e o `else` com valor.** Léxico (`?`, `null`, `yield`), o tipo
    >    `OPTIONAL` na AST e `Optional`/`Null` no `TypeContext`; no semântico, a atribuição (`T` →
    >    `T?` por um cast implícito; `T?` nunca → `T`), a comparação, a dica do `else` em toda
    >    mensagem de tipo incompatível, o literal que se adapta ao `T` de dentro, o `T?` sem valor
    >    que nasce `null` (locais, campos de classe, campos de struct, que deixam de ser
    >    obrigatórios), `T??` e `void?` recusados (também numa cópia de genérico, com o rastro; o `T`
    >    deduzido de um `T?`; `null` não diz o tipo). Na CVM, `Value::Kind::NULO` (no fim da
    >    enumeração, que o runtime nativo também usa) e `LOADNULL`/`JMPNOTNULL`; no `cinzac`, o tipo
    >    `Opc` da CIR — o `CzValor` inteiro, dono do que segura: os lugares de posse passam a
    >    perguntar `temPosse` (copiar é `cz_valor_copia`, que retem ou clona o struct; soltar,
    >    `cz_solta_valor`), o slot nasce `null`, `EhNulo` testa a marca em linha. `type()` de um `T?`
    >    é `T?` (`runtimeType`, comum aos dois).
    > 2. **O `else` com bloco e o `yield`.** O bloco roda no meio de uma expressão: no `cinzac`, os
    >    temporários da expressão em volta continuam vivos — um `return` ou um `break` para um laço
    >    de fora os solta antes de sair, e a captura de um `try` de dentro do bloco não os toca. Na
    >    AST, os percursos comuns (`filhosDe`, `expressoesDe`, `blocosDeElse`, `instrucoesDe`) para
    >    quem só olhava instruções: `hasBreak`, `basesDeAtribuicao`, o verificador de ciclos de
    >    criação e o índice do LSP. `yield` não sai de um `try` (o `finally` ficaria para trás).
    > 3. **`if (T nome = opcional)` e `while (T nome = opcional)`.** O parser olha adiante (tipo, nome,
    >    `=`), então `if (i32 == type(x))` continua sendo uma expressão; a variável é do tipo exato de
    >    dentro (ou `var`, ou a interface que a classe cumpre) e só existe no primeiro bloco. CVM:
    >    `JMPNULL`; `cinzac`: os temporários da condição soltos nos dois caminhos.
    > 4. **A biblioteca.** As buscas (`Strings.find`/`find_last`/`find_from`, `Lists.index_of`/
    >    `last_index_of`, `Regex.find`) dão `i32?`; `Dicts.get`, `Env.get`, o `get` do `Json.Value`,
    >    `Convert.to_i32`/`to_i64`/`to_f32`/`to_f64`/`to_bool` e `input()` dão `T?`; `Regex.groups`/
    >    `groups_all`/`named` dão `null` no grupo que não participou. Saíram `Dicts.get_or`,
    >    `Env.get_or`/`has`, `has`/`get_or` do `Json.Value`, os `Convert.is_*` e `to_*_or`. O menu do
    >    projeto de teste `crud_contatos` passou a tratar o fim da entrada como "sair".
    > 5. **Ferramentas.** O formatador cola o `?` ao tipo e trata `else`/`yield` como antes de uma
    >    expressão (o `else {` continua bloco); a gramática do VS Code marca `null`, `yield` e o `?`
    >    (que deixou de ser inválido); o LSP indexa o bloco do else, o `yield` e a variável da
    >    condição de um if/while.
    >
    > A spec foi reescrita onde o opcional entra: 3.10 (o opcional inteiro, com exemplos), 3.1, 3.6,
    > 4.6, 4.7, 5.2, 5.3, 5.5, 8.6, a gramática (2.2, 2.3, 2.4: o `else` como o operador de menor
    > precedência, associando à direita), o capítulo 6 ("O que pode faltar" e as assinaturas) e a
    > filosofia do README. Testes em `tests/opcional/` (o básico, estruturas, posse, o bloco com
    > `yield`, o if/while com declaração e 24 erros de compilação) e os da biblioteca migrados.
    >
    > **Um custo que apareceu na migração, decidido pelo autor:** com `Json.Value.get` devolvendo
    > `Value?`, uma cadeia de acesso ao JSON precisava resolver cada nível
    > (`(v.get("nome") else nada).as_text()`). Entra `field(k) -> Value`, que lança `KeyError` sem a
    > chave, como `d[k]` num dict: `v.field("nome").as_text()` quando o formato é conhecido, `get`
    > quando a chave pode faltar. A versão sobe para **0.6.0** (versao.h e a spec).
  - [x] Q1c. **O que fica fora da 1.0 por filosofia**, registrado como decisão para quem chegar:
    closures e funções como valor (e com elas `map`/`filter` que recebem função), herança, `null`
    (conforme Q1b), métodos genéricos, restrições nos genéricos, threads. Confirmar a lista.
    > Feito (2026-11-23), com a lista confirmada pelo autor e registrada na spec (`spec/README.md`,
    > "O que fica fora"). **Nunca:** `null` e o tipo opcional, herança, closures e funções como
    > valor (nem sem captura: o polimorfismo é pelas interfaces, e `Lists.sort_by` ordena por
    > chave), globais mutáveis, sobrecarga de funções e de operadores, conversões implícitas entre
    > números, tipo dinâmico, reflexão, ponteiros e memória manual, métodos genéricos e restrições
    > nos genéricos, infinito e NaN, import relativo. **Fora da 1.0, a rever com o uso real:**
    > concorrência (threads, `async`, canais; hoje o paralelismo é por `Process`) e macros.
  - Q1d. **Limpar as pendências do documento**: os itens da seção E já decididos com a caixa
    aberta (campo e método com o mesmo nome, `dict.add`, comentário de bloco, expoente nos
    literais, `7 / 2` num decimal), o P0 aberto (revisão do desenho do backend pelo autor) e o
    "CVM" da seção D (feito, com outro desenho: seção F).
  - Q1e. **Varredura final da linguagem**: programas-sonda para cada capítulo da spec, atrás de
    lacunas e incoerências entre a spec e as duas implementações, como a revisão bruta (E).
  - Q1f. **Sistema de módulos.** O autor considera a modularização confusa (2026-11-19) e quer
    revê-la antes da 1.0. **[decidir]** Levantar com o autor o que confunde — os nomes completos
    (`util.texto::soma`) e os apelidos, `pub`, a raiz do projeto (`czm.mod` vazio ou a pasta do
    principal) e o `CINZA_PATH`, os módulos nativos com `import` obrigatório, o que um módulo pode
    declarar no nível de cima, a ordem de inicialização dos `const` — e propor um desenho, nos dois
    backends.
    > **Levantamento com o autor (2026-11-24):** confundem o nome do módulo vir só do caminho, as
    > duas regras da raiz do projeto e a ordem de inicialização dos `const`. Os apelidos, o `import`
    > dos nativos, o `pub` item por item, o `CINZA_PATH` (que fica para o Q10) e os nomes nas
    > mensagens não incomodam. **Decidido:** (1) todo arquivo importável começa com
    > **`module util.texto;`**, conferido contra o caminho a partir da raiz (ou da pasta do
    > `CINZA_PATH` em que foi achado); o erro diz o nome certo; o principal não precisa; (2) a raiz
    > é **sempre a pasta do `czm.mod`**: sem ele, o programa é de um arquivo só (importa só os
    > módulos nativos); um `cinza init` cria o `czm.mod`, que o Q10 vai usar para o nome e as
    > dependências; (3) os **`const` são calculados na compilação** (já são expressões constantes),
    > também os que usam `const` de outros módulos: não há ordem de inicialização na execução, e
    > uma conta inválida (estouro, divisão por zero) é erro de compilação no `const`.
    > **Feito (2026-11-24), no ramo `q1-modulos`, em fatias, nos dois backends:**
    > 1. **`const` na compilação.** O semântico (`avaliaConst`/`dobraConst`) calcula cada `const`
    >    global com as contas da execução (`operacoes.cpp`: `applyBinaryOp`, `negateOp`,
    >    `convertValue`), também `null`, o `else` e os `const` de outros módulos e os nativos; um
    >    `RuntimeError` vira erro de compilação no `const`, com a mensagem da execução. O valor
    >    inicial vira o literal do resultado (número, texto, bool, null), então nenhum `const`
    >    depende de outro na execução; um `const` de enum fica como está (não há conta).
    > 2. **A raiz só pelo `czm.mod`.** Sem ele, `projectRoot` devolve "sem projeto" e um `import`
    >    de arquivo é `ImportError` (sugere o `cinza init`); os nativos continuam valendo. `cinza
    >    init [pasta]` cria o `czm.mod` (recusa se já existe). Testes no `run_tests.py`
    >    (`testes_do_projeto`, numa pasta temporária).
    > 3. **`module a.b;`.** Palavra reservada; o parser a lê antes dos imports; o carregador confere
    >    o nome contra o caminho do `import` (e o do principal, se ele declarar). Os 33 módulos dos
    >    testes, dos exemplos e a biblioteca (`module Json;`) migrados — na mesma primeira linha do
    >    comentário, para os números de linha das mensagens não mudarem. A gramática do VS Code e
    >    o completar do LSP conhecem o `module`.
    >
    > Spec: 2.1, 4.3 e 4.11.
- [x] Q10. Gerenciador de pacotes (antes da 1.0, decisão do autor de 2026-11-22). **Desenho
  decidido com o autor (2026-11-24):**
  1. **Origem**: repositórios git — uma dependência é a URL do repositório e uma versão (uma tag
     semver, `v1.2.0`); sem servidor central. Uma dependência local (para desenvolver uma
     biblioteca junto) é um caminho no disco, sem versão. O **`CINZA_PATH` sai**: os módulos vêm
     só do projeto e das dependências declaradas.
  2. **`czm.mod`**, formato próprio, uma declaração por linha, com comentários `//`:
     `projeto nome` (o nome do pacote; `cinza init` escreve o da pasta), `cinza 0.6` (a versão
     mínima da linguagem, opcional), `requer github.com/ana/json v1.2.0 [as outro]`,
     `requer ../minha_lib`.
  3. **Reprodutível**: um `czm.lock` guarda, de cada dependência (também as transitivas), a
     versão escolhida, o commit e um hash do conteúdo; a compilação confere o hash.
  4. **Versões**: vale a maior versão pedida dentro da mesma major (seleção mínima, como no Go);
     majors diferentes (v1.x e v2.x) são erro, com as cadeias de quem pediu cada uma.
  5. **Cache do usuário**: uma pasta por pacote e versão (`~/.cinza/pacotes`), compartilhada entre
     os projetos; o projeto não guarda cópias.
  6. **Download pelo `git` instalado** (sem HTTPS dentro do compilador; repositório privado usa a
     autenticação do git), e **só por comandos**: `cinza add <url> [versão] [as nome]`,
     `cinza remove <nome>`, `cinza get` (o que o `czm.lock` pede). Compilar nunca acessa a rede:
     falta um pacote no cache, o erro manda rodar `cinza get`.
  7. **Import pelo nome do pacote**: `import json.leitor;` — o primeiro trecho é o pacote (o
     nome declarado no `czm.mod` dele, ou o `as` do `requer`), o resto o caminho dentro dele. Dois
     pacotes com o mesmo nome são erro; um nome de pacote igual a uma pasta do projeto também.
  8. **Dentro de um pacote**, os módulos se importam pelo caminho a partir da raiz dele (`import
     util.texto;`, `module util.texto;`), como num projeto comum: escrever uma biblioteca não
     muda nada; só quem está de fora acrescenta o nome do pacote.
  > Feito em 2026-11-24. `pacotes.h/.cpp` (novo, nos dois binários): o leitor do `czm.mod`
  > (`leManifesto`, o mesmo do carregador), o do `czm.lock`, o cache (`CINZA_PACOTES` ou
  > `~/.cinza/pacotes`, uma pasta `<último trecho>-<12 dígitos do SHA-256 da origem>@<versão>`, curta
  > para o limite de caminho do Windows), o hash (SHA-256 próprio, dos arquivos em ordem, com o
  > caminho e o tamanho de cada um, sem o `.git`) e os comandos `cinza add/remove/get` no `main.cpp`.
  > O `get` percorre os `czm.mod` (o principal, os locais e os dos pacotes no cache), escolhe a
  > maior versão de cada origem na mesma major, baixa pelo `git clone --depth 1 --branch <tag>`
  > (sem `core.autocrlf`: o hash é o mesmo em todo sistema), tira o `.git` e grava o lock; uma
  > versão já travada cujo conteúdo mudou é baixada de novo e, se ainda não conferir (a tag foi
  > movida), recusada. O `add` sem versão pega a maior tag (`git ls-remote --tags`) e devolve o
  > `czm.mod` como estava se o `get` falhar. O `ModuleLoader` lê os projetos (`Projeto`,
  > `Dependencia`): o primeiro trecho de um import que é um pacote resolve dentro da raiz dele, com
  > o nome esperado no `module` relativo a ela; uma dependência git vem do lock do projeto
  > principal e do cache, conferida pela versão (não menor que a pedida, mesma major) e pelo hash;
  > um pacote git com dependência local é erro. O `//` só é comentário no começo da linha ou depois
  > de um espaço (o de `https://` fica). O `CINZA_PATH` saiu (o carregador, a busca dupla e o erro
  > de "mais de um lugar"); `tests/` virou um projeto com duas dependências locais
  > (`tests/pacotes/saudacao` e `exemplos/rede_neural`, que ganhou `projeto rede_neural`), e os
  > testes do `CINZA_PATH` saíram. `cinza init` passa a escrever `projeto <pasta>` e `cinza 0.6`.
  > Testes: `importacao/q10_pacote_local.cinza`, e no `run_tests.py` 11 casos de dependências locais
  > e 13 de pacotes git (repositórios temporários `file://`: add com e sem versão, a maior versão
  > pedida, majors diferentes, sem `get`, sem cache, o cache alterado, a tag movida, o lock
  > desatualizado, remove, dependência local num pacote git, tag que não existe, o `//` da URL).
  > Spec: 4.11 (o `czm.mod` e os pacotes).
- [ ] Q11. Desempenho (antes da 1.0, decisão do autor de 2026-11-22), sem mudar a semântica. Medir
  com programas de verdade e atacar o que o perfil mostra, nas duas implementações.
  1. **Medição**: `bench/` ganha seis programas com dados (`listas`, `dicts`, `arvores`, `corpos`,
     `texto`, `structs_lista`) e as versões em Python (`bench/python/referencias.py`), para a
     ordem de grandeza. Os sete de antes rodavam em 15–800 ms e quase não tinham dados.
  2. **Runtime** (comum aos dois): o alocador, a coleta de ciclos, os caminhos quentes.
  3. **Código nativo**: campos de referência e índice de lista em linha, chamada direta das
     funções numéricas da biblioteca (`Math`), emprestar em vez de clonar o struct lido só para
     um campo (`ps[i].x`).
  4. **Representação nativa** (campos tipados, `list<i32>` como vetor de `int32_t`): só se a
     medição depois das fatias 2 e 3 pedir.
  > Andamento (2026-11-24), fatia 1 (medição e runtime). O perfil (um amostrador que lê o RIP e a
  > cadeia de frames a cada 1 ms, fora do repositório) mostrou, nos programas novos: o heap do
  > Windows (`HeapAlloc`) com 30 a 45% do tempo; a coleta de ciclos varrendo todos os vivos (50%
  > do `structs_lista`); `TypeContext::intern` (uma busca no mapa com chave composta) a cada
  > conversão e a cada função da biblioteca; o `cz_nativa` alocando um vetor por chamada. Feito:
  > (a) **alocador de objetos pequenos** (`gc_object.h`, `aloca::`): classes de 16 em 16 bytes até
  > 256, listas livres, pedaços de 64 KB, sem trava (uma thread), para os objetos do runtime, os
  > vetores de campos (`Campos`) e os nós dos dicts (`Entradas`); no build de depuração vai direto
  > ao heap, para o ASan; (b) **coleta por gerações** (`gc.h`): os contêineres nascem jovens, a
  > coleta comum percorre só os jovens (a referência de um velho conta como de fora) e promove os
  > que sobram; a completa roda quando os velhos crescem metade desde a última; (c) **tipos
  > acíclicos fora da coleta**: o semântico marca as classes e os structs cujo tipo não alcança a
  > si mesmo pelos campos e parâmetros (interface e tipo não resolvido contam como alcançar tudo;
  > `marcaAciclicos`), e o descritor leva a marca aos dois backends (`rastreia`); (d) os tipos
  > primitivos guardados no `TypeContext`; (e) os argumentos do `cz_nativa` e do `cz_metodo` na
  > pilha, construindo só os usados; (f) a criação e o clone dos objetos fora de linha: no laço de
  > despacho da CVM, o corpo inlinado mudava a alocação de registradores do laço inteiro (o `fib`,
  > que nem cria objetos, ficava 35% mais lento); (g) o `czDestroi` (o objeto chegou a zero) fora de
  > linha e frio (`gnu::noinline, gnu::cold`): inlinado, o destrutor do `Value` ficava grande demais
  > para o clang inliná-lo, e o `stable_sort` de uma lista destruía milhões de valores movidos com
  > uma chamada cada; sem o `cold`, a chamada no laço de despacho do g++ deixava a CVM mais lenta;
  > (h) no código gerado, o `CzValor` é `{ i64 cabeça, i64 u }` (a cabeça é `tipo | extra << 32`),
  > gravado em duas palavras em vez de um byte, um `memset` de 7 e o `u` (`--confere-layout`
  > confere). **Medição** (A/B intercalado contra o `main`, mediana de 5, ms, mesma máquina;
  > CVM main → ramo, nativo main → ramo): `structs_lista` 3726 → 629, 3534 → 530 (5,9× e 6,7×);
  > `structs` 859 → 270, 140 → 39; `arvores` 897 → 611, 730 → 361; `ciclos` 337 → 192, 313 → 151;
  > `texto` 198 → 123, 201 → 101; `strings` 712 → 550, 361 → 217; `corpos` 1258 → 1208, 589 → 267;
  > `listas` 738 → 643, 425 → 358; `dicts` 452 → 396, 487 → 382; `fib`, `laco`, `metodos` e
  > `objetos` sem mudança (0,95–1,00). Contra o Python 3.13 (mesmos programas): o nativo ganha em
  > `listas` (358 × 883), `arvores` (361 × 585) e `corpos` (267 × 1235) e perde em `structs_lista`
  > (530 × 329), `dicts` (382 × 246) e `texto` (101 × 82) — o que sobra é a representação: cada
  > elemento de lista e cada campo de referência passam pelo runtime, e o struct numa lista é
  > clonado na entrada e na saída. **Testes**: `gc/q11_geracoes` (um velho
  > segurando jovens, ciclos que envelhecem antes de virar lixo, um jovem preso a um ciclo velho),
  > `gc/q11_aciclicos` (ciclos por campo opcional, dict, pair, interface, classe genérica e struct
  > copiado para a própria lista, todos liberados no modo sem vazamento, entre objetos de tipos
  > acíclicos) e `unit_gc` (o struct sem rastreio e o clone dele; a coleta dos jovens com um velho
  > segurando um jovem; o ciclo velho só na completa).
- [ ] Q2. Robustez.
  - Fuzzing do front-end: nenhuma entrada derruba o `cinza` nem o `cinzac` (um erro interno ou uma
    queda é defeito), começando por mutações dos programas da suíte.
  - Teste diferencial com programas gerados: as duas implementações são o oráculo uma da outra
    (a mesma saída, os mesmos erros, o mesmo stack trace).
  - ASan/UBSan no `cinzac` e na `libcinzart` (pendente desde o P3: o build de depuração é pelo
    clang do CLANG64, que liga com a `libc++`).
  - Limites: programas grandes (milhares de funções, arquivos de 100 mil linhas) — tempo de
    compilação, memória, a pilha do parser.
  > Andamento (2026-11-19): o fuzzer `tests/fuzz/fuzz.py` cobre os dois primeiros itens. Mutantes de
  > 1 a 3 mutações dos programas da suíte (apagar, duplicar, trocar e inserir símbolos e linhas,
  > enxertar linhas de outro programa, cortar, aninhamento fundo, bytes inválidos e, para o
  > diferencial, números de borda e operadores da mesma família) rodam no `cinza` (com
  > `CINZA_VAZAMENTOS=1`) e no `cinzac --emit-cir`; uma amostra é compilada e comparada com a CVM.
  > Defeito é queda, erro interno, travamento do front-end, vazamento ou diferença entre os
  > backends; cada um é gravado minimizado (delta debugging por linhas e por símbolos). Primeira
  > sessão (15 min, 6 trabalhos): 14.550 mutantes, 274 no diferencial, nenhum defeito. Segunda, com
  > as mutações de números e operadores: 9.990 mutantes, 412 no diferencial, nenhum defeito. O fuzzer
  > foi conferido com um `cinza` falso que cai com 0xC0000005 em programas com `while`: acusou as duas
  > quedas e minimizou cada uma até `while`.
  > **Gerador de programas válidos** (`tests/fuzz/gera.py`, no estilo do Csmith): programas de 140 a
  > 320 linhas gerados do zero, bem tipados, determinísticos e com fim garantido (laços com limite,
  > recursão limitada, chamadas só para funções anteriores), com números nas bordas das quatro
  > larguras, conversões que estouram, divisão por zero, strings (índice, fatia, `printf`, `format`),
  > listas e dicts, structs (a cópia), classes e interface, enum, função genérica, recursão e
  > `try`/`except`/`finally`/`throw`; a maior parte da `main` em `try`, para o primeiro estouro não
  > encerrar o programa. Cada um roda na CVM e no `cinzac` em -O0 e -O2, com saída, erros e código
  > de saída iguais nos três e sem vazamento. Sessão de 15 min: 1.784 programas, todos válidos e
  > terminando, todos comparados nos três (380 terminam com erro não pego, também comparado), nenhum
  > defeito. Conferido que um nativo com uma linha a mais é acusado.
  > **ASan/UBSan no `cinzac`**: `make cinzac-debug` (no MSYS2 CLANG64, como o `make debug`) compila o
  > `cinzac` e a `libcinzart` pelo clang com `-fsanitize=address,undefined` e a conferência da contagem
  > (`CINZA_CONFERE_REFS`); a opção nova `cinzac --asan` instrumenta também o código gerado (o passo
  > `asan` do LLVM em toda função: confere o que o P10 faz em linha, a contagem e os campos); a
  > variável nova `CINZA_LIGACAO` acrescenta opções à ligação (o runtime do ASan, pelo `clang++`). `make
  > test-llvm-debug` roda a suíte assim, com `UBSAN_OPTIONS=halt_on_error=1` — sem ela o UBSan só
  > avisa e o programa sai com 0, e um teste que só confere a saída passaria (conferido com um estouro
  > de `int` de propósito). Conferidos também: o ASan acusa um estouro de buffer de propósito, o IR com
  > `--asan` chama as rotinas do ASan, e o `--confere-layout` vale com a `libc++` do clang. Resultado:
  > 684/684, nenhum relato do ASan nem do UBSan (no compilador, no runtime e no código gerado).
  > **Programas grandes** (`tests/fuzz/grandes.py`): 20 famílias em tamanhos crescentes — 20 mil
  > funções, cadeias de 2 mil chamadas, um `main` de 100 mil linhas, 70 mil locais, aninhamento até o
  > limite e além (parênteses, blocos, `if`, operadores, chamadas, listas), 5 mil classes, structs de
  > 10 mil campos, enum de 100 mil valores, literais de 10 MB e de 1 milhão de elementos, 50 mil
  > literais distintos, identificador de 1 milhão de caracteres, 10 mil parâmetros — no `cinza` e no
  > `cinzac` -O0 e -O2, com tempo, pico de memória e a saída nativa comparada com a da CVM. Defeito é
  > queda, erro interno, tempo esgotado, diferença entre os backends ou tempo que cresce mais que o
  > dobro do tamanho. O front-end e a CVM não caem em nada e respondem em menos de 1 s em tudo; os
  > limites (2000 níveis, 65535 registradores da CVM) dão a mensagem deles. Achados e corrigidos:
  > - o front-end conferia valor repetido de enum comparando cada um com todos os anteriores (100 mil
  >   valores: 20 s → 0,1 s);
  > - o `cinzac` procurava cada string literal numa lista (`Modulo::literal`), quadrático, e criava os
  >   literais com uma chamada `cz_str_literal` por literal na partida: agora um índice, uma tabela
  >   constante e uma chamada `cz_literais`; num vetor de literal grande a string literal vai como
  >   marca constante (`CZ_TIPO_LITERAL` com o índice), e o `embrulha` de um literal só vira código onde
  >   é usado (dict de 100 mil chaves: 58 s e 1,6 GB → 1,9 s e 330 MB em -O0; em -O2, de tempo
  >   esgotado a 3,3 s);
  > - em -O2, o refcount em linha põe 5 blocos em cada `solta`, e com centenas de temporários vivos
  >   ao mesmo tempo o alocador de registradores do LLVM ficava quadrático (lista de 1000 níveis:
  >   114 s → 0,6 s): acima de 256 `solta` a função usa as chamadas do runtime;
  > - em -O2, uma função de milhares de blocos deixava o alocador e o *machine sinking* do LLVM
  >   quadráticos (5 mil classes usadas no `main`: tempo esgotado → 9,5 s; `main` de 100 mil linhas:
  >   tempo esgotado → 15 s): acima de 3000 instruções da CIR a função vai com `optnone` e, depois dos
  >   passes, num segundo objeto gerado em -O0 (`emiteObjeto` divide o módulo; llvm/DESENHO.md 4).
  > Testes novos: `enum/enum_grande`, `enum/enum_grande_repetido`, `nativo/q2_lista_funda`,
  > `nativo/q2_funcao_enorme` (passa pelo segundo objeto, com erro não pego e o trace) e
  > `nativo/q2_literais`; 689/689 na CVM, no `cinzac` -O0 e -O2 e com ASan/UBSan. Custos que ficam,
  > lineares: o -O2 leva ~1,7 ms por função pequena e ~13 ms por função numa cadeia de chamadas (o
  > inliner); o `cinzac` usa ~23 KB de memória por linha de um `main` enorme (2,3 GB para 100 mil
  > linhas em -O0) — o IR de cada linha, com a conferência de erro e a posição de cada operação.
- [x] Q3. Biblioteca padrão da 1.0. Hoje: `Strings`, `Files`, `Math`,
  `Random`, `Convert`, `Lists`, `Time`. Candidatos: JSON (texto ↔ `dict`/`list`), ambiente e
  processo (variáveis de ambiente, rodar um comando), pastas (listar, criar, apagar), o que faltar
  em datas e em texto. Cada função na spec (capítulo 6) com exemplo; as nativas valem nos dois
  backends sem trabalho a mais. Nada que precise de closure (Q1c).
  > Feito (2026-11-19), com o escopo decidido pelo autor: entram `Dicts`, `Env`, `Process`, `Regex`
  > e `Json` (as pastas já estavam em `Files`). Na spec (capítulo 6), cada um com exemplo:
  > - **`Dicts`** (nativo): `get_or`, `contains_value`, `keys_of`, `items`, `is_empty`, `update`,
  >   `pop`, `clear`, `merge`, `copy`, `from_lists`, `from_pairs`, `invert`. Chave repetida ao
  >   montar e valor repetido ao inverter são `ValueError`; o dict devolvido precisa de uma chave
  >   válida (`invert` de um `dict<string, f64>` é erro na compilação, pela regra das chaves).
  > - **`Env`** (nativo): `get` (`KeyError` se não existir), `get_or`, `has`, `all` — só para
  >   ler: mudar o ambiente seria um estado global.
  > - **`Process`** (nativo): `run` (o programa escreve no terminal; devolve o código de saída) e
  >   `capture` (`pair<i32, string>`: o código e a saída padrão), **sem shell** — o programa e a
  >   lista de argumentos (opcional), sem interpretação; `.bat`/`.cmd` (que o Windows roda pelo
  >   `cmd`) são `ValueError`, para o shell ser pedido explicitamente. `plataforma.cpp` ganhou
  >   `executa` (com a saída por um pipe, e "não iniciou" separado do código de saída; no POSIX o
  >   erro do `execvp` volta por um pipe com `O_CLOEXEC`) e `ambienteTodo`.
  > - **`Regex`** (nativo, motor próprio em `regex.cpp`, no estilo do RE2): uma máquina de Pike em
  >   tempo linear para qualquer padrão, sobre os code points; classes, `\d \w \s` (letras e
  >   espaço como em `Strings`, regras agora em `utf8.h`), grupos com nome, repetições gulosas e
  >   preguiçosas até 1000, `^ $ \b`, `(?i)`; sem `\1` nem lookaround. `matches`, `contains`,
  >   `find`, `find_all`, `count`, `groups`, `groups_all`, `named`, `replace_all`/`replace_first`
  >   (`$1`, `${nome}`, `$$`; referência inválida é erro), `split`, `escape`, `is_valid`; as várias
  >   ocorrências seguem o RE2. Conferido com `tests/fuzz/regex_dif.py` contra o `re` do Python:
  >   30 mil padrões e textos aleatórios, nenhuma diferença (a ferramenta acusou uma sabotagem do
  >   guloso); `(a+)+b` em 5000 letras responde na hora; ~4 MB/s numa busca com captura.
  > - **`Json`** — o primeiro módulo da biblioteca **escrito em Cinza** (`biblioteca/Json.cinza`,
  >   embutido com `#embed` por `biblioteca.cpp`; o carregador o acha depois dos nativos, como
  >   `<biblioteca>/Json.cinza`, e o nome é reservado). `Json.Value` com `Json.Kind` (o null do
  >   JSON é `Kind.Null`); `parse` com linha e coluna nos erros, chave repetida é erro, 500 níveis;
  >   o número guarda o texto (`as_i64` exato); o objeto mantém a ordem das chaves; `to_text` e
  >   `to_text_pretty`. Vale nos dois backends sem uma linha de código neles. Custo: ~1 MB/s no
  >   `parse` (as chamadas ao runtime por caractere); se pesar em uso real, um leitor nativo.
  > Testes: `tests/biblioteca/{dicts,env,process,regex,json}_*` e
  > `tests/importacao/q3_biblioteca_*`; 711/711 na CVM, no `cinzac` -O0 e -O2, e com ASan/UBSan
  > (CVM e nativo). A mensagem de erro de argumento de uma função de módulo em Cinza cita só o
  > nome (`'parse'`, sem `Json.`), como nos módulos do usuário.
- [x] Q4. Portabilidade.
  - Linux nos dois backends: a CVM com GCC e Clang; o `cinzac` com o triple do sistema
    (`LLVMGetDefaultTargetTriple`) no lugar do `x86_64-w64-windows-gnu` fixo em `emissor.cpp`, e
    o LLVM e o ligador configuráveis (hoje o caminho do MSYS2 está no Makefile).
  - macOS: decidido — fica para depois da 1.0.
  - Integração contínua (GitHub Actions): a suíte nos dois backends, em Windows e Linux, a cada push.
  > Feito (2026-11-19). `.github/workflows/ci.yml` roda a CVM e o `cinzac` em -O0 e -O2 em três
  > ambientes, a cada push e pull request: Linux com GCC 15 (o contêiner `gcc:15`, com o LLVM 20 do
  > apt.llvm.org), Linux com Clang 20 (Ubuntu 24.04) e Windows (MSYS2 UCRT64 com GCC, e o LLVM
  > 20.1.8 e as dependências dele fixados pelo arquivo do MSYS2 — com as versões atuais, o
  > `libLLVM-20.dll` não acha funções delas). 711/711 nos nove. O que mudou:
  > - **pilha**: no POSIX a thread principal tem a pilha do `ulimit` (8 MB); o `cinza` e o
  >   `cinzac` rodam numa thread com 64 MB (`comPilhaGrande`, `plataforma.cpp`), para os 2000
  >   níveis do front-end; no Windows continua o `-Wl,--stack`;
  > - **`cinzac`**: o triple do sistema (no Windows o do MinGW) e o alvo nativo; código PIC fora do
  >   Windows (o GCC das distribuições liga como PIE); sem `.exe` padrão nem `-Wl,--stack` fora do
  >   Windows;
  > - **Makefile**: no Linux o LLVM 20 vem do `llvm-config-20`/`llvm-config` (com rpath), e só
  >   quando o `cinzac` é pedido; no Windows, as DLLs do LLVM que existirem na pasta (o MSYS2 atual
  >   não traz mais o `liblzma`);
  > - **`Math.cbrt`**: o do glibc dava `3.0000000000000004` para 27; agora fica, entre o resultado
  >   da libm e os dois vizinhos, o de cubo mais perto (o mesmo em todo sistema, exato nos cubos
  >   perfeitos). As outras funções transcendentes vêm da libm do sistema e podem diferir no
  >   último dígito entre sistemas (nenhum teste mostrou diferença);
  > - **teste de horário de verão**: o `TZ` com as regras escritas (`EST5EDT,M3.2.0,M11.1.0`). Sem
  >   elas, o glibc sem o arquivo de zona `EST5EDT` aplica o `posixrules` e o fim do horário de
  >   verão cai uma hora antes — o `date` do próprio sistema mostra o mesmo; a Cinza segue o
  >   sistema.
  > O compilador precisa do `#embed` (GCC 15 ou Clang 19 em diante); o GCC 13 do Ubuntu 24.04 não
  > serve. Os programas gerados no Linux ficam com a pilha padrão (8 MB), que basta para as 2000
  > chamadas da linguagem (a suíte, com os testes de profundidade, passa assim).
- [x] Q5. Distribuição.
  - Versão: `cinza --version` e `cinzac --version` com a versão da linguagem e do compilador; a
    spec com a mesma versão.
  - **`cinzac` sem MSYS2 nem `g++`** — decisão do autor (2026-11-19): eliminar as duas
    dependências como outras linguagens fazem. Rust leva o próprio ligador (`rust-lld`) e as
    bibliotecas de partida; Zig leva o `lld` e a libc; Go tem um ligador próprio. Para a Cinza: o
    `lld` junto do `cinzac` (ligado pela `llvm-c`/`lld` como biblioteca, ou um executável ao lado),
    a `libcinzart` e o que ela precisa do C++ (libstdc++, libgcc, mingw-w64) já compilados e
    distribuídos com ele, e o `cinzac` sem precisar do MSYS2 para rodar (hoje ele usa ~145 MB de DLLs
    do LLVM do CLANG64). Decidido: LLVM estático, só o necessário; o runtime continua em C++, com a
    biblioteca C++ embutida; no Linux, os programas estáticos com a musl.
  - Releases no GitHub: um zip ou instalador para Windows e um pacote para Linux, com o `cinza`,
    o `cinzac`, a biblioteca e a extensão empacotada (`.vsix`); decidido: só o `.vsix` nos
    releases, sem o Marketplace por enquanto.
  > Feito (2026-11-20), no ramo `q5-distribuicao`. Versão **0.5.0** (decisão do autor, a numeração
  > da spec): `versao.h`, `cinza --version` e `cinzac --version` (com o LLVM e o alvo); a suíte
  > confere que a spec, o `versao.h` e o `--version` dizem o mesmo. O `cinzac` liga sozinho: o `lld`
  > dentro dele (`llvm/ligador.cpp`) e a pasta `runtime/` ao lado com a `libcinzart` e as bibliotecas
  > de partida e do C++, sem `g++` nem MSYS2 (llvm/DESENHO.md, seção 9):
  > - **Windows**: um `cinzac.exe` de ~45 MB, sem DLLs, compilado pelo clang do CLANG64 com o LLVM e o
  >   `lld` estáticos, só o alvo x86 (`llvm/alvos_vazios.cpp`) e sem o libxml2 (`llvm/xml_vazio.cpp`;
  >   com ele vinham o libiconv, LGPL, e o liblzma); a CVM também estática. Conferido com o `PATH`
  >   só do Windows: compila, liga e roda; o executável gerado importa só DLLs do sistema.
  > - **Linux** (`make ESTATICO=1`, no Alpine 3.22 com o clang 20): a CVM e o `cinzac` estáticos, e
  >   os programas gerados também, com a musl e as bibliotecas do LLVM para o C++ (libc++, libc++abi,
  >   libunwind, compiler-rt — licenças permissivas, como no Windows; decisão do autor, em vez da
  >   libstdc++ e da libgcc, GPL). O Alpine só traz o `lld` em `.so`: o CI compila as bibliotecas
  >   estáticas dele da fonte do 20.1.8, em cache. Na ligação, `--eh-frame-hdr` (sem ele a libunwind
  >   não acha as tabelas e as exceções do runtime terminam o programa) e
  >   `--no-dependent-libraries` (a libc++ pede `-lrt`/`-lpthread`/`-ldl`, que a musl tem na libc).
  > - **Layout**: o `gc_refs` do `GcObject` era `long` (4 bytes no Windows, 8 no Linux) e deixava o
  >   vetor de campos em 40 num e 48 no outro; agora `std::int64_t`, 48 em todo sistema, fixado por
  >   `static_assert` no emissor e no runtime (que podem usar bibliotecas C++ diferentes).
  > - **Pacotes** (`distribuicao/empacota.sh`): `cinza-<versão>-windows-x64.zip` (~23 MB) e
  >   `cinza-<versão>-linux-x64.tar.gz` (~38 MB), com `bin/` (o `cinza`, o `cinzac` e a `runtime/`), a
  >   licença, `LICENCAS-TERCEIROS.md` com os textos em `licencas/`, o README e a spec; o pacote
  >   compila e roda um programa antes de fechar. O CI os gera a cada push, com o `.vsix` da extensão
  >   (que ganhou `publisher`, `license` e `repository`), e numa tag `v<versão>` (a de `versao.h`)
  >   cria um release do GitHub (pré-lançamento) com os três e as instruções de `distribuicao/NOTAS.md`.
  > - O build de ASan continua (`make cinzac-debug`, dinâmico): o `cinzac` instrumentado com o LLVM
  >   estático dá falsos positivos de *container-overflow* nas anotações da libc++, e o
  >   `test-llvm-debug` roda com `detect_container_overflow=0`; 711/711.
  > 711/711 em todos os jobs do CI (Linux GCC e Clang, Linux estático, Windows).
- [ ] Q6. Ferramentas.
  - Servidor de linguagem (LSP) mínimo, com o front-end de hoje: os erros enquanto se digita;
    depois ir para a definição, o tipo ao passar o mouse e completar nomes. A extensão passa a
    usá-lo (hoje ela só colore).
  - Formatador (`cinza fmt`): decidido — entra na 1.0, com um estilo só, sem opções.
  - Fora da 1.0 (confirmado pelo autor): depurador (informação de depuração no `cinzac`). O
    gerenciador de pacotes saiu desta lista em 2026-11-22: entra antes da 1.0 (Q10).
  > Andamento (2026-11-20). **Formatador** (`formatador.cpp`; `cinza fmt arquivo...`, `--check`, e
  > `cinza fmt -` da entrada para a saída, em bytes): como o gofmt, as quebras de linha são as do
  > autor e o resto é refeito — o recuo (2 espaços por chave), os espaços entre os tokens, as linhas
  > em branco (no máximo uma, nenhuma depois de `{` nem antes de `}`) e o fim do arquivo; uma
  > continuação (dentro de `(`/`[`, ou de uma instrução que não terminou) guarda o alinhamento do
  > autor; comentários, BOM e CRLF ficam. O resultado passa pelo léxico da linguagem: com outros
  > tokens que o original, o arquivo é recusado (o formatador nunca muda o que o programa faz), e um
  > arquivo com erro léxico, string sem fim ou chave sem par também. Na suíte: os pares de
  > `tests/formatador` e, em todos os programas, idempotente e sem recusar programa válido; dos 660
  > `.cinza` do repositório, 68 mudariam (quase só recuo de 4 espaços) — a suíte não foi
  > reformatada, porque vários testes conferem colunas.
  > **Servidor de linguagem** (`lsp/servidor.cpp`, `cinza --lsp`; JSON próprio em `lsp/json.h`): a
  > cada arquivo aberto ou mudado, o front-end inteiro sobre o texto do editor, sem executar; os erros
  > vão como diagnósticos (a coluna convertida para UTF-16), os de um módulo importado no arquivo dele,
  > e são limpos quando somem; a formatação do documento é o `cinza fmt`. A extensão do VS Code (0.8.0)
  > passa a ter um cliente (`extension.js`, com o `vscode-languageclient`, empacotado no `.vsix` pelo
  > CI) e a configuração `cinza.caminho`; sem a biblioteca (na pasta de desenvolvimento, sem
  > `npm install`), ela segue só com o destaque. Testes em `tests/lsp` (um cliente mínimo em Python):
  > erros semânticos e de sintaxe, a coluna depois de acentos, a limpeza, análises repetidas no mesmo
  > processo, o módulo importado, a formatação e o encerramento.
  > **Índice** (`lsp/indice.cpp`, 2026-11-22): quando o texto passa sem erros, enquanto a árvore
  > anotada existe, um percurso por todos os módulos (e pelas cópias dos genéricos) guarda cada nome
  > usado com o que ele é (`i32 soma`, `fn Conta.deposita(i64 v)`, `Cor.Verde`, a assinatura de uma
  > nativa) e onde foi declarado, mais os nomes visíveis por trecho e os membros de cada tipo e de
  > cada módulo. Daí o tipo ao passar o mouse, ir para a definição (também dentro de um módulo
  > importado) e completar nomes: depois de `.`, os membros públicos do valor ou do módulo (e os
  > métodos de list, dict, string e pair); sem ponto, as locais já declaradas, o nível de cima, o
  > prelúdio e as palavras-chave. Com o texto em erro, vale o último índice bom (as posições podem
  > estar deslocadas até o texto voltar a compilar). Testes: 13 casos a mais em `tests/lsp`.
- [ ] Q7. Documentação.
  - O livro (tutorial) reescrito para a linguagem atual, com os exemplos executados pela suíte,
    como os da spec; ele leva as pendências do livro deste documento (Fase 2.5: o Apêndice A em
    EBNF; C1: a divisão inteira e o sinal do `%`, a tabela de precedência).
  - Guia de instalação e de uso dos dois comandos; o README em português e em inglês.
  - As mensagens de erro (spec, capítulo 7) revistas como referência.
- [ ] Q8. Prévia pública (0.9). Divulgar como prévia: o que funciona e por que é interessante,
  que mudanças incompatíveis ainda podem acontecer, o que falta. Um canal de retorno (issues do
  GitHub). Pelo menos alguns programas reais escritos por outras pessoas; os defeitos e as
  lacunas que eles mostrarem entram como itens desta seção.
- [ ] Q9. Congelamento e 1.0. **Decisão do autor (2026-11-19): o congelamento só acontece
  depois de as classes genéricas (Q1a) estarem implementadas de forma segura e consistente.**
  Depois disso: um período sem mudança incompatível (a candidata a 1.0); a política de compatibilidade da 1.x escrita na spec — o que é estável (sintaxe, tipos,
  semântica, biblioteca) e o que não é (o texto das mensagens, o desempenho, o bytecode da CVM, a
  CIR); a regra "sem compatibilidade com versões antigas" sai do CLAUDE.md e da memória do
  projeto; a tag `v1.0.0` e as notas da versão.

**Ordem sugerida.** Q1 primeiro: sem o núcleo fechado, o resto pode ter de ser refeito. O fuzzing
do Q2 pode começar já, porque não depende de decisão (o autor pediu para começar por ele, em
2026-11-19). Q3 e Q4 podem correr juntos. Q5, Q6 e Q7
antes da prévia (Q8), e a 1.0 (Q9) só depois de um período de uso real.

**Prioridade do autor (2026-11-22).** Nesta fase o que importa é a linguagem ser séria, segura,
madura e confiável — a divulgação (Q8) fica para perto da 1.0. Depois do Q6 (terminado com o tipo
ao passar o mouse, ir para a definição e completar nomes no servidor de linguagem), a ordem é:

1. Q1c — registrar o que fica fora por filosofia.
2. Q1b — a ausência de null como vantagem, não como problema: primeiro um levantamento de todo
   lugar onde hoje se diz "nenhum" (as sentinelas da biblioteca, `get_or`, `list<No>` nas estruturas
   recursivas, campos que podem não ter valor, buscas que não acham), com soluções sem null (sem
   `T?` nem `Option`, decisão do autor) para ele decidir uma por uma.
3. Q1a — classes e interfaces genéricas (depende do Q1b no campo do próprio tipo).
4. Q1f — o sistema de módulos.
5. **Q10 — gerenciador de pacotes, antes da 1.0** (revê a decisão de 2026-11-20 no Q6): desenhado
   junto com os módulos novos; o `czm.mod` passa a declarar o projeto e as dependências.
6. **Q11 — desempenho**, com o núcleo fechado (para não otimizar o que ainda vai mudar): a
   representação nativa dos campos e das listas, menos contagem de referências, o runtime.

---

## Registro de decisões

| Data | Item | Decisão | Motivo |
|------|------|---------|--------|
| 2026-09-26 | Fase 2.5 | Não há variáveis globais mutáveis; nível superior só declara (`fn`, `class`, `struct`, `error`, `import`, `const`). | Filosofia disciplinada; o estado compartilhado vive em objetos passados explicitamente. |
| 2026-09-27 | A5 | `int` tem 64 bits (`int64_t`), faixa -9223372036854775808 a 9223372036854775807. | Overflow vira raridade; tamanhos e índices nunca estouram; `decimal` já tem 64 bits. Mais barato agora do que depois de B1. |
| 2026-09-28 | C4 | `Error` continua sendo a raiz dos erros (sem `Exception`); erros de runtime sem tipo próprio têm tipo `Error`. | Decisão do autor da linguagem. |
| 2026-09-27 | C3 | `struct` tem cópia rasa: os campos são copiados; `list`, `dict` e objetos dentro dele continuam compartilhados. | Coerente com o resto da linguagem (`list<int> b = a;` já compartilha); sem problema de ciclos; custo previsível. |
| 2026-09-27 | C3 | `const` fecha o recipiente inteiro (struct/list/dict/pair, em qualquer profundidade), mas para no objeto de classe: os objetos dentro de um recipiente const continuam alteráveis. `const` diretamente num tipo de classe (`const Pessoa p`) é proibido. | Decisão do autor da linguagem: evita precisar saber se um método altera o objeto, e mantém `const` útil para valores e coleções. |
| 2026-09-27 | A10/B5 | `self` é implementado na Fase 4, logo depois do B5 (atribuição a campo). | Depende do lvalue genérico. |
| 2026-09-27 | C1 | `&&`, `\|\|` e `!` continuam válidos como sinônimos de `and`, `or` e `not`. | Decisão do autor da linguagem. |
| 2026-09-27 | C3 | `str` continua sinônimo de `string`. | Decisão do autor da linguagem. |
| 2026-09-27 | Fase 2.5 | Opções do interpretador vêm antes do arquivo; tudo depois dele vai para `main(list<string> args)`. | Consequência do item de `args`; é a convenção de interpretadores como `python`. |
| 2026-09-27 | A10 | `string.size()` conta caracteres (code points UTF-8), não bytes. | É o que o usuário espera ao contar letras de texto em português. |
| 2026-09-29 | C6 | Sem namespace `io`: `print` e `input` são embutidos, como no Python; `println` não existe. | Saída e entrada no terminal são usadas por todo programa; o namespace só adicionaria atrito. |
| 2026-09-29 | C6 | Namespaces da biblioteca padrão com inicial maiúscula e import obrigatório (`import Strings as st;`): `Strings`, `Files`, `Math`, `Random`, `Convert`, `Lists`. Dentro deles os nomes são curtos (`Files.read`, `Files.write`). | Decisão do autor da linguagem; a maiúscula também evita conflito com os tipos `string` e `list`. |
| 2026-09-29 | C6 | Arquivo do usuário com o nome de um módulo nativo é erro na hora. | Sem ambiguidade silenciosa entre o módulo nativo e um arquivo `.cinza`. |
| 2026-09-29 | C6 | `size` continua método de string (`"evans".size()`), como em `list`/`dict`. | Decisão do autor da linguagem (revista no mesmo dia: chegou a ir para `Strings.size`). |
| 2026-09-30 | D | `op<...>` trava o tipo na inicialização e ele nunca muda; nada dinâmico. Onde o tipo travado não é conhecido, a operação precisa valer para todos os tipos; `if (type(x) == T)` estreita. `list<op<A, B>>` tem todos os elementos do mesmo tipo e, com tipo desconhecido, é somente leitura. Recursão com `list<No>`. `TypeError` fica como erro embutido. Regra 9: no máximo um list, um dict e um pair por op. | Decisão do autor: o uso livre com `TypeError` em runtime (versão anterior, do mesmo dia) contrariava a filosofia da Cinza. |
| 2026-09-30 | D | Fugir do tipo dinâmico o máximo possível: campo de classe com `op` trava no inicializador do campo (o construtor não escolhe o tipo). | "Não combina com a Cinza" — decisão do autor. |
| 2026-09-30 | D | `enum`: valor sempre qualificado (`Cor.Verde`), só nomes (sem int por trás), só `==`/`!=`, pode ser chave de dict, `print` mostra `Cor.Verde`. `print(4.0)` continua mostrando `4`. | Decisões do autor da linguagem. |
| 2026-09-30 | D | Interfaces: `class Circulo : Forma, Desenhavel`; várias por classe; só métodos; um valor do tipo interface nunca volta a ser tratado como a classe. | Decisões do autor: polimorfismo sem herança, conferido na compilação. |
| 2026-09-30 | D | A Cinza não terá genéricos. | Decisão do autor da linguagem. |
| 2026-09-30 | D | Em vez de `option<T>`, a linguagem terá `op<T1, T2, ...>`: um lugar que aceita um valor de qualquer um dos tipos listados. | Decisão do autor da linguagem. |
| 2026-09-29 | D | Backend: VM própria (CVM), não LLVM. (Decidido primeiro LLVM, revisto no mesmo dia.) | A CVM reaproveita o runtime em C++ e se depura com as mesmas ferramentas (ASan, testes); o LLVM exigiria reescrever o runtime com interface em C, contagem de referências no IR e exceções próprias — de 3 a 5 vezes mais trabalho. |
| 2026-09-29 | C6 | Depois de `apelido.` o parser aceita palavra-chave como nome (`Random.int`, `Random.decimal`). | Mantém os nomes da biblioteca; não há ambiguidade depois de um apelido de módulo. |
| 2026-09-26 | Fase 2.5 | `const` global aceita literais, operadores e outros `const`; sem chamadas, `new` ou literais de coleção. | Avaliável antes da `main` sem efeitos colaterais; `const` ainda não é imutável em profundidade. |
| 2026-10-03 | G1 | `Files`: apagar o que não existe lança `IOError`; `copy`/`move` recebem `bool substituir` explícito; `delete_dir` só apaga pasta vazia e `delete_tree` apaga tudo; as funções de caminho ficam dentro de `Files`. | Decisão do autor da linguagem: nada falha em silêncio, e o perigo fica explícito na chamada. |
| 2026-10-03 | G2 | `Math`: sem `inf`, `is_nan` e `is_inf` (a linguagem nunca produz infinito nem NaN); `min`/`max` continuam com 2 argumentos (máximo de lista vai para `Lists.max`); `round_to` só arredonda o valor (exibir `2.50` é formatação de texto, em `Strings`/`Convert`); `factorial` e `is_prime` entram. | Decisão do autor da linguagem. |
| 2026-10-03 | G3 | `Strings`: casas fixas em `Strings.fixed(x, casas)`; sem `format` com marcadores (a concatenação já aceita primitivos); preenchimento de `pad_left`/`pad_right`/`center` opcional (espaço); sem `swap_case`. | Decisão do autor da linguagem. |
| 2026-10-03 | G4 | `Convert`: validação (`is_*`) e conversão com padrão (`to_*_or`), as duas; bases com `to_base`/`from_base` e os atalhos `to_hex`/`to_binary`/`to_octal`; `to_decimal` só aceita ponto (vírgula: `Strings.replace` antes); sem conversão entre `bool` e `int`. | Decisão do autor da linguagem; `bool` e `int` continuam tipos distintos. |
| 2026-10-03 | G5 | `Random`: decimal num intervalo é `decimal_range(a, b)`; sem `weighted`; `sample`/`choices` com `k` = 0 dão `[]`, mesmo com lista vazia. | Decisão do autor da linguagem. |
| 2026-10-03 | G6 | `Lists`: objetos se ordenam com `sort_by(l, chaves)` (chaves paralelas, reorganizadas junto); as funções que alteram a lista ficam no módulo (os métodos continuam só `size`/`add`/`remove`/`has`); `remove_value` de valor ausente lança `ValueError`; `sorted`/`reversed` devolvem cópias. | Decisão do autor da linguagem; ordenar sem closures nem genéricos, com tipagem estática. |
| 2026-10-03 | G7 | `Time`: instante é `int` em segundos desde 1970 UTC (sem tipo novo); partes, `make`, `format` e `parse` no horário local, com variantes `_utc`; padrão legível `dd/MM/yyyy HH:mm:ss`; `clock()` separado, em milissegundos, para medir duração. | Decisão do autor da linguagem; estático, comparável e ordenável sem mudar a linguagem. |
| 2026-10-03 | H1 | Interpolação só em `printf("...")` (pula linha, como `print`) e `format("...")` (devolve o texto), com texto literal; `{expr}` aceita qualquer expressão; formatos `{x:.2f}` e `{x:8}` (sem alinhamento explícito nem zeros à esquerda). | Decisão do autor da linguagem; strings comuns não mudam (nenhum programa quebra) e tudo é conferido na compilação. |
| 2026-10-03 | H2 | `s[i]` e fatias `s[a:b:p]` só em string, com índices negativos e passo; fatias estritas (`IndexError` fora dos limites, sem ajuste); listas ficam como estão. | Decisão do autor da linguagem. |
| 2026-10-04 | J2 | `Files.read_bytes(caminho) -> list<int>` e `Files.write_bytes(caminho, list<int>)`, bytes de 0 a 255 (fora disso, `ValueError`). | Decisão do autor da linguagem; formatos binários sem passar por texto UTF-8. |
| 2026-10-03 | I4 | `menor_int % -1` dá `0` (revê A5); `-0.0` continua `-0` no `print`; decimais extremos continuam em notação científica. | Decisão do autor da linguagem; `x % -1` cabe sempre em `int`. |
| 2026-10-03 | H3 | `exit([código])`: padrão 0, de 0 a 255; roda os `finally` pendentes e nenhum `except` o captura; encerra o caminho na análise de fluxo. | Decisão do autor da linguagem; 0–255 dá o mesmo resultado em todo sistema. |
| 2026-11-24 | Q10 | Pacotes: repositórios git (URL + tag) e dependências locais; `czm.mod` próprio (`projeto`, `cinza`, `requer`); `czm.lock` com commit e hash; seleção mínima (major diferente é erro); cache do usuário; o `git` instalado, só por `cinza add/remove/get` (compilar nunca usa a rede); `import pacote.caminho`; dentro do pacote, o caminho a partir da raiz dele; o `CINZA_PATH` sai. | Decisão do autor: sem servidor para manter, builds reprodutíveis e uma fonte só para os módulos. |
| 2026-11-24 | Q1f | Módulos: `module a.b;` no início de todo arquivo importável (conferido contra o caminho); a raiz é sempre a pasta do `czm.mod` (sem ele, um arquivo só; `cinza init` o cria); os `const` são calculados na compilação, sem ordem de inicialização. | Decisão do autor: o nome do módulo e a raiz do projeto precisam estar escritos, e a ordem dos `const` não pode depender de quem lê seguir os imports. |
| 2026-11-23 | Q1a | As oito questões das classes genéricas: usado = chamado, exigido por interface ou chamado por um usado; assinaturas já, corpos se usados; `new` de genéricos e de `T`, erros nunca genéricos; sem `self` nos valores iniciais com campo obrigatório; o construtor atribui no nível de cima antes de usar; campo `T` de struct segue obrigatório; uma cópia de cada interface genérica; campo obrigatório do próprio tipo é erro. | Decisão do autor, com o opcional já na linguagem (o "pode faltar" é `T?`). |
| 2026-11-23 | Q1b | `Json.Value.field(k)` lança `KeyError` sem a chave (o `get` dá `Value?`); a versão vai para 0.6.0. | Decisão do autor: a cadeia de acesso a um JSON de formato conhecido não pode pedir um `else` em cada nível; o opcional muda a linguagem o bastante para uma versão nova. |
| 2026-11-23 | Q1b | `yield` dá o valor do bloco de um else; `return` continua sendo sempre sair da função. | Decisão do autor: a mesma palavra não pode fazer duas coisas, e sair da função de dentro do bloco é o uso mais comum. |
| 2026-11-23 | Q1b | **Reaberto: o opcional `T?` com `null`** — `else` (valor, ou bloco com `yield`/saída), `if`/`while` com declaração, sem estreitamento; `T?` sem valor nasce `null`; a biblioteca devolve `T?` no que pode não existir e perde os `get_or`/`has`/`is_*`/`*_or`; o que quem chama controla continua lançando; `Regex.groups` com `string?`. | Decisão do autor: a ausência total gerava gambiarras (o "zero ou um" em `list<No>`, os campos `T` sem valor nos genéricos); a segurança vem do tipo, que obriga a resolver a ausência. |
| 2026-11-23 | Q1b | Sem `null`, como dizer "nenhum": as buscas que não acham lançam `ValueError` (sem `-1`; `contains` antes; `Strings.positions` para percorrer); um `new` nos valores iniciais que volta ao próprio tipo é erro de compilação; os idiomas na spec (3.10); ficam o `IOError` do `input()`, o `""` do grupo do Regex e a 3.6 (sem atribuição definida); nada de tipo soma. | Decisão do autor: o `-1` é um null com outro nome; a ausência se diz com perguntas, listas, erros e objetos, todos estáticos. |
| 2026-11-23 | Q1c | O que fica fora: nunca `null`/opcional, herança, closures e funções como valor (nem sem captura), globais mutáveis, sobrecarga (de funções e de operadores), conversões implícitas, tipo dinâmico, reflexão, ponteiros e memória manual, métodos genéricos e restrições nos genéricos, infinito e NaN, import relativo; fora só da 1.0: concorrência e macros. Na spec (`spec/README.md`). | Decisão do autor: o que é filosofia não é lacuna; a concorrência e as macros esperam o uso real (o contador de referências de hoje não é seguro entre threads). |
| 2026-11-22 | Q | Prioridade: segurança e maturidade antes da divulgação. A ordem: Q6 (o resto do LSP), Q1c, Q1b (levantamento da ausência de valor, sem `T?`), Q1a, Q1f, o gerenciador de pacotes (Q10, agora antes da 1.0) e o desempenho (Q11). | Decisão do autor: a linguagem precisa ser séria e funcionar de verdade antes de ser mostrada; o gerenciador de pacotes mexe na estrutura dos projetos e não pode mudar depois da 1.0. |
| 2026-11-20 | Q6 | O formatador (`cinza fmt`) entra na 1.0, com um estilo só, sem opções; o servidor de linguagem é um modo do próprio `cinza` (`cinza --lsp`, em C++, com o front-end de hoje); o depurador e o gerenciador de pacotes ficam fora da 1.0. | Decisões do autor: um estilo só evita discussão de estilo em cada projeto; os erros do editor são os da compilação, sem um segundo analisador; o depurador e os pacotes vêm depois, com a revisão dos módulos (Q1f). |
| 2026-11-20 | Q5 | Versão 0.5.0 (a numeração da spec). O `cinzac` com o LLVM estático (só o necessário) e o `lld` embutido, sem MSYS2 nem `g++`; o runtime continua em C++, com a biblioteca C++ embutida; no Linux, a libc++ (não a libstdc++) e a musl, com programas estáticos; a extensão só como `.vsix` nos releases. | Decisões do autor: quem usa o `cinzac` não instala nada; um runtime em C seria outro código além do da CVM; o pacote fica só com licenças permissivas, igual nos dois sistemas; o Marketplace fica para quando a linguagem for pública. |
| 2026-11-19 | Q4 | A 1.0 sai para Windows e Linux (x86-64); o macOS fica para depois da 1.0. O Linux é testado pelo CI (GitHub Actions), lido pelo `gh`. | Decisão do autor: sem um Mac para testar, prometer suporte seria arriscado; o código já fica portátil (POSIX e o triple do sistema). |
| 2026-11-19 | Q3 | A biblioteca da 1.0 ganha `Dicts`, `Env`, `Process`, `Regex` e `Json`. `Json` é uma classe da biblioteca (`Json.Value` com `Json.Kind`; o null do JSON é um `Kind`, não um null da Cinza). `Process` roda programas sem shell (programa e lista de argumentos). `Regex` usa um motor próprio no estilo do RE2 (tempo linear, em caracteres, sem `\1` nem lookaround). | Decisões do autor: JSON e processos são o que programas reais mais pedem; sem shell não há injeção de comando e o resultado é igual em todo sistema; um motor com retrocesso (o `std::regex`) trabalha em bytes e derruba o programa com textos longos, contra o Q2. |
| 2026-11-19 | Q | O congelamento da linguagem (Q9) só depois de as classes genéricas estarem implementadas de forma segura e consistente; o sistema de módulos é revisto antes da 1.0 (Q1f); o `cinzac` deixa de depender do MSYS2 e do `g++`, como outras linguagens fazem (o ligador e o runtime distribuídos com ele, Q5). | Decisões do autor ao ler o roteiro: a 1.0 é a promessa de estabilidade, e as classes genéricas e os módulos ainda podem mudar a linguagem; quem usa o `cinzac` não deve precisar instalar um ambiente de compilação. |
| 2026-11-19 | P10 | As duas implementações ficam: `cinza` (CVM) roda na hora, `cinzac` (LLVM) gera o executável; toda mudança da linguagem entra nas duas, e a suíte roda nas duas. Campos de objeto e struct com um caminho rápido em linha para os sem referência, no layout da CVM; sem campos tipados nem listas especializadas por enquanto. | Decisão do autor, com as medições do P10: a CVM começa a rodar na hora (o `cinzac` leva ~0,5 s, quase tudo na ligação pelo `g++`); o caminho rápido dá o grosso do ganho (getters 16× a CVM) sem mexer no runtime inteiro. |
| 2026-11-18 | P | Backend nativo: API em C do LLVM 20 (`llvm-c`); CIR própria entre a AST e o LLVM, com a CVM compilando da AST; contagem de referências não atômica embutida no objeto + coleta de ciclos; erros por retorno explícito com estado de execução encapsulado; CVM mantida até o `cinzac` passar a suíte; `cinza` e `cinzac` separados. Erro lançado no `finally` substitui o fluxo pendente (spec 5.6, como a CVM já fazia). | Decisão do autor depois de duas revisões externas: o LLVM é só backend; posse, erros e o runtime são contratos da Cinza, comuns aos dois backends. |
| 2026-11-15 | N5 | Structs genéricos (`struct Par[A, B]`), usados sempre com os tipos escritos (`Par[i32, string]`, sem inferência). Em todo struct, campo sem valor inicial é obrigatório na criação (`list`/`dict` nascem vazios); criação também pelo nome, `new Ponto(y: 4)`, omitindo os campos com valor inicial. Um campo do tipo `T` não tem valor inicial. Classes continuam exigindo valor inicial. | Decisão do autor: resolve o campo `T` sem valor possível, mantendo "nenhum struct com campo sem valor"; sem repetir os padrões na criação. |
| 2026-11-15 | O | Imports a partir da raiz do projeto: a pasta do `czm.mod` mais próximo (marcador, vazio por enquanto) ou, sem ele, a do arquivo principal; depois `CINZA_PATH`. Sai a busca na pasta de quem importa (sem import relativo); o mesmo caminho em dois lugares é erro. | Decisão do autor: projetos em pastas importam entre pastas irmãs, cada módulo tem um nome só (estável para o LLVM) e o projeto não depende de configuração da máquina. |
| 2026-11-15 | N | Funções genéricas (revê "A Cinza não terá genéricos", D): monomorfização; corpo conferido só no uso, com o tipo concreto, sem restrições declaradas (modelo do Zig); genérico não usado não é conferido; tipos deduzidos dos argumentos, explícitos opcionais e obrigatórios quando não dedutíveis; colchetes como sintaxe exclusiva da função genérica (`fn f[T]`, `f[i32](x)`), com `list<T>`/`cast<T>` mantidos; erros com rastro dos usos e limite de 64 cópias encadeadas. `struct`/`class` genéricas ficam para depois. | Decisão do autor: genéricos simples e leves, sem custo na execução, "deixando à mercê do usuário criar um bom programa". |
| 2026-11-14 | M | Infinito e NaN não existem na Cinza: divisão por zero de ponto flutuante é `ZeroDivisionError`, estouro é `OverflowError`, fora do domínio é `ValueError` (revê o "IEEE em tudo" da seção K). `TypeError` fica para os genéricos. | Decisão do autor: inf e NaN vão contra a filosofia da linguagem. |
| 2026-11-14 | L | Só a CVM (o interpretador de referência sai; o próximo backend é LLVM) e nenhuma compatibilidade com versões antigas da linguagem: o que muda, muda de vez. | Decisão do autor: a linguagem está em desenvolvimento e não tem programas de terceiros. |
| 2026-11-14 | K | Quatro tipos numéricos em duas famílias (`i32`/`i64`, `f32`/`f64`) no lugar de `int`/`decimal`, sem apelidos; promoção só dentro da família; entre famílias, `cast<T>(valor)`; literal com ponto exige sufixo (`f`/`ff`); `%` só entre inteiros; ponto flutuante IEEE em tudo (revê A5, G2 e a revisão I); tamanhos e posições `i32`; `op<...>` removido (revê D). | Especificação do autor: "simplicidade na utilização, rigor na implementação e previsibilidade no comportamento". |
