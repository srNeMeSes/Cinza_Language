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

Não existe `null`: toda variável tem sempre um valor do seu tipo.

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
| `dict<K, V>`   | associação de chaves `K` a valores `V`, ordenada pela chave            |
| `pair<A, B>`   | par imutável com os campos `first` (tipo `A`) e `second` (tipo `B`)    |

A chave de um `dict` só pode ser `i32`, `i64`, `string`, `bool` ou um `enum`. `f32` e `f64` são
proibidos (comparação exata de ponto flutuante não é confiável), assim como coleções e objetos.

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

Toda variável e todo campo precisa de valor inicial na declaração, exceto `list<T>` e
`dict<K, V>`, que nascem vazios.

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

- O `const` fecha o **recipiente inteiro**: um `struct`, `list`, `dict` ou `pair` const não pode
  ser alterado em nenhuma profundidade (campos, `add`, `remove`, atribuição por índice).
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

As regras do `const` global (o que pode aparecer no inicializador) estão na seção 4.3.

## 3.10 Sem `null`: como dizer "nenhum"

A Cinza não tem `null` nem tipo opcional: um valor de um tipo é sempre um valor daquele tipo, e
nenhuma conta precisa conferir se ele "existe". O que em outras linguagens seria `null` tem um
destes jeitos, os mesmos da biblioteca (capítulo 6):

- **Zero ou mais**: uma `list<T>`, vazia quando não há nenhum — o resultado de uma busca que pode
  achar vários (`Strings.positions`, `Regex.find_all`), os filhos de um nó (`list<No> filhos`).
- **Pode não existir**: perguntar antes e só então pegar (`d.has(k)`, `l.has(i)`,
  `Strings.contains`); onde um padrão é comum, `get_or(..., padrao)`, com o padrão escrito por
  quem chama. Pegar o que não existe lança um erro (`KeyError`, `IndexError`, `ValueError`):
  nada devolve um valor que finge ser resposta, como um `-1`.
- **Não deveria faltar**: lançar um erro próprio (seção 4.10) e tratá-lo onde fizer sentido.
- **Um "nenhum" que se comporta**: um objeto de uma classe que cumpre a mesma interface e não
  faz nada (seção 3.7) — `SemDesconto` abaixo —, em vez de conferir a ausência a cada uso.

Uma classe que cria a si mesma nos valores iniciais dos campos é erro (seção 4.6): o "próximo"
que pode não existir é uma `list`.

```cinza
import Strings as st;

error SemCadastro;

interface Desconto {
  fn aplica(f64 v) -> f64;
}

class Metade : Desconto {
  pub { fn aplica(f64 v) -> f64 { return v / 2.ff; } }
}

class SemDesconto : Desconto {
  pub { fn aplica(f64 v) -> f64 { return v; } }
}

fn desconto(string cupom) -> Desconto {
  if (cupom == "METADE") { return new Metade(); }
  return new SemDesconto();
}

fn idade(dict<string, i32> idades, string nome) -> i32 {
  if (not idades.has(nome)) { throw SemCadastro(nome + " não tem cadastro"); }
  return idades[nome];
}

fn main() {
  string s = "chave=valor";
  if (st.contains(s, "=")) { print(st.left(s, st.find(s, "="))); }
  print(st.positions(s, "x"), st.positions(s, "a"));
  print(desconto("METADE").aplica(50.ff), desconto("").aplica(50.ff));
  dict<string, i32> idades = {{"ana", 30}};
  try { print(idade(idades, "bia")); } except (SemCadastro e) { print(e.message); }
}
// expect: chave
// expect: [] [2, 7]
// expect: 25 50
// expect: bia não tem cadastro
```
