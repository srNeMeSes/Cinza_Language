# CVM — desenho do conjunto de instruções

Documento técnico da implementação (não é parte da especificação da linguagem: a `spec/` diz
*o que* a Cinza faz; aqui está *como* a CVM faz). Decisões e regras: seção F do REVISAO_V2.md.

## 1. Visão geral

```
fonte → Lexer → Parser → Semântico ─┬→ Executor (interpretador de referência, --interp)
                                    └→ Compilador CVM → bytecode → VM (padrão)
```

O compilador CVM recebe a AST **já analisada** — tipos resolvidos, slots do B2, alvos de chamada,
tabelas de interface, conversões inseridas — e gera uma função compilada (**protótipo**) para
cada função, método, construtor e inicializador. A VM executa os protótipos sobre uma área única
de registradores.

## 2. Protótipo (função compilada)

| Campo           | Conteúdo |
|-----------------|----------|
| `nome`          | nome no stack trace (`f`, `Classe.metodo`, `new Classe`) — o `trace_name` atual |
| `num_params`    | parâmetros (num método e construtor, mais 1: o `self` em `r0`) |
| `num_regs`      | registradores usados: parâmetros + locais (slots do B2) + temporários |
| `code`          | as instruções |
| `linhas`        | para cada instrução, arquivo, linha **e coluna** de origem (diagnósticos, `e.column` e stack trace precisam sair idênticos aos do interpretador; se o tamanho pesar, comprimir em faixas) |
| `constantes`    | valores usados pelas instruções: inteiros grandes, decimais, textos, valores de enum, tipos |
| `tratadores`    | tabela de exceções (seção 6) |

Tabelas globais da VM, referenciadas por índice nas instruções: protótipos, classes (com as
tabelas de interface já montadas pelo semântico), structs, funções nativas e os slots dos `const`
globais (o `GlobalLayout` do B2).

## 3. Registradores e chamadas

- Uma área contínua de `Value`. Cada chamada ocupa uma **janela**: `r0` é o primeiro registrador
  da janela. Nada é alocado por chamada.
- Ordem na janela: `self` (só em métodos, construtores e inicializadores), parâmetros, variáveis
  locais (um registrador por slot do B2) e, acima delas, temporários alocados pelo compilador em
  pilha.
- **Convenção de chamada** (como no Lua): o chamador põe os argumentos em registradores
  consecutivos `rA, rA+1, ...`; a janela do chamado **começa em `rA`**, então os argumentos já são
  os parâmetros dele, sem cópia. O resultado volta em `rA`. O compilador garante que `rA` é o topo
  dos temporários vivos (o que está acima pode ser sobrescrito pelo chamado).
- Cada chamada soma um nível; acima de 2000, `StackOverflowError` (spec 5.8).
- **Sem recursão em C++.** Um `CALL` da Cinza empilha um frame (protótipo, `pc`, base da janela,
  registrador de retorno) num vetor da VM e continua o **mesmo** laço de despacho; `RET` desempilha.
  O laço nunca chama a si mesmo. O inicializador de campos e o construtor são protótipos chamados
  assim. Só as funções nativas são chamadas do C++, e elas não chamam código Cinza.
- **Crescimento da área.** A área é um `std::vector<Value>` que pode crescer. Na entrada de cada
  função, a VM garante que `base + num_regs` cabe, crescendo ali se preciso. Como crescer invalida
  ponteiros, nenhum `Value*`/`Value&` para a área sobrevive a uma instrução que pode chamar código ou
  alocar: depois de `CALL*`, `INITOBJ` e `NEWOBJ` (onde a coleta pode rodar), o ponteiro da base é
  recalculado.
- **Registradores mortos.** Os registradores seguram objetos e contam como raízes para a coleta. No
  `RET` e no desempilhamento por erro, a janela do chamado é **limpa** (os `Value` voltam a vazio),
  para a VM não manter vivo um objeto que o interpretador já teria liberado.
- `Value` continua o mesmo do interpretador: copiar um registrador que guarda struct clona o
  struct (cópia rasa), list/dict/objeto são compartilhados — a semântica da spec 5.1 vem de graça.

## 4. Formato das instruções

Tamanho fixo de 8 bytes: `op` e três operandos de 16 bits.

```
struct Instr { uint16_t op; uint16_t a; uint16_t b; uint16_t c; };
```

