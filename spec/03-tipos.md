# 3. Tipos

A Cinza tem tipagem estática: toda expressão tem um tipo conhecido na compilação, e as regras
deste capítulo são conferidas antes de executar.

## 3.1 Tipos primitivos

| Tipo      | Valores                                                                 |
|-----------|-------------------------------------------------------------------------|
| `i32`     | inteiro de 32 bits com sinal: -2147483648 a 2147483647                   |
| `i64`     | inteiro de 64 bits com sinal: -9223372036854775808 a 9223372036854775807 |
| `f32`     | ponto flutuante de 32 bits (IEEE 754, precisão simples)                  |
| `f64`     | ponto flutuante de 64 bits (IEEE 754, precisão dupla)                    |
| `string`  | texto em UTF-8 (sinônimo: `str`)                                         |
| `bool`    | `true` ou `false`                                                        |

`void` só existe como tipo de retorno de função. Nenhuma variável, parâmetro, campo ou
elemento de coleção pode ser `void`, e uma expressão `void` não é um valor (não pode ser
atribuída nem passada como argumento).

Um valor de um desses tipos nunca é `null`: a ausência de valor só existe no tipo opcional, `T?`
(seção 3.10), e o compilador obriga a resolvê-la antes de usar o valor.

### Números: duas famílias

Os quatro tipos numéricos formam duas **famílias que não se misturam**: os inteiros (`i32`,
`i64`) e os de ponto flutuante (`f32`, `f64`). Princípio: simplicidade no uso, rigor na
implementação e previsibilidade no comportamento.

- **Dentro da família**, o mais estreito é promovido ao mais largo **antes** da operação e o
  resultado tem o tipo mais largo: `i32 + i64` é `i64`, `f32 * f64` é `f64`; `i32 + i32` continua
  `i32` (e pode estourar em 32 bits). A mesma promoção vale em atribuição, argumento e retorno
  (seção 3.5): `i32` → `i64` e `f32` → `f64`.
- **Entre famílias**, nada é implícito: `i32 + f64` é erro de compilação, inclusive em comparações
  (`n < x`) e com literais (`x * 2` com `x` de ponto flutuante: escreva `x * 2.ff`). A conversão é
  explícita, com `cast<T>`.
- `%` só existe entre inteiros.
- Inteiros: estouro, divisão por zero e `menor / -1` são erros de runtime (seção 5.3), em 32 e em 64
  bits.
- Ponto flutuante: **infinito e NaN não existem** na Cinza. Divisão por zero (inclusive `0 / 0`)
  lança `ZeroDivisionError`; um resultado além do maior valor finito do tipo lança `OverflowError`
  (em `f64` e em `f32`). O que é pequeno demais vira `0`, e `-0` existe (`print(-0.ff)` mostra `-0`).

```cinza
fn main() {
  i32 a = 2000000000;
  i64 b = 2000000000;
  f32 x = 0.5f;
  print(a + b, type(a + b), x * 2.f, type(x + 0.25ff));
  try { print(1.ff / 0.ff); } except (ZeroDivisionError e) { print(e.message); }
}
// expect: 4000000000 i64 1 f64
// expect: divisão por zero
```

```cinza
fn main() {
  i32 n = 2;
  f64 x = 1.5ff;
  print(n + x);
}
// expect-error: Operador '+' entre 'i32' e 'f64': inteiro e ponto flutuante não se misturam
```

### `cast<T>(valor)`

`cast<T>(valor)` converte entre `i32`, `i64`, `f32`, `f64` e `string` (`bool` e coleções não
convertem). O valor é convertido **depois** de calculado: `cast<f32>(2 + 3.f)` é erro (a conta
mistura famílias), `cast<f32>(2) + 3.f` vale.

| De → para                | Regra |
|--------------------------|-------|
| inteiro → inteiro        | precisa caber na faixa do destino; senão `OverflowError` |
| ponto flutuante → inteiro | trunca em direção a zero (`cast<i32>(-2.9ff)` é `-2`); fora da faixa, `OverflowError` |
| inteiro → ponto flutuante | o valor mais próximo: `cast<f32>(16777217)` é `16777216` |
| `f64` → `f32`             | o valor mais próximo; grande demais para `f32`, `OverflowError` |
| `f32` → `f64`             | exato |
| número → `string`         | o mesmo texto do `print` |
| `string` → inteiro        | sinal opcional (`+`/`-`) e dígitos, sem espaços; outro texto lança `ValueError`; fora da faixa, `OverflowError` |
| `string` → ponto flutuante | `[sinal] dígitos [. dígitos] [e [sinal] dígitos]`; outro texto (inclusive `"inf"` e `"nan"`) lança `ValueError`; grande demais, `OverflowError`; pequeno demais vira 0 |

