# CVM — desenho do conjunto de instruções

Documento técnico da implementação (não é parte da especificação da linguagem: a `spec/` diz
*o que* a Cinza faz; aqui está *como* a CVM faz). Decisões e regras: seção F do REVISAO_V2.md.

## 1. Visão geral

```
fonte → Lexer → Parser → Semântico → Compilador CVM → bytecode → VM
```

A CVM é o único runtime da Cinza (o interpretador tree-walk de referência saiu em 2026-11-14).
O próximo passo é um backend LLVM (seção 12), a partir da mesma AST analisada; o desenho dele
está em `llvm/DESENHO.md` (a CVM continua compilando da AST, e o runtime passa a ser comum aos
dois).

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
| `linhas`        | para cada instrução, arquivo, linha **e coluna** de origem (diagnósticos, `e.column` e stack trace da spec 7; se o tamanho pesar, comprimir em faixas) |
| `constantes`    | valores usados pelas instruções: inteiros grandes, ponto flutuante, textos, valores de enum, tipos |
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
  alocar: depois de `CALL*` e `NEWOBJ` (onde a coleta pode rodar), o ponteiro da base é
  recalculado.
- **Registradores mortos.** Os registradores seguram objetos e contam como raízes para a coleta. No
  `RET` e no desempilhamento por erro, a janela do chamado é **limpa** (os `Value` voltam a vazio),
  para a VM não manter vivo um objeto que a contagem de referências já teria liberado.
- `Value` (value.h): copiar um registrador que guarda struct clona o
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

A lista abaixo é a implementada (`cvm/bytecode.h`, que é a fonte da verdade). `R[x]` é um
registrador da janela, `K[x]` uma constante do protótipo, `G[x]` um slot global; `bc` são `b` e
`c` juntos, inteiro de 32 bits com sinal.

### 5.1 Carga e movimento

| Instrução               | Efeito |
|-------------------------|--------|
| `MOVE a b`              | `R[a] = R[b]` |
| `LOADK a bc`            | `R[a] = K[bc]` (índice de 32 bits: uma função pode ter mais de 65535 constantes) |
| `LOADINT a bc`          | `R[a] = bc` como `i64` (literal `i64` que cabe em 32 bits; também os códigos internos do `finally`) |
| `LOADI32 a bc`          | `R[a] = bc` como `i32` |
| `LOADBOOL a b`          | `R[a] = (b != 0)` |
| `LOADVOID a`            | `R[a] =` valor vazio (parte omitida de uma fatia) |
| `GETGLOBAL a b`         | `R[a] = G[b]` |
| `SETGLOBAL a b`         | `G[b] = R[a]` (só na avaliação dos `const`) |

### 5.2 Aritmética, texto e comparação (específicas por tipo)

Seção K do REVISAO_V2.md: quatro tipos numéricos em duas famílias. O semântico já promoveu os dois
lados de cada operação ao mesmo tipo (`i32 + i64` chega como `i64 + i64`, com um `CONV` no lado
`i32`), então cada instrução tem um tipo só. As inteiras conferem overflow na largura (o `i32`
conta em 64 bits e confere a faixa de 32) e divisão por zero; as de ponto flutuante conferem
divisão por zero e resultado não finito (infinito e NaN não existem na Cinza, seção M do
REVISAO_V2.md; `f32` conta em `float`). Os casos de borda passam
pela regra comum (`applyBinaryOp`, `operacoes.cpp`), que lança o erro com a mensagem da spec 5.3.

