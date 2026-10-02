# 3. Tipos

A Cinza tem tipagem estática: toda expressão tem um tipo conhecido na compilação, e as regras
deste capítulo são conferidas antes de executar.

## 3.1 Tipos primitivos

| Tipo      | Valores                                                                 |
|-----------|-------------------------------------------------------------------------|
| `int`     | inteiro de 64 bits com sinal: -9223372036854775808 a 9223372036854775807 |
| `decimal` | ponto flutuante de 64 bits (IEEE 754, precisão dupla)                    |
| `string`  | texto em UTF-8 (sinônimo: `str`)                                         |
| `bool`    | `true` ou `false`                                                        |

`void` só existe como tipo de retorno de função. Nenhuma variável, parâmetro, campo ou
elemento de coleção pode ser `void`, e uma expressão `void` não é um valor (não pode ser
atribuída nem passada como argumento).

Não existe `null`: toda variável tem sempre um valor do seu tipo.

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
// expect-error: esperado 'int', mas recebeu 'string'
```

`var` só pode aparecer em declarações de variável local e no iterador do `for`; não vale em
parâmetros, retornos, campos nem dentro de `list<>`, `dict<>` ou `pair<>`.

## 3.3 Coleções

| Tipo           | Descrição                                                              |
|----------------|------------------------------------------------------------------------|
| `list<T>`      | sequência indexada de elementos do tipo `T`, a partir do índice 0      |
| `dict<K, V>`   | associação de chaves `K` a valores `V`, ordenada pela chave            |
| `pair<A, B>`   | par imutável com os campos `first` (tipo `A`) e `second` (tipo `B`)    |

A chave de um `dict` só pode ser `int`, `string`, `bool` ou um `enum`. `decimal` é proibido
(comparação exata de ponto flutuante não é confiável), assim como coleções, objetos e `op`.

Uma variável ou campo `list` ou `dict` declarado sem valor inicial nasce vazio.

## 3.4 Tipos declarados

- **class** — objeto com campos privados e métodos; é uma referência (seção 5.1).
- **struct** — agrupamento de campos públicos, sem métodos; é um valor (seção 5.1).
- **enum** — um conjunto fixo de nomes (seção 4.8).
- **interface** — um contrato de métodos que classes cumprem (seção 3.8).
- **error** — um tipo de erro, usado com `throw` e `except` (seção 4.10).

## 3.5 Regras de atribuição

Um valor do tipo `V` pode ser usado onde se espera o tipo `T` (atribuição, inicialização,
argumento, retorno, elemento de coleção) quando:

1. `V` e `T` são o mesmo tipo; ou
2. `V` é `int` e `T` é `decimal` — o valor é convertido; ou
3. `T` é `Error` e `V` é qualquer tipo de erro (`Error` é a raiz dos erros); ou
4. `T` é uma interface e `V` é uma classe que declara cumpri-la (seção 3.8); ou
5. uma das regras do `op<...>` (seção 3.7) se aplica.

Nenhuma outra conversão é implícita: `decimal` não vira `int`, número não vira `string`, `bool`
não vira número.

```cinza
fn metade(decimal x) -> decimal { return x / 2; }
fn main() {
  decimal d = 3;
  print(d, metade(5));
}
// expect: 3 2.5
```

### Invariância das coleções

`list`, `dict` e `pair` são **invariantes**: `list<int>` não é `list<decimal>`, mesmo que `int`
caiba em `decimal`. (Se fosse, uma função poderia pôr um `decimal` numa lista que o chamador
acredita ser de `int`.)

**Literais se adaptam ao tipo esperado**: `[1, 2]` onde se espera `list<decimal>` vira uma
`list<decimal>` com os elementos convertidos. Uma variável `list<int>` não se adapta.

```cinza
fn soma(list<decimal> l) -> decimal {
  decimal s = 0.0;
  for (decimal x in l) { s += x; }
  return s;
}
fn main() {
  print(soma([1, 2.5]));
}
// expect: 3.5
```

```cinza
fn soma(list<decimal> l) -> decimal { return 0.0; }
fn main() {
  list<int> inteiros = [1, 2];
  print(soma(inteiros));
}
// expect-error: esperado 'list<decimal>', mas recebeu 'list<int>'
```

Sem tipo esperado, um literal de lista toma o tipo comum dos elementos: `[1, 2.5]` é
`list<decimal>`; elementos de tipos incompatíveis são erro.

## 3.6 Valor inicial obrigatório

Toda variável e todo campo precisa de valor inicial na declaração, exceto `list<T>` e
`dict<K, V>`, que nascem vazios.

```cinza
fn main() {
  int x;
}
// expect-error: 'x' deve ser inicializado na declaração
```

## 3.7 `op<T1, T2, ...>`

Um `op` declara que um lugar aceita um valor de **qualquer um** dos tipos listados:

```
op<decimal, int> numero = 1;
op<string, bool, int> status = "ok";
```

### Regras da declaração

1. pelo menos 2 tipos;
2. sem repetir tipo (`str` e `string` são o mesmo);
3. a ordem não importa: `op<int, string>` e `op<string, int>` são o mesmo tipo (e as mensagens
   mostram os tipos em ordem alfabética);
4. sem `void` e sem `var`;
5. um `op` não pode conter outro `op`, em nenhuma profundidade;
6. `op` não pode ser chave de `dict`;
7. no máximo um `list`, um `dict` e um `pair` (a coleção não guarda o tipo dos elementos em
   runtime, então `type()` não saberia distingui-los);
8. não aceita interface (a própria interface já aceita objetos de várias classes).

### O tipo trava e nunca muda

O valor de um `op` tem um tipo, escolhido na inicialização, que **nunca muda**. Um literal usa
o próprio tipo se ele estiver na lista; senão, `int` vira `decimal` se `decimal` estiver na
lista.

Quando o compilador conhece o tipo travado — uma variável local ou `const` inicializada com um
valor de tipo conhecido —, a variável passa a ter esse tipo:

```cinza
fn main() {
  op<decimal, int, string> coisa = 1;
  coisa = 2;
  int x = coisa + 1;
  print(coisa, x, type(coisa));
}
// expect: 2 3 int
```

```cinza
fn main() {
  op<decimal, int, string> coisa = 1;
  coisa = "x";
}
// expect-error: esperado 'int', mas recebeu 'string'
```

Um `op` precisa de valor inicial, inclusive dentro de coleções (`list<op<int, string>> l;` é
erro).

### Quando o tipo travado não é conhecido

Em parâmetros, retornos, campos e iteradores, o compilador não sabe qual tipo foi travado.
Ali vale a regra de **todos os tipos**: uma operação só é aceita se valer para todos os tipos
possíveis do `op`, e o resultado é o tipo (ou o `op`) dos resultados possíveis.

```cinza
fn mais_um(op<decimal, int> v) -> op<decimal, int> { return v + 1; }
fn metade(op<decimal, int> v) -> decimal { return v / 2.0; }
fn main() {
  print(mais_um(1), type(mais_um(1)), mais_um(1.5), metade(3));
}
// expect: 2 int 2.5 1.5
```

```cinza
fn f(op<decimal, int> v) {
  int x = v;
}
fn main() { f(1); }
// expect-error: declarado 'int', mas atribuído 'op<decimal, int>'
```

Outras consequências da regra de todos os tipos:

- `==` e `!=` também precisam valer para todos os tipos (`v == 1` com `v` do tipo
  `op<int, string>` é erro);
- método, campo, `[]` e `for` direto num `op` são erro — é preciso estreitar primeiro;
- atribuir um novo valor a um `op` de tipo desconhecido só é permitido com um valor que sirva
  para todos os tipos; o tipo travado se mantém (`v = 5;` guarda `5.0` se `v` travou em
  `decimal`).

### Estreitamento com `type()`

Dentro de `if (type(x) == T) { ... }` — em que `x` é uma variável, parâmetro ou campo cujo tipo
é um `op` —, `x` tem o tipo `T`. Como o tipo travado nunca muda, isso é sempre seguro. Testar um
tipo que o `op` não tem é erro (a condição seria sempre falsa). Também vale escrever
`T == type(x)`. O `else` não estreita.

```cinza
fn descreve(op<decimal, int> v) -> string {
  if (type(v) == int) {
    int x = v;
    if (x % 2 == 0) { return "par"; }
    return "ímpar";
  }
  return "decimal";
}
fn main() {
  print(descreve(4), descreve(7), descreve(2.5));
}
// expect: par ímpar decimal
```

### Coleções de `op`

Numa coleção de `op`, **todos os elementos têm o mesmo tipo**: `list<op<decimal, int>>` é uma
`list<decimal>` ou uma `list<int>`. Por isso uma função com parâmetro `list<op<decimal, int>>`
aceita as duas:

```cinza
fn media(list<op<decimal, int>> valores) -> decimal {
  decimal s = 0.0;
  for (op<decimal, int> v in valores) { s = s + v; }
  return s / valores.size();
}
fn main() {
  list<int> a = [1, 2, 3, 4, 5];
  list<decimal> b = [8.6, 4.5, 5.2, 7.8];
  print(media(a), media(b));
}
// expect: 3 6.525
```

Uma coleção de `op` cujo tipo travado não é conhecido (por exemplo, recebida por parâmetro) é
**somente leitura**: `add`, `remove`, atribuição por índice e `Lists.sort`/`Lists.reverse` são
erro — alterar poderia quebrar o tipo da coleção original. `type()` também não vale numa
coleção de `op` (use-o num elemento).

```cinza
fn estraga(list<op<decimal, int>> valores) {
  valores.add(2.5);
}
fn main() {
  list<int> l = [1];
  estraga(l);
}
// expect-error: é somente leitura aqui
```

## 3.8 Interfaces

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
interface Forma { fn area() -> decimal; }
class Quadrado : Forma {
  decimal lado = 1.0;
  pub {
    Quadrado(decimal l) { lado = l; }
    fn area() -> decimal { return lado * lado; }
  }
}
class Retangulo : Forma {
  decimal a = 1.0;
  decimal b = 1.0;
  pub {
    Retangulo(decimal x, decimal y) { a = x; b = y; }
    fn area() -> decimal { return a * b; }
  }
}
fn total(list<Forma> formas) -> decimal {
  decimal s = 0.0;
  for (Forma f in formas) { s += f.area(); }
  return s;
}
fn main() {
  list<Forma> formas = [new Quadrado(2.0), new Retangulo(1.5, 2.0)];
  print(total(formas), type(formas[0]));
}
// expect: 7 Quadrado
```

