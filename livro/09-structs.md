# 9. Structs

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar um struct, com campos obrigatórios e com valores iniciais, e criá-lo dos três jeitos;
- explicar por que um struct é um **valor** — copiado como um número — e o que isso muda;
- alterar structs guardados em listas e dicionários;
- decidir quando um struct basta e quando é hora de uma classe.

**Pré-requisitos.** Os capítulos 7 e 8.

---

Um `struct` agrupa campos com nome num tipo só: um ponto com `x` e `y`, um contato com nome e
telefone, uma tarefa com título e prioridade. Ele não tem métodos nem campos privados — é **dado**,
à vista. E, ao contrário de uma lista ou de um objeto de classe, um struct é um **valor**: cada
atribuição, argumento e retorno leva uma cópia dele.

## 9.1 Declarar e criar

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

fn main() {
  Ponto a = new Ponto();
  Ponto b = new Ponto(3, 4);
  Ponto c = new Ponto(y: 7);
  print(a, b, c, b.x + b.y);
  c.x = 1;
  print(c, type(c));
}
```

```saida
Ponto{x: 0, y: 0} Ponto{x: 3, y: 4} Ponto{x: 0, y: 7} 7
Ponto{x: 1, y: 7} Ponto
```

Um struct se cria com `new`, de três formas:

| Forma | Os campos |
|-------|-----------|
| `new Ponto()` | todos com o valor inicial |
| `new Ponto(3, 4)` | **todos**, na ordem da declaração |
| `new Ponto(y: 7)` | pelo nome, em qualquer ordem; os omitidos ficam com o valor inicial |

Os campos são todos públicos: se lê e se grava `p.x` de qualquer lugar.

## 9.2 Campos obrigatórios

Um campo pode ter valor inicial (`i32 x = 0;`) ou não. **Um campo sem valor inicial é obrigatório**:
quem cria o struct precisa informá-lo. Assim nenhum struct existe com um campo sem valor.

```cinza
struct Contato {
  string nome;
  string telefone = "";
  i32 prioridade = 1;
}

fn main() {
  Contato a = new Contato(nome: "Ana");
  Contato b = new Contato(prioridade: 3, nome: "Bia");
  Contato c = new Contato("Caio", "1199", 2);
  print(a);
  print(b);
  print(c.telefone);
}
```

```saida
Contato{nome: "Ana", telefone: "", prioridade: 1}
Contato{nome: "Bia", telefone: "", prioridade: 3}
1199
```

Esquecer um obrigatório é erro de compilação, e a mensagem diz qual:

```cinza contato.cinza
struct Contato {
  string nome;
  string telefone = "";
}
fn main() {
  Contato c = new Contato(telefone: "1199");
}
```

```erro
contato.cinza:6:15: SemanticError: Na criação de 'Contato', falta o campo obrigatório 'nome' (ele não tem valor inicial): informe-o, ex.: new Contato(nome: ...)
```

As outras formas também são conferidas: um número parcial de argumentos sem nome, um nome que não
é campo, um campo repetido, a mistura de argumentos com e sem nome.

Três tipos de campo **nascem com valor** mesmo sem `=`, e por isso não são obrigatórios: `list` e
`dict` nascem vazios, e o opcional `T?` (capítulo 11) nasce `null`.

```cinza
struct Turma {
  string nome;
  list<string> alunos;
  dict<string, f64> notas;
  string? professor;
}

fn main() {
  Turma t = new Turma(nome: "A");
  t.alunos.add("Ana");
  t.notas.add({"Ana", 9.5ff});
  print(t);
}
```

```saida
Turma{nome: "A", alunos: ["Ana"], notas: {"Ana": 9.5}, professor: null}
```

> **Vindo de outra linguagem.** Os argumentos pelo nome existem **só** aqui, na criação de um
> struct (numa chamada de função, eles não existem: seção 5.6). É onde eles mais ajudam: um struct
> costuma ter vários campos com valor padrão, e `new Contato(nome: "Ana")` diz o que está sendo
> informado sem depender da ordem.

## 9.3 Um struct é um valor

Esta é a propriedade que separa o struct de tudo o que vem depois no livro: **atribuir um struct
copia os campos**. As duas variáveis passam a ter dois structs independentes:

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

fn main() {
  Ponto a = new Ponto(1, 2);
  Ponto b = a;
  b.x = 10;
  print(a, b);
}
```

