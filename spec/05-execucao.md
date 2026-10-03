# 5. Execução

## 5.1 Valores: cópia e compartilhamento

| Tipo                                              | Na atribuição, argumento e retorno |
|---------------------------------------------------|------------------------------------|
| `int`, `decimal`, `string`, `bool`, enum, `type`  | copiado                            |
| `pair<A, B>`                                      | copiado (e imutável)               |
| `struct`                                          | copiado — **cópia rasa**           |
| `list<T>`, `dict<K, V>`                           | compartilhado (mesma coleção)      |
| objeto de `class`                                 | compartilhado (mesmo objeto)       |
| erro                                              | compartilhado (imutável)           |

**Cópia rasa** de struct: os campos são copiados um a um; um campo `list`, `dict` ou objeto
continua apontando para a mesma coleção ou objeto. Um struct guardado numa lista ou noutro struct
é alterado no lugar (`l[0].x = 1`, `q.p.x = 1`).

```cinza
struct P { int x = 0; list<int> l; }
fn main() {
  list<int> compartilhada = [1];
  P a = new P(1, compartilhada);
  P b = a;
  b.x = 2;
  b.l.add(9);
  print(a.x, b.x, a.l);
}
// expect: 1 2 [1, 9]
```

## 5.2 Ordem de avaliação

- Operandos, argumentos de chamada e elementos de literais são avaliados **da esquerda para a
  direita**. Numa chamada de método, o objeto é avaliado antes dos argumentos.
- `&&`/`and` e `||`/`or` avaliam o lado direito só se necessário (curto-circuito).
- Numa atribuição, primeiro são avaliadas as partes do alvo (base e índices, da esquerda para a
  direita), depois o valor; então o valor é gravado. Em `a op= b`, o alvo é avaliado **uma única
  vez**, e o valor atual do alvo é lido **depois** de avaliar `b`. Se o valor alterar o lugar de
  destino (por exemplo, outro campo do mesmo struct), essa alteração é preservada; se removê-lo,
  a gravação lança o erro correspondente (`IndexError`, `KeyError`).

```cinza
struct P { int x = 1; int y = 0; }
fn g(list<P> l) -> int {
  l[0].y = 7;
  l[0].x = 100;
  return 5;
}
fn main() {
  list<P> l = [new P()];
  l[0].x += g(l);
  print(l[0]);
}
// expect: P{x: 105, y: 7}
```

```cinza
fn mostra(int n) -> int {
  print(n);
  return n;
}
fn main() {
  list<int> l = [0, 0, 0];
  l[mostra(1)] = mostra(2);
  print(mostra(3) + mostra(4));
  print(false && mostra(5) == 5);
}
// expect: 1
// expect: 2
// expect: 3
// expect: 4
// expect: 7
// expect: false
```

## 5.3 Operadores

### Aritméticos: `+ - * / %`

- `int` com `int` dá `int`; se algum lado é `decimal`, o outro é convertido e o resultado é
  `decimal`.
- `/` entre inteiros **trunca em direção a zero** (`-7 / 2` é `-3`); o sinal de `%` segue o
  dividendo (`-7 % 2` é `-1`). Entre decimais, `%` é o resto de ponto flutuante (`7.5 % 2` é
  `1.5`).
- Divisão ou resto por zero (`int` ou `decimal`) lança `ZeroDivisionError`.
- Resultado de `int` fora da faixa (inclusive `-x` e `x / -1` com o menor `int`) lança
  `OverflowError`. Resultado de `decimal` infinito também lança `OverflowError`.
- `-` unário vale para números; `+` também concatena textos (abaixo).

A divisão entre inteiros acontece antes de qualquer conversão: `decimal x = 7 / 2;` guarda `3`.

```cinza
fn main() {
  print(7 / 2, -7 / 2, 7 % 3, -7 % 2, 7.0 / 2, 7.5 % 2);
}
// expect: 3 -3 1 -1 3.5 1.5
```

```cinza
fn main() {
  int grande = 9223372036854775807;
  try { grande += 1; } except (OverflowError e) { print(e.kind); }
  try { print(1 / 0); } except (ZeroDivisionError e) { print(e.message); }
}
// expect: OverflowError
// expect: divisão por zero
```

### Concatenação

`+` com uma `string` de um lado e uma `string` ou qualquer primitivo do outro concatena, usando a
forma textual da seção 5.4. Coleções e objetos não se concatenam.

```cinza
fn main() {
  print("a" + 1 + 2, 1 + 2 + "a", "x" + true, "v" + 2.5);
}
// expect: a12 3a xtrue v2.5
```

### Comparação: `< <= > >=`

Entre números (`int` e `decimal` podem se misturar) ou entre textos (ordem lexicográfica dos
bytes UTF-8, ou seja, dos code points). Outros tipos não se comparam por ordem.

### Igualdade: `== !=`

Os dois lados precisam ser do mesmo tipo, ou ambos numéricos (`1 == 1.0` é `true`).

