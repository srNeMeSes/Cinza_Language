# 10. Enums e tipos como valores

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar um enum e usar os seus valores em variáveis, structs, listas e dicionários;
- explicar por que um valor de enum é só um nome, sem número por trás;
- descobrir e comparar o tipo de um valor com `type(x)`;
- entender o que a Cinza deixa de fora de propósito: a reflexão.

**Pré-requisitos.** Os capítulos 8 e 9.

---

Um `enum` declara um tipo com um conjunto fixo de valores: as cores de um semáforo, os dias da
semana, a prioridade de uma tarefa. Na Cinza esses valores são **só nomes** — `Cor.Verde` não é o
número 1 disfarçado. E `type(x)` mostra o tipo de qualquer valor, o que serve para conferir e para
comparar, mas não para inspecionar o que há dentro dele.

## 10.1 Declarar e usar

```cinza
enum Cor { Vermelho, Verde, Azul }

fn main() {
  Cor c = Cor.Verde;
  print(c, c == Cor.Verde, c != Cor.Azul, type(c));
  c = Cor.Azul;
  print(c);
}
```

```saida
Cor.Verde true true Cor
Cor.Azul
```

- Os valores se escrevem **sempre qualificados**: `Cor.Verde`, nunca `Verde` sozinho.
- Um enum tem pelo menos um valor, e os nomes não se repetem.
- No `print`, o valor aparece com o nome do enum: `Cor.Verde`.

Esquecer a qualificação é o engano mais comum, e a mensagem diz o que escrever:

```cinza semaforo.cinza
enum Cor { Vermelho, Verde, Azul }
fn main() {
  Cor c = Verde;
}
```

```erro
semaforo.cinza:3:11: SemanticError: Variável 'Verde' não declarada: 'Verde' é um valor do enum 'Cor'; escreva Cor.Verde (o valor de um enum é sempre qualificado)
```

## 10.2 Só nomes

Um valor de enum **não é um número**: não se atribui um número a ele, não se faz conta com ele, e
ele não tem ordem. Só `==` e `!=` se aplicam, e só entre valores do mesmo enum.

```cinza ordem.cinza
enum Cor { Vermelho, Verde, Azul }
fn main() {
  print(Cor.Vermelho < Cor.Azul);
}
```

```erro
ordem.cinza:3:22: SemanticError: Comparação '<' entre valores de enum: um enum só tem == e != (os valores não têm ordem nem número)
```

Nem na declaração há número:

```cinza numerado.cinza
enum Cor { Vermelho = 1, Verde = 2 }
fn main() { }
```

```erro
numerado.cinza:1:21: SyntaxError: Os valores de um enum são só nomes, sem número por trás: escreva enum Cor { Vermelho, ... } (para associar um número a cada valor, use um dict<Cor, i32>) (token: '=')
```

> **Por quê?** Em C e em Java, o enum é um número com nome, e o número escapa: `cor + 1`, um `int`
> qualquer convertido em `Cor`, um valor que não existe no enum mas passa pelo tipo. Na Cinza um
> `Cor` só pode ser um dos valores declarados. Quando um número (ou um texto) precisa andar junto
> de cada valor, a associação fica escrita — numa função ou num `dict<Cor, ...>` —, e não escondida
> na ordem da declaração.

## 10.3 Enums em structs e coleções

Um enum é um tipo como os outros: campo de struct, elemento de lista, **chave de dicionário**.

```cinza
enum Cor { Vermelho, Verde, Azul }

struct Carro {
  string modelo;
  Cor cor = Cor.Vermelho;
}

fn main() {
  Carro c = new Carro(modelo: "Fusca", cor: Cor.Azul);
  list<Cor> todas = [Cor.Vermelho, Cor.Verde, Cor.Azul];
  dict<Cor, i32> contagem = {{Cor.Verde, 2}, {Cor.Azul, 1}};
  contagem[Cor.Verde] += 1;
  print(c);
  print(todas, contagem);
}
```

```saida
Carro{modelo: "Fusca", cor: Cor.Azul}
[Cor.Vermelho, Cor.Verde, Cor.Azul] {Cor.Verde: 3, Cor.Azul: 1}
```

Um enum como chave é uma escolha melhor que um texto (`"verde"`): a chave não pode ser escrita
errada, e o compilador confere o tipo.

## 10.4 Escolher pelo valor

Sem `switch` (seção 4.7), a escolha pelo valor é uma cadeia de `if`/`else if`. O último caso fica
fora dos testes: assim a função retorna em todos os caminhos.

```cinza
enum Cor { Vermelho, Verde, Azul }

fn nome(Cor c) -> string {
  if (c == Cor.Vermelho) {
    return "vermelho";
  } else if (c == Cor.Verde) {
    return "verde";
  }
  return "azul";
}

fn main() {
  print(nome(Cor.Verde), nome(Cor.Azul));
}
```

```saida
verde azul
```

Quando cada valor só tem um dado associado, um dicionário diz o mesmo de forma mais compacta:

