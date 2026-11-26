# Soluções do capítulo 3

[Voltar ao capítulo](../03-operadores.md)

## Exercício 1

```cinza
fn main() {
  print(-7 / 2, -7 % 2, 7 % -2);
  print("1" + 2 + 3, 1 + 2 + "3");
  print(2 + 3 * 4 % 5, not false and false);
}
```

```saida
-3 -1 1
123 33
4 false
```

- `-7 / 2` trunca em direção ao zero: `-3`. O resto tem o sinal do dividendo: `-7 % 2` é `-1` e
  `7 % -2` é `1`.
- `"1" + 2 + 3`: a concatenação vai da esquerda para a direita, `"12"` e depois `"123"`. Em
  `1 + 2 + "3"`, a primeira soma é entre números: `3`, e depois `"33"`.
- `2 + 3 * 4 % 5`: `*` e `%` têm a mesma precedência e associam à esquerda, `(3 * 4) % 5` é `2`;
  depois `2 + 2`.
- `not false and false`: o `not` se liga primeiro, `(not false) and false`, que é `true and false`.

## Exercício 2

Os parênteses não são obrigatórios (o `and` se liga antes do `or`), mas deixam a regra legível:

```cinza
fn bissexto(i32 ano) -> bool {
  return (ano % 4 == 0 and ano % 100 != 0) or ano % 400 == 0;
}

fn main() {
  print(bissexto(1900), bissexto(2000), bissexto(2024), bissexto(2026));
}
```

```saida
false true true false
```

## Exercício 3

`0 <= nota <= 10` não encadeia: compara o `bool` de `0 <= nota` com `10`, e o compilador recusa.
A faixa se escreve com `and`:

```cinza
fn main() {
  i32 nota = 7;
  print(0 <= nota and nota <= 10);
}
```

```saida
true
```

## Exercício 4

`total + x` é `i32 + i64`, que dá `i64`, e um `i64` não cabe num `i32` sem conversão. Um jeito é
alargar o alvo:

```cinza
fn main() {
  i64 total = 0;
  i64 x = 5;
  total += x;
  print(total);
}
```

```saida
5
```

O outro é estreitar o valor, dizendo isso com um `cast` — que lança `OverflowError` se o valor não
couber em `i32`:

```cinza
fn main() {
  i32 total = 0;
  i64 x = 5;
  total += cast<i32>(x);
  print(total);
}
```

```saida
5
```

O primeiro é o melhor quando o total pode crescer; o segundo, quando se sabe que o valor é pequeno.

## Exercício 5

A ordem é da esquerda para a direita: `x`, `y` e `z`. Como `1 * 2 == 2` é `true`, o `or` não
precisa do lado direito, e `w` nunca é avaliado:

```cinza
fn f(string s, i32 n) -> i32 {
  print(s);
  return n;
}
fn main() {
  print(f("x", 1) * f("y", 2) == f("z", 2) or f("w", 0) > 0);
}
```

```saida
x
y
z
true
```
