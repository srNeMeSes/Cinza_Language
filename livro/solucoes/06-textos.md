# Soluções do capítulo 6

[Voltar ao capítulo](../06-textos.md)

## Exercício 1

Primeiro, o texto normalizado (minúsculas, sem espaços); depois, a comparação com ele invertido.

```cinza
import Strings as st;

fn palindromo(string s) -> bool {
  string limpo = st.remove(st.lower(s), " ");
  return limpo == limpo[::-1];
}

fn main() {
  print(palindromo("Socorram me subi no onibus em Marrocos"), palindromo("ovo"), palindromo("Cinza"));
}
```

```saida
true true false
```

## Exercício 2

Cada volta do `for` é um caractere (uma `string` de tamanho 1), e `contains` procura ele no texto
das vogais:

```cinza
import Strings as st;

fn vogais(string s) -> i32 {
  i32 n = 0;
  for (string c in s) {
    if (st.contains("aeiouAEIOU", c)) {
      n += 1;
    }
  }
  return n;
}

fn main() {
  print(vogais("Cinza é uma linguagem"), vogais("ritmo"), vogais(""));
}
```

```saida
8 2 0
```

O `é` não conta: ele não está em `"aeiouAEIOU"`. Para contar as acentuadas também, basta pô-las no
texto das vogais.

## Exercício 3

`words` separa as palavras; de cada uma, o primeiro caractere em maiúscula e um ponto; `join` junta
com espaços.

```cinza
import Strings as st;

fn iniciais(string nome) -> string {
  list<string> partes;
  for (string palavra in st.words(nome)) {
    partes.add(st.upper(palavra[0]) + ".");
  }
  return st.join(partes, " ");
}

fn main() {
  print(iniciais("joão da silva"));
  print(iniciais("  Ana  Maria "));
}
```

```saida
J. D. S.
A. M.
```

## Exercício 4

```cinza
import Strings as st;

fn mascara(string cartao) -> string {
  return st.repeat("*", cartao.size() - 4) + st.right(cartao, 4);
}

fn main() {
  print(mascara("1234567812345678"));
}
```

```saida
************5678
```

Com um texto de menos de quatro caracteres, `cartao.size() - 4` é negativo, e `repeat` lança
`ValueError` — o que é melhor do que devolver o número sem máscara nenhuma.

## Exercício 5

Uma expressão entre chaves pode ser uma conta (`{7 * i:3}`) ou até um literal (`{7:2}`):

```cinza
fn main() {
  for (i32 i in range(1, 11)) {
    printf("{7:2} x {i:2} = {7 * i:3}");
  }
}
```

```saida
 7 x  1 =   7
 7 x  2 =  14
 7 x  3 =  21
 7 x  4 =  28
 7 x  5 =  35
 7 x  6 =  42
 7 x  7 =  49
 7 x  8 =  56
 7 x  9 =  63
 7 x 10 =  70
```