| Instrução                                   | Efeito |
|---------------------------------------------|--------|
| `ADD_I64 a b c`, `SUB_I64`, `MUL_I64`, `DIV_I64`, `MOD_I64` | `R[a] = R[b] op R[c]` em `i64` |
| `ADD_I32 a b c`, `SUB_I32`, `MUL_I32`, `DIV_I32`, `MOD_I32` | idem em `i32` |
| `ADD_F64 a b c`, `SUB_F64`, `MUL_F64`, `DIV_F64` | idem em `f64` (não há resto de ponto flutuante) |
| `ADD_F32 a b c`, `SUB_F32`, `MUL_F32`, `DIV_F32` | idem em `f32` |
| `ADDK_I64 a b c`, `SUBK_I64`, `MULK_I64`, `DIVK_I64`, `MODK_I64` | `R[a] = R[b] op c`, com `c` inteiro de 16 bits com sinal embutido na instrução (seção 11) |
| `ADDK_I32 a b c` … `MODK_I32`               | idem em `i32` |
| `LTK_I a b c`, `LEK_I`, `GTK_I`, `GEK_I`    | `R[a] = R[b] cmp c`, `R[b]` inteiro de qualquer largura |
| `NEG_I64 a b`, `NEG_I32`, `NEG_F64`, `NEG_F32` | `R[a] = -R[b]` (inteiro: o menor valor não tem negativo, `OverflowError`) |
| `CONV a b c`                                | `R[a] = cast<T>(R[b])`, `T` pelo código `c` (0 `i32`, 1 `i64`, 2 `f32`, 3 `f64`, 4 `string`); regra em `convertValue` (`operacoes.cpp`): `cast<T>` escrito, promoções do semântico e o resultado das nativas genéricas |
| `CONCAT a b c`                              | `R[a] = texto(R[b]) + texto(R[c])` (spec 5.4) |
| `FMT a b c`                                 | `R[a] =` `R[b]` como texto com o formato de `printf`/`format` (`K[c]` = largura × 1000 + casas + 1) |
| `SLICE a b`                                 | `R[a] = R[b][R[b+1]:R[b+2]:R[b+3]]`, fatia de string (parte omitida = vazio) |
| `LT_I a b c`, `LE_I`, `LT_F`, `LE_F`, `LT_S`, `LE_S` | `R[a] = R[b] < R[c]` (ou `<=`) entre inteiros, ponto flutuante ou textos; `>` e `>=` trocam os **registradores** na instrução final (`a > b` vira `LT r, rb, ra`) — os operandos continuam sendo avaliados na ordem do fonte |
| `EQ a b c`, `NE a b c`                      | igualdade da spec 5.3 (valor, estrutural ou identidade) |
| `NOT a b`                                   | `R[a] = !R[b]` |

`&&` e `||` não são instruções: viram saltos (curto-circuito).

### 5.3 Saltos

| Instrução               | Efeito |
|-------------------------|--------|
| `JMP bc`                | `pc += bc` |
| `JMPIF a bc`            | se `R[a]`, `pc += bc` |
| `JMPIFNOT a bc`         | se não `R[a]`, `pc += bc` |
| `JLT_I a b`, `JLE_I a b` | compara-e-salta (seção 11): se `R[a] < R[b]` (ou `<=`), executa o `JMP` seguinte no mesmo despacho; senão o pula |
| `JLTK_I a b`, `JLEK_I`, `JGTK_I`, `JGEK_I` | idem, comparando `R[a]` com `b` (inteiro de 16 bits com sinal) |

### 5.4 Laço `for`

| Instrução               | Efeito |
|-------------------------|--------|
| `FORPREP a b c`         | `R[a] =` cópia dos elementos de `R[b]` (lista: os elementos; dict: os pares em ordem de chave) — numa string, a própria string, percorrida um caractere por vez (é imutável, a cópia não é observável) —, `R[a+1] = 0`, `R[a+2] = c` (o slot do iterador) |
| `FORNEXT a bc`          | se há próximo: `R[slot] =` o elemento (`slot = R[a+2]`) e avança `R[a+1]`; senão `pc += bc` (sai do laço) |
| `FORNEXT_W a bc`        | idem, alargando o elemento (`for (i64 x in list<i32>)`, `f32` → `f64`) |
| `RANGEPREP a b`         | `for` sobre `range(...)` embutido, sem criar a lista: `R[a]` atual, `R[a+1]` fim, `R[a+2]` passo (`ValueError` se 0), `R[a+3] = b` (slot) |
| `FORRANGE a bc`         | próximo inteiro em `R[slot]`, na largura do fim (`R[a+1]`, `i32` ou `i64`), ou `pc += bc`. Com o iterador mais largo que o `range` (`for (i64 i in range(0, 10))`), o compilador alarga início e fim com `CONV` antes |

