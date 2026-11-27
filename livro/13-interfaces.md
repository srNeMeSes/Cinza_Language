# 13. Interfaces

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar uma interface e fazer classes diferentes cumprirem o mesmo contrato;
- usar valores de interface em listas, parâmetros e campos — o polimorfismo da Cinza;
- entender por que um valor de interface nunca volta a ser a classe;
- usar objetos que cumprem interfaces no lugar das funções passadas como argumento.

**Pré-requisitos.** O capítulo 12.

---

Uma interface é um **contrato**: uma lista de assinaturas de métodos, sem código nem campos. Uma
classe declara que o cumpre, e então pode ser usada onde a interface é esperada. É assim que a
Cinza faz o que outras linguagens fazem com herança (tratar tipos diferentes do mesmo jeito) e com
funções passadas como valor (um comportamento escolhido por quem chama).

## 13.1 Uma interface

```cinza formas.cinza
interface Forma {
  fn area() -> f64;
  fn nome() -> string;
}

class Quadrado : Forma {
  f64 lado;
  pub {
    Quadrado(f64 l) { lado = l; }
    fn area() -> f64 { return lado * lado; }
    fn nome() -> string { return "quadrado"; }
  }
}

class Circulo : Forma {
  f64 raio;
  pub {
    Circulo(f64 r) { raio = r; }
    fn area() -> f64 { return 3.14159ff * raio * raio; }
    fn nome() -> string { return "círculo"; }
  }
}

fn main() {
  list<Forma> formas = [new Quadrado(2.ff), new Circulo(1.ff), new Quadrado(3.ff)];
  f64 total = 0.ff;
  for (Forma f in formas) {
    printf("{f.nome()}: {f.area():.2f}");
    total += f.area();
  }
  printf("total: {total:.2f}");
}
```

```saida
quadrado: 4.00
círculo: 3.14
quadrado: 9.00
total: 16.14
```

- `interface Forma { ... }` lista as assinaturas, cada uma terminada em `;`. Pelo menos uma.
- `class Quadrado : Forma` **declara** que cumpre `Forma`.
- Uma `list<Forma>` guarda objetos de classes diferentes, e `f.area()` chama o método da classe real
  de cada um.

## 13.2 O que a classe precisa cumprir

A classe precisa ter **cada método da interface**, no bloco `pub`, com **exatamente** a mesma
assinatura. O compilador confere tudo na declaração da classe:

```cinza triangulo.cinza
interface Forma {
  fn area() -> f64;
  fn nome() -> string;
}
class Triangulo : Forma {
  pub {
    fn area() -> f64 { return 1.ff; }
  }
}
fn main() { }
```

```erro
triangulo.cinza:5:19: SemanticError: A classe 'Triangulo' declara ': Forma', mas não tem o método nome() -> string.
```

"Exatamente" quer dizer os mesmos tipos de parâmetro e de retorno — um `f32` onde a interface diz
`f64` não serve, mesmo que caiba:

```cinza precisao.cinza
interface Forma {
  fn area() -> f64;
}
class Triangulo : Forma {
  pub {
    fn area() -> f32 { return 1.f; }
  }
}
fn main() { }
```

```erro
precisao.cinza:6:5: SemanticError: O método area() -> f32 da classe 'Triangulo' não tem a assinatura exigida por 'Forma': area() -> f64.
```

E **a declaração é obrigatória**: uma classe que tem os métodos, mas não declarou `: Forma`, não é
uma `Forma`.

```cinza declara.cinza
interface Forma {
  fn area() -> f64;
}
class Triangulo {
  pub {
    fn area() -> f64 { return 1.ff; }
  }
}
fn main() {
  Forma f = new Triangulo();
}
```

```erro
declara.cinza:10:9: SemanticError: Tipo incompatível na declaração de 'f': declarado 'Forma', mas atribuído 'Triangulo'. A classe 'Triangulo' não declara cumprir 'Forma' (class Triangulo : Forma)
```

> **Vindo de outra linguagem.** No Go e no TypeScript, um tipo cumpre uma interface só por ter os
> métodos (tipagem estrutural). Na Cinza, como em Java e em C#, a classe **declara** o que cumpre:
> uma classe com um método `area()` que mede outra coisa não vira uma `Forma` por acidente, e quem
> lê a classe vê o contrato na primeira linha.

Uma classe pode cumprir várias interfaces, separadas por vírgula:

```cinza
interface Nomeado {
  fn nome() -> string;
}
interface Medido {
  fn tamanho() -> i32;
}

class Arquivo : Nomeado, Medido {
  string n;
  i32 t;
  pub {
    Arquivo(string nome, i32 tam) {
      n = nome;
      t = tam;
    }
    fn nome() -> string { return n; }
    fn tamanho() -> i32 { return t; }
  }
}

fn mostra(Nomeado x) {
  print("nome:", x.nome());
}

fn mede(Medido x) {
  print("tamanho:", x.tamanho());
}

fn main() {
  Arquivo a = new Arquivo("livro.md", 1200);
  mostra(a);
  mede(a);
}
```

```saida
nome: livro.md
tamanho: 1200
```

## 13.3 O que se faz com um valor de interface

Por um valor do tipo `Forma` só se chamam **os métodos que `Forma` declara** — mesmo que o objeto
real tenha outros:

```cinza diagonal.cinza
interface Forma {
  fn area() -> f64;
}
class Quadrado : Forma {
  f64 lado = 1.ff;
  pub {
    fn area() -> f64 { return lado * lado; }
    fn diagonal() -> f64 { return lado * 1.4142135623730951ff; }
  }
}
fn main() {
  Forma f = new Quadrado();
  print(f.diagonal());
}
```

```erro
diagonal.cinza:13:10: SemanticError: A interface 'Forma' não tem o método 'diagonal'
```

E **um valor de interface nunca volta a ser tratado como a classe**: não há atribuição nem `cast`
de `Forma` para `Quadrado`.

```cinza volta.cinza
interface Forma {
  fn area() -> f64;
}
class Quadrado : Forma {
  pub {
    fn area() -> f64 { return 1.ff; }
  }
}
fn main() {
  Forma f = new Quadrado();
  Quadrado q = f;
}
```

```erro
volta.cinza:11:12: SemanticError: Tipo incompatível na declaração de 'q': declarado 'Quadrado', mas atribuído 'Forma'. Um valor do tipo interface não volta a ser a classe: use os métodos de 'Forma'
```

`type(f)` mostra a classe real (seção 10.6), e `==` compara a identidade — o mesmo objeto, visto
por duas variáveis —, mas nenhum dos dois devolve o objeto como `Quadrado`.

> **Por quê?** O *downcast* (`(Quadrado) f`, `f as Quadrado`) é a porta pela qual o código volta a
> depender da classe concreta — e cada nova classe de `Forma` passa a exigir um `if` a mais
> espalhado pelo programa. Na Cinza, se o código precisa de algo de todas as formas, isso vira um
> método da interface; se precisa de algo só dos quadrados, ele guarda os quadrados como
> `Quadrado`.

## 13.4 Interfaces em todo lugar

Uma interface é um tipo como os outros: parâmetro, retorno, campo, elemento de lista, opcional.

```cinza
interface Forma {
  fn area() -> f64;
  fn nome() -> string;
}

class Quadrado : Forma {
  f64 lado;
  pub {
    Quadrado(f64 l) { lado = l; }
    fn area() -> f64 { return lado * lado; }
    fn nome() -> string { return "quadrado"; }
  }
}

class Circulo : Forma {
  f64 raio;
  pub {
    Circulo(f64 r) { raio = r; }
    fn area() -> f64 { return 3.14159ff * raio * raio; }
    fn nome() -> string { return "círculo"; }
  }
}

fn maior(list<Forma> l) -> Forma? {
  Forma? m;
  for (Forma f in l) {
    if (Forma atual = m) {
      if (f.area() > atual.area()) {
        m = f;
      }
    } else {
      m = f;
    }
  }
  return m;
}

fn main() {
  list<Forma> l = [new Quadrado(1.ff), new Circulo(2.ff), new Quadrado(3.ff)];
  if (Forma f = maior(l)) {
    print(f.nome(), f.area());
  }
  list<Forma> vazia;
  print(maior(vazia) == null);
}
```

```saida
círculo 12.56636
true
```

Como as outras coleções, uma lista de interface é **invariante**: uma `list<Quadrado>` não é uma
`list<Forma>` (seção 7.4). Um literal se adapta — `[new Quadrado(1.ff)]` onde se espera
`list<Forma>` cria direto uma `list<Forma>`.

## 13.5 Interfaces no lugar de funções

Em outras linguagens, um filtro, um comparador ou uma ação para cada elemento é uma função passada
como argumento — ou uma lambda. Na Cinza, funções não são valores (seção 5.7): o comportamento que
se passa adiante é **um objeto de uma classe que cumpre uma interface**.

```cinza
interface Filtro {
  fn aceita(i32 x) -> bool;
}

class Pares : Filtro {
  pub {
    fn aceita(i32 x) -> bool { return x % 2 == 0; }
  }
}

class MaioresQue : Filtro {
  i32 limite;
  pub {
    MaioresQue(i32 l) { limite = l; }
    fn aceita(i32 x) -> bool { return x > limite; }
  }
}

fn filtra(list<i32> l, Filtro f) -> list<i32> {
  list<i32> r;
  for (i32 x in l) {
    if (f.aceita(x)) {
      r.add(x);
    }
  }
  return r;
}

fn main() {
  list<i32> n = [1, 4, 7, 10, 13];
  print(filtra(n, new Pares()), filtra(n, new MaioresQue(5)));
}
```

