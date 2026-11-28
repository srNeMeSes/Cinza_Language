# 15. `const` e imutabilidade

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar constantes locais, parâmetros `const` e constantes globais;
- prever o que o `const` fecha (o recipiente inteiro, em qualquer profundidade) e onde ele para (no
  objeto de classe);
- passar um valor `const` adiante sem abrir uma brecha nele: const só vai para const;
- escrever constantes globais, calculadas na compilação.

**Pré-requisitos.** Os capítulos 7 a 9 (listas, dicionários, structs) e 12 (classes).

---

`const` antes do tipo diz que um valor **não muda**. Numa variável de número ou de texto, isso quer
dizer só que ela não pode ser reatribuída. Numa coleção ou num struct, quer dizer mais: nada dentro
dele muda — nem um elemento, nem um campo, nem a lista dentro do campo. E o compilador garante que
essa promessa não se quebra por outro caminho: um valor const só vai para outro lugar const.

## 15.1 A constante local

```cinza limite.cinza
fn main() {
  const i32 LIMITE = 10;
  print(LIMITE * 2);
  LIMITE = 20;
}
```

```erro
limite.cinza:4:3: SemanticError: Constante 'LIMITE' não pode ser reatribuída. Remova o 'const' na declaração se precisar de variável mutável.
```

O valor de uma constante vem na declaração, e pode vir de qualquer expressão — uma conta, uma
chamada, o que o usuário digitou. Ela só não muda depois. (A regra do valor inicial é a de qualquer
variável, seção 2.2: uma lista ou um dicionário const sem valor nasce vazio — e fica vazio.)
`const var` também vale: o tipo vem do valor, e a constante continua constante.

```cinza
fn main() {
  const var x = 7;
  const string nome = "Ana";
  print(x * 2, nome + "!");
}
```

```saida
14 Ana!
```

O nome em maiúsculas (`LIMITE`) é um costume do livro para as constantes globais e as que fazem o
papel delas; a linguagem não exige nada.

## 15.2 O recipiente inteiro

Numa lista, num dicionário ou num struct, o `const` fecha **o recipiente inteiro**: nada pode ser
acrescentado, removido ou trocado, em nenhuma profundidade.

```cinza primos.cinza
fn main() {
  const list<i32> primos = [2, 3, 5];
  primos.add(7);
}
```

```erro
primos.cinza:3:9: SemanticError: Não é possível chamar '.add()' em lista const. Listas const não podem ser modificadas.
```

O mesmo vale para a atribuição por índice e para as funções do `Lists` que alteram a lista recebida:

```cinza ordena.cinza
import Lists;

fn main() {
  const list<i32> n = [3, 1, 2];
  Lists.sort(n);
}
```

```erro
ordena.cinza:5:3: SemanticError: 'Lists.sort' altera a lista, mas 'n' é const. Listas const não podem ser modificadas.
```

Ler, por outro lado, é livre — e as funções que **devolvem uma lista nova** também:

```cinza
import Lists;

fn main() {
  const list<i32> n = [3, 1, 2];
  print(Lists.sorted(n), n, Lists.sum(n));
}
```

```saida
[1, 2, 3] [3, 1, 2] 6
```

Num struct, o `const` fecha os campos e o que estiver dentro deles:

```cinza turma.cinza
struct Turma {
  string nome;
  list<string> alunos;
}

fn main() {
  const Turma t = new Turma(nome: "A");
  t.alunos.add("Ana");
}
```

```erro
turma.cinza:8:11: SemanticError: Não é possível chamar '.add()' em 't.alunos': a lista é parte de 't', que é const. Listas const não podem ser modificadas, nem o que está dentro delas.
```

E numa lista de listas, a lista de dentro é tão const quanto a de fora:

```cinza matriz.cinza
fn main() {
  const list<list<i32>> m = [[1, 2], [3, 4]];
  m[0][1] = 9;
}
```

```erro
matriz.cinza:3:7: SemanticError: Constante 'm' não pode ser modificada: 'm[...][...]' altera o conteúdo de 'm', que é const.
```

> **Por quê?** Um `const` que fechasse só o primeiro nível seria uma meia promessa: a lista não
> ganharia elementos, mas os elementos mudariam. Quem lê `const` na declaração quer saber que o
> valor que está vendo agora é o valor que vai ver depois — inteiro.

## 15.3 Parâmetros `const`

O capítulo 5 mostrou que uma lista passada a uma função é a mesma lista, e que a função pode
alterá-la. O `const` no parâmetro é a promessa contrária, escrita na assinatura: **esta função não
altera o que recebe**.