```saida
Ponto{x: 1, y: 2} Ponto{x: 10, y: 2}
```

O mesmo vale para os argumentos e o retorno. Uma função recebe a sua cópia e pode alterá-la à
vontade; quem chama só vê o resultado se a função o devolver:

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

fn move(Ponto p, i32 dx) -> Ponto {
  p.x += dx;
  return p;
}

fn main() {
  Ponto a = new Ponto(1, 2);
  Ponto b = move(a, 5);
  print(a, b);
}
```

```saida
Ponto{x: 1, y: 2} Ponto{x: 6, y: 2}
```

A igualdade de structs é estrutural: dois structs são iguais quando os campos são iguais
(`new Ponto(1, 2) == new Ponto(1, 2)` é `true`).

> **Vindo de outra linguagem.** É o `struct` do C, do C# e do Go. Em Java, em Python e em
> JavaScript, um objeto é sempre compartilhado — `b = a` faz as duas variáveis verem o mesmo
> objeto, e `b.x = 10` muda `a.x` também. Na Cinza esse é o comportamento das **classes**
> (capítulo 12); o struct é a ferramenta para o dado que deve se comportar como um número.

## 9.4 A cópia é rasa

A cópia de um struct copia **os campos** um a um, como numa atribuição. Um campo que é uma lista,
um dicionário ou um objeto continua apontando para a mesma coleção ou o mesmo objeto:

```cinza
struct Turma {
  string nome;
  list<string> alunos;
}

fn main() {
  Turma a = new Turma(nome: "A");
  Turma b = a;
  b.nome = "B";
  b.alunos.add("Ana");
  print(a, b);
}
```

```saida
Turma{nome: "A", alunos: ["Ana"]} Turma{nome: "B", alunos: ["Ana"]}
```

O nome de `b` mudou só em `b`, mas o aluno acrescentado aparece nos dois: `a.alunos` e `b.alunos` são
a mesma lista. Para um struct com coleções totalmente independente, copie também as coleções
(`b.alunos = Lists.copy(a.alunos)`).

> **Por quê?** Copiar tudo, em profundidade, a cada atribuição seria caro e às vezes impossível
> (um objeto que aponta para outro, que aponta de volta). A cópia rasa é previsível: o struct se
> comporta como um valor; o que ele **contém** se comporta como o seu próprio tipo.

## 9.5 Structs dentro de coleções

Um struct guardado numa lista ou num dicionário é alterado **no lugar** pelo índice ou pela chave:

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

fn main() {
  list<Ponto> pontos = [new Ponto(1, 1), new Ponto(2, 2)];
  pontos[0].x = 100;
  dict<string, Ponto> lugares = {{"casa", new Ponto(1, 2)}};
  lugares["casa"].x = 9;
  Ponto p = pontos[1];
  p.x = 200;
  print(pontos, lugares);
}
```

```saida
[Ponto{x: 100, y: 1}, Ponto{x: 2, y: 2}] {"casa": Ponto{x: 9, y: 2}}
```

`pontos[0].x = 100` mexe no struct que está na lista. Já `Ponto p = pontos[1]` tira uma **cópia**: o
`p.x = 200` não chega à lista. Pelo mesmo motivo, o iterador de um `for` é uma cópia; para alterar
os structs de uma lista, percorra os índices:

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

fn main() {
  list<Ponto> pontos = [new Ponto(1, 1), new Ponto(2, 2)];
  for (Ponto p in pontos) {
    p.x = 0;
  }
  print(pontos);
  for (i32 i in range(0, pontos.size())) {
    pontos[i].x = 0;
  }
  print(pontos);
}
```

```saida
[Ponto{x: 1, y: 1}, Ponto{x: 2, y: 2}]
[Ponto{x: 0, y: 1}, Ponto{x: 0, y: 2}]
```

### Temporários

Atribuir a um campo de um struct que não está guardado em lugar nenhum — o resultado de um `new`
ou de uma chamada — seria alterar uma cópia que ninguém vê. É erro:

```cinza origem.cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}
fn origem() -> Ponto {
  return new Ponto();
}
fn main() {
  origem().x = 1;
}
```

```erro
origem.cinza:9:11: SemanticError: Atribuição a 'origem().x' não tem efeito: o struct é uma cópia temporária. Guarde-o numa variável, altere e use a variável.
```

## 9.6 Structs dentro de structs

Um campo pode ser outro struct — e ele também é parte do valor, copiado junto:

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

struct Retangulo {
  Ponto canto = new Ponto();
  i32 largura = 1;
  i32 altura = 1;
}

fn main() {
  Retangulo r = new Retangulo(largura: 4, altura: 3);
  r.canto.x = 10;
  print(r, r.largura * r.altura);
}
```

