# 16. Genéricos: funções

**Objetivos.** Ao fim deste capítulo você sabe:

- escrever uma função que serve a vários tipos, com parâmetros de tipo entre colchetes;
- prever quando uma chamada compila: a dedução dos tipos, os tipos explícitos e a conferência do
  corpo com o tipo concreto;
- ler o erro de uma função genérica, com o rastro dos usos;
- explicar o que a monomorfização custa (nada, na execução) e o que ela cobra (o erro no uso).

**Pré-requisitos.** Os capítulos 5 (funções), 7 e 8 (coleções) e 9 (structs).

---

Uma função que acha o maior de dois `i32` é igual, linha por linha, à que acha o maior de dois
textos — só os tipos mudam. Sem genéricos, seriam duas funções (ou três, ou dez), que divergem com
o tempo. Com genéricos, é uma só: `fn maior[T](T a, T b) -> T`, em que `T` é um **parâmetro de
tipo**, trocado pelo tipo de verdade em cada uso.

## 16.1 Uma função para vários tipos

```cinza
fn maior[T](T a, T b) -> T {
  if (a > b) {
    return a;
  }
  return b;
}

fn main() {
  print(maior(3, 7), maior("ana", "bia"), maior(2.5ff, 1.ff));
}
```

```saida
7 bia 2.5
```

Os parâmetros de tipo vêm entre colchetes, depois do nome: `[T]`, ou `[K, V]` quando são vários.
Dentro da função, `T` vale onde se escreve um tipo — num parâmetro, no retorno, numa variável, em
`list<T>`, `dict<string, T>`, `cast<T>(...)`. Quem chama não escreve os tipos: eles são deduzidos
dos argumentos (seção 16.4).

Os colchetes são dos genéricos que **você** declara. Os tipos embutidos continuam com `< >`:
`list<T>`, `dict<K, V>`, `cast<T>(x)`.

## 16.2 Uma cópia por tipo

Para cada combinação de tipos com que a função é usada, o compilador gera uma **cópia** dela, com
`T` trocado pelo tipo concreto: `maior[i32]`, `maior[string]` e `maior[f64]` são três funções
comuns, conferidas e compiladas como se você as tivesse escrito à mão. É a **monomorfização**.

A cópia aparece com o nome e os tipos no stack trace:

```cinza primeiro.cinza
fn primeiro[T](list<T> l) -> T {
  return l[0];
}

fn main() {
  list<string> l;
  print(primeiro(l));
}
```

```erro
primeiro.cinza:2:11: IndexError: índice 0 fora dos limites (tamanho: 0)
    em primeiro[string] (primeiro.cinza:2)
    em main (primeiro.cinza:7)
```

> **Por quê?** Uma cópia por tipo não custa nada na execução: `maior[i32]` compara dois `i32`
> direto, sem embrulhar os números em objetos nem conferir tipos no caminho. Java faz o contrário
> (uma função só, que trabalha com `Object` e perde os tipos na execução — o *type erasure*), e
> por isso não aceita `List<int>`. C++, Rust e Zig fazem como a Cinza. O preço é o programa
> compilado ficar maior quando uma genérica é usada com muitos tipos — na prática, com poucos.

## 16.3 A conferência no uso

A Cinza **não pede que você declare o que `T` precisa ter**. O corpo de uma genérica é conferido
em cada cópia, com o tipo concreto: `maior[i32]` vale porque `i32` tem `>`. Se o tipo não tem o que
o corpo usa, a cópia é que dá erro:

```cinza ponto.cinza
struct Ponto {
  i32 x = 0;
}

fn maior[T](T a, T b) -> T {
  if (a > b) {
    return a;
  }
  return b;
}

fn main() {
  print(maior(new Ponto(1), new Ponto(2)));
}
```

```erro
ponto.cinza:6:9: SemanticError: Comparação '>' não é suportada entre 'Ponto' e 'Ponto'
    em maior[Ponto], usada em ponto.cinza:13
```

O erro aponta o lugar **no corpo** (a linha 6, o `a > b`), e a linha de baixo diz qual cópia
falhou e onde ela foi pedida (a linha 13). É o **rastro dos usos**.

A mesma regra que recusa o `Ponto` aceita qualquer tipo que tenha o que o corpo usa — inclusive
tipos que não têm nada em comum, a não ser isso:

