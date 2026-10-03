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