```cinza
enum Cor { Vermelho, Verde, Azul }

fn main() {
  dict<Cor, string> hex = {{Cor.Vermelho, "#ff0000"}, {Cor.Verde, "#00ff00"}, {Cor.Azul, "#0000ff"}};
  print(hex[Cor.Verde], hex.size());
}
```

```saida
#00ff00 3
```

## 10.5 O texto de um enum

O `print` e a interpolação mostram o valor pelo nome. Já o `+` com texto e o `cast<string>` são só
para os primitivos (números, textos e `bool`), e as mensagens apontam a interpolação:

```cinza
enum Cor { Vermelho, Verde, Azul }

fn main() {
  Cor c = Cor.Azul;
  printf("cor: {c}");
  string s = format("{c}");
  print(s, s.size());
}
```

```saida
cor: Cor.Azul
Cor.Azul 8
```

```cinza rotulo.cinza
enum Cor { Vermelho, Verde, Azul }
fn main() {
  print("cor: " + Cor.Verde);
}
```

```erro
rotulo.cinza:3:17: SemanticError: Operador '+' não é suportado entre 'string' e 'Cor'. O + junta texto com números, textos e bool; para pôr outro valor num texto, use a interpolação: format("... {x}")
```

Para mostrar ao usuário um texto diferente do nome (`"vermelho"` em vez de `Cor.Vermelho`), use a
função ou o dicionário da seção 10.4.

## 10.6 Tipos como valores

`type(x)` devolve o tipo de `x`. Um tipo também pode ser escrito como valor — `i64`,
`list<string>`, o nome de um struct, de uma classe, de um enum — e comparado com `==` e `!=`. No
`print`, aparece pelo nome:

```cinza
struct Ponto { i32 x = 0; }

fn main() {
  list<i64> l = [1];
  print(type(5) == i32, type(l) == list<i64>, type(l) == list<i32>, type(new Ponto()) == Ponto);
  print(type(l), i64, list<string>, type([[1.5f]]), type({"a", 1}), type({{"a", true}}));
}
```

```saida
true true false true
list<i64> i64 list<string> list<list<f32>> pair<string, i32> dict<string, bool>
```

O uso mais comum de `type` é conferir o que o compilador entendeu, como nos capítulos anteriores.
O outro é saber a **classe real** de um valor de interface (capítulo 13):

```cinza
interface Forma { fn area() -> f64; }

class Quadrado : Forma {
  pub {
    fn area() -> f64 { return 4.ff; }
  }
}

class Circulo : Forma {
  pub {
    fn area() -> f64 { return 3.14ff; }
  }
}

fn main() {
  list<Forma> formas = [new Quadrado(), new Circulo()];
  for (Forma f in formas) {
    print(type(f), type(f) == Quadrado);
  }
}
```

```saida
Quadrado true
Circulo false
```

Mas `type(x) == Quadrado` **não estreita** o tipo: dentro do `if`, `f` continua sendo uma `Forma`,
e só os métodos da interface valem. A Cinza não tem o *cast* de volta para a classe (capítulo 13).

Um tipo como valor só serve para comparar: não há variável do tipo `type`, e um tipo não tem
métodos.

```cinza tipo.cinza
fn main() {
  type t = type(1);
}
```

```erro
tipo.cinza:2:8: SemanticError: Não existe variável do tipo 'type': o valor de type(x) só serve para comparar com um tipo (type(x) == i32)
```

## 10.7 O que fica de fora: a reflexão

A Cinza **não tem reflexão**: em execução, de um valor só se sabe o tipo, não os campos nem os
métodos. Não há como listar os valores de um enum, ler um campo pelo nome num texto ou chamar um
método pelo nome:

```cinza valores.cinza
enum Cor { Vermelho, Verde, Azul }
fn main() {
  print(Cor.values());
}
```

```erro
valores.cinza:3:12: SemanticError: Um tipo escrito como valor não tem métodos: ele só serve para comparar com type(x). Para percorrer os valores de um enum, monte a lista: [Cor.A, Cor.B, ...]
```

> **Por quê?** O que um programa faz fica todo escrito no código e conferido na compilação. Com
> reflexão, um texto vindo de fora pode virar o nome de um campo ou de um método — e o compilador
> deixa de saber o que roda. Os casos em que outras linguagens a usam (serializar um objeto em JSON,
> mapear um formulário) a Cinza resolve com código escrito, que diz o que vai para onde (capítulo
> 22).

A lista de todos os valores de um enum se escreve uma vez, numa constante ou numa função, e é
usada onde for preciso — como a `list<Cor> todas` da seção 10.3.

## Programa do capítulo: prioridade e situação

Na versão 2 do `tarefas` (capítulo 9), a prioridade era um número com um comentário e a situação
um `bool`. Na versão 3, **as duas viram enums**: a prioridade não pode mais ser 7, e a situação
ganha um terceiro estado, "em andamento", que um `bool` não tinha como representar.