```cinza
fn main() {
  print(cast<i32>(2.9ff), cast<i64>("-42"), cast<f32>(0.1ff), cast<f64>(0.1f), cast<string>(7) + "!");
  try { print(cast<i32>(3000000000)); } except (OverflowError e) { print(e.message); }
  try { print(cast<i64>("12a")); } except (ValueError e) { print(e.message); }
}
// expect: 2 -42 0.1 0.10000000149011612 7!
// expect: cast<i32>(3000000000): o valor não cabe em i32
// expect: cast<i64>("12a"): não é um número inteiro (só sinal e dígitos)
```

## 3.2 Inferência com `var`

`var` declara uma variável local cujo tipo é o do valor inicial. Só vale para tipos primitivos;
depois da declaração, o tipo é fixo.

```cinza
fn main() {
  var x = 1;
  var s = "a";
  print(x + 1, s);
}
// expect: 2 a
```

```cinza
fn main() {
  var x = 1;
  x = "a";
}
// expect-error: esperado 'i32', mas recebeu 'string'
```

`var` só pode aparecer em declarações de variável local e no iterador do `for`; não vale em
parâmetros, retornos, campos nem dentro de `list<>`, `dict<>` ou `pair<>`.

## 3.3 Coleções

| Tipo           | Descrição                                                              |
|----------------|------------------------------------------------------------------------|
| `list<T>`      | sequência indexada de elementos do tipo `T`, a partir do índice 0      |
| `dict<K, V>`   | associação de chaves `K` a valores `V`, na ordem de inserção           |
| `pair<A, B>`   | par imutável com os campos `first` (tipo `A`) e `second` (tipo `B`)    |

A chave de um `dict` só pode ser `i32`, `i64`, `string`, `bool` ou um `enum`. `f32` e `f64` são
proibidos (comparação exata de ponto flutuante não é confiável), assim como coleções e objetos.

As entradas de um `dict` ficam **na ordem em que entraram**: é a ordem do `print`, do `for`, de
`keys()` e `values()` e das funções do `Dicts`. Trocar o valor de uma chave que já existe não a
move; remover uma chave e inseri-la de novo a põe no fim. Dois dicts são iguais (`==`) quando têm
as mesmas chaves com os mesmos valores, em qualquer ordem. Para percorrer pela ordem das chaves,
ordene-as: `Lists.sorted(d.keys())`.

Uma variável ou campo `list` ou `dict` declarado sem valor inicial nasce vazio.

## 3.4 Tipos declarados

- **class** — objeto com campos privados e métodos; é uma referência (seção 5.1).
- **struct** — agrupamento de campos públicos, sem métodos; é um valor (seção 5.1).
- **enum** — um conjunto fixo de nomes (seção 4.8).
- **interface** — um contrato de métodos que classes cumprem (seção 3.7).
- **error** — um tipo de erro, usado com `throw` e `except` (seção 4.10).

## 3.5 Regras de atribuição

Um valor do tipo `V` pode ser usado onde se espera o tipo `T` (atribuição, inicialização,
argumento, retorno, elemento de coleção) quando:

1. `V` e `T` são o mesmo tipo; ou
2. `V` é `i32` e `T` é `i64`, ou `V` é `f32` e `T` é `f64` — o valor é alargado (seção 3.1); ou
3. `T` é `Error` e `V` é qualquer tipo de erro (`Error` é a raiz dos erros); ou
4. `T` é uma interface e `V` é uma classe que declara cumpri-la (seção 3.7).

Nenhuma outra conversão é implícita: inteiro não vira ponto flutuante (nem o contrário), o
mais largo não vira o mais estreito, número não vira `string`, `bool` não vira número. Para
essas, `cast<T>(valor)` (seção 3.1).

```cinza
fn metade(f64 x) -> f64 { return x / 2.ff; }
fn main() {
  i64 n = 3;
  f64 d = 0.5f;
  print(n, d, metade(5.ff), metade(cast<f64>(n)));
}
// expect: 3 0.5 2.5 1.5
```

### Invariância das coleções

