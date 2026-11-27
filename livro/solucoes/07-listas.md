# Soluções do capítulo 7

[Voltar ao capítulo](../07-listas.md)

## Exercício 1

O `range` com passo `-1` anda do último índice até o 0 (o `fim`, `-1`, fica de fora):

```cinza
fn inverte(list<i32> l) -> list<i32> {
  list<i32> r;
  for (i32 i in range(l.size() - 1, -1, -1)) {
    r.add(l[i]);
  }
  return r;
}

fn main() {
  list<i32> vazia;
  print(inverte([1, 2, 3]), inverte([7]), inverte(vazia));
}
```

```saida
[3, 2, 1] [7] []
```

## Exercício 2

`unique` tira os repetidos (o 9 aparece uma vez só), e depois de ordenar do maior para o menor o
segundo maior está no índice 1:

```cinza
import Lists;

fn segundo_maior(list<i32> l) -> i32 {
  list<i32> distintos = Lists.unique(l);
  Lists.sort_desc(distintos);
  return distintos[1];
}

fn main() {
  print(segundo_maior([5, 9, 9, 3]), segundo_maior([1, 2]));
}
```

```saida
5 1
```

Com menos de dois valores distintos não há segundo maior, e `distintos[1]` lança `IndexError` —
o comportamento certo para uma pergunta sem resposta. (O capítulo 11 mostra como devolver um
`i32?` em vez disso.)

## Exercício 3

`mais_um` recebe a **mesma** lista que `a` (listas são compartilhadas), acrescenta o 3 nela e a
devolve: `a` e `b` são a mesma lista. Para não mexer na original, a função trabalha numa cópia:

```cinza
import Lists;

fn mais_um(list<i32> l) -> list<i32> {
  list<i32> nova = Lists.copy(l);
  nova.add(3);
  return nova;
}

fn main() {
  list<i32> a = [1, 2];
  list<i32> b = mais_um(a);
  print(a, b);
}
```

```saida
[1, 2] [1, 2, 3]
```

## Exercício 4

```cinza
import Lists;

fn main() {
  list<f64> notas = [7.5ff, 4.ff, 9.ff, 5.5ff, 8.ff];
  list<f64> aprovadas;
  for (f64 n in notas) {
    if (n >= 6.ff) {
      aprovadas.add(n);
    }
  }
  f64 todas = Lists.average(notas);
  f64 boas = Lists.average(aprovadas);
  printf("média geral: {todas:.2f}; das notas a partir de 6: {boas:.2f}");
}
```

```saida
média geral: 6.80; das notas a partir de 6: 8.17
```

Se nenhuma nota passasse de 6, `aprovadas` ficaria vazia e `Lists.average` lançaria `ValueError` —
um caso que um programa de verdade trataria antes, com `Lists.is_empty`.

## Exercício 5

`k % n` reduz as voltas completas (girar 7 posições numa lista de 5 é girar 2), e o resultado é o
trecho a partir de `k` seguido do trecho antes dele:

```cinza
import Lists;

fn rotaciona(list<i32> l, i32 k) -> list<i32> {
  if (l.size() == 0) {
    return Lists.copy(l);
  }
  i32 passo = k % l.size();
  return Lists.concat(Lists.slice(l, passo, l.size()), Lists.slice(l, 0, passo));
}

fn main() {
  print(rotaciona([1, 2, 3, 4, 5], 2), rotaciona([1, 2, 3, 4, 5], 7), rotaciona([1, 2, 3], 0));
}
```

```saida
[3, 4, 5, 1, 2] [3, 4, 5, 1, 2] [1, 2, 3]
```

A lista vazia é tratada à parte: `k % 0` seria uma divisão por zero.