O laço usa registradores consecutivos, como o `FORLOOP` do Lua. (O slot do iterador é decidido
pelo semântico, B2, então não pode ser forçado a ser `a+2`; guardar o número dele mantém o
`FORNEXT` numa instrução só.) A cópia no `FORPREP` é a da spec 5.5 (alterar a coleção no corpo
não muda as voltas); a regra fica em `forElements` (`operacoes.cpp`).

### 5.5 Funções e objetos

| Instrução                  | Efeito |
|----------------------------|--------|
| `CALL a b c`               | chama o protótipo `b` com `c` argumentos em `R[a]...`; resultado em `R[a]`. Num método, `R[a]` é o `self` e os argumentos vêm depois |
| `CALLIFACE a b c`          | chamada pela interface `b`, método `c`: escolhe o protótipo pela classe de `R[a]` (tabela de interface); argumentos em `R[a+1]...` |
| `CALLNATIVE a b c`         | nativa `b` com `c` argumentos em `R[a]...`; resultado em `R[a]` |
| `TYPEOF a b c`             | `R[a] =` tipo real de `R[b]`, sendo `K[c]` o tipo estático (spec 3.8) |
| `RET a` / `RETVOID`        | retorna `R[a]` / retorna sem valor |
| `NEWOBJ a b`               | `R[a] =` novo objeto da classe `b` (campos ainda vazios) |
| `NEWSTRUCT a b c`          | `R[a] =` struct `b` com os campos `R[a]...R[a+c-1]` |
| `GETFIELD a b c`, `SETFIELD a b c` | campo do objeto ou struct: `R[a] = R[b].campo[c]` / `R[a].campo[b] = R[c]` |
| `GETFIRST a b`, `GETSECOND a b` | campos de um `pair` |

**`new Classe(args)`** vira, nesta ordem (spec 5.2): argumentos em registradores,
`NEWOBJ`, `CALL` do protótipo oculto de inicialização dos campos (se a classe tiver valores
iniciais) e `CALL` do construtor com o objeto como `self`. Os campos de um struct com valores
padrão (`new S()`) são calculados no próprio chamador, antes do `NEWSTRUCT`.

### 5.6 Coleções

| Instrução                                     | Efeito |
|-----------------------------------------------|--------|
| `NEWLIST a b c`                               | `R[a] =` lista com `R[b]...R[b+c-1]` |
| `NEWDICT a b c`                               | `R[a] =` dict com `c` pares (chave, valor) a partir de `R[b]` |
| `APPEND a b c`, `DICTADD a b c`               | acrescentam `c` elementos (ou pares) a partir de `R[b]` à lista (ou dict) `R[a]`: um literal com mais de 256 elementos é montado em blocos, reaproveitando os registradores |
| `NEWPAIR a b c`                               | `R[a] = {R[b], R[c]}` |
| `GETINDEX a b c`                              | `R[a] = R[b][R[c]]`: lista (`IndexError`), dict (`KeyError`), string (o caractere) |
| `INDEXPLACE a b c`, `SETINDEX a b c`          | atribuição a um lugar indexado (`m[i][j] = v`): `INDEXPLACE` desce o caminho, `SETINDEX` grava (no dict, só atualiza chave existente) |
| `CALLBUILTIN a b c`                           | método embutido `b` de `R[a]` (`size`, `add`, `remove`, `has`, `keys`, `values`) com `c` argumentos depois dele (spec 5.7) |

### 5.7 Exceções

| Instrução               | Efeito |
|-------------------------|--------|
| `NEWERROR a b c`        | `R[a] =` erro do tipo `K[b]` com a mensagem `R[c]` |
| `ERRFIELD a b c`        | `R[a] =` `kind`, `message`, `line` ou `column` (`c` = 0 a 3) do erro `R[b]` |
| `THROW a`               | lança o erro `R[a]` (relançar preserva posição e trace) |

## 6. Exceções e `finally`

### Tabela de tratadores