`list`, `dict` e `pair` são **invariantes**: `list<i32>` não é `list<i64>`, mesmo que `i32`
caiba em `i64`. (Se fosse, uma função poderia pôr um `i64` numa lista que o chamador
acredita ser de `i32`.)

**Literais se adaptam ao tipo esperado**: `[1, 2]` onde se espera `list<i64>` vira uma
`list<i64>` com os elementos alargados. Uma variável `list<i32>` não se adapta.

```cinza
fn soma(list<f64> l) -> f64 {
  f64 s = 0.0ff;
  for (f64 x in l) { s += x; }
  return s;
}
fn main() {
  print(soma([1.f, 2.5ff]));
}
// expect: 3.5
```

```cinza
fn soma(list<i64> l) -> i64 { return 0; }
fn main() {
  list<i32> inteiros = [1, 2];
  print(soma(inteiros));
}
// expect-error: esperado 'list<i64>', mas recebeu 'list<i32>'
```

Sem tipo esperado, um literal de lista toma o tipo comum dos elementos, o mais largo da
família: `[1, 2]` é `list<i32>`, `[1.f, 2.5ff]` é `list<f64>`; elementos de famílias diferentes
(`[1, 2.5ff]`) ou de tipos incompatíveis são erro.

## 3.6 Valor inicial obrigatório

Toda variável precisa de valor inicial na declaração, exceto `list<T>` e `dict<K, V>`, que nascem
vazios, e o opcional `T?`, que nasce `null` (seção 3.10). Um campo sem valor inicial (que não é
list, dict nem opcional) é **obrigatório**: num struct, quem o cria o informa (seção 4.7); numa
classe, o construtor o atribui (seção 4.6).

```cinza
fn main() {
  i64 x;
}
// expect-error: 'x' deve ser inicializado na declaração
```

## 3.7 Interfaces

Uma interface é uma lista de assinaturas de métodos (seção 4.9). Uma classe que declara
cumpri-la (`class Circulo : Forma`) pode ser usada onde se espera a interface.

- A classe precisa ter cada método da interface em `pub { }`, com **exatamente** a mesma
  assinatura (mesmos tipos de parâmetro e de retorno).
- Uma classe que não declarou a interface não é aceita, mesmo que tenha os métodos.
- Por um valor do tipo interface só se chamam os métodos que ela declara. Não há acesso a
  campos.
- Um valor do tipo interface **nunca volta** a ser tratado como a classe: `Circulo c = f;` com
  `f` do tipo `Forma` é erro. `type(f)` informa a classe real, mas não estreita.
- `list<Circulo>` não é `list<Forma>` (invariância); um literal `[new Circulo(...), ...]` onde
  se espera `list<Forma>` é.

```cinza
interface Forma { fn area() -> f64; }
class Quadrado : Forma {
  f64 lado = 1.0ff;
  pub {
    Quadrado(f64 l) { lado = l; }
    fn area() -> f64 { return lado * lado; }
  }
}
class Retangulo : Forma {
  f64 a = 1.0ff;
  f64 b = 1.0ff;
  pub {
    Retangulo(f64 x, f64 y) { a = x; b = y; }
    fn area() -> f64 { return a * b; }
  }
}
fn total(list<Forma> formas) -> f64 {
  f64 s = 0.0ff;
  for (Forma f in formas) { s += f.area(); }
  return s;
}
fn main() {
  list<Forma> formas = [new Quadrado(2.0ff), new Retangulo(1.5ff, 2.0ff)];
  print(total(formas), type(formas[0]));
}
// expect: 7 Quadrado
```

## 3.8 Tipos como valores

`type(x)` (seção 6.1) devolve um valor do tipo `type`, que representa o tipo real de `x`. Um
tipo escrito como valor (`i64`, `list<i64>`, o nome de uma classe, struct, enum ou erro) pode ser
comparado com ele por `==` e `!=`. No `print`, um tipo aparece pelo nome. Não existe declaração
de variável do tipo `type`.

```cinza
struct Ponto { i64 x = 0; }
fn main() {
  list<i64> l = [1];
  print(type(5) == i32, type(l) == list<i64>, type(new Ponto()) == Ponto, type(l));
}
// expect: true true true list<i64>
```

## 3.9 `const`

`const` declara que uma variável, parâmetro ou `const` global não pode ser reatribuído.

- O `const` fecha o **recipiente inteiro**: por um `struct`, `list`, `dict` ou `pair` const, nada
  pode ser alterado, em nenhuma profundidade (campos, `add`, `remove`, atribuição por índice). É
  uma garantia de somente leitura (ver adiante).