```saida
Retangulo{canto: Ponto{x: 10, y: 0}, largura: 4, altura: 3} 12
```

Um struct **não pode conter a si mesmo por valor** — o valor seria infinito. Dentro de uma lista,
pode: é assim que se faz uma árvore.

```cinza no.cinza
struct No {
  i32 valor = 0;
  No proximo;
}
fn main() { }
```

```erro
no.cinza:1:1: SemanticError: Struct 'No' contém a si mesmo por valor (No → No): o valor seria infinito. Use list<No> ou uma class para estruturas recursivas.
```

```cinza
struct No {
  i32 valor = 0;
  list<No> filhos;
}

fn soma(No n) -> i32 {
  i32 s = n.valor;
  for (No f in n.filhos) {
    s += soma(f);
  }
  return s;
}

fn main() {
  No raiz = new No(valor: 1);
  raiz.filhos.add(new No(valor: 2));
  raiz.filhos.add(new No(valor: 3));
  print(soma(raiz));
}
```

```saida
6
```

## 9.7 O que um struct não tem

Um struct só tem campos: sem métodos, sem construtor, sem o bloco `pub`. Quando o dado precisa de
comportamento ou de campos escondidos, é uma **classe** (capítulo 12):

```cinza norma.cinza
struct Ponto {
  i32 x = 0;
  fn norma() -> i32 { return x; }
}
fn main() { }
```

```erro
norma.cinza:3:3: SyntaxError: struct 'Ponto' só pode ter campos: métodos e o bloco 'pub' não são permitidos em struct (todos os campos já são públicos). Use 'class' se precisar de métodos. (token: 'fn')
```

O "comportamento" de um struct fica em funções que o recebem — `fn move(Ponto p, i32 dx) -> Ponto`,
acima — e que, por receberem uma cópia, devolvem o struct novo.

E todo struct se cria com `new`. As formas de outras linguagens são reconhecidas:

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `Ponto(1, 2)` (Python, Kotlin) | `new Ponto(1, 2)` |
| `Ponto p = {1, 2};` (C, C++) | `Ponto p = new Ponto(1, 2);` |
| `Ponto{x: 1, y: 2}` (Go, Rust) | `new Ponto(x: 1, y: 2)` |

```cinza ponto.cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}
fn main() {
  Ponto p = Ponto(1, 2);
}
```

```erro
ponto.cinza:6:13: SemanticError: 'Ponto' não é uma função: um struct ou uma classe se cria com new Ponto(...)
```

### Struct ou classe?

| Use um struct quando... | Use uma classe quando... |
|-------------------------|--------------------------|
| é um dado: um ponto, uma data, uma tarefa | é algo com estado que muda por regras: uma conta, uma agenda |
| copiar faz sentido (dois pontos iguais são o mesmo ponto) | a identidade importa (duas contas com o mesmo saldo são contas diferentes) |
| todos os campos podem ficar à vista | os campos precisam de proteção (só os métodos mexem neles) |

## Programa do capítulo: a tarefa vira um struct

A versão 1 do `tarefas` (capítulo 7) tinha duas listas de textos, que andavam juntas e podiam se
desencontrar. Na versão 2, **a tarefa é um struct** com título, prioridade e se já foi feita — tudo
num lugar só, numa lista só:

