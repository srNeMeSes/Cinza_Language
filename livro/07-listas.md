# 7. Listas

**Objetivos.** Ao fim deste capítulo você sabe:

- criar, ler, alterar e percorrer uma `list<T>`;
- prever quando duas variáveis veem a mesma lista, e copiar quando precisar;
- entender por que `list<i32>` não é `list<i64>`;
- usar o módulo `Lists` para ordenar, buscar, somar e montar listas.

**Pré-requisitos.** A Parte I e o capítulo 6.

---

Uma `list<T>` é uma sequência de elementos de um mesmo tipo `T`, indexada a partir de 0, que cresce
e diminui. Ela é a coleção do dia a dia da Cinza. Como em Java e em Python, uma lista é
**compartilhada**: atribuí-la ou passá-la a uma função não a copia. E, como em todo o resto da
linguagem, o tipo dos elementos é conhecido na compilação e não muda.

## 7.1 Criar e acessar

```cinza
fn main() {
  list<string> nomes = ["Ana", "Bia"];
  nomes.add("Caio");
  print(nomes, nomes.size(), nomes[0], nomes[2]);
  nomes[1] = "Beatriz";
  nomes.remove(0);
  print(nomes, nomes.has(1), nomes.has(2));
}
```

```saida
["Ana", "Bia", "Caio"] 3 Ana Caio
["Beatriz", "Caio"] true false
```

A lista tem quatro métodos — o resto fica no módulo `Lists` (seção 7.7):

| Método | O que faz |
|--------|-----------|
| `l.size()` | o número de elementos (`i32`) |
| `l.add(x)` | acrescenta `x` no fim |
| `l.remove(i)` | remove o elemento do **índice** `i` |
| `l.has(i)` | `true` se `i` é um índice válido |

Uma lista declarada sem valor **nasce vazia** — é um dos poucos casos em que a declaração pode
omitir o `=` (seção 2.2):

```cinza
fn main() {
  list<i32> numeros;
  print(numeros, numeros.size());
  numeros.add(5);
  print(numeros);
}
```

```saida
[] 0
[5]
```

O índice é um inteiro (`i32` ou `i64`) de 0 até `size() - 1`. Um índice fora disso, ao ler, ao
gravar ou ao remover, é um erro de execução:

```cinza indice.cinza
fn main() {
  list<i32> l = [1, 2];
  i32 i = 5;
  print(l[i]);
}
```

```erro
indice.cinza:4:10: IndexError: índice 5 fora dos limites (tamanho: 2)
    em main (indice.cinza:4)
```

**Índices negativos não existem em listas** (nos textos, sim: seção 6.1). Um literal negativo já é
recusado na compilação; para o último elemento, use `l[l.size() - 1]` ou `Lists.last(l)`:

```cinza ultimo.cinza
fn main() {
  list<i32> l = [1, 2];
  print(l[-1]);
}
```

```erro
ultimo.cinza:3:10: SemanticError: Índice negativo em lista não é permitido. Índices de lista devem ser inteiros não-negativos.
```

> **Por quê?** Num texto, `s[-1]` é uma leitura. Numa lista, um índice negativo quase sempre é uma
> conta que deu errado (`i - 1` com `i` igual a 0), e no Python ele lê silenciosamente o último
> elemento. Na Cinza, uma conta errada num índice de lista acaba num `IndexError`, no lugar dela.

## 7.2 Literais e o tipo dos elementos

Um literal de lista se escreve entre colchetes, e os elementos precisam ter um tipo comum. Sem
tipo esperado, o tipo é o mais largo da família:

```cinza
fn main() {
  print(type([1, 2]), type([1, 3000000000]), type([1.5f, 2.ff]), type([["a"]]));
}
```

```saida
list<i32> list<i64> list<f64> list<list<string>>
```

Elementos que não combinam são erro:

```cinza mistura.cinza
fn main() {
  list<i32> l = [1, "dois"];
}
```

