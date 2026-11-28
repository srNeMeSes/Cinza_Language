# Soluções do capítulo 16

[Voltar ao capítulo](../16-genericos-funcoes.md)

## Exercício 1

```cinza
fn troca[A, B](pair<A, B> p) -> pair<B, A> {
  return {p.second, p.first};
}

fn main() {
  print(troca({1, "um"}), troca({"x", true}));
}
```

```saida
{"um", 1} {true, "x"}
```

`A` e `B` são deduzidos do par: `troca[i32, string]` e `troca[string, bool]`.

## Exercício 2

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

fn quantas[T](const list<T> l, T x) -> i32 {
  i32 n = 0;
  for (T e in l) {
    if (e == x) {
      n += 1;
    }
  }
  return n;
}

fn main() {
  print(quantas(["a", "b", "a"], "a"), quantas([1, 2, 2, 2], 2));
  list<Ponto> pontos = [new Ponto(1, 2), new Ponto(0, 0), new Ponto(1, 2)];
  print(quantas(pontos, new Ponto(1, 2)));
}
```

```saida
2 3
2
```

O corpo só usa `==`, e um struct tem `==`: ele compara os campos (seção 9.3). A cópia
`quantas[Ponto]` é válida pelo mesmo motivo que `quantas[string]` — o tipo tem o que o corpo usa.

## Exercício 3

```cinza
fn menor_de[T](const list<T> l) -> T {
  T m = l[0];
  for (T x in l) {
    if (x < m) {
      m = x;
    }
  }
  return m;
}

fn main() {
  print(menor_de([5, 2, 8]), menor_de(["pera", "uva", "maçã"]));
}
```

```saida
2 maçã
```

Com uma lista de `bool`, a cópia `menor_de[bool]` não vale, porque `bool` não tem `<` — e isso se
sabe **na compilação**, olhando o tipo:

```cinza bool.cinza
fn menor_de[T](const list<T> l) -> T {
  T m = l[0];
  for (T x in l) {
    if (x < m) {
      m = x;
    }
  }
  return m;
}

fn main() {
  print(menor_de([true, false]));
}
```

```erro
bool.cinza:4:11: SemanticError: Comparação '<' não é suportada entre 'bool' e 'bool'
    em menor_de[bool], usada em bool.cinza:12
```

Com uma lista vazia, a cópia é válida (`menor_de[i32]`), mas `l[0]` falha **na execução**: o
tamanho da lista não é parte do tipo, então o compilador não tem como saber.

```cinza vazia.cinza
fn menor_de[T](const list<T> l) -> T {
  T m = l[0];
  for (T x in l) {
    if (x < m) {
      m = x;
    }
  }
  return m;
}

fn main() {
  list<i32> nada;
  print(menor_de(nada));
}
```

```erro
vazia.cinza:2:10: IndexError: índice 0 fora dos limites (tamanho: 0)
    em menor_de[i32] (vazia.cinza:2)
```

## Exercício 4

```cinza
fn repete[T](T x, i32 n) -> list<T> {
  list<T> l;
  for (i32 i in range(0, n)) {
    l.add(x);
  }
  return l;
}

fn main() {
  print(repete(1, 3), type(repete(1, 3)));
  print(repete[i64](1, 3), type(repete[i64](1, 3)));
}
```

```saida
[1, 1, 1] list<i32>
[1, 1, 1] list<i64>
```

Sem tipos, `T` vem do argumento: o literal `1` é um `i32`. Com `repete[i64]`, o tipo é dado, e o
literal se adapta a ele. (A variável que recebe o resultado não entra na dedução: `list<i64> b =
repete(1, 3);` seria um erro de tipo, `list<i32>` não é `list<i64>`.)

## Exercício 5

```cinza
fn inverte[K, V](const dict<K, V> d) -> dict<V, K> {
  dict<V, K> r;
  for (pair<K, V> p in d) {
    r.add({p.second, p.first});
  }
  return r;
}

fn main() {
  dict<string, i32> codigos = {{"ana", 1}, {"bia", 2}};
  print(inverte(codigos));
}
```

```saida
{1: "ana", 2: "bia"}
```

Com um `dict<string, f64>`, a cópia teria de criar um `dict<f64, string>`, e `f64` não pode ser
chave (seção 8.4). É um erro da cópia, com o rastro:

```cinza notas.cinza
fn inverte[K, V](const dict<K, V> d) -> dict<V, K> {
  dict<V, K> r;
  for (pair<K, V> p in d) {
    r.add({p.second, p.first});
  }
  return r;
}

fn main() {
  dict<string, f64> notas = {{"ana", 9.5ff}};
  print(inverte(notas));
}
```

```erro
notas.cinza:1:41: SemanticError: Chave de dict não pode ser 'f64': comparação exata de ponto flutuante não é confiável. Use i32, i64, string, bool ou enum.
    em inverte[string, f64], usada em notas.cinza:11
```

(Se dois valores forem iguais, o `r.add` do segundo substitui o do primeiro, e uma das chaves some
do resultado. Um `inverte` de verdade precisa decidir o que fazer nesse caso.)
