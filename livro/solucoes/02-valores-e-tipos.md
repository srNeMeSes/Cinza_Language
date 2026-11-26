# Soluções do capítulo 2

[Voltar ao capítulo](../02-valores-e-tipos.md)

## Exercício 1

```cinza
fn main() {
  i32 pequeno = 42;
  i64 grande = 9000000000;
  f32 simples = 2.5f;
  f64 dupla = 2.5ff;
  string nome = "Cinza";
  bool pronto = true;
  print(pequeno, type(pequeno));
  print(grande, type(grande));
  print(simples, type(simples));
  print(dupla, type(dupla));
  print(nome, type(nome));
  print(pronto, type(pronto));
}
```

```saida
42 i32
9000000000 i64
2.5 f32
2.5 f64
Cinza string
true bool
```

## Exercício 2

```cinza
fn main() {
  print(7 / 2, type(7 / 2));
  print(cast<f64>(7) / 2.ff, type(cast<f64>(7) / 2.ff));
  print(cast<f64>(7 / 2), type(cast<f64>(7 / 2)));
  print(cast<i32>(-7.9ff), type(cast<i32>(-7.9ff)));
  print(1 + cast<i64>(2), type(1 + cast<i64>(2)));
  print(cast<f32>(0.1ff), type(cast<f32>(0.1ff)));
}
```

```saida
3 i32
3.5 f64
3 f64
-7 i32
3 i64
0.1 f32
```

- `7 / 2` é divisão inteira: os dois lados são `i32`, o resultado também (o capítulo 3 mostra que
  ela trunca em direção ao zero).
- `cast<f64>(7 / 2)` converte **depois** de dividir: a divisão inteira já deu `3`.
- `cast<i32>(-7.9ff)` corta as casas em direção ao zero: `-7`, não `-8`.
- `1 + cast<i64>(2)`: `i32 + i64` alarga para `i64`.
- `cast<f32>(0.1ff)` é o `f32` mais próximo de 0,1, que o `print` mostra como `0.1` — o menor
  número de dígitos que volta a ele **em `f32`**.

## Exercício 3

`x + 1` é uma conta entre dois `i32`, e o resultado, 2147483648, não cabe em `i32`: a Cinza para
com `OverflowError` em vez de voltar ao começo da faixa. Com `x` em `i64` a conta cabe; o valor
inicial continua o mesmo, porque um literal `i32` cabe num `i64`:

```cinza
fn main() {
  i64 x = 2147483647;
  x = x + 1;
  print(x);
}
```

```saida
2147483648
```

## Exercício 4

A fórmula precisa ser toda em `f64`: `9 / 5` entre inteiros daria `1`, e um literal inteiro não
se mistura com `f64`. Daí `9.ff / 5.ff` e `32.ff`:

<!-- args: 37 -->
```cinza temperatura.cinza
// temperatura.cinza: cinza temperatura.cinza <graus Celsius>

fn main(list<string> args) {
  if (args.size() != 1) {
    print("Uso: cinza temperatura.cinza <graus Celsius>");
    exit(2);
  }
  f64 celsius = cast<f64>(args[0]);
  f64 fahrenheit = celsius * 9.ff / 5.ff + 32.ff;
  print(celsius, "°C =", fahrenheit, "°F");
}
```

```console
$ cinza temperatura.cinza 37
```

```saida
37 °C = 98.6 °F
```

Nem toda conta sai tão limpa: com 36,6 °C, por exemplo, o resultado tem os dígitos do
arredondamento binário. Para mostrar sempre com uma casa decimal, o capítulo 6 apresenta o
`printf("{fahrenheit:.1f}")`.