Operandos de 16 bits permitem até 65.535 registradores, constantes e entradas de tabela por
função. Saltos e inteiros imediatos usam `b` e `c` juntos como um inteiro de 32 bits com sinal
(`bc` abaixo). Notação: `R[x]` registrador, `K[x]` constante, `G[x]` slot global.

Uma instrução usa **ou** `b` e `c` separados **ou** `bc`, nunca os dois.

**Verificador de bytecode** (só no build de debug): depois de compilar, confere em cada protótipo
que todo registrador está abaixo de `num_regs`, todo salto cai dentro do código, todo índice de
constante, protótipo, classe e nativa existe, e todo intervalo da tabela de tratadores é válido.
Erro de formato aparece na hora, e não como comportamento estranho em runtime.

## 5. Instruções

### 5.1 Carga e movimento

| Instrução               | Efeito |
|-------------------------|--------|
| `MOVE a b`              | `R[a] = R[b]` |
| `LOADK a b`             | `R[a] = K[b]` |
| `LOADINT a bc`          | `R[a] = bc` (inteiro de 32 bits) |
| `LOADBOOL a b`          | `R[a] = (b != 0)` |
| `GETGLOBAL a b`         | `R[a] = G[b]` |
| `SETGLOBAL a b`         | `G[b] = R[a]` (só na avaliação dos `const`) |

### 5.2 Aritmética e comparação (específicas por tipo)

O semântico já inseriu as conversões (`int` → `decimal`); cada operação sabe o tipo dos
operandos. As versões `_I` conferem overflow e divisão por zero, as `_D`, divisão por zero e
resultado infinito (spec 5.3).

| Instrução                                   | Efeito |
|---------------------------------------------|--------|
| `ADD_I a b c`, `SUB_I`, `MUL_I`, `DIV_I`, `MOD_I` | `R[a] = R[b] op R[c]` em `int` |
| `ADD_D a b c`, `SUB_D`, `MUL_D`, `DIV_D`, `MOD_D` | idem em `decimal` |
| `NEG_I a b`, `NEG_D a b`                    | `R[a] = -R[b]` |
| `I2D a b`                                   | `R[a] = decimal(R[b])` |
| `CONCAT a b c`                              | `R[a] = texto(R[b]) + texto(R[c])` (spec 5.4) |
| `LT_I a b c`, `LE_I`, `LT_D`, `LE_D`, `LT_S`, `LE_S` | `R[a] = R[b] < R[c]` (ou `<=`); `>` e `>=` trocam os **registradores** na instrução final (`a > b` vira `LT r, rb, ra`) — os operandos continuam sendo avaliados na ordem do fonte |
| `EQ a b c`, `NE a b c`                      | igualdade da spec 5.3 (valor, estrutural ou identidade) |
| `NOT a b`                                   | `R[a] = !R[b]` |
| `ADD a b c`, `SUB`, `MUL`, `DIV`, `MOD`, `NEG`, `LT`, `LE` | **genéricas**: decidem pelo tipo real dos valores. Só para operandos `op<...>` de tipo travado desconhecido (spec 3.7), onde o tipo varia em runtime mas o semântico garantiu que toda combinação é válida |

`&&` e `||` não são instruções: viram saltos (curto-circuito).

### 5.3 Saltos

| Instrução               | Efeito |
|-------------------------|--------|
| `JMP bc`                | `pc += bc` |
| `JMPIF a bc`            | se `R[a]`, `pc += bc` |
| `JMPIFNOT a bc`         | se não `R[a]`, `pc += bc` |

### 5.4 Laço `for`

| Instrução               | Efeito |
|-------------------------|--------|
| `FORPREP a b c`         | `R[a] =` cópia dos elementos de `R[b]` (lista: os elementos; dict: os pares em ordem de chave; string: os caracteres), `R[a+1] = 0`, `R[a+2] = c` (o slot do iterador) |
| `FORNEXT a bc`          | se `R[a+1] < tamanho`: `R[slot] = R[a][R[a+1]]` (com `slot = R[a+2]`), `R[a+1] += 1`; senão `pc += bc` (sai do laço) |
| `FORNEXT_D a bc`        | idem, convertendo o elemento para `decimal` (`for (decimal x in list<int>)`) |

O laço usa três registradores consecutivos, como o `FORLOOP` do Lua: `R[a]` a cópia, `R[a+1]` o
índice e `R[a+2]` o número do slot do iterador. (O slot é decidido pelo semântico, B2, então não
pode ser forçado a ser `a+2`; guardar o número dele mantém o `FORNEXT` numa instrução só.)
A cópia no `FORPREP` é a da spec 5.5 (alterar a coleção no corpo não muda as voltas).

