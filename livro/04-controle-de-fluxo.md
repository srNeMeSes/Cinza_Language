# 4. Controle de fluxo

**Objetivos.** Ao fim deste capítulo você sabe:

- escrever decisões com `if`, `else if` e `else`, e laços com `while` e `for`;
- percorrer listas, dicionários, textos e faixas de números com `for`;
- controlar um laço com `break` e `continue`;
- entender o que o compilador confere no fluxo: o retorno em todos os caminhos e o código
  inalcançável.

**Pré-requisitos.** Os capítulos 2 e 3.

---

A Cinza tem poucas instruções de controle — `if`, `while`, `for`, `break`, `continue`, `return`
—, todas com uma forma só. O que muda em relação a outras linguagens está nos detalhes: o corpo é
sempre um bloco entre chaves, a condição é sempre um `bool`, e o `for` percorre coleções em vez de
contar com três expressões.

## 4.1 `if`, `else if` e `else`

```cinza
fn classifica(i32 n) -> string {
  if (n < 0) {
    return "negativo";
  } else if (n == 0) {
    return "zero";
  } else {
    return "positivo";
  }
}
fn main() {
  print(classifica(-5), classifica(0), classifica(7));
}
```

```saida
negativo zero positivo
```

A condição vai entre parênteses e precisa ser um `bool` (seção 3.4). O corpo é **sempre um bloco
entre chaves**, mesmo com uma instrução só:

```cinza grande.cinza
fn main() {
  i32 n = 5;
  if (n > 3) print("grande");
}
```

```erro
grande.cinza:3:14: SyntaxError: Bloco '{...}' obrigatório após 'if'. Corpos sem bloco não são permitidos na Cinza. (token: 'print')
```

> **Por quê?** Sem chaves obrigatórias, acrescentar uma linha a um `if` de uma linha só — e
> achar que ela também está dentro dele — é um engano fácil e silencioso; um defeito famoso de
> segurança (o *goto fail* da Apple, em 2014) foi exatamente isso. Com as chaves sempre, o que está
> dentro do `if` está à vista.

`else if` encadeia sem chaves extras: é um `else` cujo corpo é outro `if`. Quem vem de Python, Ruby
ou PHP e escreve `elif`, `elsif` ou `elseif` recebe a forma certa na mensagem:

```cinza nota.cinza
fn main() {
  i32 n = 5;
  if (n > 3) {
    print("a");
  } elif (n > 1) {
    print("b");
  }
}
```

```erro
nota.cinza:5:5: SyntaxError: A Cinza escreve 'else if', não 'elif': } else if (condição) { ... } (token: 'elif')
```

## 4.2 `while`

`while` repete o bloco enquanto a condição for verdadeira. Aqui, a sequência de Collatz de 27: se
o número é par, divida por 2; se é ímpar, multiplique por 3 e some 1 — até chegar a 1:

```cinza
fn main() {
  i32 n = 27;
  i32 passos = 0;
  while (n != 1) {
    if (n % 2 == 0) {
      n /= 2;
    } else {
      n = 3 * n + 1;
    }
    passos += 1;
  }
  print("passos:", passos);
}
```

```saida
passos: 111
```

A Cinza não tem `do`/`while` (o laço que roda pelo menos uma vez). O equivalente é um `while
(true)` com a saída no fim:

```cinza
fn main() {
  i32 x = 1;
  while (true) {
    print(x);
    x *= 10;
    if (x > 1000) {
      break;
    }
  }
}
```

```saida
1
10
100
1000
```

## 4.3 `for`

O `for` da Cinza **percorre uma coleção**: a cada volta, o iterador recebe o próximo elemento.

```cinza
fn main() {
  list<string> frutas = ["maçã", "banana", "uva"];
  for (string f in frutas) {
    print(f);
  }
}
```

```saida
maçã
banana
uva
```

O que se percorre e o que o iterador recebe:

| Iterável | Cada volta recebe |
|----------|-------------------|
| `list<T>` | um elemento, do primeiro ao último |
| `dict<K, V>` | um `pair<K, V>` — a chave em `first`, o valor em `second` —, na ordem de inserção |
| `string` | um caractere, como `string` |

