# Soluções do capítulo 1

[Voltar ao capítulo](../01-comecando.md)

## Exercício 1

O nome e o ano numa linha; a soma na seguinte.

```cinza
fn main() {
  print("Ana", 2026);
  print(1234 + 4321);
}
```

```saida
Ana 2026
5555
```

## Exercício 2

O compilador acha os erros por fase, e cada fase só começa se a anterior terminou sem erros. Por
isso aparece um de cada vez. Primeiro o léxico, que não conhece o `$`:

```cinza tres.cinza
fn main() {
  print("Cinza" $ "!");
  print("versão", 0.6f)
  imprime("fim");
}
```

```erro
tres.cinza:2:17: LexicalError: Caractere inesperado: '$'
```

Trocado o `$` por `+`, a sintaxe aparece — falta o `;` no fim da linha 3:

```cinza tres.cinza
fn main() {
  print("Cinza" + "!");
  print("versão", 0.6f)
  imprime("fim");
}
```

```erro
tres.cinza:3:24: SyntaxError: Esperado ';' após expressão (token: 'imprime')
```

Com o `;`, a análise semântica encontra o nome que não existe (a função embutida é `print`):

```cinza tres.cinza
fn main() {
  print("Cinza" + "!");
  print("versão", 0.6f);
  imprime("fim");
}
```

```erro
tres.cinza:4:3: SemanticError: Função 'imprime' não declarada
```

O programa corrigido:

```cinza tres.cinza
fn main() {
  print("Cinza" + "!");
  print("versão", 0.6f);
  print("fim");
}
```

```saida
Cinza!
versão 0.6
fim
```

## Exercício 3

Sem argumentos, `mundo`; o `return` sai da `main` antes do laço.

```cinza saudacao.cinza
fn saudacao(string nome) -> string {
  return "Olá, " + nome + "!";
}

fn main(list<string> args) {
  if (args.size() == 0) {
    print(saudacao("mundo"));
    return;
  }
  for (string nome in args) {
    print(saudacao(nome));
  }
  print("Saudei", args.size(), "pessoas.");
}
```

```console
$ cinza saudacao.cinza
```

```saida
Olá, mundo!
```

## Exercício 4

O último elemento é o de índice `args.size() - 1` (o capítulo 7 mostra `Lists.last`).

<!-- args: um dois três -->
```cinza extremos.cinza
fn main(list<string> args) {
  print("argumentos:", args.size());
  if (args.size() > 0) {
    print("primeiro:", args[0]);
    print("último:", args[args.size() - 1]);
  }
}
```

```console
$ cinza extremos.cinza um dois três
```

```saida
argumentos: 3
primeiro: um
último: três
```