### 5.5 Funções e objetos

| Instrução                  | Efeito |
|----------------------------|--------|
| `CALL a b c`               | chama o protótipo `b` com `c` argumentos em `R[a]...`; resultado em `R[a]` |
| `CALLMETHOD a b c`         | idem para método: `R[a]` é o `self`, argumentos em `R[a+1]...` |
| `CALLIFACE a b c`          | chamada pela interface `b`, método `c`: escolhe o protótipo pela classe de `R[a]` (tabela de interface); argumentos em `R[a+1]...`, na quantidade da assinatura do método na interface |
| `CALLNATIVE a b c`         | nativa `b` com `c` argumentos em `R[a]...`; resultado em `R[a]` |
| `TYPEOF a b c`             | `R[a] =` tipo real de `R[b]`, sendo `K[c]` o tipo estático (spec 3.9) |
| `RET a` / `RETVOID`        | retorna `R[a]` / retorna sem valor |
| `NEWOBJ a b`               | `R[a] =` novo objeto da classe `b` (campos ainda vazios) |
| `INITOBJ a b`              | roda o inicializador de campos da classe `b` com `self = R[a]` |
| `NEWSTRUCT a b c`          | `R[a] =` struct `b` com os campos `R[a+1]...R[a+c]` |
| `GETFIELD a b c`, `SETFIELD a b c` | campo `c` do objeto ou struct: `R[a] = R[b].c` / `R[a].c = R[b]` |
| `GETFIRST a b`, `GETSECOND a b` | campos de um `pair` |
| `ERRFIELD a b c`           | `kind`, `message`, `line` ou `column` de um erro |

**`new Classe(args)`** vira, nesta ordem (a do interpretador): argumentos em `R[a+1]...`,
`NEWOBJ a`, `INITOBJ a`, `CALLMETHOD a <construtor> n`. Os campos de um struct com valores
padrão (`new S()`) são calculados no próprio chamador, antes do `NEWSTRUCT`.

### 5.6 Coleções

| Instrução                                     | Efeito |
|-----------------------------------------------|--------|
| `NEWLIST a b c`                               | `R[a] =` lista com `R[b]...R[b+c-1]` |
| `NEWDICT a b c`                               | `R[a] =` dict com `c` pares (chave, valor) a partir de `R[b]` |
| `NEWPAIR a b c`                               | `R[a] = {R[b], R[c]}` |
| `GETINDEX_L a b c`, `SETINDEX_L a b c`        | índice de lista (`IndexError`) |
| `GETINDEX_D a b c`, `SETINDEX_D a b c`        | chave de dict (`KeyError`; o `SET` só atualiza) |
| `LIST_ADD`, `LIST_REMOVE`, `LIST_SIZE`, `LIST_HAS` | métodos de `list` (spec 5.7) |
| `DICT_ADD`, `DICT_REMOVE`, `DICT_SIZE`, `DICT_HAS`, `DICT_KEYS`, `DICT_VALUES` | métodos de `dict` |
| `STR_SIZE a b`                                | `R[a] =` caracteres de `R[b]` |

### 5.7 `op<...>` e exceções

| Instrução               | Efeito |
|-------------------------|--------|
| `CAST a b c`            | conversão inserida pelo semântico de/para `op` (`K[c]` = tipo de destino; só `int` → `decimal` acontece de fato) |
| `KEEPLOCK a b`          | atribuição a um `op` de tipo travado desconhecido: se `R[a]` guarda `decimal` e `R[b]` é `int`, converte; depois `R[a] = R[b]` |
| `NEWERROR a b c`        | `R[a] =` erro do tipo `K[b]` com a mensagem `R[c]` |
| `THROW a`               | lança o erro `R[a]` (relançar preserva posição e trace) |

## 6. Exceções e `finally`

### Tabela de tratadores

Cada protótipo tem uma tabela de entradas `{início, fim, destino, tipo, registrador}` — "um erro
lançado numa instrução de *início* a *fim* (exclusive) cujo tipo é *tipo* (ou qualquer, para
`Error`) vai para *destino*, com o erro guardado em *registrador*".