```erro
mistura.cinza:2:17: SemanticError: Literal de lista com tipos incompatíveis: elemento 2 tem tipo 'string', incompatível com 'i32'
```

O `[]` vazio não existe — uma lista vazia se declara sem valor, como na seção anterior, e o tipo
fica escrito na declaração:

```cinza vazia.cinza
fn main() {
  list<i32> l = [];
}
```

```erro
vazia.cinza:2:17: SyntaxError: Inicializador vazio '[]' não é permitido. Para declarar sem valor, omita o inicializador: 'list<T> nome;' (token: '[')
```

### Listas de listas

O elemento pode ser outra coleção. `m[1][0]` é o primeiro elemento da segunda lista:

```cinza
fn main() {
  list<list<i32>> m = [[1, 2], [3, 4]];
  m[1][0] = 30;
  m[0].add(5);
  print(m, m[1][1], m.size(), m[0].size());
}
```

```saida
[[1, 2, 5], [30, 4]] 4 2 3
```

## 7.3 A lista é compartilhada

Atribuir uma lista a outra variável **não a copia**: as duas passam a ver a mesma lista.

```cinza
fn main() {
  list<i32> a = [1, 2];
  list<i32> b = a;
  b.add(3);
  print(a, b);
}
```

```saida
[1, 2, 3] [1, 2, 3]
```

O mesmo vale ao passar a lista a uma função (seção 5.3) e ao guardá-la dentro de outra lista ou de
um objeto. Quando você quer uma lista independente, copie-a com `Lists.copy`:

```cinza
import Lists;
fn main() {
  list<i32> a = [1, 2];
  list<i32> b = Lists.copy(a);
  b.add(3);
  print(a, b);
}
```

```saida
[1, 2] [1, 2, 3]
```

A cópia é **rasa**: os elementos são copiados um a um, como numa atribuição. Uma lista de números
vira outra lista de números; uma lista de listas vira outra lista que contém as **mesmas** listas
de dentro.

A **igualdade** de duas listas é estrutural: `==` compara os elementos, na ordem (seção 3.3).

```cinza
fn main() {
  print([1, 2] == [1, 2], [1, 2] == [2, 1], [[1]] == [[1]]);
}
```

```saida
true false true
```

## 7.4 Invariância

`list<i32>` **não é** `list<i64>`, mesmo que cada `i32` caiba num `i64`:

```cinza soma.cinza
fn soma(list<i64> l) -> i64 {
  i64 s = 0;
  for (i64 x in l) {
    s += x;
  }
  return s;
}
fn main() {
  list<i32> pequenos = [1, 2];
  print(soma(pequenos));
}
```

```erro
soma.cinza:10:9: SemanticError: Argumento 1 de 'soma': esperado 'list<i64>', mas recebeu 'list<i32>'
```

O motivo: a função recebe a **mesma** lista, não uma cópia. Se `soma` pudesse tratar a lista como
`list<i64>`, ela poderia acrescentar um `i64` grande — e o chamador, que acredita ter uma lista de
`i32`, passaria a ter nela um valor que não cabe em `i32`. A invariância fecha essa porta.

Um **literal**, ao contrário, se adapta ao tipo esperado: ele é uma lista nova, que ninguém mais vê.
`soma([1, 2])` cria direto uma `list<i64>`:

```cinza
fn soma(list<i64> l) -> i64 {
  i64 s = 0;
  for (i64 x in l) {
    s += x;
  }
  return s;
}
fn main() {
  print(soma([1, 2]));
}
```

```saida
3
```

> **Vindo de outra linguagem.** É a regra do Java (`List<Integer>` não é `List<Number>`) e do C#;
> o Python não confere nada disso. O C# e o Kotlin oferecem variância declarada (`out T`); a Cinza
> não, pela simplicidade: para passar uma lista de outro tipo, converta-a num laço.

## 7.5 Percorrer

O `for` percorre os elementos, copiados no início do laço (seção 4.3). Quando o índice também
interessa, percorra `range(0, l.size())`:

```cinza
fn main() {
  list<string> l = ["a", "b", "c"];
  for (string x in l) {
    print(x);
  }
  for (i32 i in range(0, l.size())) {
    print(i, l[i]);
  }
}
```

```saida
a
b
c
0 a
1 b
2 c
```

## 7.6 Listas grandes

Uma lista cresce sem que você precise reservar espaço, e o acesso por índice é imediato,
qualquer que seja o tamanho:

```cinza
fn main() {
  list<i64> quadrados;
  for (i64 i in range(cast<i64>(0), 1000000)) {
    quadrados.add(i * i);
  }
  print(quadrados.size(), quadrados[999999]);
}
```

```saida
1000000 999998000001
```

## 7.7 O módulo `Lists`

As funções de lista ficam no módulo `Lists` (`import Lists;`, ou `import Lists as L;`). As que
**mudam a lista no lugar** não devolvem nada; as que **criam uma lista nova** deixam a original
como está. A lista completa está na [spec, seção 6.2](../spec/06-biblioteca.md).

### Ordenar

```cinza
import Lists as L;
fn main() {
  list<i32> n = [5, 3, 9, 1];
  L.sort(n);
  print(n);
  L.sort_desc(n);
  print(n);
  list<string> palavras = ["pera", "Uva", "banana"];
  print(L.sorted(palavras), palavras);
  print(L.reversed(palavras));
}
```

```saida
[1, 3, 5, 9]
[9, 5, 3, 1]
["Uva", "banana", "pera"] ["pera", "Uva", "banana"]
["banana", "Uva", "pera"]
```

`sort` e `sort_desc` ordenam a própria lista; `sorted` e `reversed` devolvem uma nova. Os textos
seguem a ordem dos code points (seção 3.2), por isso `"Uva"` vem antes de `"banana"`.

`sort` só ordena **números e textos**. Para ordenar por um critério — objetos por um campo, textos
pelo tamanho —, `sort_by` recebe a lista e a lista das **chaves**, na mesma ordem, e ordena as duas
juntas:

```cinza
import Lists as L;
fn main() {
  list<string> nomes = ["Caio", "Ana", "Bia", "Davi"];
  list<i32> idades = [30, 25, 30, 18];
  L.sort_by(nomes, idades);
  print(nomes, idades);
}
```

```saida
["Davi", "Ana", "Caio", "Bia"] [18, 25, 30, 30]
```

Toda ordenação da Cinza é **estável**: elementos de chave igual mantêm a ordem em que estavam (Caio
e Bia, os dois com 30). É esse o caminho no lugar da função de comparação que outras linguagens
passariam (`sort(key=...)` no Python, um `Comparator` no Java), já que funções não são valores
(seção 5.7) — e o compilador aponta o caminho:

```cinza objetos.cinza
import Lists;
class Tarefa { i32 prioridade = 0; }
fn main() {
  list<Tarefa> l = [new Tarefa()];
  Lists.sort(l);
}
```

```erro
objetos.cinza:5:3: SemanticError: Argumento 1 de 'Lists.sort': esperado 'list<comparável>', mas recebeu 'list<Tarefa>'. Para ordenar objetos ou structs, use Lists.sort_by(l, chaves), com a lista das chaves (números ou textos) na mesma ordem
```

### Estatística

```cinza
import Lists as L;
fn main() {
  list<i32> n = [4, 8, 15, 16, 23, 42];
  print(L.sum(n), L.product([1, 2, 3, 4]), L.average(n), L.min(n), L.max(n), L.max(["b", "a"]));
}
```

```saida
108 24 18 4 42 b
```

A soma tem o tipo dos elementos — e estoura como qualquer conta (seção 2.3). Com elementos que
podem somar muito, use uma `list<i64>`:

```cinza somatorio.cinza
import Lists as L;
fn main() {
  print(L.sum([2147483647, 1]));
}
```

