# 17. Genéricos: structs, classes e interfaces

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar structs, classes e interfaces genéricas, e escrever os tipos em cada uso;
- prever o que é conferido quando: os campos e as assinaturas já, o corpo de um método só quando
  ele é usado;
- fazer uma classe genérica cumprir uma interface genérica;
- escrever uma coleção que serve a qualquer tipo, com filtros que também servem.

**Pré-requisitos.** Os capítulos 9 (structs), 12 e 13 (classes e interfaces) e 16 (funções
genéricas).

---

O capítulo 16 fez funções servirem a vários tipos. Este faz o mesmo com os **tipos**: um par de
quaisquer dois valores, uma pilha de qualquer coisa, um filtro de qualquer elemento. As regras são
as mesmas — os parâmetros de tipo entre colchetes, uma cópia por combinação de tipos, a conferência
no uso —, com uma diferença no uso: os tipos são sempre escritos.

## 17.1 Structs genéricos

```cinza
struct Par[A, B] {
  A primeiro;
  B segundo;
}

fn main() {
  Par[string, i32] p = new Par[string, i32]("ana", 30);
  Par[i32, bool] q = new Par[i32, bool](segundo: true, primeiro: 1);
  print(p, q.primeiro, type(p));
  print(type(p) == Par[string, i32]);
}
```

```saida
Par[string, i32]{primeiro: "ana", segundo: 30} 1 Par[string, i32]
true
```

`Par[string, i32]` é um struct comum, a cópia de `Par` com `A = string` e `B = i32`: ele se cria
como qualquer struct (os campos em ordem, ou pelo nome), é um valor (seção 9.3), e aparece com os
tipos no `print` e no `type`.

## 17.2 Os tipos, sempre escritos

Numa função genérica, os tipos são deduzidos dos argumentos. Num struct ou numa classe, **não**:
eles são escritos em todo uso — na declaração da variável, no `new`, num parâmetro, dentro de
`list<...>`.

```cinza caixa.cinza
struct Caixa[T] {
  T valor;
}

fn main() {
  Caixa c = new Caixa(valor: 1);
}
```

```erro
caixa.cinza:6:3: SemanticError: o struct genérico 'Caixa' precisa dos tipos entre colchetes, ex.: Caixa[i32]
```

> **Por quê?** O tipo de uma variável já é escrito na declaração (`var` só infere os primitivos,
> seção 2.2). Deduzir `Caixa[i32]` de `new Caixa(valor: 1)` seria adivinhar pelo primeiro uso —
> e `valor: 1` poderia querer ser um `i64`. Escritos, os tipos dizem em cada linha o que a variável
> guarda, e uma mudança num lugar não muda em silêncio o tipo de outro.

## 17.3 Campos do tipo `T`

Um campo do tipo `T` não pode ter valor inicial — não há valor que sirva a todo `T` —, então é
**obrigatório**: quem cria o struct o informa (seção 9.2).

```cinza inicial.cinza
struct Caixa[T] {
  T valor = 0;
}

fn main() {}
```

```erro
inicial.cinza:2:3: SemanticError: Em 'Caixa', o campo 'valor' é do tipo 'T' e não pode ter valor inicial: não há valor que sirva a todo T. Deixe-o sem valor; quem cria o struct o informa.
```

Um campo `T?` nasce `null`, como todo opcional (capítulo 11), e deixa de ser obrigatório:

```cinza
struct Caixa[T] {
  T? valor;
  i32 usos = 0;
}

fn main() {
  Caixa[string] c = new Caixa[string]();
  print(c);
  c.valor = "x";
  print(c.valor else "vazio");
}
```

```saida
Caixa[string]{valor: null, usos: 0}
x
```

## 17.4 Uma cópia por combinação de tipos

Cada combinação vira um struct comum, conferido quando é usado — como as funções genéricas. Um
erro na cópia vem com o rastro:

```cinza indice.cinza
struct Indice[K] {
  dict<K, i32> posicoes;
}

fn main() {
  Indice[string] ok = new Indice[string]();
  Indice[f64] ruim = new Indice[f64]();
}
```

```erro
indice.cinza:2:3: SemanticError: Chave de dict não pode ser 'f64': comparação exata de ponto flutuante não é confiável. Use i32, i64, string, bool ou enum.
    em Indice[f64], usada em indice.cinza:7
```

Duas cópias são dois tipos, e nenhuma se atribui à outra — a invariância do capítulo 7:

```cinza par.cinza
struct Par[A, B] {
  A primeiro;
  B segundo;
}

fn main() {
  Par[string, i32] p = new Par[string, i32]("a", 1);
  Par[string, i64] q = p;
}
```

```erro
par.cinza:8:20: SemanticError: Tipo incompatível na declaração de 'q': declarado 'Par[string, i64]', mas atribuído 'Par[string, i32]'
```

Um struct genérico não contém a si mesmo por valor, mas pode conter uma lista de si mesmo — uma
árvore:

```cinza
struct No[T] {
  T valor;
  list<No[T]> filhos;
}

fn soma(No[i32] n) -> i32 {
  i32 s = n.valor;
  for (No[i32] f in n.filhos) {
    s += soma(f);
  }
  return s;
}

fn main() {
  No[i32] raiz = new No[i32](valor: 1);
  raiz.filhos.add(new No[i32](valor: 2));
  raiz.filhos.add(new No[i32](valor: 3));
  print(soma(raiz));
}
```

```saida
6
```

E uma função genérica recebe structs genéricos, deduzindo os tipos de dentro deles:

```cinza
struct Par[A, B] {
  A primeiro;
  B segundo;
}

fn troca[A, B](Par[A, B] p) -> Par[B, A] {
  return new Par[B, A](p.segundo, p.primeiro);
}

fn main() {
  Par[string, i32] p = new Par[string, i32]("ana", 30);
  print(troca(p));
}
```

```saida
Par[i32, string]{primeiro: 30, segundo: "ana"}
```

## 17.5 Classes genéricas

Uma classe declara os parâmetros de tipo do mesmo jeito, e eles valem nos campos, no construtor e
nos métodos:

```cinza
class Pilha[T] {
  list<T> itens;
  pub {
    fn poe(T x) {
      itens.add(x);
    }
    fn tira() -> T? {
      if (itens.size() == 0) {
        return null;
      }
      T x = itens[itens.size() - 1];
      itens.remove(itens.size() - 1);
      return x;
    }
    fn tamanho() -> i32 {
      return itens.size();
    }
  }
}

fn main() {
  Pilha[i32] p = new Pilha[i32]();
  p.poe(3);
  p.poe(9);
  print(p.tira(), p.tamanho(), type(p));
  Pilha[string] nomes = new Pilha[string]();
  nomes.poe("ana");
  print(nomes.tira() else "vazia", nomes.tira() else "vazia");
}
```

```saida
9 1 Pilha[i32]
ana vazia
```

Dentro da classe, ela mesma é `Pilha[T]`; o construtor tem o nome puro, `Pilha(...)`, sem os
colchetes. Um campo `T` sem valor inicial é obrigatório, e o construtor precisa atribuí-lo (seção
12.5) — em cada cópia:

```cinza
class Caixa[T] {
  T valor;
  pub {
    Caixa(T v) {
      valor = v;
    }
    fn pega() -> T {
      return valor;
    }
  }
}

fn main() {
  Caixa[string] c = new Caixa[string]("ana");
  print(c.pega());
}
```

```saida
ana
```

Os métodos não têm parâmetros de tipo próprios (seção 16.9): usam os da classe. Para um método
que serviria a outros tipos, escreva uma função genérica que recebe o objeto.

## 17.6 O corpo de cada método, quando ele é usado

Numa cópia de uma classe, os campos e as **assinaturas** de todos os métodos são conferidos já. O
**corpo** de um método só é conferido quando ele é **usado** — chamado, exigido por uma interface
que a classe cumpre, ou chamado por outro método usado. Assim, uma `Pilha[Ponto]` existe mesmo que
a classe tenha um `maior()` que compara com `>` — que um struct não tem —, enquanto ninguém o
chamar:

```cinza
struct Ponto {
  i32 x = 0;
}

class Pilha[T] {
  list<T> itens;
  pub {
    fn poe(T x) {
      itens.add(x);
    }
    fn maior() -> T {
      T m = itens[0];
      for (T x in itens) {
        if (x > m) {
          m = x;
        }
      }
      return m;
    }
  }
}

fn main() {
  Pilha[i32] n = new Pilha[i32]();
  n.poe(4);
  n.poe(7);
  print(n.maior());
  Pilha[Ponto] p = new Pilha[Ponto]();
  p.poe(new Ponto(1));
  print("pontos guardados");
}
```

```saida
7
pontos guardados
```

Chamar `p.maior()` com a `Pilha[Ponto]` é que daria o erro, no `x > m`, com o rastro:

```cinza maior.cinza
struct Ponto {
  i32 x = 0;
}

class Pilha[T] {
  list<T> itens;
  pub {
    fn poe(T x) {
      itens.add(x);
    }
    fn maior() -> T {
      T m = itens[0];
      for (T x in itens) {
        if (x > m) {
          m = x;
        }
      }
      return m;
    }
  }
}

fn main() {
  Pilha[Ponto] p = new Pilha[Ponto]();
  p.poe(new Ponto(1));
  print(p.maior());
}
```

```erro
maior.cinza:14:15: SemanticError: Comparação '>' não é suportada entre 'Ponto' e 'Ponto'
    em Pilha[Ponto], usada em maior.cinza:24
```

> **Por quê?** Uma classe de coleção costuma ter métodos que só fazem sentido para alguns tipos: o
> maior de uma pilha de números, a soma de uma lista de valores. Se a classe inteira tivesse de
> valer para cada `T`, seria preciso dividi-la em várias, ou declarar o que `T` precisa ter — as
> restrições que a Cinza não tem (seção 16.3). Conferindo método a método, a classe oferece o que
> pode, e cada tipo usa o que tem.

## 17.7 Interfaces genéricas

Uma interface também declara parâmetros de tipo. Cada uso — `Comparavel[Nota]` — é uma interface
comum, que uma classe cumpre como qualquer outra (capítulo 13):

```cinza
interface Comparavel[T] {
  fn compara(T outro) -> i32;
}

class Nota : Comparavel[Nota] {
  i32 valor;
  pub {
    Nota(i32 v) {
      valor = v;
    }
    fn compara(Nota outra) -> i32 {
      return valor - outra.valor;
    }
    fn mostra() -> i32 {
      return valor;
    }
  }
}

fn maior[T](list<T> l) -> T {
  T m = l[0];
  for (T x in l) {
    if (x.compara(m) > 0) {
      m = x;
    }
  }
  return m;
}

fn main() {
  Comparavel[Nota] c = new Nota(5);
  print(c.compara(new Nota(3)), maior([new Nota(4), new Nota(9), new Nota(2)]).mostra());
}
```

```saida
2 9
```

`Nota` cumpre `Comparavel[Nota]`: comparável com outra nota. E a função genérica `maior` funciona
com qualquer `T` que tenha um `compara` — a conferência no uso não pergunta se `T` cumpre a
interface, só se o corpo vale.

Uma classe genérica pode cumprir uma interface com os parâmetros dela. Uma `Pilha[T]` e uma
`Fila[T]` que cumprem `Colecao[T]` vão juntas numa lista de `Colecao[i32]`:

```cinza
interface Colecao[T] {
  fn poe(T x);
  fn tamanho() -> i32;
}

class Pilha[T] : Colecao[T] {
  list<T> itens;
  pub {
    fn poe(T x) {
      itens.add(x);
    }
    fn tamanho() -> i32 {
      return itens.size();
    }
  }
}

class Fila[T] : Colecao[T] {
  list<T> itens;
  pub {
    fn poe(T x) {
      itens.add(x);
    }
    fn tamanho() -> i32 {
      return itens.size();
    }
  }
}

fn enche(Colecao[i32] c, i32 n) {
  for (i32 i in range(0, n)) {
    c.poe(i);
  }
}

fn main() {
  list<Colecao[i32]> todas = [new Pilha[i32](), new Fila[i32]()];
  enche(todas[0], 3);
  enche(todas[1], 5);
  for (Colecao[i32] c in todas) {
    print(c.tamanho());
  }
}
```

```saida
3
5
```

Na cópia, a assinatura é conferida com os tipos concretos — e o erro vem com o rastro:

```cinza colecao.cinza
interface Colecao[T] {
  fn poe(T x);
}

class Pilha[T] : Colecao[T] {
  list<T> itens;
  pub {
    fn poe(i32 x) {}
  }
}

fn main() {
  Pilha[string] p = new Pilha[string]();
}
```

```erro
colecao.cinza:8:5: SemanticError: O método poe(i32) da classe 'Pilha[string]' não tem a assinatura exigida por 'Colecao[string]': poe(string).
    em Pilha[string], usada em colecao.cinza:13
```

Uma classe cumpre **no máximo uma cópia** de cada interface genérica: `Comparavel[i32]` e
`Comparavel[string]` exigiriam dois métodos `compara`, e não há sobrecarga.

```cinza duas.cinza
interface Comparavel[T] {
  fn compara(T outro) -> i32;
}

class A : Comparavel[i32], Comparavel[string] {
  pub {
    fn compara(i32 outro) -> i32 {
      return 0;
    }
  }
}

fn main() {}
```

