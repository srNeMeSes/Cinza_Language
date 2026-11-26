# Soluções do capítulo 5

[Voltar ao capítulo](../05-funcoes.md)

## Exercício 1

```cinza
fn maior(i32 a, i32 b) -> i32 {
  if (a > b) {
    return a;
  }
  return b;
}

fn maior_de_tres(i32 a, i32 b, i32 c) -> i32 {
  return maior(maior(a, b), c);
}

fn main() {
  print(maior_de_tres(3, 9, 5), maior_de_tres(9, 3, 5), maior_de_tres(4, 4, 4));
}
```

```saida
9 9 4
```

## Exercício 2

A versão recursiva é a definição; a do laço guarda só os dois últimos valores.

```cinza
fn fib_recursivo(i32 n) -> i64 {
  if (n < 2) {
    return n;
  }
  return fib_recursivo(n - 1) + fib_recursivo(n - 2);
}

fn fib_laco(i32 n) -> i64 {
  i64 anterior = 0;
  i64 atual = 1;
  for (i32 i in range(0, n)) {
    i64 proximo = anterior + atual;
    anterior = atual;
    atual = proximo;
  }
  return anterior;
}

fn main() {
  for (i32 n in range(0, 11)) {
    print(n, fib_recursivo(n), fib_laco(n));
  }
}
```

```saida
0 0 0
1 1 1
2 1 1
3 2 2
4 3 3
5 5 5
6 8 8
7 13 13
8 21 21
9 34 34
10 55 55
```

As duas dão o mesmo resultado, mas não com o mesmo trabalho: a recursiva recalcula os mesmos
valores muitas vezes (`fib_recursivo(30)` faz mais de um milhão de chamadas), e a do laço dá `n`
voltas. A recursão também não passa da profundidade `n`, então o limite de 2000 não é o problema
aqui — o tempo é.

## Exercício 3

```cinza potencia.cinza
fn potencia(i64 base, i32 expoente) -> i64 {
  if (expoente == 0) {
    return 1;
  }
  return base * potencia(base, expoente - 1);
}

fn main() {
  print(potencia(2, 10), potencia(3, 4));
  print(potencia(2, 63));
}
```

```saida
1024 81
```

```erro
potencia.cinza:5:15: OverflowError: 2 * 4611686018427387904 excede a faixa de i64 (-9223372036854775808 a 9223372036854775807)
    em potencia (potencia.cinza:5)
```

2⁶³ é uma unidade a mais que o maior `i64` (2⁶³ − 1): a última multiplicação estoura, e o erro diz
qual foi. O stack trace mostra as 10 chamadas mais internas de `potencia`, a linha das omitidas e
a `main`.

## Exercício 4

`n` é uma cópia de `x` (seção 5.3): `incrementa` muda a cópia, e `x` continua `1`. A função precisa
devolver o valor novo, e quem chama o guarda:

```cinza
fn incrementa(i32 n) -> i32 {
  return n + 1;
}
fn main() {
  i32 x = 1;
  x = incrementa(x);
  print(x);
}
```

```saida
2
```

## Exercício 5

```cinza
fn mdc(i64 a, i64 b) -> i64 {
  if (b == 0) {
    return a;
  }
  return mdc(b, a % b);
}

fn main() {
  print(mdc(48, 18), mdc(17, 5), mdc(0, 9));
}
```

```saida
6 1 9
```

Com `(0, 9)`, a primeira chamada vira `mdc(9, 0 % 9)`, que é `mdc(9, 0)`: `9`.