- O `const` **para no objeto de classe**: num `const list<Pessoa>`, a lista não muda, mas os
  objetos dentro dela continuam alteráveis pelos seus métodos.
- `const` diretamente num tipo de classe ou interface (`const Pessoa p`, inclusive em
  parâmetro) é erro.

```cinza
fn main() {
  const list<i64> l = [1, 2];
  l.add(3);
}
// expect-error: Listas const não podem ser modificadas
```

**Const só vai para const.** Listas e dicts são compartilhados na atribuição e na passagem de
argumento (seção 3.3), então um valor que vem de um const e **guarda uma lista ou um dict** — o
próprio const, uma parte dele, ou um valor montado com ele (`[a]`, `{x, a}`, `new Turma("A", a)`) —
só pode ir para outro lugar const:

- uma variável const (local ou o iterador já ligado ao mesmo const) ou um parâmetro const;
- nunca para uma variável alterável, uma atribuição, um parâmetro sem `const`, um `return`, um
  `.add` ou uma nativa que guarda o argumento (`Lists.insert`, `Lists.extend` de listas de listas).

Para ter uma versão alterável, copie explicitamente: `Lists.copy(a)`, `Dicts.copy(d)`. A cópia é
rasa: numa lista de listas, as de dentro continuam do const, e é preciso copiar cada uma. Números,
textos, `bool`, enums e structs só de valores copiam livres, e objetos de classe também (o const
para no objeto).

O que vem de um const continua do const: o iterador de um `for` sobre uma lista const de listas, a
variável de um `if`/`while` com declaração sobre um opcional const, o elemento que uma nativa
devolve (`Lists.first(m)`), a alternativa de um `else`. Um parâmetro const numa interface precisa
ser const também no método da classe que a cumpre.

**Somente leitura, não imutável.** O `const` garante o que o nome const — e o que sai dele — pode
fazer, não o que os outros nomes fazem: se uma variável alterável e uma const veem a mesma lista, a
alterável ainda a muda, e a const vê a mudança. Da alterável para a const, o valor vai livre (é o
caso de todo argumento comum passado a um parâmetro const). Um valor que ninguém altera nasce no
const: um literal, ou uma cópia guardada nele. Números e textos const são sempre imutáveis, porque
são copiados.

```cinza
import Lists;

fn main() {
  list<i32> a = [1, 2];
  const list<i32> b = a;
  const list<i32> c = Lists.copy(a);
  a.add(3);
  print(b, c);
}
// expect: [1, 2, 3] [1, 2]
```

```cinza
fn altera(list<i32> l) {
  l.add(1);
}

fn f(const list<i32> l) {
  altera(l);
}

fn main() {}
// expect-error: e o parâmetro 'l' não é
```

```cinza
import Lists;

fn main() {
  const list<i32> a = [1, 2];
  const list<i32> b = a;
  list<i32> c = Lists.copy(a);
  c.add(3);
  print(a, b, c);
}
// expect: [1, 2] [1, 2] [1, 2, 3]
```

As regras do `const` global (o que pode aparecer no inicializador) estão na seção 4.3.

## 3.10 Opcional: `T?` e `null`

`null` é a ausência de um valor válido ou conhecido. Ele só existe num **tipo opcional**, escrito
com `?` depois do tipo: um `string?` é uma `string` ou `null`; um `string`, nunca. Qualquer tipo de
valor pode ser opcional — `i32?`, `list<i32>?`, `Pessoa?`, `Forma?` —, também dentro de coleções
(`list<string?>`, o valor de um `dict<K, V?>`, `pair<A?, B>`). Não há `T??`, nem `void?` ou
`var?`, e a chave de um dict não pode ser opcional.

- Um `T` cabe num `T?` (`string? s = "a";`, também ao passar um argumento ou retornar), e `null`
  só cabe num `T?`. Um `T?` **nunca** cabe num `T` sem a ausência resolvida.
- Uma variável ou um campo `T?` declarado sem valor nasce `null`; num struct, um campo `T?` não é
  obrigatório na criação (seção 4.7). `var` não infere de `null`, que não diz o tipo.
- Sobre um `T?` só valem `==` e `!=`: com `null`, com outro `T?` do mesmo tipo ou com um valor do
  tipo dele (`n == 5`); dois `null` são iguais. Operadores, métodos, campos, índices, `for` e
  condições de `if`/`while` sobre um `T?` são erro de compilação. Comparar com `null` um valor que
  não é opcional também é erro: ele nunca é `null`.
