# Soluções do capítulo 11

[Voltar ao capítulo](../11-opcional.md)

## Exercício 1

```cinza
fn primeiro_maior(list<i32> l, i32 limite) -> i32? {
  for (i32 x in l) {
    if (x > limite) {
      return x;
    }
  }
  return null;
}

fn main() {
  list<i32> l = [3, 8, 2, 12];
  print(primeiro_maior(l, 5) else -1, primeiro_maior(l, 20) else -1);
  if (i32 x = primeiro_maior(l, 10)) {
    print("o primeiro maior que 10 é", x);
  } else {
    print("nenhum maior que 10");
  }
}
```

```saida
8 -1
o primeiro maior que 10 é 12
```

## Exercício 2

Duas guardas: a linha sem `=` é pulada pelo `else { continue; }` do `find`, e a de chave vazia pelo
`if`:

```cinza
import Strings;

fn main() {
  list<string> linhas = ["nome=Ana", "idade", "cidade=Recife", "=vazio"];
  dict<string, string> config;
  for (string linha in linhas) {
    i32 pos = Strings.find(linha, "=") else { continue; };
    if (pos == 0) {
      continue;
    }
    config.add({linha[:pos], linha[pos + 1:]});
  }
  print(config);
}
```

```saida
{"nome": "Ana", "cidade": "Recife"}
```

## Exercício 3

`input` devolve `null` no fim da entrada, e o `while` com declaração para ali; `Convert.to_i32` devolve
`null` para o que não é número:

<!-- stdin: 10 -->
<!-- stdin: dez -->
<!-- stdin: 5 -->
```cinza soma.cinza
import Convert;

fn main() {
  i32 soma = 0;
  while (string linha = input("")) {
    if (i32 n = Convert.to_i32(linha)) {
      soma += n;
    } else {
      print("não é número:", linha);
    }
  }
  print("soma:", soma);
}
```

```console
$ printf "10\ndez\n5\n" | cinza soma.cinza
```

```saida
não é número: dez
soma: 15
```

## Exercício 4

```cinza
struct Pessoa {
  string nome;
  string? apelido;
}

fn como_chamar(Pessoa p) -> string {
  return p.apelido else p.nome;
}

fn main() {
  print(como_chamar(new Pessoa(nome: "Beatriz", apelido: "Bia")), como_chamar(new Pessoa(nome: "Caio")));
}
```

```saida
Bia Caio
```

## Exercício 5

Não há estreitamento: depois de `nome != null`, `nome` continua `string?`, e `nome[0]` exige um
`string`. Com o `if` com declaração, o valor ganha um nome do tipo certo:

```cinza
fn iniciais(string? nome) -> string {
  if (string n = nome) {
    return n[0];
  }
  return "?";
}

fn main() {
  print(iniciais("Ana"), iniciais(null));
}
```

```saida
A ?
```

Com um `else` de guarda, que sai da função quando falta o valor:

```cinza
fn iniciais(string? nome) -> string {
  string n = nome else { return "?"; };
  return n[0];
}

fn main() {
  print(iniciais("Ana"), iniciais(null));
}
```

```saida
A ?
```

(As duas versões ainda falham com um texto vazio, `""`: `n[0]` não existe. Um programa de verdade
testaria `n.size() > 0` também.)