Cada protótipo tem uma tabela de entradas `{início, fim, destino, tipo, registrador}` — "um erro
lançado numa instrução de *início* a *fim* (exclusive) cujo tipo é *tipo* vai para *destino*, com
o erro guardado em *registrador*". `Error` pega qualquer erro, **menos** o sinal do `exit()`; o
tipo vazio (o pega-tudo do `finally`, abaixo) pega também o `exit()` — é assim que o `exit()` roda
os `finally` pendentes sem que nenhum `except` o capture. No topo, o sinal encerra o programa com
o código.

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
**ação pendente** (`i64`) e o **valor pendente**. Toda saída do bloco protegido grava o que fazer
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
intervalos do próprio `try`: um erro lançado pelo `finally` sobe para fora e nunca roda o
`finally` duas vezes.

`return`, `break` e `continue` **dentro** do `finally` são proibidos pela linguagem (spec 5.6),
então um `finally` nunca descarta um retorno ou um erro pendente.

### Exemplo

```cinza
fn f(i32 n) -> i32 {
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
(`l[0].x = v`, `q.p.x = v`), o compilador segue esta ordem:

1. avalia a base e os índices, da esquerda para a direita, guardando cada um num registrador;
2. avalia o valor;
3. **só então** lê o struct do lugar (`l[idx]`), grava o campo e **devolve** o struct ao lugar.

Ler o struct antes do passo 2 seria um erro: se o valor alterar outro campo do mesmo struct
(`l[0].x = f()` com `f` mudando `l[0].y`), a devolução apagaria essa alteração. Na ordem acima,
nenhum código do programa roda entre ler e devolver, e se o valor tiver removido o elemento, a
leitura do passo 3 lança `IndexError` na posição da gravação.

Em `a op= b`: passos 1 e 2 iguais (com `b`), depois lê o valor **atual** do lugar, calcula, grava
e devolve. O valor atual é lido depois de `b` (spec 5.2): em `l[0].x += g()`, uma alteração que
`g()` faça em `l[0].x` entra na conta.

Em níveis que são objetos ou coleções (referências), a gravação vai direto, sem devolução.

Testes: `tests/structs/cvm_struct_lugar_valor.cinza`, `cvm_struct_lugar_composto.cinza` e
`cvm_struct_lugar_removido.cinza`.

## 8. Módulos e início do programa

Cada módulo ganha um protótipo de inicialização com os `const` globais (`SETGLOBAL`). A VM roda
os de todos os módulos na ordem topológica (spec 4.2) e depois chama a `main`, com `args` em `r0`
se ela tiver o parâmetro. As constantes dos módulos nativos (`Math.pi`) já começam nos seus slots.

**Funções genéricas** (spec, capítulo 8) não chegam à VM: o semântico cria uma cópia por
combinação de tipos usada (`Program::instancias` do módulo da declaração), já com os tipos
concretos, e o compilador as trata como funções comuns, com o nome `maior[i32]` no protótipo
(e no stack trace). A declaração genérica em si não gera protótipo. Nenhuma instrução nova.
Os **structs genéricos** (spec 8.6) seguem o mesmo caminho: cada cópia (`Par[i32, string]`) está
em `Program::instancias` e entra na tabela de structs como um struct comum; a declaração genérica
fica de fora. A criação pelo nome (`new Ponto(y: 4)`, spec 4.7) continua um `NEWSTRUCT` com
todos os campos em registros seguidos: o compilador avalia os valores informados, na ordem
escrita, direto no registro de cada campo, e depois os valores iniciais dos omitidos.

## 9. Coleta de ciclos

Os registradores guardam `Value`, então os objetos que eles seguram contam como referências
externas para o coletor (gc.h). A coleta continua sendo disparada na criação de objetos
(`NEWOBJ`), que é um ponto seguro: a VM não guarda ponteiros crus para dentro dos registradores
ou dos contêineres entre instruções.

## 10. Desmontador (`--bytecode`)

Uma linha por instrução: posição, nome, operandos e, quando ajuda, um comentário.

```cinza
fn fib(i32 n) -> i32 {
  if (n < 2) { return n; }
  return fib(n - 1) + fib(n - 2);
}
```

```
fn fib  (1 parâmetro, 5 registradores)
  0000  JGEK_I     r0, 2             ; compara-e-salta: n >= 2 → 0003
  0001  JMP        -> 0003
  0002  RET        r0
  0003  SUBK_I32   r3, r0, 1         ; n - 1 (constante embutida)
  0004  CALL       r3, fib, 1
  0005  MOVE       r2, r3
  0006  SUBK_I32   r4, r0, 2
  0007  CALL       r4, fib, 1
  0008  MOVE       r3, r4
  0009  ADD_I32    r1, r2, r3
  0010  RET        r1
  0011  RETVOID