```saida
[4, 10] [7, 10, 13]
```

`MaioresQue(5)` é o que, em outra linguagem, seria a lambda `x -> x > 5`: o `limite` que ela
"captura" é um campo, à vista. O estado que uma closure esconderia fica declarado no objeto — e pode
ser lido depois:

```cinza
interface Acao {
  fn executa(i32 x);
}

class Soma : Acao {
  i32 total = 0;
  pub {
    fn executa(i32 x) { total += x; }
    fn resultado() -> i32 { return total; }
  }
}

fn para_cada(list<i32> l, Acao a) {
  for (i32 x in l) {
    a.executa(x);
  }
}

fn main() {
  Soma s = new Soma();
  para_cada([1, 2, 3, 4], s);
  print(s.resultado());
}
```

```saida
10
```

Para **ordenar** por um critério, a interface calcula a chave, e `Lists.sort_by` (seção 7.7) faz o
resto:

```cinza
import Lists;

interface Criterio {
  fn chave(string s) -> i32;
}

class PorTamanho : Criterio {
  pub {
    fn chave(string s) -> i32 { return s.size(); }
  }
}

fn ordena(list<string> l, Criterio c) {
  list<i32> chaves;
  for (string s in l) {
    chaves.add(c.chave(s));
  }
  Lists.sort_by(l, chaves);
}

fn main() {
  list<string> l = ["banana", "kiwi", "uva", "abacaxi"];
  ordena(l, new PorTamanho());
  print(l);
}
```

```saida
["uva", "kiwi", "banana", "abacaxi"]
```

> **Por quê?** É mais texto que uma lambda — e esse é o preço. Em troca, cada comportamento tem um
> nome (`MaioresQue`), o estado dele está em campos declarados, ele pode ter vários métodos, e nada
> captura uma variável do lugar onde foi criado para mudá-la depois, longe dos olhos.

## 13.6 O que uma interface não tem

Uma interface só tem assinaturas: **sem campos, sem corpo de método** (os *default methods* do Java)
e sem construtor. Ela não se instancia — `new Forma()` é erro —, e um struct não a cumpre, porque não
tem métodos.

```cinza corpo.cinza
interface Forma {
  fn nome() -> string { return "?"; }
}
fn main() { }
```

```erro
corpo.cinza:2:23: SyntaxError: Esperado ';' após a assinatura: na interface o método não tem corpo (token: '{')
```

O código comum a várias classes vai para uma função que recebe a interface (`fn total(list<Forma>
l)`), ou para uma classe auxiliar que as outras usam por composição (seção 12.9).

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| herança de classe para polimorfismo | interfaces |
| `default` methods, classes abstratas | uma função que recebe a interface |
| downcast (`(Quadrado) f`, `f as Quadrado`, `instanceof` + cast) | um método na interface, ou guardar o tipo concreto |
| tipagem estrutural (Go, TypeScript) | a classe declara `: Interface` |
| lambda, função como argumento | um objeto de uma classe que cumpre uma interface |

## Programa do capítulo: filtros de tarefas

A versão 6 do `tarefas` acrescenta à `Agenda` um método `filtra`, que recebe um `Filtro` — uma
interface. Cada filtro é uma classe pequena, e um filtro pode **combinar** outros: `E` aceita uma
tarefa quando os dois filtros dentro dele aceitam.

