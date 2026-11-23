# 8. Genéricos

Uma **função genérica** declara parâmetros de tipo entre colchetes, depois do nome:
`fn maior[T](T a, T b) -> T`. Cada combinação de tipos com que ela é usada gera uma cópia comum
da função para esses tipos (**monomorfização**): `maior[i32]` e `maior[string]` são duas funções,
conferidas e compiladas como se tivessem sido escritas à mão. Na execução não há custo nem
conferência de tipo a mais.

Os colchetes são a sintaxe dos genéricos declarados no programa: na declaração trazem apenas os
nomes dos parâmetros de tipo; no uso, apenas os tipos. Os tipos parametrizados embutidos
continuam com `<>` (`list<T>`, `dict<K, V>`, `pair<A, B>`, `cast<T>(...)`).

Nesta versão podem ser genéricas as funções do nível superior (seções 8.1 a 8.5) e os structs
(seção 8.6).

## 8.1 Declaração

```
fn nome[T, U](parâmetros) -> tipo { ... }
```

- Os parâmetros de tipo são identificadores (seção 1.3), pelo menos um, sem repetir. Nenhum pode
  ter o nome de um tipo embutido ou de um tipo declarado no módulo (classe, struct, enum,
  interface, erro).
- Dentro da função — parâmetros, tipo de retorno e corpo — um parâmetro de tipo vale em qualquer
  lugar onde se escreve um tipo: `T x`, `list<T>`, `dict<string, T>`, `pair<T, U>`, `cast<T>(...)`,
  `type(x) == T`, `new T()`, `except (T e)`, inclusive nas expressões `{...}` de `printf` e `format`.
  Depois de `.` o nome é de membro, não o parâmetro de tipo (`x.T` é o campo `T`).
- Métodos, construtores, assinaturas de interface, `class`, `enum` e `error` não aceitam
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
class Caixa[T] { list<T> itens; }
fn main() {}
// expect-error: class não aceita parâmetros de tipo
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

## 8.6 Structs genéricos

```
struct Nome[T, U] { campos }
```

Um struct também pode declarar parâmetros de tipo, com as mesmas regras da seção 8.1. Nos
campos, um parâmetro de tipo vale onde se escreve um tipo: `T valor;`, `list<T>`,
`dict<string, T>`, `pair<T, U>`, outro struct genérico (`Caixa[T]`).

Um campo do tipo `T` não pode ter valor inicial, porque não há valor que sirva a todo `T`: ele é
**obrigatório** e quem cria o struct o informa (seção 4.7). Um campo `T?` nasce `null` e não é
obrigatório (seção 3.10). Os demais campos seguem a seção 4.7 normalmente. Como não há `T??`, um
`T?` numa genérica com `T` já opcional (`Caixa[i32?]` com um campo `T?`) é erro na cópia, com o
rastro (seção 8.4).

**No uso, os tipos são sempre escritos**, sem inferência: `Par[string, i32] p`,
`new Par[string, i32](...)`, `list<Par[string, i32]>`, `fn f(Caixa[i32] c)`. `Par` sozinho, sem os
tipos, é erro. Os tipos vão na ordem dos parâmetros de tipo, todos, e qualquer tipo de valor vale
como argumento (não `void` nem `var`).

```cinza
struct Ponto { i32 x = 0; i32 y = 0; }
struct A[T] {
  string nm = "nome";
  T ct;
  i32 valor = 1;
}
fn main() {
  Ponto ponto = new Ponto(1, 2);
  A[Ponto] a = new A[Ponto](ct: ponto);
  A[i32] b = new A[i32]("outro", 7, 2);
  print(a);
  print(b, type(b));
}
// expect: A[Ponto]{nm: "nome", ct: Ponto{x: 1, y: 2}, valor: 1}
// expect: A[i32]{nm: "outro", ct: 7, valor: 2} A[i32]
```

```cinza
struct Caixa[T] { T valor; }
fn main() {
  Caixa c = new Caixa(valor: 1);
}
// expect-error: o struct genérico 'Caixa' precisa dos tipos
```

```cinza
struct Caixa[T] { T valor = 0; }
fn main() {}
// expect-error: o campo 'valor' é do tipo 'T' e não pode ter valor inicial
```

Cada combinação de tipos usada vira um **struct comum** (monomorfização), conferido no uso como
na seção 8.2: um struct genérico que nunca é usado só passa pela análise sintática e pelas regras
da declaração. Um erro numa cópia vem com o rastro dos usos (seção 8.4). `Par[string, i32]` e
`Par[string, i64]` são tipos diferentes, e nenhum se atribui ao outro (invariância, seção 3.5).
No `print` e no `type()`, o nome aparece com os tipos, e o tipo também se escreve como valor
(`type(p) == Par[string, i32]`).

```cinza
struct Indice[K] { dict<K, i32> posicoes; }
fn main() {
  Indice[string] ok = new Indice[string]();
  Indice[list<i32>] ruim = new Indice[list<i32>]();
}
// expect-error: Chave de dict deve ser i32, i64, string, bool ou enum
// expect-error:     em Indice[list<i32>], usada em
```

Uma função genérica pode receber structs genéricos. Na dedução (seção 8.3), os tipos dentro de
`Par[A, B]` precisam ser exatos, como dentro de uma coleção:

```cinza
struct Par[A, B] { A primeiro; B segundo; }
fn troca[A, B](Par[A, B] p) -> Par[B, A] {
  return new Par[B, A](p.segundo, p.primeiro);
}
fn main() {
  Par[string, i32] p = new Par[string, i32](primeiro: "ana", segundo: 30);
  Par[i32, string] q = troca(p);
  print(q);
}
// expect: Par[i32, string]{primeiro: 30, segundo: "ana"}
```

Um struct genérico segue a regra da seção 4.7: não contém a si mesmo por valor, mas pode conter
uma `list` de si mesmo. Um struct que se usa com tipos que só crescem (`list<T>` dentro de
`Cresce[T]` vira `Cresce[list<T>]`) passa do limite de 64 cópias encadeadas da seção 8.4.

```cinza
struct No[T] { T valor; list<No[T]> filhos; }
fn soma(No[i32] n) -> i32 {
  i32 s = n.valor;
  for (No[i32] f in n.filhos) { s += soma(f); }
  return s;
}
fn main() {
  No[i32] raiz = new No[i32](valor: 1);
  raiz.filhos.add(new No[i32](valor: 2));
  raiz.filhos.add(new No[i32](valor: 3));
  print(soma(raiz));
}
// expect: 6
```

```cinza
struct Cresce[T] { list<Cresce[list<T>]> mais; }
fn main() {
  Cresce[i32] c = new Cresce[i32]();
}
// expect-error: Instanciações genéricas encadeadas demais (mais de 64)
```

**Leitura dos colchetes.** `Nome[tipos]` é um tipo onde se espera um tipo (declaração de
variável, campo, parâmetro, retorno, `list<...>`, `cast<...>`) e depois de `new`. No começo de
uma instrução, `Nome[...]` seguido de um nome é uma declaração de variável (`Caixa[i32] c = ...`);
seguido de qualquer outra coisa, é um índice, como sempre (`l[0] = 5;`). Numa expressão,
`Nome[...]` é o tipo escrito como valor quando `Nome` é um struct genérico (e não uma variável
local com esse nome); senão, é índice. Com mais de um tipo, ou um tipo que não é um nome
(`l[i32]`), só pode ser tipo: depois de uma variável, é erro.
