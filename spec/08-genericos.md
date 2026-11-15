# 8. Genéricos

Uma **função genérica** declara parâmetros de tipo entre colchetes, depois do nome:
`fn maior[T](T a, T b) -> T`. Cada combinação de tipos com que ela é usada gera uma cópia comum
da função para esses tipos (**monomorfização**): `maior[i32]` e `maior[string]` são duas funções,
conferidas e compiladas como se tivessem sido escritas à mão. Na execução não há custo nem
conferência de tipo a mais.

Os colchetes são a sintaxe das funções genéricas, e só delas: na declaração trazem apenas os
nomes dos parâmetros de tipo; na chamada, apenas os tipos. Os tipos parametrizados embutidos
continuam com `<>` (`list<T>`, `dict<K, V>`, `pair<A, B>`, `cast<T>(...)`).

Nesta versão só funções do nível superior podem ser genéricas.

## 8.1 Declaração

```
fn nome[T, U](parâmetros) -> tipo { ... }
```

- Os parâmetros de tipo são identificadores (seção 1.3), pelo menos um, sem repetir. Nenhum pode
  ter o nome de um tipo embutido ou de um tipo declarado no módulo (classe, struct, enum,
  interface, erro).
- Dentro da função — parâmetros, tipo de retorno e corpo — um parâmetro de tipo vale em qualquer
  lugar onde se escreve um tipo: `T x`, `list<T>`, `dict<string, T>`, `pair<T, U>`, `cast<T>(...)`,
  `type(x) == T`.
- Métodos, construtores, assinaturas de interface, `struct`, `class`, `enum` e `error` não aceitam
  parâmetros de tipo. A `main` não pode ser genérica.
- Continua não havendo sobrecarga (seção 4.4): uma função genérica e outro item do nível superior
  com o mesmo nome são erro.
- Uma função genérica pode ser `pub` e usada por outros módulos (seção 4.11). Os nomes do corpo
  são resolvidos no módulo onde ela foi declarada; os tipos vêm de quem a usa.

```cinza
fn maior[T](T a, T b) -> T {
  if (a > b) { return a; }
  return b;
}
fn main() {
  print(maior(3, 7), maior("ana", "bia"), maior(2.5f, 1.f));
}
// expect: 7 bia 2.5
```

```cinza
struct Ponto { i32 x = 0; }
fn f[Ponto](Ponto p) {}
fn main() {}
// expect-error: o parâmetro de tipo 'Ponto' tem o nome de um tipo
```

```cinza
struct Par[A, B] { A primeiro; B segundo; }
fn main() {}
// expect-error: struct não aceita parâmetros de tipo
```

```cinza
class Caixa {
  pub {
    fn guarda[T](T x) {}
  }
}
fn main() {}
// expect-error: Métodos não podem ser genéricos
```

## 8.2 Conferência no uso

O corpo de uma função genérica é analisado **só quando ela é usada**, uma vez para cada
combinação distinta de tipos, com cada parâmetro de tipo trocado pelo tipo concreto. Em cada
cópia, `T` se comporta exatamente como esse tipo: aceita os campos, métodos, operadores e
conversões que ele tiver, e é erro onde ele daria erro. Não é preciso declarar o que `T` precisa
ter.

```cinza
struct Conta { string dono = ""; f64 saldo = 0.ff; }
struct Cliente { string dono = ""; i32 idade = 0; }
fn dono_de[T](T x) -> string { return x.dono; }
fn main() {
  print(dono_de(new Conta("ana", 10.ff)), dono_de(new Cliente("bia", 30)));
}
// expect: ana bia
```

```cinza
fn dono_de[T](T x) -> string { return x.dono; }
fn main() {
  print(dono_de(5));
}
// expect-error: Tipo 'i32' não possui campos
// expect-error:     em dono_de[i32], usada em
```

Uma função genérica **que nunca é usada só passa pela análise sintática**: um erro de tipo no seu
corpo só aparece quando alguém a usa. Erros de sintaxe e as regras da declaração (seção 8.1) são
sempre conferidos.

```cinza
fn nunca_usada[T](T x) -> i32 { return x.campo_que_nao_existe; }
fn main() { print("ok"); }
// expect: ok
```

Literais seguem as regras de sempre para o tipo concreto. `T z = 0;` vale com `T = i32` e com
`T = i64` (o literal se adapta), mas é erro com `T = f64`; para um valor que sirva a qualquer
número, use `cast<T>(...)`:

```cinza
fn soma[T](list<T> l) -> T {
  T total = cast<T>(0);
  for (T x in l) { total += x; }
  return total;
}
fn main() {
  print(soma([1, 2, 3]), soma([1.5ff, 2.ff]));
}
// expect: 6 3.5
```

```cinza
fn zero[T](T exemplo) -> T {
  T z = 0;
  return z;
}
fn main() { print(zero(5), zero(1.5ff)); }
// expect-error: em zero[f64], usada em
```

O mesmo vale para a sintaxe: `new T()` vale com `T` struct ou classe, mas com `T = i32` o corpo
da cópia (`new i32()`) não é válido, e o erro aponta o lugar no corpo, com o rastro dos usos
(seção 8.4).

Os parâmetros seguem a seção 5.1 com o tipo concreto: com `T` struct ou primitivo, o parâmetro é
uma cópia; com `T` list, dict ou objeto, é compartilhado. `type(x) == T` compara com o tipo
concreto, mas não estreita nada: todo o corpo precisa valer para o tipo da cópia.