```cinza tarefas.cinza
// tarefas.cinza — versão 3: prioridade e situação como enums
import Lists;
import Dicts;

enum Prioridade { Alta, Normal, Baixa }
enum Situacao { Pendente, Andamento, Feita }

struct Tarefa {
  string titulo;
  Prioridade prioridade = Prioridade.Normal;
  Situacao situacao = Situacao.Pendente;
}

fn peso(Prioridade p) -> i32 {
  if (p == Prioridade.Alta) {
    return 1;
  } else if (p == Prioridade.Normal) {
    return 2;
  }
  return 3;
}

fn marca(Situacao s) -> string {
  if (s == Situacao.Feita) {
    return "[x]";
  } else if (s == Situacao.Andamento) {
    return "[~]";
  }
  return "[ ]";
}

fn muda(list<Tarefa> tarefas, string titulo, Situacao nova) {
  for (i32 i in range(0, tarefas.size())) {
    if (tarefas[i].titulo == titulo) {
      tarefas[i].situacao = nova;
      return;
    }
  }
  print("não achei:", titulo);
}

fn main() {
  list<Tarefa> tarefas = [
    new Tarefa(titulo: "comprar pão"),
    new Tarefa(titulo: "pagar a conta de luz", prioridade: Prioridade.Alta),
    new Tarefa(titulo: "estudar a Cinza"),
    new Tarefa(titulo: "arrumar a garagem", prioridade: Prioridade.Baixa)
  ];
  muda(tarefas, "pagar a conta de luz", Situacao.Feita);
  muda(tarefas, "estudar a Cinza", Situacao.Andamento);

  list<i32> chaves;
  for (Tarefa t in tarefas) {
    chaves.add(peso(t.prioridade));
  }
  Lists.sort_by(tarefas, chaves);

  dict<Situacao, i32> resumo;
  for (Tarefa t in tarefas) {
    printf("{marca(t.situacao)} {t.titulo} ({t.prioridade})");
    resumo.add({t.situacao, (Dicts.get(resumo, t.situacao) else 0) + 1});
  }
  print(resumo);
}
```

```saida
[x] pagar a conta de luz (Prioridade.Alta)
[ ] comprar pão (Prioridade.Normal)
[~] estudar a Cinza (Prioridade.Normal)
[ ] arrumar a garagem (Prioridade.Baixa)
{Situacao.Feita: 1, Situacao.Pendente: 2, Situacao.Andamento: 1}
```

Repare em:

- `peso` é a associação, escrita, entre a prioridade e a ordem: como os valores não têm número, é
  ela que diz que `Alta` vem antes de `Normal`.
- `resumo` usa o enum como chave, e a ordem das entradas é a de inserção — a da primeira tarefa de
  cada situação na lista já ordenada.
- O `sort_by` ordena a própria lista `tarefas` (aqui, diferente da versão 2, a ordem por prioridade
  passa a ser a da lista).

As tarefas ainda vivem numa lista solta, que qualquer função pode alterar de qualquer jeito — e
"buscar uma tarefa pelo título" ainda não tem um jeito de dizer "não achei" sem imprimir. O capítulo
11 resolve o segundo ponto com o opcional; o capítulo 12, o primeiro, com uma classe.

## Resumo

- `enum Nome { A, B, C }`: valores sempre qualificados (`Nome.A`), só nomes, sem número, sem
  ordem; só `==` e `!=` entre valores do mesmo enum.
- Um enum é um tipo como os outros: campo, elemento, chave de dicionário (uma boa chave).
- A escolha pelo valor é uma cadeia de `if`/`else if`, ou um `dict` quando há um dado por valor.
- O `print` e a interpolação mostram `Nome.A`; `+` com texto e `cast<string>` não aceitam enum.
- `type(x)` e os tipos escritos como valor (`i64`, `list<i64>`, `Ponto`) se comparam com `==`; não
  estreitam, não viram variável e não têm métodos.
- Sem reflexão: a lista dos valores de um enum se escreve à mão.

## Exercícios

1. Declare `enum Dia { Seg, Ter, Qua, Qui, Sex, Sab, Dom }` e escreva `fn fim_de_semana(Dia d) ->
   bool`. Imprima o resultado para todos os dias, percorrendo uma lista com os sete.
2. Um semáforo vai de verde para amarelo, de amarelo para vermelho e de vermelho para verde. Escreva
   `fn proximo(Sinal s) -> Sinal` e imprima os sinais de seis trocas seguidas, a partir do verde.
3. Dada a lista `[Cor.Azul, Cor.Verde, Cor.Azul, Cor.Vermelho, Cor.Azul]`, conte quantas vezes cada
   cor aparece num `dict<Cor, i32>` e imprima o dicionário.
4. Sem rodar, diga o que cada `type` imprime — e confira: `type(1 + cast<i64>(1))`, `type([1.f])`,
   `type({"a", 1})`, `type(Cor.Verde)` e `type(type(1))`.
5. Reescreva a função `peso` do programa do capítulo como um `dict<Prioridade, i32>` e use-o no
   lugar dela.

As soluções estão em [`solucoes/10-enums-e-tipos.md`](solucoes/10-enums-e-tipos.md).

## Na spec

- O `enum`: [4.8](../spec/04-declaracoes.md)
- Tipos como valores: [3.8](../spec/03-tipos.md)
- Enum como chave de dicionário: [3.3](../spec/03-tipos.md)
- O que fica de fora (a reflexão): [README](../spec/README.md)