```cinza
fn soma(const list<i32> l) -> i32 {
  i32 s = 0;
  for (i32 x in l) {
    s += x;
  }
  return s;
}

fn main() {
  list<i32> l = [1, 2, 3];
  print(soma(l));
  l.add(4);
  print(soma(l));
}
```

```saida
6
10
```

Quem chama não precisa ter uma lista const: a lista de `main` é comum, e `main` continua podendo
alterá-la. O `const` vale dentro de `soma` — e lá, é conferido:

```cinza limpa.cinza
fn limpa(const list<i32> l) {
  l.add(0);
}

fn main() {}
```

```erro
limpa.cinza:2:4: SemanticError: Não é possível chamar '.add()' em lista const. Listas const não podem ser modificadas.
```

Num parâmetro de número ou de texto, o `const` só impede a reatribuição dentro da função (o valor já
chega como cópia). Numa coleção, é a garantia que importa para quem chama.

## 15.4 Const só vai para const

As listas e os dicionários são compartilhados (seção 7.3): atribuir não copia. Então, se uma
variável comum pudesse receber uma lista const, ela seria outra porta para a mesma lista — e por
ela a lista mudaria. A Cinza fecha essa porta:

```cinza porta.cinza
fn main() {
  const list<i32> a = [1, 2];
  list<i32> b = a;
  b.add(3);
}
```

```erro
porta.cinza:3:13: SemanticError: 'b' não pode receber 'a', que é const: as duas variáveis ficariam com a mesma lista, e uma alteração por 'b' mudaria 'a'. Declare 'b' const, ou guarde uma cópia: Lists.copy(a)
```

A regra: **um valor que vem de um const e guarda uma lista ou um dicionário só vai para outro
lugar const** — uma variável const ou um parâmetro const. Para uma versão que se pode alterar, a
cópia é explícita:

```cinza
import Lists;

fn main() {
  const list<i32> a = [1, 2];
  const list<i32> b = a;
  list<i32> c = Lists.copy(a);
  c.add(3);
  print(a, b, c);
}
```

```saida
[1, 2] [1, 2] [1, 2, 3]
```

A mesma regra vale para os outros caminhos por onde a lista sairia: o argumento de um parâmetro que
não é const, o `return`, o `.add` numa lista comum.

```cinza repassa.cinza
fn altera(list<i32> l) {
  l.add(1);
}

fn mostra(const list<i32> l) {
  altera(l);
}

fn main() {}
```

```erro
repassa.cinza:6:3: SemanticError: Argumento 1 de 'altera': 'l', que é const, e o parâmetro 'l' não é: a função ficaria com a mesma lista e poderia alterar 'l'. Declare o parâmetro const, ou passe uma cópia: Lists.copy(l)
```

Sem essa regra, o `const` do parâmetro de `mostra` seria só uma sugestão: bastaria repassar a lista
para uma função sem `const`. Com ela, um parâmetro const garante o que diz, por qualquer número de
chamadas abaixo.

O que **não** guarda coleção copia livre — números, textos, `bool`, enums e structs só de valores:

```cinza
struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

fn main() {
  const list<i32> a = [10, 20];
  i32 primeiro = a[0];
  const Ponto p = new Ponto(1, 2);
  Ponto q = p;
  q.x = 5;
  print(primeiro, p.x, q.x);
}
```

```saida
10 1 5
```

`q` é uma cópia do ponto (seção 9.3), e alterá-la não muda `p`. Já um struct com uma lista dentro
não copia livre, porque a cópia é rasa (seção 9.4): a lista de dentro continuaria a mesma.

### A cópia rasa

`Lists.copy` copia a lista de fora. Numa lista de listas, as de dentro continuam sendo as do const —
então a cópia também não vai para uma variável comum:

```cinza rasa.cinza
import Lists;

fn main() {
  const list<list<i32>> m = [[1], [2]];
  list<list<i32>> c = Lists.copy(m);
}
```

```erro
rasa.cinza:5:19: SemanticError: 'c' não pode receber um valor que guarda parte de 'm' (const): as duas variáveis ficariam com a mesma lista, e uma alteração por 'c' mudaria 'm'. Declare 'c' const, ou guarde uma cópia: uma cópia de cada coleção de dentro (Lists.copy copia só a de fora)
```

A cópia independente copia cada lista de dentro. O `for` sobre a lista const dá cada linha — que
também é const, por ser parte de `m` —, e `Lists.copy(linha)` é uma lista nova:

```cinza
import Lists;

fn main() {
  const list<list<i32>> m = [[1], [2]];
  list<list<i32>> c;
  for (list<i32> linha in m) {
    c.add(Lists.copy(linha));
  }
  c[0].add(9);
  print(m, c);
}
```

```saida
[[1], [2]] [[1, 9], [2]]
```

O que vem de um const continua do const: o iterador de um `for`, a variável de um `if` com
declaração sobre um opcional const, o elemento que `Lists.first` devolve, a alternativa de um
`else`. E um valor montado com um const — `[a]`, `{"x", a}`, `new Turma("A", a)` — guarda a lista
dele, então também só vai para um const.

> **Por quê?** Em muitas linguagens, a constante protege só o nome: em JavaScript, `const l = [1]`
> impede `l = outra`, mas `l.push(2)` funciona; o `final` de Java é igual. Em C++, `const` protege
> o conteúdo, mas um `const_cast` desfaz a promessa. A Cinza quis o `const` que se pode ler como um
> fato: o que é const não muda, por nenhum caminho. O preço é escrever a cópia quando você quer
> mesmo uma versão alterável — e a cópia, escrita, fica visível no código.

## 15.5 O `const` para no objeto

Um objeto de classe nunca é const:

```cinza conta.cinza
class Conta {
  i64 saldo = 0;
  pub {
    fn deposita(i64 v) { saldo += v; }
  }
}

fn main() {
  const Conta c = new Conta();
}
```

```erro
conta.cinza:9:15: SemanticError: 'const' não pode ser usado em objeto de classe ('Conta'): const protege valores e coleções, mas a linguagem não tem como garantir que os métodos não alteram o objeto. Remova o 'const'.
```

Os métodos de uma classe alteram os campos dela, e o compilador não marca quais métodos alteram e
quais só leem. Um `const Conta` teria de proibir todos os métodos — ou confiar em cada um. Em vez de
uma promessa que não pode cumprir, a Cinza não a faz.

Numa lista const de objetos, a **lista** é const; os objetos dentro dela, não:

```cinza
class Conta {
  i64 saldo = 0;
  pub {
    fn deposita(i64 v) { saldo += v; }
    fn total() -> i64 { return saldo; }
  }
}

fn main() {
  const list<Conta> contas = [new Conta()];
  contas[0].deposita(10);
  Conta c = contas[0];
  c.deposita(5);
  print(contas[0].total(), contas.size());
}
```

```saida
15 1
```

`contas.add(...)` seria erro: a lista não muda. Mas o objeto sai livre da lista (`Conta c =
contas[0]`), porque objetos já são compartilhados por natureza (seção 12.8) e nunca são const.

## 15.6 As constantes globais

No nível de cima do arquivo só há constantes (capítulo 1), e o valor delas é **calculado na
compilação**. O inicializador pode ter literais, operadores, outras constantes já declaradas, as
constantes dos módulos nativos (`Math.pi`), valores de enum, `null` e o `else` do opcional, e
`cast`:

```cinza
import Math;

const i32 LIMITE = 100;
const f64 RAIO = 2.ff;
const f64 AREA = Math.pi * RAIO * RAIO;
const string NOME = "Cinza " + "0.6";
const bool GRANDE = LIMITE > 50;

enum Modo { Claro, Escuro }
const Modo PADRAO = Modo.Escuro;
const i32? SEM_LIMITE = null;
const i32 MAXIMO = SEM_LIMITE else 1000;

fn main() {
  print(LIMITE, AREA, NOME, GRANDE);
  print(PADRAO, SEM_LIMITE, MAXIMO);
}
```

```saida
100 12.566370614359172 Cinza 0.6 true
Modo.Escuro null 1000
```

Chamadas de função, `new` e literais de lista, dicionário ou par **não** entram:

```cinza dobro.cinza
fn dobro(i32 x) -> i32 {
  return x * 2;
}

const i32 X = dobro(2);

fn main() {}
```

```erro
dobro.cinza:5:11: SemanticError: Inicializador do const global 'X' precisa ser uma expressão constante: literais, operadores e outros const já declarados. Chamadas de função, 'new' e literais de list/dict/pair não são permitidos.
```

Uma lista fixa que o programa inteiro usa vai numa função que a devolve (cada chamada dá uma lista
nova, que quem chamou pode alterar à vontade), ou numa constante local da `main`, passada adiante
por parâmetros const.