Quando um erro acontece, a VM percorre a tabela **em ordem** e usa a primeira entrada que cobre a
instrução corrente. As entradas ficam ordenadas com o `try` **mais interno primeiro**, então a
busca linear acha o tratador mais aninhado. Sem entrada na função atual, a janela é limpa (seção
3), o frame é desempilhado e a busca continua em quem chamou, a partir da instrução da chamada. Sem
tratador em lugar nenhum, o erro termina o programa com o diagnóstico e o stack trace da spec 7.3.
O `try` não custa nada quando não há erro.

### `finally` compilado uma vez, com ação pendente

(Decisão de 2026-10-02: substitui a cópia do `finally` em cada saída com intervalos partidos —
mais simples e sem os erros clássicos da cópia.)

O `finally` é compilado **uma única vez**. Um `try` com `finally` reserva dois registradores: a
**ação pendente** (`int`) e o **valor pendente**. Toda saída do bloco protegido grava o que fazer
depois e salta para o `finally`:

| Saída                                   | Ação pendente | Valor pendente |
|-----------------------------------------|---------------|----------------|
| fim normal do `try` ou de um `except`   | 0 (seguir)    | —              |
| `return v` dentro do `try`/`except`     | 1             | `v`            |
| `break` que sai do `try`                | 2             | —              |
| `continue` que sai do `try`             | 3             | —              |
| erro não tratado (tratador pega-tudo)   | 4             | o erro         |

No fim do `finally`, um despacho compilado testa a ação e a executa **no contexto de fora**: um
`return` pendente vira um `return` ali (que, se houver outro `try` com `finally` por fora, passa
pelo `finally` dele também), o mesmo para `break` e `continue`; a ação 4 relança o erro, que
preserva tipo, mensagem, posição e stack trace. A ação 0 segue para depois do `try`.

Os intervalos da tabela cobrem só o bloco `try` (para cada `except` e para o pega-tudo) e os
blocos `except` (só para o pega-tudo). O `finally` e o despacho ficam **fora** de todos os
intervalos do próprio `try`: um erro lançado pelo `finally` sobe para fora, como no interpretador,
e nunca roda o `finally` duas vezes.

`return`, `break` e `continue` **dentro** do `finally` são proibidos pela linguagem (spec 5.6),
então um `finally` nunca descarta um retorno ou um erro pendente.

### Exemplo

```cinza
fn f(int n) -> int {
  try {
    return 10 / n;
  } except (ZeroDivisionError e) {
    return -1;
  } finally {
    print("fim");
  }
}
```

```
  try:      calcula 10 / n em v ......... ação = 1, pendente = v, salta para FINALLY
  except:   pendente = -1, ação = 1, salta para FINALLY
  pega-tudo: pendente = erro, ação = 4
  FINALLY:  print("fim")
            se ação == 1: RET pendente
            se ação == 4: THROW pendente
tratadores (mais interno primeiro):
  [try]     ZeroDivisionError → except
  [try]     qualquer          → pega-tudo
  [except]  qualquer          → pega-tudo
```

## 7. Atribuição a lugares

`x = v` vira um `MOVE` (ou o cálculo direto em `R[x]`). Para campo e índice, o compilador segue a
ordem da spec 5.2: avalia a base e os índices, depois o valor, e então grava.

Struct é um valor. Para gravar num campo de um struct que está dentro de outro lugar
(`l[0].x = v`, `q.p.x = v`), o compilador segue esta ordem — a mesma do interpretador:

1. avalia a base e os índices, da esquerda para a direita, guardando cada um num registrador;
2. avalia o valor;
3. **só então** lê o struct do lugar (`l[idx]`), grava o campo e **devolve** o struct ao lugar.

Ler o struct antes do passo 2 seria um erro: se o valor alterar outro campo do mesmo struct
(`l[0].x = f()` com `f` mudando `l[0].y`), a devolução apagaria essa alteração. Na ordem acima,
nenhum código do programa roda entre ler e devolver, e se o valor tiver removido o elemento, a
leitura do passo 3 lança `IndexError` exatamente onde o interpretador lança na gravação.

Em `a op= b`: passos 1 e 2 iguais (com `b`), depois lê o valor **atual** do lugar, calcula, grava
e devolve. O valor atual é lido depois de `b` (spec 5.2): em `l[0].x += g()`, uma alteração que
`g()` faça em `l[0].x` entra na conta.

Em níveis que são objetos ou coleções (referências), a gravação vai direto, sem devolução.