```cinza
struct Conta {
  string dono;
  f64 saldo = 0.ff;
}

struct Cliente {
  string dono;
  i32 idade = 0;
}

fn dono_de[T](T x) -> string {
  return x.dono;
}

fn main() {
  print(dono_de(new Conta(dono: "ana")), dono_de(new Cliente("bia", 30)));
}
```

```saida
ana bia
```

Uma genérica que **nunca é usada** não gera cópia, e o corpo dela só passa pela análise da sintaxe:
um erro de tipo nele só aparece quando alguém a usa.

```cinza
fn nunca_usada[T](T x) -> i32 {
  return x.campo_que_nao_existe;
}

fn main() {
  print("ok");
}
```

```saida
ok
```

> **Por quê?** Rust (`T: PartialOrd`), Swift, Java (`T extends Comparable<T>`) e C# pedem que a
> declaração liste o que `T` precisa ter. A vantagem é o erro aparecer na declaração, antes de
> qualquer uso. O custo é uma camada inteira de linguagem — os traits, as interfaces de restrição,
> as regras de quais tipos as cumprem —, e escrever na assinatura o que o corpo já diz. A Cinza
> escolheu o modelo do Zig e dos templates de C++: o corpo é o contrato. O preço é o erro aparecer
> no uso; o rastro existe para pagá-lo, levando do uso ao lugar exato do corpo.

> **Cuidado.** Uma genérica nunca usada pode estar errada sem que você saiba. Ao escrever uma, use-a
> pelo menos uma vez — de preferência com cada tipo que ela pretende servir.

## 16.4 A dedução dos tipos

Numa chamada sem tipos, cada parâmetro de tipo é deduzido dos argumentos em que aparece — sozinho
(`T`) ou dentro de `list<T>`, `dict<K, V>` e `pair<A, B>`:

```cinza
fn ultimo[T](list<T> l) -> T {
  return l[l.size() - 1];
}

fn chaves[K, V](dict<K, V> d) -> list<K> {
  return d.keys();
}

fn main() {
  print(ultimo([1, 2, 3]), ultimo(["a", "b"]));
  dict<string, i32> idades = {{"ana", 30}, {"bia", 25}};
  print(chaves(idades));
}
```

```saida
3 b
["ana", "bia"]
```

Quando `T` aparece em mais de um argumento, os tipos precisam combinar. Números da mesma família
combinam no mais largo, como numa conta (seção 2.5): `maior(1, grande)`, com `grande` um `i64`, é
`maior[i64]`.

```cinza
fn maior[T](T a, T b) -> T {
  if (a > b) {
    return a;
  }
  return b;
}

fn main() {
  i64 grande = 5000000000;
  var m = maior(1, grande);
  print(m, type(m));
}
```

```saida
5000000000 i64
```

Fora disso, o tipo precisa ser o mesmo:

```cinza conflito.cinza
fn maior[T](T a, T b) -> T {
  if (a > b) {
    return a;
  }
  return b;
}

fn main() {
  print(maior(1, 2.5ff));
}
```

```erro
conflito.cinza:9:9: SemanticError: tipos conflitantes para 'T' em 'maior': 'i32' e 'f64': inteiro e ponto flutuante não se misturam; escreva o inteiro como ponto flutuante (2.ff) ou converta com cast<f64>(...)
```

E dentro de uma coleção, sempre o mesmo, sem promoção — a invariância do capítulo 7:

```cinza junta.cinza
fn junta[T](list<T> a, list<T> b) -> i32 {
  return a.size() + b.size();
}

fn main() {
  list<i32> a = [1];
  list<i64> b = [2];
  print(junta(a, b));
}
```

```erro
junta.cinza:8:9: SemanticError: tipos conflitantes para 'T' em 'junta': 'i32' e 'i64': dentro de list, dict, pair ou de um tipo genérico, o tipo precisa ser o mesmo (invariância)
```

O tipo de retorno e o tipo da variável que recebe o resultado **não** entram na dedução: só os
argumentos.

## 16.5 Os tipos explícitos

Os tipos também podem ser escritos na chamada, entre colchetes, todos e na ordem da declaração:
`maior[f64](...)`. Eles são **obrigatórios** quando um parâmetro de tipo não aparece nos parâmetros
da função — e então não há de onde deduzi-lo:

```cinza vazia.cinza
fn vazia[T]() -> list<T> {
  list<T> l;
  return l;
}

fn main() {
  list<string> nomes = vazia[string]();
  nomes.add("ana");
  print(nomes);
  list<i32> n = vazia();
}
```