- `type(x)` de um `T?` é `T?`, com valor ou sem; `print` e a interpolação mostram `null`.

**`valor else alternativa`** resolve a ausência: o resultado é o valor do opcional, ou a
alternativa quando ele é `null` — e a alternativa **só é avaliada** nesse caso. O valor antes de
`else` precisa ser opcional, e a alternativa é do tipo de dentro (o resultado é um `T`) ou outro
`T?` (o resultado continua `T?`, para encadear: `a else b else c`). `else null` é erro: não
resolve nada.

```cinza
fn acha(list<string> nomes, string alvo) -> string? {
  for (string n in nomes) { if (n == alvo) { return n; } }
  return null;
}

class No {
  i32 valor = 0;
  No? prox;
  pub {
    No(i32 v) { valor = v; }
    fn liga(No n) { prox = n; }
    fn ultimo() -> bool { return prox == null; }
  }
}

fn main() {
  string? a = acha(["ana", "bia"], "bia");
  string? b = acha(["ana"], "zé");
  print(a, b, a == "bia", b == null, type(b));
  string nome = b else "ninguém";
  print(nome, b else a else "fim");
  No n = new No(1);
  print(n.ultimo());
  n.liga(new No(2));
  print(n.ultimo());
  list<i32?> l = [1, null, 3];
  print(l, l[1] else 0);
}
// expect: bia null true true string?
// expect: ninguém bia
// expect: true
// expect: false
// expect: [1, null, 3] 0
```

**`valor else { ... }`**: quando a ausência pede mais que um valor — um aviso, uma conta, sair
—, a alternativa é um bloco. Ele só roda se o valor for `null`, e **todo caminho dele termina**
com `yield valor` (o valor que a expressão recebe, do tipo de dentro) ou saindo do fluxo
(`return`, que continua sendo sair da função; `throw`; `break` ou `continue` do laço em volta da
expressão). Chegar ao `}` é erro de compilação. `yield` só existe no bloco de um else, e não sai
de dentro de um `try` (guarde o valor numa variável e use `yield` depois do `try`).

**`if (T nome = opcional) { ... } else { ... }`** faz uma coisa quando há valor e outra quando não
há: o primeiro bloco roda com o valor em `nome` (do tipo de dentro, ou `var`), que só existe
nele; o `else`, sem ele. **`while (T nome = opcional) { ... }`** repete enquanto houver valor: o
opcional é avaliado de novo a cada volta. O valor precisa ser opcional, e `nome` é do tipo exato
de dentro (sem `?` e sem alargar; uma interface que a classe cumpre também vale). Não há
estreitamento: `x != null` não muda o tipo de `x`.

```cinza
class Fila {
  list<string> itens;
  i32 pos = 0;
  pub {
    fn poe(string s) { itens.add(s); }
    fn proximo() -> string? {
      if (pos >= itens.size()) { return null; }
      pos += 1;
      return itens[pos - 1];
    }
  }
}

fn tamanho(string? s) -> i32 {
  string t = s else { return -1; };
  return t.size();
}

fn main() {
  string? nada;
  string nome = nada else {
    print("sem nome");
    yield "<semNome>";
  };
  print(nome, tamanho("abc"), tamanho(null));
  Fila f = new Fila();
  f.poe("a");
  f.poe("b");
  while (string s = f.proximo()) { print("item", s); }
  if (string n = nada) { print("tem", n); } else { print("não tem"); }
}
// expect: sem nome
// expect: <semNome> 3 -1
// expect: item a
// expect: item b
// expect: não tem
```

```cinza
fn main() {
  string? s = "a";
  print(s.size());
}
// expect-error: o valor é opcional; resolva a ausência antes
```

```cinza
fn main() {
  string? s;
  string t = s else { print("sem valor"); };
}
// expect-error: O bloco do 'else' precisa terminar em todos os caminhos
```

```cinza
fn main() {
  i32 n = 1;
  i32 m = n else 0;
}
// expect-error: O valor antes de 'else' não é opcional ('i32')
```

O que pode faltar de verdade se diz com um `T?`; o que quem chama controla — um índice fora da
lista, uma chave que não existe, o primeiro elemento de uma lista vazia — continua sendo erro de
execução (`IndexError`, `KeyError`, `ValueError`), como na biblioteca (capítulo 6).