```cinza
fn main() {
  dict<string, i32> estoque = {{"pera", 3}, {"uva", 0}};
  for (pair<string, i32> p in estoque) {
    print(p.first, p.second);
  }
  for (string c in "ação") {
    print(c);
  }
}
```

```saida
pera 3
uva 0
a
ç
ã
o
```

Num texto, cada volta é um **caractere** (um code point), não um byte: o `ç` e o `ã` vêm inteiros.

### Contar com `range`

Para contar, percorra uma faixa de números. `range(ini, fim)` vai de `ini` (inclusive) até `fim`
(exclusive); `range(ini, fim, passo)` anda de `passo` em `passo`, e um passo negativo conta para
baixo:

```cinza
fn main() {
  for (i32 i in range(0, 3)) {
    print(i);
  }
  print(range(0, 10, 3), range(5, 0, -2), range(3, 3));
}
```

```saida
0
1
2
[0, 3, 6, 9] [5, 3, 1] []
```

O tipo dos números é o dos argumentos: `range(0, 10)` é `i32`; com um `i64`, `i64`. Fora de um
`for`, `range` cria uma lista de verdade (limitada a 100 milhões de elementos); **dentro do `for`,
a lista não é criada** — os números são gerados um a um, sem limite e sem gastar memória:

```cinza
fn main() {
  i64 soma = 0;
  for (i64 i in range(cast<i64>(0), 200000000)) {
    soma += i;
  }
  print(soma);
}
```

```saida
19999999900000000
```

O for de três partes do C (`for (i = 0; i < n; i++)`) não existe; o compilador o reconhece e mostra
a forma da Cinza:

```cinza contagem.cinza
fn main() {
  for (i32 i = 0; i < 3; i += 1) {
    print(i);
  }
}
```

```erro
contagem.cinza:2:14: SyntaxError: O for da Cinza percorre uma coleção: for (i32 i in range(0, n)) { ... }. Não há o for de três partes do C (inicialização; condição; passo) (token: '=')
```

> **Por quê?** O for de três partes permite qualquer coisa — mudar o contador no meio, a condição
> que nunca termina, o erro de um a mais (`<=` no lugar de `<`). O `for` sobre uma coleção ou um
> `range` diz o que vai ser percorrido, e o laço não tem como sair do lugar. Quando a lógica do
> laço é mais complicada que isso, um `while` deixa a complicação à vista.

### O tipo do iterador

O tipo escrito no `for` precisa aceitar o elemento. `var` deixa o compilador usar o tipo do
elemento, e um tipo mais largo da mesma família também vale (`i64` para os elementos de uma
`list<i32>`, alargando cada um). Um tipo que não aceita o elemento é erro:

```cinza iterador.cinza
fn main() {
  for (string s in [1, 2]) {
    print(s);
  }
}
```

```erro
iterador.cinza:2:3: SemanticError: Tipo do iterador 'string' é incompatível com o elemento da lista 'i32'
```

O iterador existe só dentro do corpo do laço.

### A coleção é copiada no início

**Os elementos são copiados quando o `for` começa**: mudar a coleção dentro do corpo não muda as
voltas. O laço abaixo acrescenta elementos à lista que percorre, e mesmo assim dá duas voltas,
uma para cada elemento que ela tinha no começo:

```cinza
fn main() {
  list<i32> l = [1, 2];
  for (i32 v in l) {
    l.add(v * 10);
  }
  print(l);
}
```

```saida
[1, 2, 10, 20]
```

> **Vindo de outra linguagem.** Em Java isso lança `ConcurrentModificationException`; em Python
> é um laço que não termina (a lista cresce a cada volta). Na Cinza o resultado é sempre definido:
> as voltas são as dos elementos do início.

## 4.4 `break` e `continue`

`break` sai do laço; `continue` pula para a próxima volta. Os dois valem para **o laço mais
interno** em que estão:

```cinza
fn main() {
  for (i32 i in range(1, 10)) {
    if (i % 2 == 0) {
      continue;
    }
    if (i > 7) {
      break;
    }
    print(i);
  }
}
```

```saida
1
3
5
7
```

Não há rótulos para sair de vários laços de uma vez (`break externo`). Quando é preciso, ponha os
laços numa função e saia com `return` — o que costuma deixar o código mais claro de qualquer jeito.
Fora de um laço, `break` e `continue` são erro:

```cinza fora.cinza
fn main() {
  break;
}
```

