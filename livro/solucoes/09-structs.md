# Soluções do capítulo 9

[Voltar ao capítulo](../09-structs.md)

## Exercício 1

```cinza
struct Fracao {
  i64 num;
  i64 den = 1;
}

fn mdc(i64 a, i64 b) -> i64 {
  if (b == 0) {
    return a;
  }
  return mdc(b, a % b);
}

fn soma(Fracao a, Fracao b) -> Fracao {
  i64 num = a.num * b.den + b.num * a.den;
  i64 den = a.den * b.den;
  i64 g = mdc(num, den);
  return new Fracao(num / g, den / g);
}

fn main() {
  print(soma(new Fracao(1, 2), new Fracao(1, 3)));
  print(soma(new Fracao(1, 4), new Fracao(1, 4)));
  print(soma(new Fracao(num: 2), new Fracao(1, 2)));
}
```

```saida
Fracao{num: 5, den: 6}
Fracao{num: 1, den: 2}
Fracao{num: 5, den: 2}
```

`new Fracao(num: 2)` usa o valor inicial do denominador: é o inteiro 2.

## Exercício 2

```cinza
struct C {
  i32 n = 0;
  list<i32> l;
}

fn main() {
  C a = new C();
  C b = a;
  b.n = 1;
  b.l.add(1);
  print(a, b);
}
```

```saida
C{n: 0, l: [1]} C{n: 1, l: [1]}
```

`b = a` copia os campos: `b.n` é independente de `a.n`, mas `b.l` é a **mesma** lista que `a.l` (a
cópia é rasa). O `1` acrescentado aparece nos dois.

## Exercício 3

Na própria lista, pelo índice; na versão que copia, `Lists.copy` faz uma lista nova cujos structs
são cópias (structs são valores), e a original fica intocada:

```cinza
import Lists;

struct Produto {
  string nome;
  f64 preco;
}

fn com_desconto(list<Produto> l, f64 pct) {
  for (i32 i in range(0, l.size())) {
    l[i].preco *= 1.ff - pct / 100.ff;
  }
}

fn com_desconto_copia(list<Produto> l, f64 pct) -> list<Produto> {
  list<Produto> r = Lists.copy(l);
  com_desconto(r, pct);
  return r;
}

fn main() {
  list<Produto> l = [new Produto("café", 10.ff), new Produto("bolo", 25.ff)];
  list<Produto> promocao = com_desconto_copia(l, 10.ff);
  print(l);
  print(promocao);
  com_desconto(l, 20.ff);
  print(l);
}
```

```saida
[Produto{nome: "café", preco: 10}, Produto{nome: "bolo", preco: 25}]
[Produto{nome: "café", preco: 9}, Produto{nome: "bolo", preco: 22.5}]
[Produto{nome: "café", preco: 8}, Produto{nome: "bolo", preco: 20}]
```

## Exercício 4

```cinza
struct No {
  i32 valor = 0;
  list<No> filhos;
}

fn altura(No n) -> i32 {
  i32 maior = 0;
  for (No f in n.filhos) {
    i32 h = altura(f);
    if (h > maior) {
      maior = h;
    }
  }
  return 1 + maior;
}

fn main() {
  No folha = new No(valor: 3);
  No meio = new No(valor: 2);
  meio.filhos.add(folha);
  No raiz = new No(valor: 1);
  raiz.filhos.add(meio);
  raiz.filhos.add(new No(valor: 4));
  print(altura(folha), altura(meio), altura(raiz));
}
```

```saida
1 2 3
```

Um detalhe da cópia rasa (seção 9.4): `raiz.filhos.add(meio)` guarda uma **cópia** de `meio`, mas a
lista `filhos` dessa cópia é a mesma de `meio`. Por isso a ordem da montagem não importa para os
filhos — uma folha acrescentada a `meio` depois apareceria também dentro de `raiz`. Já um campo
simples, como `valor`, mudado em `meio` depois do `add`, não chegaria à cópia:

```cinza
struct No {
  i32 valor = 0;
  list<No> filhos;
}

fn main() {
  No meio = new No(valor: 2);
  No raiz = new No(valor: 1);
  raiz.filhos.add(meio);
  meio.filhos.add(new No(valor: 3));
  meio.valor = 20;
  print(raiz);
}
```

```saida
No{valor: 1, filhos: [No{valor: 2, filhos: [No{valor: 3, filhos: []}]}]}
```

O `3` chegou (a lista é compartilhada); o `20`, não (o campo foi copiado).

## Exercício 5

Dois intervalos fechados têm um número em comum quando cada um começa antes de o outro terminar:

```cinza
struct Intervalo {
  i32 ini;
  i32 fim;
}

fn sobrepoe(Intervalo a, Intervalo b) -> bool {
  return a.ini <= b.fim and b.ini <= a.fim;
}

fn main() {
  print(sobrepoe(new Intervalo(1, 5), new Intervalo(5, 9)));
  print(sobrepoe(new Intervalo(1, 4), new Intervalo(5, 9)));
  print(sobrepoe(new Intervalo(3, 4), new Intervalo(1, 9)));
}
```

```saida
true
false
true
```
