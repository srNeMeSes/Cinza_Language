# Soluções do capítulo 4

[Voltar ao capítulo](../04-controle-de-fluxo.md)

## Exercício 1

O teste dos dois múltiplos vem primeiro: um número múltiplo de 15 também é de 3, e o `else if`
para no primeiro teste verdadeiro.

```cinza
fn main() {
  for (i32 n in range(1, 16)) {
    if (n % 15 == 0) {
      print("FizzBuzz");
    } else if (n % 3 == 0) {
      print("Fizz");
    } else if (n % 5 == 0) {
      print("Buzz");
    } else {
      print(n);
    }
  }
}
```

```saida
1
2
Fizz
4
Buzz
Fizz
7
8
Fizz
Buzz
11
Fizz
13
14
FizzBuzz
```

## Exercício 2

```cinza
fn soma_digitos(i64 n) -> i64 {
  i64 resto = n;
  i64 soma = 0;
  while (resto > 0) {
    soma += resto % 10;
    resto /= 10;
  }
  return soma;
}

fn main() {
  print(soma_digitos(0), soma_digitos(7), soma_digitos(9876543210));
}
```

```saida
0 7 45
```

Com `0`, o `while` não dá nenhuma volta e a soma fica `0`, que é a resposta certa.

## Exercício 3

O `break` sai do laço interno no primeiro divisor encontrado; `primo` diz se algum apareceu.

```cinza
fn main() {
  for (i32 n in range(2, 30)) {
    bool primo = true;
    for (i32 d in range(2, n)) {
      if (n % d == 0) {
        primo = false;
        break;
      }
    }
    if (primo) {
      print(n);
    }
  }
}
```

```saida
2
3
5
7
11
13
17
19
23
29
```

Com `n` igual a 2, `range(2, 2)` é vazio e o laço interno não dá volta nenhuma: 2 é primo.

## Exercício 4

Quando `a` e `b` são iguais, nenhum dos dois testes vale, e a função chegaria ao fim sem retornar
— o compilador não aceita esse caminho. Como, nesse caso, `a` e `b` são o mesmo valor, basta trocar
o último teste por um `else`:

```cinza
fn maior(i32 a, i32 b) -> i32 {
  if (a > b) {
    return a;
  } else {
    return b;
  }
}

fn main() {
  print(maior(3, 7), maior(7, 3), maior(5, 5));
}
```

```saida
7 7 5
```

## Exercício 5

O `do`/`while` vira um `while (true)` com o teste no fim: a condição do C, `x <= 100`, aparece
invertida no `if` (`x > 100`), porque ela diz quando **sair**.

```cinza
fn main() {
  i32 x = 1;
  while (true) {
    print(x);
    x *= 2;
    if (x > 100) {
      break;
    }
  }
}
```

```saida
1
2
4
8
16
32
64
```
