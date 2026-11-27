# Soluções do capítulo 14

[Voltar ao capítulo](../14-erros.md)

## Exercício 1

```cinza
fn divide_seguro(i32 a, i32 b) -> i32? {
  try {
    return a / b;
  } except (ZeroDivisionError e) {
    return null;
  }
}

fn main() {
  print(divide_seguro(10, 3), divide_seguro(1, 0));
}
```

```saida
3 null
```

Testar `b == 0` antes (`if (b == 0) { return null; } return a / b;`) é mais claro: diz a condição
na primeira linha, e não depende de saber qual erro a divisão lança. Capturar o erro vale a pena
quando a condição é difícil de testar antes — um texto que pode não ser número, um arquivo que pode
não abrir.

## Exercício 2

```cinza
import Strings;

error SenhaFraca;

fn tem_digito(string s) -> bool {
  for (string c in s) {
    if (Strings.is_digit(c)) {
      return true;
    }
  }
  return false;
}

fn valida(string senha) {
  if (senha.size() < 8) {
    throw SenhaFraca("tem menos de 8 caracteres");
  }
  if (not tem_digito(senha)) {
    throw SenhaFraca("não tem nenhum dígito");
  }
}

fn main() {
  for (string s in ["abc", "abcdefgh", "abc12345"]) {
    try {
      valida(s);
      print(s, "-> ok");
    } except (SenhaFraca e) {
      print(s, "->", e.message);
    }
  }
}
```

```saida
abc -> tem menos de 8 caracteres
abcdefgh -> não tem nenhum dígito
abc12345 -> ok
```

## Exercício 3

O `finally` roda antes de o valor do `return` chegar a quem chamou; só então o `print` da `main`
recebe o 1:

```cinza
fn f() -> i32 {
  try {
    print("try");
    return 1;
  } finally {
    print("finally");
  }
}

fn main() {
  print("resultado", f());
}
```

```saida
try
finally
resultado 1
```

## Exercício 4

```cinza
fn registra_e_relanca(list<i32> l, i32 i) -> i32 {
  try {
    return l[i];
  } except (Error e) {
    print("erro registrado:", e.kind);
    throw e;
  }
}

fn main() {
  try {
    print(registra_e_relanca([1, 2], 5));
  } except (IndexError e) {
    print("na main:", e.message, "na linha", e.line);
  }
}
```

```saida
erro registrado: IndexError
na main: índice 5 fora dos limites (tamanho: 2) na linha 3
```

A linha é a 3, onde o `l[i]` falhou — o `throw e` preservou a origem.

## Exercício 5

O `try` vai **dentro** do laço: cada conversão que falha é registrada, e o laço segue com a próxima.

```cinza
fn main() {
  list<i32> validos;
  list<string> invalidos;
  for (string s in ["10", "abc", "7", "", "3"]) {
    try {
      validos.add(cast<i32>(s));
    } except (ValueError e) {
      invalidos.add(s);
    }
  }
  print(validos, invalidos);
}
```

```saida
[10, 7, 3] ["abc", ""]
```

(O mesmo sem exceções: `if (i32 n = Convert.to_i32(s)) { validos.add(n); } else { invalidos.add(s); }`
— o caminho do capítulo 11, quando "não ser número" é um caso esperado.)