## 8.3 Chamada: inferência e tipos explícitos

Numa chamada sem tipos, `f(argumentos)`, cada parâmetro de tipo é **deduzido dos argumentos**: dos
parâmetros em que ele aparece, sozinho (`T`) ou dentro de `list<T>`, `dict<K, V>` e `pair<A, B>`.

- Se `T` aparece em mais de um argumento, os tipos precisam combinar. Números da mesma família
  combinam no mais largo (seção 3.1): um literal se adapta e um `i32` é promovido a `i64`. Fora
  disso o tipo precisa ser o mesmo; dentro de coleções, sempre o mesmo (invariância, seção 3.5).
- O tipo de retorno e o tipo da variável que recebe o resultado não participam da dedução.

Com tipos explícitos, `f[i32, string](argumentos)`, informam-se **todos** os parâmetros de tipo, na
ordem da declaração. Os argumentos seguem então as regras normais para a assinatura concreta
(promoção, adaptação de literais). Os tipos explícitos são **obrigatórios** quando algum parâmetro
de tipo não pode ser deduzido, porque não aparece nos parâmetros da função. Qualquer tipo vale
como argumento de tipo, exceto `void` e `var`.

```cinza
fn maior[T](T a, T b) -> T {
  if (a > b) { return a; }
  return b;
}
fn main() {
  i64 grande = 5000000000;
  var m = maior(1, grande);
  print(m, type(m), maior[f64](1.5ff, 2.ff));
}
// expect: 5000000000 i64 2
```

```cinza
struct A { i32 x = 0; }
fn junta[T, U, K](T a, dict<string, U> d, list<K> l) -> string {
  return "" + a + " " + d.size() + " " + l.size();
}
fn main() {
  dict<string, A> d = {{"a", new A()}};
  print(junta[f64, A, bool](1.5ff, d, [true, false]), junta(1, d, ["x"]));
}
// expect: 1.5 1 2 1 1 1
```

```cinza
fn vazia[T]() -> list<T> {
  list<T> l;
  return l;
}
fn main() {
  list<string> nomes = vazia[string]();
  nomes.add("ana");
  print(nomes);
}
// expect: ["ana"]
```

```cinza
fn vazia[T]() -> list<T> {
  list<T> l;
  return l;
}
fn main() { list<i32> l = vazia(); }
// expect-error: Não é possível deduzir o tipo 'T' de 'vazia'
```

```cinza
fn maior[T](T a, T b) -> T {
  if (a > b) { return a; }
  return b;
}
fn main() { print(maior(1, "dois")); }
// expect-error: tipos conflitantes para 'T' em 'maior': 'i32' e 'string'
```

Funções de módulos também aceitam tipos explícitos: `u.maior[i32](a, b)`.

**Leitura dos colchetes.** Depois do nome de uma função (ou de `apelido.nome`), `[...]` seguido
imediatamente de `(` é uma chamada genérica, e o conteúdo dos colchetes é lido como tipos. Isso
não tem ambiguidade: funções não são valores (seção 4.4), então o resultado de um índice nunca
pode ser chamado e `x[i](...)` não teria outro significado. Em qualquer outro caso, `[...]` é
índice ou fatia, como sempre. Se o conteúdo não for uma lista de tipos (`l[0](2)`), o erro diz
que o resultado de um índice não pode ser chamado; se for, mas o nome não for de uma função
genérica (`dobro[i32](2)`), o erro diz que ela não é genérica.

```cinza
fn main() {
  list<i32> l = [1];
  print(l[0](2));
}
// expect-error: O resultado de um índice não pode ser chamado
```

## 8.4 Erros com rastro

Um erro no corpo de uma cópia é relatado na posição dentro do corpo, seguido de uma linha por uso,
da cópia mais interna para fora (no formato da seção 7.3):

```
prog.cinza:1:39: SemanticError: Tipo 'i32' não possui campos
    em dono_de[i32], usada em prog.cinza:5
    em mostra[i32], usada em prog.cinza:9
```

Cada combinação de tipos é analisada uma vez, então o erro aparece uma vez, com o primeiro uso.
Cadeias longas seguem a regra do stack trace: as 10 mais internas, uma linha
`... (N usos omitidos)` e a mais externa.

Uma cópia pode gerar outras (uma genérica que chama outra, ou a si mesma com outros tipos). Mais
de **64** cópias encadeadas é erro de compilação. O caso típico é uma recursão que faz o tipo
crescer, o que nunca termina na compilação, qualquer que seja o valor na execução:

```cinza
fn cresce[T](T x, i32 n) -> i32 {
  if (n == 0) { return 0; }
  list<T> l = [x];
  return cresce(l, n - 1);
}
fn main() { print(cresce(1, 3)); }
// expect-error: Instanciações genéricas encadeadas demais (mais de 64)
```

A recursão com os mesmos tipos usa a mesma cópia e não tem limite além do da execução (seção 5.8).

## 8.5 Execução

Cada cópia é uma função comum. No stack trace ela aparece com os tipos: `em primeiro[string]`.
Genéricos nunca falham por tipo na execução: toda conferência acontece na compilação.

```cinza
fn primeiro[T](list<T> l) -> T { return l[0]; }
fn main() {
  list<string> l;
  print(primeiro(l));
}
// expect-error: IndexError
// expect-error:     em primeiro[string] (
```