```cinza tarefas.cinza
// tarefas.cinza — versão 2: a tarefa é um struct
import Lists;

struct Tarefa {
  string titulo;          // obrigatório
  i32 prioridade = 2;     // 1 alta, 2 normal, 3 baixa
  bool feita = false;
}

fn conclui(list<Tarefa> tarefas, string titulo) {
  for (i32 i in range(0, tarefas.size())) {
    if (tarefas[i].titulo == titulo) {
      tarefas[i].feita = true;
      return;
    }
  }
  print("não achei:", titulo);
}

fn mostra(list<Tarefa> tarefas) {
  list<Tarefa> ordem = Lists.copy(tarefas);
  list<i32> chaves;
  for (Tarefa t in ordem) {
    chaves.add(t.prioridade);
  }
  Lists.sort_by(ordem, chaves);
  for (Tarefa t in ordem) {
    string marca = "[ ]";
    if (t.feita) {
      marca = "[x]";
    }
    printf("{marca} {t.prioridade} {t.titulo}");
  }
}

fn main() {
  list<Tarefa> tarefas = [
    new Tarefa(titulo: "comprar pão"),
    new Tarefa(titulo: "pagar a conta de luz", prioridade: 1),
    new Tarefa(titulo: "estudar a Cinza")
  ];
  tarefas.add(new Tarefa("arrumar a garagem", 3, false));
  conclui(tarefas, "pagar a conta de luz");
  conclui(tarefas, "lavar o carro");
  mostra(tarefas);
}
```

```saida
não achei: lavar o carro
[x] 1 pagar a conta de luz
[ ] 2 comprar pão
[ ] 2 estudar a Cinza
[ ] 3 arrumar a garagem
```

Repare em:

- `new Tarefa(titulo: "comprar pão")` informa só o obrigatório; prioridade e situação ficam com o
  valor inicial.
- `conclui` altera `tarefas[i].feita` pelo índice: o struct que está na lista. Um `for (Tarefa t in
  tarefas)` com `t.feita = true` mudaria só a cópia.
- `mostra` ordena uma **cópia** da lista (`Lists.copy`), para não mudar a ordem das tarefas de quem
  chamou; o `sort_by` estável mantém, entre as de mesma prioridade, a ordem em que foram criadas.

A prioridade ainda é um número com um comentário dizendo o que cada um significa — um convite a
erros como `prioridade: 7`. O capítulo 10 troca o número por um enum.

## Resumo

- `struct Nome { campos }`, só com campos, todos públicos. Criação com `new`: `new P()`, todos em
  ordem, ou pelo nome (os omitidos com o valor inicial).
- Campo sem valor inicial é obrigatório na criação (menos `list`, `dict` e `T?`).
- Um struct é um **valor**: atribuição, argumento e retorno copiam os campos (cópia rasa: as
  coleções dentro continuam compartilhadas). `==` compara os campos.
- Dentro de uma lista ou dicionário, `l[i].campo = v` altera no lugar; o iterador do `for` e
  `P p = l[i]` são cópias; atribuir a um temporário é erro.
- Um struct não contém a si mesmo por valor, mas pode conter uma lista de si mesmo.
- Struct é dado; comportamento e estado protegido são de uma classe.

## Exercícios

1. Declare `struct Fracao { i64 num; i64 den = 1; }` e escreva `fn soma(Fracao a, Fracao b) ->
   Fracao`, que devolve a fração somada e simplificada (use o `mdc` do capítulo 5). Teste com 1/2 +
   1/3 e com 1/4 + 1/4.
2. Sem rodar, diga o que o programa imprime, e por quê:

   ```text
   struct C { i32 n = 0; list<i32> l; }
   fn main() {
     C a = new C();
     C b = a;
     b.n = 1;
     b.l.add(1);
     print(a, b);
   }
   ```

3. Com `struct Produto { string nome; f64 preco; }`, escreva `fn com_desconto(list<Produto> l, f64
   pct)`, que aplica o desconto **na própria lista**, e `fn com_desconto_copia(list<Produto> l, f64
   pct) -> list<Produto>`, que devolve uma lista nova e deixa a original como estava.
4. Com o `struct No` da seção 9.6, escreva `fn altura(No n) -> i32`, a quantidade de níveis da
   árvore (uma folha tem altura 1). Monte uma árvore de três níveis e confira.
5. Escreva `struct Intervalo { i32 ini; i32 fim; }` e `fn sobrepoe(Intervalo a, Intervalo b) ->
   bool`, que diz se dois intervalos fechados têm algum número em comum. Teste com `[1, 5]` e `[5,
   9]` (sim), e com `[1, 4]` e `[5, 9]` (não).

As soluções estão em [`solucoes/09-structs.md`](solucoes/09-structs.md).

## Na spec

- A declaração, os campos obrigatórios, a criação e os temporários: [4.7](../spec/04-declaracoes.md)
- Valor e cópia rasa: [5.1](../spec/05-execucao.md)
- A igualdade estrutural: [5.3](../spec/05-execucao.md)
- O campo `T?`, que nasce `null`: [3.10](../spec/03-tipos.md)