Como a conta é feita na compilação, o erro dela também é: um estouro ou um `cast` que não cabe
aparece antes de o programa rodar, com o tipo do erro que ele teria na execução:

```cinza estouro.cinza
const i32 X = 2147483647 + 1;

fn main() {}
```

```erro
estouro.cinza:1:11: SemanticError: O valor do const 'X' não pode ser calculado: OverflowError: 2147483647 + 1 excede a faixa de i32 (-2147483648 a 2147483647)
```

Uma constante usa as que vêm **antes** dela:

```cinza ordem.cinza
const i64 B = A * 3;
const i64 A = 2;

fn main() {}
```

```erro
ordem.cinza:1:15: SemanticError: 'A' é declarado mais abaixo: um const global só usa os const declarados antes dele. Mova a declaração de 'A' para cima.
```

As funções, ao contrário, enxergam todas as constantes do arquivo, de cima ou de baixo — como
enxergam todas as funções:

```cinza
fn main() {
  print(dobro());
}

fn dobro() -> i64 {
  return A * 2;
}

const i64 A = 21;
```

```saida
42
```

> **Por quê?** Uma constante global calculada na compilação não tem ordem de inicialização para
> dar errado — em C++, a ordem em que as globais de arquivos diferentes nascem é um problema
> conhecido —, não roda código antes da `main` e não falha no meio da execução. E, sem globais
> mutáveis, o estado de um programa Cinza está todo nas variáveis das funções e nos argumentos.

