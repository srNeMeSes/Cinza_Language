# Soluções do capítulo 8

[Voltar ao capítulo](../08-dicionarios-e-pares.md)

## Exercício 1

```cinza
import Dicts;

fn main() {
  dict<string, string> telefones = {{"Ana", "1111"}, {"Bia", "2222"}};
  dict<string, string> por_numero;
  for (pair<string, string> p in telefones) {
    por_numero.add({p.second, p.first});
  }
  print(por_numero, por_numero == Dicts.invert(telefones));
}
```

```saida
{"1111": "Ana", "2222": "Bia"} true
```

Se dois nomes tivessem o mesmo número, o `for` ficaria com o último (o `add` troca o valor), e o
`Dicts.invert` pararia com `ValueError` — ele não escolhe por você.

## Exercício 2

A lista de cada letra é criada na primeira palavra com ela; nas seguintes, `grupos[letra]` devolve
a mesma lista, que recebe a palavra no lugar:

```cinza
import Strings;

fn main() {
  dict<string, list<string>> grupos;
  for (string palavra in Strings.words("banana abacate cereja amora berinjela caju")) {
    string letra = palavra[0];
    if (grupos.has(letra)) {
      grupos[letra].add(palavra);
    } else {
      grupos.add({letra, [palavra]});
    }
  }
  print(grupos);
}
```

```saida
{"b": ["banana", "berinjela"], "a": ["abacate", "amora"], "c": ["cereja", "caju"]}
```

## Exercício 3

`contagem[p] += 1` lê o valor atual de `contagem[p]` antes de somar — e na primeira vez a chave não
existe: `KeyError`. Com `has`:

```cinza
fn main() {
  dict<string, i32> contagem;
  for (string p in ["a", "b", "a"]) {
    if (contagem.has(p)) {
      contagem[p] += 1;
    } else {
      contagem.add({p, 1});
    }
  }
  print(contagem);
}
```

```saida
{"a": 2, "b": 1}
```

Com `Dicts.get`, numa linha (o valor atual ou 0, mais um, inserido ou trocado pelo `add`):

```cinza
import Dicts;

fn main() {
  dict<string, i32> contagem;
  for (string p in ["a", "b", "a"]) {
    contagem.add({p, (Dicts.get(contagem, p) else 0) + 1});
  }
  print(contagem);
}
```

```saida
{"a": 2, "b": 1}
```

## Exercício 4

```cinza
import Dicts;

fn junta(dict<string, i32> a, dict<string, i32> b) -> dict<string, i32> {
  dict<string, i32> r = Dicts.copy(a);
  for (pair<string, i32> p in b) {
    r.add({p.first, (Dicts.get(r, p.first) else 0) + p.second});
  }
  return r;
}

fn main() {
  dict<string, i32> loja = {{"pera", 3}, {"uva", 2}};
  dict<string, i32> deposito = {{"uva", 10}, {"kiwi", 4}};
  print(junta(loja, deposito), loja);
}
```

```saida
{"pera": 3, "uva": 12, "kiwi": 4} {"pera": 3, "uva": 2}
```

A função começa de uma **cópia** de `a`: sem ela, `junta` alteraria o estoque da loja de quem
chamou (o dicionário é compartilhado).

## Exercício 5

```cinza
fn extremos(list<i32> l) -> pair<i32, i32> {
  i32 menor = l[0];
  i32 maior = l[0];
  for (i32 x in l) {
    if (x < menor) {
      menor = x;
    }
    if (x > maior) {
      maior = x;
    }
  }
  return {menor, maior};
}

fn main() {
  pair<i32, i32> e = extremos([4, -2, 9, 0]);
  print(e, e.first, e.second);
}
```

```saida
{-2, 9} -2 9
```

Com uma lista vazia, `l[0]` lança `IndexError`: não há extremos de uma lista vazia.