| Tipo                                  | Igualdade                                  |
|---------------------------------------|--------------------------------------------|
| primitivos, enum, `type`              | por valor                                  |
| `list`, `dict`, `pair`, `struct`      | estrutural (mesmos elementos/campos)       |
| objeto de classe, interface, erro     | identidade (o mesmo objeto)                |

### Lógicos: `&& || !` (e `and or not`)

Só sobre `bool`.

### Atribuição composta: `+= -= *= /= %=`

`a op= b` equivale a `a = a op b` com `a` avaliado uma vez. O resultado precisa caber no tipo de
`a`: `int x; x += 1.5;` é erro, porque `int + decimal` dá `decimal`.

## 5.4 Forma textual dos valores

É a forma usada por `print`, pela concatenação e por `Convert.to_string`.

| Valor          | Forma                                                                  |
|----------------|------------------------------------------------------------------------|
| `int`          | dígitos decimais: `-42`                                                |
| `decimal`      | a menor forma que volta ao mesmo número: `3.14`, `0.1`; valor inteiro sem `.0` (`4`); muito grande ou muito pequeno em notação científica (`1e+24`) |
| `string`       | o próprio texto (dentro de coleções, entre aspas)                     |
| `bool`         | `true`, `false`                                                        |
| `list`         | `[1, 2]`, `["a", "b"]`                                                 |
| `dict`         | `{"a": 1, "b": 2}`, em ordem de chave                                  |
| `pair`         | `{1, "x"}`                                                             |
| `struct`       | `Ponto{x: 1, y: 2}`                                                    |
| objeto         | `<Pessoa object>`                                                      |
| erro           | `ValueError: mensagem`                                                 |
| enum           | `Cor.Verde`                                                            |
| tipo           | `int`, `list<int>`, `Ponto`                                            |

```cinza
struct Ponto { int x = 0; int y = 0; }
fn main() {
  print(123456.789, 4.0, 1.0 / 4, [1, 2], ["a"], {{"b", 2}, {"a", 1}}, {1, "x"}, new Ponto(1, 2));
}
// expect: 123456.789 4 0.25 [1, 2] ["a"] {"a": 1, "b": 2} {1, "x"} Ponto{x: 1, y: 2}
```

## 5.5 Instruções

### `if` / `else`

A condição precisa ser `bool`. `else if` encadeia.

### `while`

Repete o bloco enquanto a condição (`bool`) for verdadeira.

### `for`

```
for (tipo nome in iteravel) { ... }
```

| Iterável       | Cada volta recebe                                   |
|----------------|-----------------------------------------------------|
| `list<T>`      | um elemento, do primeiro ao último                  |
| `dict<K, V>`   | um `pair<K, V>`, em ordem de chave                  |
| `string`       | um caractere (code point UTF-8), como `string`      |

Os elementos são **copiados no início** do laço: alterar a coleção dentro do corpo não muda as
voltas. O tipo do iterador precisa aceitar o elemento (ou ser `var`); `for (decimal x in
list_de_int)` converte cada elemento.

```cinza
fn main() {
  list<int> l = [1, 2];
  for (int v in l) { l.add(v * 10); }
  print(l);
  for (string c in "ação") { print(c); }
}
// expect: [1, 2, 10, 20]
// expect: a
// expect: ç
// expect: ã
// expect: o
```

### `break` e `continue`

Só dentro de um laço; afetam o laço mais interno. Não podem sair de um bloco `finally`.

### `return`

Encerra a função. Numa função `void`, `return;` (sem valor); nas demais, `return valor;`. Uma
função com tipo de retorno precisa **retornar em todos os caminhos**: um caminho garante o
retorno se termina em `return` ou `throw`, se é um `if`/`else` com os dois ramos garantindo, um
`try` com o bloco e todos os `except` garantindo, ou um `while (true)` sem `break` que o encerre.

### Código inalcançável

Uma instrução depois de outra que **sempre sai do bloco** é erro de compilação. Sempre saem:
`return`, `throw`, `exit(...)`, `break`, `continue`, um `if`/`else` com os dois ramos saindo, um
`while (true)` sem `break` e um `try` com o bloco e todos os `except` saindo.

```cinza
fn f() -> int {
  return 1;
  print("nunca");
}
fn main() { print(f()); }
// expect-error: Código inalcançável
```

## 5.6 Exceções

### `throw`

`throw expressao;` lança um erro; a expressão precisa ser um erro (`ValueError("x")`, um erro
capturado...). Lançar de novo um erro capturado (`throw e;`) preserva o tipo, a mensagem, a
posição e o stack trace originais.

### `try` / `except` / `finally`

```
try { ... }
except (ValueError e) { ... }
except (Error e) { ... }
finally { ... }
```

- Um erro no bloco `try` vai para o **primeiro** `except` cujo tipo é o do erro, ou `Error` (que
  captura qualquer erro). Por isso `except (Error ...)` precisa ser o último, e repetir um tipo é
  erro. Sem `except` correspondente, o erro continua subindo.