## 15.7 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `final int x = 1;` (Java), `final x = 1;` (Dart) | `const i32 x = 1;` |
| `let x = 1;`, `val x = 1` (JavaScript, Rust, Kotlin) | `i32 x = 1;` ou `var x = 1;` |
| `let mut x` (Rust) | a variável comum: alterável é o padrão |
| `const l = [1]` em JavaScript (`l.push` vale) | `const list<i32> l = [1];` (nada muda) |
| `const std::vector<int>& v` (C++) | `const list<i32> v` no parâmetro |
| `readonly` (C#), `Object.freeze` (JavaScript) | `const`, em qualquer profundidade |
| `static final` / `#define` / `constexpr` | `const` global, calculado na compilação |

O compilador reconhece o `final` e o `let`:

```cinza final.cinza
fn main() {
  final i32 x = 1;
}
```

```erro
final.cinza:2:3: SyntaxError: A Cinza escreve 'const', não 'final': const i32 x = 1; (token: 'final')
```

```cinza let.cinza
fn main() {
  let x = 1;
}
```

```erro
let.cinza:2:3: SyntaxError: A Cinza não tem 'let': declare com o tipo (i32 x = 1;), com var (var x = 1;), ou com const para um valor que não muda (const i32 x = 1;) (token: 'let')
```

## Programa do capítulo: constantes e parâmetros const

A versão 8 do `tarefas` tira os números soltos do código — o tamanho máximo do título e o separador
viram constantes globais — e deixa os comandos numa constante da `main`, passada para
`processa` por um parâmetro const: a assinatura diz que processar os comandos não os altera.

```cinza tarefas.cinza
// tarefas.cinza — versão 8: constantes e parâmetros const
import Strings;

const i32 TITULO_MAXIMO = 20;
const string SEPARADOR = " | ";

error TituloInvalido;
error TarefaRepetida;
error TarefaNaoEncontrada;

struct Tarefa {
  string titulo;
  bool feita = false;
}

class Agenda {
  list<Tarefa> tarefas;

  fn busca(string titulo) -> i32? {
    for (i32 i in range(0, tarefas.size())) {
      if (tarefas[i].titulo == titulo) {
        return i;
      }
    }
    return null;
  }

  pub {
    fn adiciona(string titulo) {
      string t = Strings.trim(titulo);
      if (t == "") {
        throw TituloInvalido("o título não pode ser vazio");
      }
      if (t.size() > TITULO_MAXIMO) {
        throw TituloInvalido("o título passa de " + TITULO_MAXIMO + " caracteres");
      }
      if (busca(t) != null) {
        throw TarefaRepetida("já existe a tarefa '" + t + "'");
      }
      tarefas.add(new Tarefa(titulo: t));
    }

    fn conclui(string titulo) {
      i32 i = busca(titulo) else { throw TarefaNaoEncontrada("não há a tarefa '" + titulo + "'"); };
      tarefas[i].feita = true;
    }

    fn titulos() -> list<string> {
      list<string> r;
      for (Tarefa t in tarefas) {
        string marca = "[ ] ";
        if (t.feita) {
          marca = "[x] ";
        }
        r.add(marca + t.titulo);
      }
      return r;
    }

    fn resumo() -> string {
      i32 feitas = 0;
      for (Tarefa t in tarefas) {
        if (t.feita) {
          feitas += 1;
        }
      }
      return feitas + " de " + tarefas.size() + " feitas";
    }
  }
}

fn executa(Agenda a, string comando) {
  string acao = comando[:3];
  string titulo = comando[4:];
  if (acao == "add") {
    a.adiciona(titulo);
  } else if (acao == "fim") {
    a.conclui(titulo);
  } else {
    throw ValueError("comando desconhecido: " + acao);
  }
}

fn processa(Agenda a, const list<string> comandos) -> i32 {
  i32 falhas = 0;
  for (string c in comandos) {
    try {
      executa(a, c);
    } except (Error e) {
      printf("{e.kind}: {e.message}");
      falhas += 1;
    }
  }
  return falhas;
}

fn main() {
  const list<string> comandos = ["add comprar pão", "add estudar a Cinza", "add organizar a garagem inteira",
                                 "fim comprar pão", "add comprar pão"];
  Agenda a = new Agenda();
  i32 falhas = processa(a, comandos);
  print(Strings.join(a.titulos(), SEPARADOR));
  print(a.resumo() + SEPARADOR + falhas + " comandos falharam");
}
```

```saida
TituloInvalido: o título passa de 20 caracteres
TarefaRepetida: já existe a tarefa 'comprar pão'
[x] comprar pão | [ ] estudar a Cinza
1 de 2 feitas | 2 comandos falharam
```

Repare em:

- `TITULO_MAXIMO` e `SEPARADOR` são usados dentro da classe e nas funções: as constantes globais
  são vistas em todo o arquivo.
- `comandos` é const na `main`, e `processa` o recebe num parâmetro const — se o parâmetro não
  fosse const, a chamada seria um erro (seção 15.4). Dentro de `processa`, cada `c` é um texto, que
  copia livre.
- `titulos` devolve uma **lista nova**, montada a cada chamada: quem a recebe pode alterá-la sem
  mexer na agenda. Devolver o campo `tarefas` daria a quem chamou a lista de dentro do objeto.

## Resumo

- `const` impede a reatribuição; numa coleção ou num struct, fecha o recipiente inteiro, em qualquer
  profundidade.
- O parâmetro `const` é a promessa de que a função não altera o que recebe; quem chama pode passar um
  valor comum.
- **Const só vai para const**: um valor de um const que guarda lista ou dicionário só vai para uma
  variável ou um parâmetro const. A versão alterável é a cópia explícita (`Lists.copy`,
  `Dicts.copy`), que é rasa. Números, textos e structs só de valores copiam livres.
- O `const` para no objeto de classe: `const Conta c` é erro, e os objetos de uma lista const
  continuam alteráveis.
- As constantes globais são calculadas na compilação, só com literais, operadores, constantes já
  declaradas, enums e `cast`; os erros da conta aparecem antes de rodar.

## Exercícios

1. Declare constantes globais para os segundos de um minuto, de uma hora, de um dia e de uma semana,
   cada uma calculada a partir da anterior, e imprima a da semana.
2. Escreva `fn media(const list<f64> l) -> f64` e chame-a com uma lista comum, alterada entre duas
   chamadas, e com uma lista const. Por que o `const` do parâmetro não atrapalha nenhuma das duas?
3. O programa abaixo não compila. Diga por quê e conserte-o de dois jeitos: mudando só a função, e
   mudando só a `main`.

   ```text
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

4. Dada `const list<list<i32>> M = ...` (numa `main`), monte uma cópia em que trocar `copia[0][0]`
   não muda `M`, e imprima as duas.
5. Sem rodar, diga quais destas linhas compilam, numa `main` com a classe `Conta` da seção 15.5:
   `const list<Conta> l = [new Conta()];`, `l[0].deposita(1);`, `l.add(new Conta());`,
   `Conta c = l[0];`, `list<Conta> outra = l;`.

As soluções estão em [`solucoes/15-const.md`](solucoes/15-const.md).

## Na spec

- `const`, o recipiente, o objeto e "const só vai para const": [3.9](../spec/03-tipos.md)
- As constantes globais: [4.3](../spec/04-declaracoes.md)
- O compartilhamento e a cópia: [5.1](../spec/05-execucao.md)