```erro
duas.cinza:5:28: SemanticError: A classe 'A' já cumpre 'Comparavel[i32]': no máximo uma cópia de cada interface genérica (as duas exigiriam métodos com o mesmo nome, e a Cinza não tem sobrecarga)
```

> **Cuidado.** Numa função genérica, os tipos não são deduzidos **pela interface** que uma classe
> cumpre. Com `fn mapeia[A, B](const list<A> l, Transforma[A, B] t)`, a chamada
> `mapeia(nomes, new Tamanho())` não descobre `A` e `B` em `Tamanho : Transforma[string, i32]`: o
> compilador diz isso, e os tipos vão na chamada, `mapeia[string, i32](nomes, new Tamanho())`.

## 17.8 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `struct Caixa<T>`, `class Pilha<T>` | `struct Caixa[T]`, `class Pilha[T]` |
| `Pilha<int> p = new Pilha<>()` (Java), `new Pilha<int>()` (C#) | `Pilha[i32] p = new Pilha[i32]();` |
| `Vec::new()` com o tipo inferido (Rust) | os tipos sempre escritos |
| `class Nota implements Comparable<Nota>` | `class Nota : Comparavel[Nota]` |
| métodos genéricos (`<U> U converte()`) | uma função genérica que recebe o objeto |
| `where T : IComparable<T>` | nada: cada método é conferido quando usado |

O compilador reconhece os ângulos, na declaração e no uso:

```cinza angulo.cinza
struct Caixa<T> {
  T valor;
}

fn main() {}
```

```erro
angulo.cinza:1:13: SyntaxError: Os parâmetros de tipo vão entre colchetes, não entre '<' e '>': struct Caixa[T] { ... } (token: '<')
```

```cinza uso.cinza
struct Caixa[T] {
  T valor;
}

fn main() {
  Caixa<i32> c = new Caixa<i32>(valor: 1);
}
```

```erro
uso.cinza:6:8: SyntaxError: Os tipos de um struct ou de uma classe genérica vão entre colchetes: Caixa[i32] (token: '<')
```

## Programa do capítulo: uma coleção genérica de tarefas

A versão 9 do `tarefas` troca a lista de tarefas por uma **coleção genérica**, `Colecao[T]`, com
filtros genéricos, `Filtro[T]` — o filtro do capítulo 13, agora para qualquer elemento. A mesma
coleção guarda as tarefas e as etiquetas:

```cinza tarefas.cinza
// tarefas.cinza — versão 9: uma coleção genérica, com filtros genéricos
import Strings;

enum Prioridade { Baixa, Media, Alta }

struct Tarefa {
  string titulo;
  Prioridade prioridade = Prioridade.Media;
  bool feita = false;
}

interface Filtro[T] {
  fn aceita(T x) -> bool;
}

class Colecao[T] {
  list<T> itens;

  pub {
    fn poe(T x) {
      itens.add(x);
    }

    fn item(i32 i) -> T {
      return itens[i];
    }

    fn troca(i32 i, T x) {
      itens[i] = x;
    }

    fn filtra(Filtro[T] f) -> list<T> {
      list<T> r;
      for (T x in itens) {
        if (f.aceita(x)) {
          r.add(x);
        }
      }
      return r;
    }

    fn primeiro(Filtro[T] f) -> i32? {
      for (i32 i in range(0, itens.size())) {
        if (f.aceita(itens[i])) {
          return i;
        }
      }
      return null;
    }

    fn maior() -> T? {
      if (itens.size() == 0) {
        return null;
      }
      T m = itens[0];
      for (T x in itens) {
        if (x > m) {
          m = x;
        }
      }
      return m;
    }
  }
}

class Pendente : Filtro[Tarefa] {
  pub {
    fn aceita(Tarefa t) -> bool {
      return not t.feita;
    }
  }
}

class DeTitulo : Filtro[Tarefa] {
  string titulo;
  pub {
    DeTitulo(string titulo) {
      self.titulo = titulo;
    }
    fn aceita(Tarefa t) -> bool {
      return t.titulo == titulo;
    }
  }
}

class Comeca : Filtro[string] {
  string prefixo;
  pub {
    Comeca(string p) {
      prefixo = p;
    }
    fn aceita(string s) -> bool {
      return Strings.starts_with(s, prefixo);
    }
  }
}

fn conclui(Colecao[Tarefa] c, string titulo) {
  i32 i = c.primeiro(new DeTitulo(titulo)) else {
    print("não há a tarefa", titulo);
    return;
  };
  Tarefa t = c.item(i);
  t.feita = true;
  c.troca(i, t);
}

fn main() {
  Colecao[Tarefa] tarefas = new Colecao[Tarefa]();
  tarefas.poe(new Tarefa(titulo: "comprar pão", prioridade: Prioridade.Alta));
  tarefas.poe(new Tarefa(titulo: "estudar a Cinza"));
  tarefas.poe(new Tarefa(titulo: "lavar o carro", prioridade: Prioridade.Baixa));
  conclui(tarefas, "comprar pão");
  conclui(tarefas, "pintar a casa");
  for (Tarefa t in tarefas.filtra(new Pendente())) {
    print("pendente:", t.titulo, t.prioridade);
  }

  Colecao[string] etiquetas = new Colecao[string]();
  for (string e in ["casa", "trabalho", "casa/cozinha", "estudo"]) {
    etiquetas.poe(e);
  }
  print(etiquetas.filtra(new Comeca("casa")), etiquetas.maior() else "nenhuma");
}
```

```saida
não há a tarefa pintar a casa
pendente: estudar a Cinza Prioridade.Media
pendente: lavar o carro Prioridade.Baixa
["casa", "casa/cozinha"] trabalho
```

Repare em:

- `Colecao[Tarefa]` existe, embora `maior` compare com `>` e um struct não tenha `>`: ninguém chama
  `tarefas.maior()`, e o corpo de `maior` só é conferido na cópia `Colecao[string]`, onde ele é
  usado (e onde `>` compara textos).
- `filtra` recebe um `Filtro[T]`: na cópia `Colecao[Tarefa]`, um `Filtro[Tarefa]` (`Pendente`,
  `DeTitulo`); na `Colecao[string]`, um `Filtro[string]` (`Comeca`). Passar um `Comeca` para
  `tarefas.filtra` seria um erro de tipo.
- `conclui` pega a tarefa com `item`, altera e devolve com `troca`: a tarefa é um struct, um valor
  (seção 9.3), e `c.item(i).feita = true` alteraria uma cópia que ninguém vê — o compilador recusa
  essa atribuição.

## Resumo

- `struct Nome[T]`, `class Nome[T]`, `interface Nome[T]`: os parâmetros de tipo valem nos campos,
  no construtor e nos métodos.
- No uso, os tipos são sempre escritos: `Pilha[i32] p = new Pilha[i32]();`. `Pilha` sozinho é erro.
- Cada combinação de tipos é um tipo comum (`type(p)` é `Pilha[i32]`), e duas cópias diferentes não
  se atribuem uma à outra.
- Um campo `T` não tem valor inicial e é obrigatório; um `T?` nasce `null`.
- Numa classe genérica, os campos e as assinaturas são conferidos já; o corpo de cada método, quando
  ele é usado.
- Uma classe cumpre uma cópia de uma interface genérica (`: Comparavel[Nota]`, `: Colecao[T]`), no
  máximo uma de cada.

## Exercícios

1. Declare `struct Intervalo[T] { T inicio; T fim; }` e escreva `fn contem[T](Intervalo[T] i, T x)
   -> bool`. Use-a com números e com textos.
2. Escreva `class Fila[T]`, com `entra(T x)`, `sai() -> T?` (o primeiro que entrou) e
   `vazia() -> bool`, e use-a com textos.
3. No programa do capítulo, o que acontece se a `main` chamar `tarefas.maior()`? Por que o programa
   compila sem essa chamada?
4. Escreva `class Contador[T]`, que conta quantas vezes cada valor foi registrado (`registra(T x)`,
   `quantas(T x) -> i32`), guardando um `dict<T, i32>`. O que acontece com um `Contador[f64]`?
5. Declare `interface Transforma[A, B] { fn aplica(A x) -> B; }`, uma classe `Tamanho :
   Transforma[string, i32]` e `fn mapeia[A, B](const list<A> l, Transforma[A, B] t) -> list<B>`.
   Mapeie uma lista de nomes para os tamanhos deles.

As soluções estão em [`solucoes/17-genericos-tipos.md`](solucoes/17-genericos-tipos.md).

## Na spec

- Structs genéricos: [8.6](../spec/08-genericos.md)
- Classes e interfaces genéricas: [8.7](../spec/08-genericos.md)
- O rastro e o limite das cópias: [8.4](../spec/08-genericos.md)