```

## 11. Otimizações previstas (depois de tudo funcionar)

Só entram com o benchmark mostrando ganho e a suíte verde:

- instruções com constante imediata (`ADDK_I r1, r0, 1`) e comparação-e-salto fundidos
  (`JLT_I r0, r1, -> 0004`), que reduzem o número de instruções dos laços;
- `for (i32 i in range(a, b))` sem criar a lista, quando `range` é a embutida (o resultado é o
  mesmo: ela não tem efeito colateral);
- registradores com números sem embrulho no `Value`, se a medição mostrar que o `Value`
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
| compara-e-salta (`JLT_I`…`JGEK_I` + o `JMP` seguinte num só despacho, no `if`/`while`) | — | — | 92 | 46 |
| `for` sobre `string` sem lista (`FORNEXT` corta um caractere UTF-8 por vez) | — | — | — | — |

O `for` sobre `string` não aparece nos quatro programas; num laço de 1 milhão de caracteres caiu de
~120 para ~63 ms. `Value` sem embrulho nos registradores ficou de fora: nenhuma medição apontou o
`Value` como gargalo (o que resta em `ciclos` é coleta e criação de objetos, no runtime compartilhado).

O *computed goto* foi implementado, testado e medido lado a lado com o `switch`: nenhum ganho
mensurável (os processadores atuais já preveem bem o salto indireto do `switch`), e ele traz uma
armadilha — o salto computado não destrói os locais do caso (`std::string`, `Value`), o que exige
fechar o bloco antes de cada salto. Sem ganho, não compensa; fica registrado para não ser refeito.

## 12. Mapeamento para LLVM (futuro)

O backend LLVM partirá da mesma AST analisada (tipos já resolvidos e promoções já inseridas como
`CastExpr`), então cada operação numérica tem um tipo só, como na CVM:

| Cinza                        | LLVM |
|------------------------------|------|
| `i32`, `i64`                 | `i32`, `i64` |
| `f32`, `f64`                 | `float`, `double` |
| `+ - *` inteiros             | `llvm.sadd/ssub/smul.with.overflow.*` + salto para o `OverflowError` (sem `nsw`/`nuw`: o estouro é erro definido, não comportamento indefinido) |
| `/ %` inteiros               | teste de divisor 0 (`ZeroDivisionError`) e de `menor / -1` (`OverflowError`; `x % -1` dá 0) antes do `sdiv`/`srem` |
| `+ - * /` ponto flutuante    | teste de divisor 0 (`ZeroDivisionError`) antes do `fdiv`; `fadd`/`fsub`/`fmul`/`fdiv` e teste de resultado não finito (`OverflowError`: infinito e NaN não existem), sem *fast-math* (o `-0` e o arredondamento continuam os do IEEE) |
| comparações                  | `icmp slt/sle/eq/ne` e `fcmp olt/ole/oeq/one` (sem NaN, ordenado e não ordenado coincidem) |
| `-x`                         | inteiro: teste do menor valor + `sub 0, x`; ponto flutuante: `fneg` |
| `i32` → `i64`                | `sext` |
| `f32` → `f64`                | `fpext` |
| `cast<i32>(i64)`             | teste de faixa + `trunc` |
| `cast<inteiro>(flutuante)`   | teste de faixa (`OverflowError`) + `fptosi` (trunca em direção a zero) |
| `cast<flutuante>(inteiro)`   | `sitofp` |
| `cast<f32>(f64)`             | teste de faixa do `f32` (`OverflowError`) + `fptrunc` |
| `cast<T>(string)` e para `string` | chamada ao runtime (`convertValue`) |

O runtime (coleções, strings, coleta de ciclos, biblioteca padrão, erros e stack trace) continua
em C++ e seria chamado pelo código gerado. As funções e os structs genéricos chegam como as cópias
concretas (seção 8): o LLVM nunca vê um parâmetro de tipo.