```erro
fora.cinza:2:3: SemanticError: 'break' só pode ser usado dentro de um laço (while ou for).
```

## 4.5 Blocos e escopo

Uma variável vale do ponto em que é declarada até o fim do bloco que a contém. Um bloco interno
pode declarar uma variável com o nome de outra de fora — a interna a esconde até o fim do bloco —,
mas, no mesmo bloco, repetir um nome é erro:

```cinza
fn main() {
  i32 x = 1;
  if (true) {
    i32 x = 2;
    i32 y = 3;
    print(x, y);
  }
  print(x);
}
```

```saida
2 3
1
```

Depois do `if`, `y` não existe mais, e `x` volta a ser o de fora.

## 4.6 O que o compilador confere no fluxo

### O retorno em todos os caminhos

Uma função que declara um tipo de retorno precisa **retornar em todos os caminhos** — não pode
haver um jeito de chegar ao fim dela sem um `return`:

```cinza sinal.cinza
fn sinal(i32 n) -> string {
  if (n > 0) {
    return "positivo";
  } else if (n < 0) {
    return "negativo";
  }
}
fn main() {
  print(sinal(0));
}
```

```erro
sinal.cinza:1:1: SemanticError: Função 'sinal' não retorna em todos os caminhos de execução
```

Para o compilador, `n` igual a zero passa pelos dois testes e chega ao fim sem retornar. Ele não
tenta adivinhar o que é impossível: um caminho só conta como garantido se termina em `return`,
`throw` ou `exit(...)`, se é um `if`/`else` com os dois lados garantidos, ou um `while (true)` sem
`break`:

```cinza
fn primeiro_multiplo(i32 n, i32 de) -> i32 {
  i32 x = n;
  while (true) {
    if (x % de == 0) {
      return x;
    }
    x += 1;
  }
}
fn main() {
  print(primeiro_multiplo(10, 7));
}
```

```saida
14
```

O `while (true)` só termina pelo `return`, e o compilador sabe disso: não há caminho que chegue ao
fim da função.

### Código inalcançável

Uma instrução depois de outra que **sempre sai do bloco** nunca roda — e é um erro, porque quase
sempre é um engano (um `return` esquecido no meio, uma linha que ficou depois de um `break`):

```cinza dobro.cinza
fn dobro(i32 n) -> i32 {
  return n * 2;
  print("nunca");
}
fn main() {
  print(dobro(2));
}
```

```erro
dobro.cinza:3:3: SemanticError: Código inalcançável: esta instrução nunca é executada, porque a anterior sempre sai do bloco (return, break, continue, throw, exit ou yield).
```

Sempre saem: `return`, `throw` (capítulo 14), `exit(...)`, `break`, `continue`, um `if`/`else` com
os dois lados saindo, um `while (true)` sem `break` e um `try` com o bloco e todos os `except`
saindo.

> **Por quê?** As duas conferências pegam, antes de rodar, enganos que de outro jeito só
> apareceriam num caso particular — a função que devolve lixo (em C) ou `None` (em Python) quando
> um caminho esquecido acontece, ou a linha que alguém achava que rodava.

## 4.7 O que a Cinza não tem

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `switch`, `match` | `if` / `else if`; para associar valores a chaves, um `dict` (capítulo 8) |
| `do { ... } while (c)` | `while (true) { ...; if (not c) { break; } }` |
| `for (i = 0; i < n; i++)` | `for (i32 i in range(0, n))` |
| `elif`, `elsif`, `elseif` | `else if` |
| `break rótulo` | uma função com `return` |
| `if x { }` (sem parênteses) | `if (x) { }` |

Em todos esses casos o compilador reconhece a construção e diz como escrevê-la — um erro só, sem
mensagens em cascata:

```cinza escolha.cinza
fn main() {
  i32 n = 1;
  switch (n) {
    print(n);
  }
}
```

```erro
escolha.cinza:3:3: SyntaxError: A Cinza não tem switch: use if / else if (ou um dict, para associar valores) (token: 'switch')
```

## Programa do capítulo: sequências de Collatz

O programa recebe números na linha de comando, conta os passos da sequência de Collatz de cada um
e diz qual foi a mais longa. Ele usa `for` sobre os argumentos, `while` para a sequência,
`continue` para pular o que não serve e uma função que retorna em todos os caminhos:

<!-- args: 6 27 -3 97 -->
```cinza collatz.cinza
// collatz.cinza: cinza collatz.cinza <n>...

fn passos(i64 inicio) -> i64 {
  i64 n = inicio;
  i64 contagem = 0;
  while (n != 1) {
    if (n % 2 == 0) {
      n /= 2;
    } else {
      n = 3 * n + 1;
    }
    contagem += 1;
  }
  return contagem;
}

fn main(list<string> args) {
  i64 melhor = 0;
  i64 melhor_passos = -1;
  for (string a in args) {
    i64 n = cast<i64>(a);
    if (n < 1) {
      print(n, "não é positivo: ignorado");
      continue;
    }
    i64 p = passos(n);
    print(n, "->", p, "passos");
    if (p > melhor_passos) {
      melhor = n;
      melhor_passos = p;
    }
  }
  if (melhor_passos < 0) {
    print("nenhum número positivo");
  } else {
    print("a mais longa:", melhor, "com", melhor_passos, "passos");
  }
}
```

```console
$ cinza collatz.cinza 6 27 -3 97
```

```saida
6 -> 8 passos
27 -> 111 passos
-3 não é positivo: ignorado
97 -> 118 passos
a mais longa: 97 com 118 passos
```

Repare em:

- `passos` usa `i64`: os números da sequência sobem bastante antes de descer (a de 27 passa de
  9000), e em sequências maiores um `i32` estouraria — com um `OverflowError`, não com um número
  errado.
- `melhor_passos` começa em `-1`, um valor que nenhuma contagem real tem, para o último `if` saber
  se algum número foi aceito. O capítulo 11 mostra o jeito da Cinza para "ainda não há valor": o
  opcional, `i64? melhor`.
- `continue` pula o resto da volta para o número inválido, sem aninhar o resto do corpo num `else`.

## Resumo

- `if (cond) { } else if (cond) { } else { }`: a condição é um `bool` entre parênteses, e o corpo é
  sempre um bloco.
- `while (cond) { }` repete; `while (true)` com `break` faz o papel do `do`/`while`.
- `for (tipo x in coleção) { }` percorre listas, dicionários (em `pair`), textos (em caracteres) e
  `range`; a coleção é copiada no início, e no `for` o `range` não cria lista.
- `break` e `continue` valem para o laço mais interno; não há rótulos.
- Uma função com tipo de retorno retorna em todos os caminhos, e código depois de uma saída é erro.
- Não há `switch`, `do`/`while`, o for de três partes nem `elif` — o compilador mostra o equivalente.

## Exercícios

1. Escreva o *FizzBuzz* de 1 a 15: para cada número, imprima `Fizz` se for múltiplo de 3, `Buzz` se
   for de 5, `FizzBuzz` se for dos dois, e o próprio número nos outros casos.
2. Escreva `fn soma_digitos(i64 n) -> i64`, que soma os dígitos de um número não negativo com um
   `while` (dica: `n % 10` é o último dígito, `n / 10` tira o último dígito). Teste com 0, 7 e
   9876543210.
3. Imprima os números primos menores que 30, com dois `for` aninhados e um `break` (um número é
   primo se é maior que 1 e nenhum número de 2 até ele menos um o divide).
4. A função abaixo não compila. Explique por quê e corrija-a sem mudar o que ela faz para números
   diferentes.

   ```text
   fn maior(i32 a, i32 b) -> i32 {
     if (a > b) {
       return a;
     } else if (b > a) {
       return b;
     }
   }
   ```

5. Reescreva o laço abaixo, em C, na Cinza: ele imprime as potências de 2 até passar de 100, e
   imprime pelo menos uma vez.

   ```text
   int x = 1;
   do {
     printf("%d\n", x);
     x *= 2;
   } while (x <= 100);
   ```

As soluções estão em [`solucoes/04-controle-de-fluxo.md`](solucoes/04-controle-de-fluxo.md).

## Na spec

- A gramática das instruções: [2.3](../spec/02-gramatica.md)
- Variáveis e escopo: [4.5](../spec/04-declaracoes.md)
- `if`, `while`, `for`, `break`, `continue`, `return` e o código inalcançável: [5.5](../spec/05-execucao.md)
- `range`: [6.1](../spec/06-biblioteca.md)