```erro
vazia.cinza:10:17: SemanticError: Não é possível deduzir o tipo 'T' de 'vazia' pelos argumentos (ele não aparece nos parâmetros): informe os tipos na chamada, ex.: vazia[i32](...)
```

Com os tipos explícitos, os argumentos seguem as regras de sempre para a assinatura concreta:
`maior[i64](3, 4)` adapta os literais a `i64`; `maior[f64](1, 2.5ff)` é erro, porque o literal
`1` é inteiro e não vira ponto flutuante sozinho (seção 2.5).

```cinza
fn maior[T](T a, T b) -> T {
  if (a > b) {
    return a;
  }
  return b;
}

fn main() {
  var m = maior[i64](3, 4);
  print(m, type(m), maior[f64](1.5ff, 2.ff));
}
```

```saida
4 i64 2
```

## 16.6 Literais e `cast<T>`

Dentro da cópia, um literal segue as regras do tipo concreto. `T total = 0;` vale com `T = i32` e
com `T = i64` (o literal se adapta), mas não com `T = f64`:

```cinza soma.cinza
fn soma[T](list<T> l) -> T {
  T total = 0;
  for (T x in l) {
    total += x;
  }
  return total;
}

fn main() {
  print(soma([1, 2, 3]));
  print(soma([1.5ff, 2.ff]));
}
```

```erro
soma.cinza:2:13: SemanticError: O literal 0 é inteiro, mas aqui se espera 'f64': inteiro e ponto flutuante não se misturam. Escreva 0.ff (ou converta com cast<f64>(...))
    em soma[f64], usada em soma.cinza:11
```

Para um valor que sirva a qualquer número, `cast<T>(0)`: na cópia `soma[f64]`, ele é `cast<f64>(0)`,
um `0.0`.

```cinza
fn soma[T](list<T> l) -> T {
  T total = cast<T>(0);
  for (T x in l) {
    total += x;
  }
  return total;
}

fn main() {
  print(soma([1, 2, 3]), soma([1.5ff, 2.ff]));
}
```

```saida
6 3.5
```

## 16.7 `type(x) == T` não estreita

`type(x)` compara com o tipo concreto da cópia, e serve para mudar o que a função **faz**:

```cinza
fn descreve[T](T x) -> string {
  if (type(x) == string) {
    return "um texto";
  }
  if (type(x) == i32) {
    return "um i32";
  }
  return "outra coisa";
}

fn main() {
  print(descreve("a"), descreve(1), descreve(2.5ff));
}
```

```saida
um texto um i32 outra coisa
```

Mas não muda o que o corpo **pode usar**: a cópia inteira precisa valer para o tipo dela,
inclusive o trecho que nunca roda. Em `tamanho[i32]`, o `x.size()` dentro do `if` é conferido com
`x` um `i32`:

```cinza tamanho.cinza
fn tamanho[T](T x) -> i32 {
  if (type(x) == string) {
    return x.size();
  }
  return 0;
}

fn main() {
  print(tamanho("abc"));
  print(tamanho(5));
}
```

```erro
tamanho.cinza:3:13: SemanticError: Tipo 'i32' não possui métodos (não é uma classe conhecida)
    em tamanho[i32], usada em tamanho.cinza:10
```

O `format` e o `printf` mostram qualquer valor, então servem a qualquer `T`:

```cinza
fn mostra[T](T x) {
  printf("{x} é {type(x)}");
}

fn main() {
  mostra(1);
  mostra("a");
  mostra([1.5ff]);
}
```

```saida
1 é i32
a é string
[1.5] é list<f64>
```

## 16.8 O rastro e o limite

Quando uma genérica chama outra, o rastro tem uma linha por cópia, da mais interna para fora:

```cinza rastro.cinza
fn dono_de[T](T x) -> string {
  return x.dono;
}

fn mostra[T](T x) {
  print(dono_de(x));
}

fn main() {
  mostra(5);
}
```

```erro
rastro.cinza:2:11: SemanticError: Tipo 'i32' não possui campos
    em dono_de[i32], usada em rastro.cinza:6
    em mostra[i32], usada em rastro.cinza:10
```

Cada combinação de tipos gera uma cópia só, então a recursão com os mesmos tipos é a recursão de
sempre. Já uma recursão em que o tipo **cresce** — `T`, depois `list<T>`, depois `list<list<T>>`
— pediria uma cópia nova a cada nível, e nunca terminaria na compilação. Mais de 64 cópias
encadeadas é erro:

```cinza cresce.cinza
fn cresce[T](T x, i32 n) -> i32 {
  if (n == 0) {
    return 0;
  }
  list<T> l = [x];
  return cresce(l, n - 1);
}

fn main() {
  print(cresce(1, 3));
}
```

```erro
cresce.cinza:6:10: SemanticError: Instanciações genéricas encadeadas demais (mais de 64): 'cresce' gera uma cópia nova a cada chamada, com tipos que só crescem (como T → list<T>); isso nunca termina na compilação, qualquer que seja o valor na execução.
```

(O erro vem seguido do rastro, encurtado como um stack trace: as 10 cópias mais internas e a mais
externa.)

## 16.9 O que não é genérico

- **Métodos** não têm parâmetros de tipo próprios; uma classe genérica (capítulo 17) dá os dela aos
  métodos. Para um método que serviria a vários tipos, escreva uma função genérica que recebe o
  objeto.
- **A `main`** não é genérica: ela é chamada sem tipos.
- **Sobrecarga** continua não existindo: uma genérica `f` e uma função comum `f` são erro.
- **Uma genérica não é um valor**, como nenhuma função (seção 5.7): `var f = maior;` é erro.
- O nome de um parâmetro de tipo não pode ser o de um tipo existente: `fn f[Ponto](...)`, com um
  struct `Ponto`, é erro — escolha `T`.

```cinza caixa.cinza
class Caixa {
  pub {
    fn guarda[T](T x) {}
  }
}

fn main() {}
```

```erro
caixa.cinza:3:14: SyntaxError: Métodos não podem ser genéricos: só funções do nível superior (fn nome[T](...)). Use uma função genérica que recebe o objeto. (token: '[')
```

## 16.10 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `fn maior<T>(...)` (Rust), `T Maior<T>(...)` (C#), `<T> T maior(...)` (Java) | `fn maior[T](...)` |
| `maior::<i32>(...)`, `maior<int>(...)` | `maior[i32](...)` |
| `T: PartialOrd`, `T extends Comparable<T>`, `where T : IComparable` | nada: o corpo é o contrato |
| `func Maior[T cmp.Ordered](...)` (Go) | `fn maior[T](...)`, sem a restrição |
| templates de C++, `comptime` do Zig | o mesmo modelo: uma cópia por tipo, conferida no uso |
| `List<Object>`, `interface{}`, `any` | não há: o tipo é sempre o concreto |

O compilador reconhece os ângulos e as restrições:

```cinza angulo.cinza
fn maior<T>(T a, T b) -> T {
  return a;
}

fn main() {}
```

```erro
angulo.cinza:1:9: SyntaxError: Os parâmetros de tipo vão entre colchetes, não entre '<' e '>': fn maior[T](...) (token: '<')
```

```cinza restricao.cinza
fn maior[T: Comparable](T a, T b) -> T {
  return a;
}

fn main() {}
```

```erro
restricao.cinza:1:11: SyntaxError: A Cinza não tem restrições nos parâmetros de tipo ('T: ...'): o corpo é conferido em cada uso, com o tipo concreto. Escreva só o nome: [T] (token: ':')
```

```cinza chamada.cinza
fn maior[T](T a, T b) -> T {
  return a;
}

fn main() {
  print(maior<i32>(1, 2));
}
```

```erro
chamada.cinza:6:14: SyntaxError: Os tipos de uma chamada genérica vão entre colchetes: maior[i32](...) (token: '<')
```

## Programa do capítulo: frequências de qualquer coisa

Contar quantas vezes cada valor aparece, ordenar do mais comum ao menos comum e mostrar uma barra
para cada um — o mesmo código serve a palavras, a números e a enums. São quatro funções genéricas,
cada uma usada com três tipos:

```cinza frequencias.cinza
// frequencias.cinza — contar, ordenar e mostrar qualquer coisa
import Lists;
import Strings;

enum Naipe { Copas, Espadas, Ouros, Paus }

fn conta[T](const list<T> l) -> dict<T, i32> {
  dict<T, i32> c;
  for (T x in l) {
    if (c.has(x)) {
      c[x] += 1;
    } else {
      c.add({x, 1});
    }
  }
  return c;
}

fn mais_comuns[T](const dict<T, i32> contagem, i32 n) -> list<T> {
  list<T> chaves = contagem.keys();
  list<i32> ordem;
  for (T k in chaves) {
    ordem.add(-contagem[k]);
  }
  Lists.sort_by(chaves, ordem);
  if (n > chaves.size()) {
    return chaves;
  }
  return Lists.slice(chaves, 0, n);
}

fn barra[T](T rotulo, i32 n) -> string {
  return Strings.pad_right(format("{rotulo}"), 12) + Strings.repeat("#", n) + " " + n;
}

fn relatorio[T](string titulo, const list<T> dados, i32 n) {
  print(titulo + ":");
  dict<T, i32> c = conta(dados);
  for (T k in mais_comuns(c, n)) {
    print("  " + barra(k, c[k]));
  }
}

fn main() {
  relatorio("palavras", Strings.split("o rato roeu a roupa do rei de roma e o rei riu", " "), 3);
  relatorio("dados", [3, 6, 1, 6, 3, 6, 2], 2);
  relatorio("naipes", [Naipe.Copas, Naipe.Ouros, Naipe.Copas, Naipe.Paus], 5);
}
```

```saida
palavras:
  o           ## 2
  rei         ## 2
  rato        # 1
dados:
  6           ### 3
  3           ## 2
naipes:
  Naipe.Copas ## 2
  Naipe.Ouros # 1
  Naipe.Paus  # 1
```

Repare em:

- **Doze cópias**: `conta`, `mais_comuns`, `barra` e `relatorio`, cada uma com `string`, `i32` e
  `Naipe`. A `main` só pede as três de `relatorio`; cada uma delas pede as outras.
- `conta` cria um `dict<T, i32>`, então `T` precisa ser um tipo de chave (números inteiros, texto,
  `bool`, enum). Com uma lista de structs, a cópia de `conta` daria o erro de chave — com o rastro
  até a `main`.
- `barra` usa `format("{rotulo}")` e não `"" + rotulo`: o `+` junta texto com números e textos, mas
  não com um enum, e a cópia `barra[Naipe]` daria erro. O `format` mostra qualquer valor.
- `Lists.sort_by` ordena as chaves pela lista `ordem`, com os negativos das contagens (a maior
  contagem vem primeiro); a ordenação é estável, então os empates ficam na ordem em que apareceram.
- Os parâmetros `const` (capítulo 15) valem nas genéricas como em qualquer função.

## Resumo

- `fn nome[T, U](...)` declara parâmetros de tipo; `T` vale onde se escreve um tipo.
- Cada combinação de tipos gera uma cópia comum da função (monomorfização): nenhum custo na
  execução, e o nome com os tipos no stack trace (`primeiro[string]`).
- O corpo é conferido em cada cópia, com o tipo concreto; não há restrições declaradas. O erro
  aparece no uso, com o rastro: o lugar no corpo, e uma linha por cópia até a chamada.
- Os tipos são deduzidos dos argumentos (números da mesma família combinam no mais largo; dentro de
  coleções, o tipo é exato); ou escritos na chamada, todos: `vazia[string]()`.
- `cast<T>(0)` é o literal que serve a qualquer número; `type(x) == T` compara, mas não estreita.
- Mais de 64 cópias encadeadas é erro; métodos e `main` não são genéricos.

## Exercícios

1. Escreva `fn troca[A, B](pair<A, B> p) -> pair<B, A>` e use-a com `{1, "um"}` e com `{"x", true}`.
2. Escreva `fn quantas[T](const list<T> l, T x) -> i32`, que conta quantas vezes `x` aparece em `l`,
   e use-a com textos, com números e com uma lista de structs `Ponto`. Por que funciona com os
   structs?
3. Escreva `fn menor_de[T](const list<T> l) -> T`. O que acontece com uma lista de `bool`? E com uma
   lista vazia? (Um erro é na compilação; o outro, na execução. Por quê?)
4. Escreva `fn repete[T](T x, i32 n) -> list<T>` e mostre o tipo de `repete(1, 3)` e de
   `repete[i64](1, 3)`. Por que são diferentes?
5. Escreva `fn inverte[K, V](const dict<K, V> d) -> dict<V, K>`, que troca as chaves pelos valores,
   e teste-a com um `dict<string, i32>`. O que acontece com um `dict<string, f64>`?

As soluções estão em [`solucoes/16-genericos-funcoes.md`](solucoes/16-genericos-funcoes.md).

## Na spec

- A declaração: [8.1](../spec/08-genericos.md)
- A conferência no uso: [8.2](../spec/08-genericos.md)
- A dedução e os tipos explícitos: [8.3](../spec/08-genericos.md)
- O rastro e o limite das cópias: [8.4](../spec/08-genericos.md)
- A execução: [8.5](../spec/08-genericos.md)