```cinza tarefas.cinza
// tarefas.cinza — versão 6: filtros de tarefas por interface
import Strings;

enum Prioridade { Alta, Normal, Baixa }

struct Tarefa {
  string titulo;
  Prioridade prioridade = Prioridade.Normal;
  bool feita = false;
}

interface Filtro {
  fn aceita(Tarefa t) -> bool;
}

class Pendentes : Filtro {
  pub {
    fn aceita(Tarefa t) -> bool { return not t.feita; }
  }
}

class DePrioridade : Filtro {
  Prioridade p;
  pub {
    DePrioridade(Prioridade p) { self.p = p; }
    fn aceita(Tarefa t) -> bool { return t.prioridade == p; }
  }
}

class ComTexto : Filtro {
  string trecho;
  pub {
    ComTexto(string t) { trecho = t; }
    fn aceita(Tarefa t) -> bool { return Strings.contains(Strings.lower(t.titulo), trecho); }
  }
}

class E : Filtro {
  Filtro a;
  Filtro b;
  pub {
    E(Filtro x, Filtro y) {
      a = x;
      b = y;
    }
    fn aceita(Tarefa t) -> bool { return a.aceita(t) and b.aceita(t); }
  }
}

class Agenda {
  list<Tarefa> tarefas;
  pub {
    fn adiciona(string titulo, Prioridade p) {
      tarefas.add(new Tarefa(titulo: titulo, prioridade: p));
    }
    fn conclui(string titulo) {
      for (i32 i in range(0, tarefas.size())) {
        if (tarefas[i].titulo == titulo) {
          tarefas[i].feita = true;
        }
      }
    }
    fn filtra(Filtro f) -> list<string> {
      list<string> r;
      for (Tarefa t in tarefas) {
        if (f.aceita(t)) {
          r.add(t.titulo);
        }
      }
      return r;
    }
  }
}

fn main() {
  Agenda a = new Agenda();
  a.adiciona("comprar pão", Prioridade.Normal);
  a.adiciona("pagar a conta de luz", Prioridade.Alta);
  a.adiciona("pagar o aluguel", Prioridade.Alta);
  a.adiciona("estudar a Cinza", Prioridade.Normal);
  a.conclui("pagar a conta de luz");

  print(a.filtra(new Pendentes()));
  print(a.filtra(new DePrioridade(Prioridade.Alta)));
  print(a.filtra(new ComTexto("pagar")));
  print(a.filtra(new E(new Pendentes(), new DePrioridade(Prioridade.Alta))));
}
```

```saida
["comprar pão", "pagar o aluguel", "estudar a Cinza"]
["pagar a conta de luz", "pagar o aluguel"]
["pagar a conta de luz", "pagar o aluguel"]
["pagar o aluguel"]
```

Repare em:

- A `Agenda` não sabe nada dos filtros que existem: ela conhece só a interface. Um filtro novo é uma
  classe nova, sem mudar a `Agenda`.
- `DePrioridade` e `ComTexto` guardam o parâmetro do filtro num campo — o que uma lambda capturaria.
- `E` cumpre `Filtro` e **contém** dois `Filtro`: filtros compostos de filtros, sem limite. É a
  composição da seção 12.9 aplicada ao comportamento.

O `adiciona` desta versão voltou a aceitar qualquer título, para o exemplo ficar curto. O capítulo
14 devolve as regras, agora com erros.

## Resumo

- `interface Nome { fn m(...) -> T; ... }`: só assinaturas. `class C : I1, I2` declara o que cumpre.
- A classe tem cada método da interface no `pub`, com a assinatura exata; uma classe que não
  declarou a interface não a cumpre, mesmo com os métodos.
- Por um valor de interface, só os métodos da interface; ele nunca volta a ser a classe (nem por
  `cast`); `type(x)` mostra a classe sem estreitar; `==` é identidade.
- Interfaces valem em parâmetros, retornos, campos, listas (invariantes) e opcionais.
- Um objeto que cumpre uma interface faz o papel da função passada como argumento: filtro,
  critério de ordenação, ação com estado.
- Uma interface não tem campos, corpos nem construtor, e não se instancia.

## Exercícios

1. Declare `interface Animal { fn som() -> string; fn nome() -> string; }` e três classes que a
   cumprem. Percorra uma `list<Animal>` imprimindo `"<nome> faz <som>"`.
2. Declare `interface Regra { fn valida(string s) -> bool; }` e as regras `NaoVazia`,
   `TamanhoMinimo(n)` e `ComecaCom(prefixo)`. Escreva `fn confere(string s, list<Regra> regras) ->
   bool`, que exige todas, e teste com algumas senhas.
3. Declare `interface Reducao { fn passo(i32 x); fn resultado() -> i32; }` com as classes `Soma` e
   `Maximo`, e `fn reduz(list<i32> l, Reducao r) -> i32`. Use as duas na mesma lista.
4. Numa `list<Forma>` com quadrados e círculos, você quer a soma das diagonais **só dos quadrados**.
   Explique por que não dá para fazer `Quadrado q = f` e mostre dois jeitos de resolver.
5. Escreva `class Nao : Filtro`, que aceita o que outro filtro recusa, e use-a no programa do
   capítulo para listar as tarefas que **não** contêm "pagar".

As soluções estão em [`solucoes/13-interfaces.md`](solucoes/13-interfaces.md).

## Na spec

- As interfaces e as regras de uso: [3.7](../spec/03-tipos.md)
- A declaração `interface`: [4.9](../spec/04-declaracoes.md)
- A igualdade por identidade: [5.3](../spec/05-execucao.md)
- Funções como valor e herança, o que fica de fora: [README](../spec/README.md)