```erro
somatorio.cinza:3:9: OverflowError: a soma de 'sum' não cabe em i32
    em main (somatorio.cinza:3)
```

`average` (que devolve um `f64`), `min` e `max` de uma lista vazia são erro: não há média de nada.

### Buscar

```cinza
import Lists as L;
fn main() {
  list<string> l = ["a", "b", "a", "c"];
  print(L.contains(l, "b"), L.index_of(l, "a"), L.last_index_of(l, "a"), L.index_of(l, "z"));
  print(L.count(l, "a"), L.first(l), L.last(l), L.is_empty(l));
}
```

```saida
true 0 2 null
2 a c false
```

Como em `Strings.find`, `index_of` devolve um `i32?`: a posição, ou `null` se não achar. "Igual" é
o `==` da seção 3.3.

### Alterar no lugar e criar listas novas

```cinza
import Lists as L;
fn main() {
  list<i32> l = [1, 2, 3];
  L.insert(l, 0, 0);
  print(l);
  i32 ultimo = L.pop(l);
  print(ultimo, l);
  L.remove_value(l, 2);
  L.extend(l, [7, 8]);
  L.swap(l, 0, 1);
  print(l);
  print(L.slice(l, 1, 3), L.concat([1], [2, 3]), L.unique([1, 2, 1, 3, 2]), L.repeat(0, 3));
  L.clear(l);
  print(l, L.is_empty(l));
}
```

```saida
[0, 1, 2, 3]
3 [0, 1, 2]
[1, 0, 7, 8]
[0, 7] [1, 2, 3] [1, 2, 3] [0, 0, 0]
[] true
```

- `l.remove(i)` remove pelo **índice**; `Lists.remove_value(l, x)` remove o primeiro elemento
  **igual** a `x`.
- `pop` remove e devolve o último; numa lista vazia, `IndexError` (como `first` e `last`).
- `slice(l, ini, fim)` é o trecho de uma lista — as fatias `l[1:3]` são só de texto.

## 7.8 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `l.append(x)`, `l.push(x)` | `l.add(x)` |
| `len(l)`, `l.length`, `l.size` | `l.size()` |
| `l[-1]` | `Lists.last(l)` ou `l[l.size() - 1]` |
| `l[1:3]` | `Lists.slice(l, 1, 3)` |
| `l.sort()`, `sorted(l, key=...)` | `Lists.sort(l)`, `Lists.sort_by(l, chaves)` |
| `l.index(x)`, `l.indexOf(x)` | `Lists.index_of(l, x)` (um opcional) |
| `[]` | `list<T> l;` |

O compilador conhece esses nomes e aponta o da Cinza:

```cinza acrescenta.cinza
fn main() {
  list<i32> l = [1];
  l.append(2);
}
```

```erro
acrescenta.cinza:3:4: SemanticError: Método 'append' não existe em 'list<T>'. Para acrescentar no fim, use l.add(x)
```

## Programa do capítulo: a primeira lista de tarefas

Aqui começa o programa que acompanha o livro até o capítulo 26: o **gerenciador de tarefas**. Nesta
primeira versão, uma tarefa é só um texto, e há duas listas — a das pendentes e a das feitas.
Concluir uma tarefa é tirá-la de uma e pôr na outra:

```cinza tarefas.cinza
// tarefas.cinza — versão 1: as tarefas em duas listas de textos
import Lists;

fn mostra(string titulo, list<string> tarefas) {
  print(titulo + " (" + tarefas.size() + "):");
  if (Lists.is_empty(tarefas)) {
    print("  (nenhuma)");
    return;
  }
  for (i32 i in range(0, tarefas.size())) {
    printf("  {i + 1}. {tarefas[i]}");
  }
}

fn conclui(list<string> pendentes, list<string> feitas, string tarefa) {
  if (i32 i = Lists.index_of(pendentes, tarefa)) {
    pendentes.remove(i);
    feitas.add(tarefa);
  } else {
    print("não achei:", tarefa);
  }
}

fn main() {
  list<string> pendentes = ["comprar pão", "pagar a conta de luz", "estudar a Cinza"];
  list<string> feitas;
  pendentes.add("ligar para a Ana");
  conclui(pendentes, feitas, "pagar a conta de luz");
  conclui(pendentes, feitas, "lavar o carro");
  Lists.sort(pendentes);
  mostra("Pendentes", pendentes);
  mostra("Feitas", feitas);
}
```