Testes: `tests/cvm_struct_lugar_valor.cinza`, `cvm_struct_lugar_composto.cinza` e
`cvm_struct_lugar_removido.cinza` (já verdes no interpretador; a CVM precisa reproduzi-los).

## 8. Módulos e início do programa

Cada módulo ganha um protótipo de inicialização com os `const` globais (`SETGLOBAL`). A VM roda
os de todos os módulos na ordem topológica (spec 4.2) e depois chama a `main`, com `args` em `r0`
se ela tiver o parâmetro. As constantes dos módulos nativos (`Math.pi`) já começam nos seus slots.

## 9. Coleta de ciclos

Os registradores guardam `Value`, então os objetos que eles seguram contam como referências
externas para o coletor (gc.h). A coleta continua sendo disparada na criação de objetos
(`NEWOBJ`), que é um ponto seguro: a VM não guarda ponteiros crus para dentro dos registradores
ou dos contêineres entre instruções.

## 10. Desmontador (`--bytecode`)

Uma linha por instrução: posição, nome, operandos e, quando ajuda, um comentário.

```cinza
fn fib(int n) -> int {
  if (n < 2) { return n; }
  return fib(n - 1) + fib(n - 2);
}
```

```
fn fib  (1 parâmetro, 3 registradores)
  0000  LOADINT    r1, 2
  0001  LT_I       r1, r0, r1        ; n < 2
  0002  JMPIFNOT   r1, -> 0004
  0003  RET        r0
  0004  LOADINT    r1, 1
  0005  SUB_I      r1, r0, r1        ; n - 1
  0006  CALL       r1, fib, 1        ; r1 = fib(n - 1)
  0007  LOADINT    r2, 2
  0008  SUB_I      r2, r0, r2        ; n - 2
  0009  CALL       r2, fib, 1        ; r2 = fib(n - 2)
  0010  ADD_I      r1, r1, r2
  0011  RET        r1
```

São 12 instruções; o interpretador percorre, para a mesma função, cerca de 20 nós da AST por
chamada, cada um com despacho, cópias de `Value` e montagem de frame.

## 11. Otimizações previstas (depois de tudo funcionar)

Só entram com o benchmark mostrando ganho e a suíte verde nos dois modos:

- instruções com constante imediata (`ADDK_I r1, r0, 1`) e comparação-e-salto fundidos
  (`JLT_I r0, r1, -> 0004`), que reduzem o número de instruções dos laços;
- `for (int i in range(a, b))` sem criar a lista, quando `range` é a embutida (o resultado é o
  mesmo: ela não tem efeito colateral);
- registradores com `int`/`decimal` sem embrulho no `Value`, se a medição mostrar que o `Value`
  pesa;
- `for` sobre `string` sem copiar os caracteres (strings são imutáveis, a cópia da spec 5.5 não é
  observável);
- despacho com *computed goto* (`&&rotulo`, extensão do GCC e do Clang) no lugar do `switch`, atrás
  de um `#ifdef` — costuma ser o primeiro ganho mensurável neste tipo de VM.

### Resultado (bench/medir.ps1, melhor de 5, ms na CVM)

| Otimização                                         | objetos  | ciclos    | laco      | fib     |
|----------------------------------------------------|----------|-----------|-----------|---------|
| ponto de partida                                   | 117      | 657       | 107       | 54      |
| acessores do `Value` por referência (não estava na lista: a cópia do `shared_ptr` mexia no contador atômico a cada leitura de campo) | 100 | — | — | — |
| `for` sobre `range` embutido sem lista (`RANGEPREP`/`FORRANGE`) | 77 | 473 | — | — |
| constante imediata (`ADDK_I`…`GEK_I`, 16 bits com sinal) | — | — | ~104 | ~52 |
| *computed goto* (**descartado**, ver abaixo)       | =        | =         | =         | =       |

O *computed goto* foi implementado, testado e medido lado a lado com o `switch`: nenhum ganho
mensurável (os processadores atuais já preveem bem o salto indireto do `switch`), e ele traz uma
armadilha — o salto computado não destrói os locais do caso (`std::string`, `Value`), o que exige
fechar o bloco antes de cada salto. Sem ganho, não compensa; fica registrado para não ser refeito.

### Teste diferencial

Além da suíte, um modo do `run_tests.py` roda cada programa dos testes e da spec nos dois modos
(`--interp` e CVM) e compara saída, erros e código de saída **byte a byte**. Qualquer diferença é
defeito da CVM.