- A variável do `except` existe só no bloco dele.
- O `finally` **sempre roda**: depois do fluxo normal, de um `return`, `break` ou `continue`
  dentro do `try` ou `except`, de um erro não capturado e de um erro lançado dentro do próprio
  `except`. Depois dele, o fluxo pendente continua (o erro sobe, o `return` acontece).
- `return`, `break` e `continue` não podem sair de um `finally`.

```cinza
fn divide(int a, int b) -> int {
  try {
    return a / b;
  } except (ZeroDivisionError e) {
    print("capturado:", e.message);
    return 0;
  } finally {
    print("finally");
  }
}
fn main() {
  print(divide(10, 2));
  print(divide(1, 0));
}
// expect: finally
// expect: 5
// expect: capturado: divisão por zero
// expect: finally
// expect: 0
```

### Campos de um erro

`e.kind` (tipo, `string`), `e.message` (`string`), `e.line` e `e.column` (`int`, a posição onde
o erro surgiu). São somente leitura.

### Erro não capturado

Termina o programa com código 1 e mostra o erro no formato do capítulo 7, seguido do stack trace
(da chamada mais interna para fora).

## 5.7 Coleções em execução

- Índice de `list` é `int`. Um literal negativo como índice é erro de compilação; um índice
  negativo ou além do fim, em runtime, lança `IndexError`.
- Ler `d[k]` com uma chave que não existe lança `KeyError`. `d[k] = v` **só atualiza** uma chave
  existente (com chave nova, `KeyError`); para inserir, use `d.add({k, v})`, que também substitui
  o valor se a chave já existir.

| `list<T>`          | Efeito                                                      |
|--------------------|-------------------------------------------------------------|
| `l.size()`         | número de elementos (`int`)                                 |
| `l.add(x)`         | acrescenta `x` ao fim                                       |
| `l.remove(i)`      | remove o elemento do índice `i` (`IndexError` se inválido)  |
| `l.has(i)`         | `true` se `i` é um índice válido                            |

| `dict<K, V>`       | Efeito                                                      |
|--------------------|-------------------------------------------------------------|
| `d.size()`         | número de entradas                                          |
| `d.add({k, v})`    | insere ou substitui                                         |
| `d.remove(k)`      | remove a chave (`KeyError` se não existir)                  |
| `d.has(k)`         | `true` se a chave existe                                    |
| `d.keys()`         | `list<K>` das chaves, em ordem                              |
| `d.values()`       | `list<V>` dos valores, na ordem das chaves                  |

| `string`           | Efeito                                                      |
|--------------------|-------------------------------------------------------------|
| `s.size()`         | número de caracteres (code points UTF-8), não de bytes      |
| `s[i]`             | o caractere do índice `i`, como `string`; `i` negativo conta do fim (`s[-1]` é o último); fora do tamanho, `IndexError` |
| `s[ini:fim]`       | o trecho de `ini` até `fim` (exclusive); `ini` omitido é `0`, `fim` omitido é o tamanho |
| `s[ini:fim:passo]` | de `passo` em `passo`; passo negativo anda para trás (`s[::-1]` inverte); passo `0` lança `ValueError` |

Índices e fatias contam **caracteres** e aceitam negativos, que contam do fim. As fatias **não**
ajustam limites: com passo positivo, `ini` e `fim` precisam ficar entre `0` e o tamanho, com
`ini ≤ fim` (o fim pode ser a posição depois do último caractere); com passo negativo, simétrico:
`ini` precisa ser um índice válido e `fim` pode ir até a posição antes do primeiro caractere
(`-tamanho - 1`), com `ini ≥ fim` — `s[-1:-7:-1]` inverte uma string de 6. Omitidos: do começo ao
fim (ou do último ao primeiro). Fora disso, `IndexError`.

Strings são **imutáveis**: `s[i] = ...` é erro de compilação. Índices negativos e fatias valem só
para `string`; em `list` o índice continua não-negativo e a fatia é `Lists.slice`.

As demais funções de texto e de lista estão nos módulos `Strings` e `Lists` (capítulo 6).

```cinza
fn main() {
  dict<string, int> d = {{"a", 1}};
  d.add({"b", 2});
  d["a"] = 10;
  print(d, d.keys(), "ação".size());
  try { d["z"] = 0; } except (KeyError e) { print(e.kind); }
  string s = "coração";
  print(s[0], s[-1], s[2:5], s[:3], s[4:], s[::-1]);
  try { print(s[2:10]); } except (IndexError e) { print(e.kind); }
}
// expect: {"a": 10, "b": 2} ["a", "b"] 4
// expect: KeyError
// expect: c o raç cor ção oãçaroc
// expect: IndexError
```

## 5.8 Recursão

Cada chamada de função, método ou construtor ocupa um nível de profundidade. Acima de **2000**
níveis, a chamada lança `StackOverflowError` (capturável), em vez de travar o programa.

## 5.9 Memória

A memória é gerenciada automaticamente: um valor deixa de existir quando nada mais o alcança,
inclusive objetos que se referenciam em ciclo. O programa não observa quando isso acontece (não
há destrutores).