## 3.9 Tipos como valores

`type(x)` (seção 6.1) devolve um valor do tipo `type`, que representa o tipo real de `x`. Um
tipo escrito como valor (`int`, `list<int>`, o nome de uma classe, struct, enum ou erro) pode ser
comparado com ele por `==` e `!=`. No `print`, um tipo aparece pelo nome. Não existe declaração
de variável do tipo `type`.

```cinza
struct Ponto { int x = 0; }
fn main() {
  list<int> l = [1];
  print(type(5) == int, type(l) == list<int>, type(new Ponto()) == Ponto, type(l));
}
// expect: true true true list<int>
```

## 3.10 `const`

`const` declara que uma variável, parâmetro ou `const` global não pode ser reatribuído.

- O `const` fecha o **recipiente inteiro**: um `struct`, `list`, `dict` ou `pair` const não pode
  ser alterado em nenhuma profundidade (campos, `add`, `remove`, atribuição por índice).
- O `const` **para no objeto de classe**: num `const list<Pessoa>`, a lista não muda, mas os
  objetos dentro dela continuam alteráveis pelos seus métodos.
- `const` diretamente num tipo de classe ou interface (`const Pessoa p`, inclusive em
  parâmetro), ou num `op` que aceita classe, é erro.

```cinza
fn main() {
  const list<int> l = [1, 2];
  l.add(3);
}
// expect-error: Listas const não podem ser modificadas
```

As regras do `const` global (o que pode aparecer no inicializador) estão na seção 4.3.