```saida
não achei: lavar o carro
Pendentes (3):
  1. comprar pão
  2. estudar a Cinza
  3. ligar para a Ana
Feitas (1):
  1. pagar a conta de luz
```

Repare em:

- `conclui` recebe as duas listas e as altera: elas são compartilhadas, então `main` vê a mudança
  sem nenhum retorno.
- `if (i32 i = Lists.index_of(...))` só entra no bloco se a tarefa foi achada, com a posição em
  `i`; senão, o `else`. É o opcional do capítulo 11, aqui no seu uso mais comum.
- A numeração para o usuário começa em 1 (`{i + 1}`), e o índice da lista, em 0.

Esta versão tem os seus limites: uma tarefa não tem prazo nem prioridade, e duas listas que andam
juntas são fáceis de desencontrar. Os próximos capítulos resolvem isso — com structs (capítulo 9),
enums (capítulo 10) e uma classe que guarda tudo num lugar só (capítulo 12).

## Resumo

- `list<T>` com `size()`, `add(x)`, `remove(i)` e `has(i)`; índices de 0 a `size() - 1`, sem
  negativos; fora disso, `IndexError`. Sem valor, a lista nasce vazia; não há `[]`.
- A lista é compartilhada: atribuir ou passar não copia. `Lists.copy` faz uma cópia rasa. `==`
  compara os elementos.
- `list<i32>` não é `list<i64>` (invariância), mas um literal se adapta ao tipo esperado.
- O módulo `Lists` ordena (`sort`, `sort_by` estável), soma, busca (`index_of` devolve um
  opcional), altera no lugar e cria listas novas. Lista vazia onde não faz sentido é erro.

## Exercícios

1. Escreva `fn inverte(list<i32> l) -> list<i32>`, que devolve uma lista nova com os elementos em
   ordem inversa, **sem** usar `Lists.reverse` nem `Lists.reversed`.
2. Escreva `fn segundo_maior(list<i32> l) -> i32`, que devolve o segundo maior valor **distinto**
   da lista (em `[5, 9, 9, 3]`, é `5`). Use o módulo `Lists`.
3. O programa abaixo imprime `[1, 2, 3] [1, 2, 3]`, e quem o escreveu esperava `[1, 2] [1, 2, 3]`.
   Explique por quê e corrija-o.

   ```text
   fn mais_um(list<i32> l) -> list<i32> {
     l.add(3);
     return l;
   }
   fn main() {
     list<i32> a = [1, 2];
     list<i32> b = mais_um(a);
     print(a, b);
   }
   ```

4. Dadas as notas `[7.5ff, 4.ff, 9.ff, 5.5ff, 8.ff]`, imprima a média de todas e a média só das
   que são maiores ou iguais a 6.
5. Escreva `fn rotaciona(list<i32> l, i32 k) -> list<i32>`, que devolve a lista "girada" `k`
   posições para a esquerda: `rotaciona([1, 2, 3, 4, 5], 2)` é `[3, 4, 5, 1, 2]`. Funcione também
   com `k` maior que o tamanho.

As soluções estão em [`solucoes/07-listas.md`](solucoes/07-listas.md).

## Na spec

- O tipo `list<T>`: [3.3](../spec/03-tipos.md)
- A invariância e os literais que se adaptam: [3.5](../spec/03-tipos.md)
- O compartilhamento: [5.1](../spec/05-execucao.md)
- Os métodos e os índices: [5.7](../spec/05-execucao.md)
- O módulo `Lists`: [6.2](../spec/06-biblioteca.md)
