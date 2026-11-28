# Soluções do capítulo 15

[Voltar ao capítulo](../15-const.md)

## Exercício 1

```cinza
const i64 MINUTO = 60;
const i64 HORA = 60 * MINUTO;
const i64 DIA = 24 * HORA;
const i64 SEMANA = 7 * DIA;

fn main() {
  print(SEMANA);
}
```

```saida
604800
```

Cada constante usa as de cima, e a conta inteira é feita na compilação: o programa só imprime o
número pronto.

## Exercício 2

```cinza
fn media(const list<f64> l) -> f64 {
  f64 s = 0.ff;
  for (f64 x in l) {
    s += x;
  }
  return s / cast<f64>(l.size());
}

fn main() {
  list<f64> notas = [7.ff, 8.ff];
  print(media(notas));
  notas.add(9.ff);
  print(media(notas));
  const list<f64> fixas = [10.ff, 5.ff];
  print(media(fixas));
}
```

```saida
7.5
8
7.5
```

O `const` do parâmetro é uma promessa de `media`, não uma exigência sobre quem chama: uma lista
comum entra (e continua comum em `main`, que a altera entre as chamadas), e uma lista const também
entra — const vai para const.

## Exercício 3

`notas` é const, e o parâmetro de `mostra_ordenada` não é: a função poderia alterar a lista (e
altera, com o `Lists.sort`).

```cinza notas.cinza
import Lists;

fn mostra_ordenada(list<i32> l) {
  Lists.sort(l);
  print(l);
}

fn main() {
  const list<i32> notas = [7, 3, 9];
  mostra_ordenada(notas);
  print(notas);
}
```

```erro
notas.cinza:10:3: SemanticError: Argumento 1 de 'mostra_ordenada': 'notas', que é const, e o parâmetro 'l' não é: a função ficaria com a mesma lista e poderia alterar 'notas'. Declare o parâmetro const, ou passe uma cópia: Lists.copy(notas)
```

Mudando só a função: o parâmetro vira const, e a função ordena uma lista nova, com `Lists.sorted`
(com o `Lists.sort`, o erro passaria a ser dentro dela):

```cinza
import Lists;

fn mostra_ordenada(const list<i32> l) {
  print(Lists.sorted(l));
}

fn main() {
  const list<i32> notas = [7, 3, 9];
  mostra_ordenada(notas);
  print(notas);
}
```

```saida
[3, 7, 9]
[7, 3, 9]
```

Mudando só a `main`: ela passa uma cópia, que a função pode ordenar à vontade:

```cinza
import Lists;

fn mostra_ordenada(list<i32> l) {
  Lists.sort(l);
  print(l);
}

fn main() {
  const list<i32> notas = [7, 3, 9];
  mostra_ordenada(Lists.copy(notas));
  print(notas);
}
```

```saida
[3, 7, 9]
[7, 3, 9]
```

O primeiro conserto é o melhor quando a função só precisa ler: a assinatura passa a dizer isso, e
nenhuma chamada precisa copiar.

## Exercício 4

`Lists.copy(M)` não basta: a cópia seria rasa, e as listas de dentro continuariam as de `M` (o
compilador recusa). Cada linha é copiada:

```cinza
import Lists;

fn main() {
  const list<list<i32>> M = [[1, 2], [3, 4]];
  list<list<i32>> copia;
  for (list<i32> linha in M) {
    copia.add(Lists.copy(linha));
  }
  copia[0][0] = 100;
  print(M);
  print(copia);
}
```

```saida
[[1, 2], [3, 4]]
[[100, 2], [3, 4]]
```

## Exercício 5

Compilam a primeira (`const` numa lista de objetos vale), a segunda (o objeto dentro da lista
continua alterável) e a quarta (o objeto sai livre: objetos são compartilhados e nunca são const).
Não compilam a terceira (a lista const não ganha elementos) e a quinta (a lista const não vai para
uma variável comum).

```cinza
class Conta {
  i64 saldo = 0;
  pub {
    fn deposita(i64 v) { saldo += v; }
    fn total() -> i64 { return saldo; }
  }
}

fn main() {
  const list<Conta> l = [new Conta()];
  l[0].deposita(1);
  Conta c = l[0];
  c.deposita(2);
  print(l[0].total());
}
```

```saida
3
```

```cinza contas.cinza
class Conta {}

fn main() {
  const list<Conta> l = [new Conta()];
  l.add(new Conta());
}
```

```erro
contas.cinza:5:4: SemanticError: Não é possível chamar '.add()' em lista const. Listas const não podem ser modificadas.
```

```cinza outra.cinza
class Conta {}

fn main() {
  const list<Conta> l = [new Conta()];
  list<Conta> outra = l;
}
```

```erro
outra.cinza:5:15: SemanticError: 'outra' não pode receber 'l', que é const: as duas variáveis ficariam com a mesma lista, e uma alteração por 'outra' mudaria 'l'. Declare 'outra' const, ou guarde uma cópia: Lists.copy(l)
```
