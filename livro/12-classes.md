# 12. Classes

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar uma classe com campos privados, métodos públicos e privados e um construtor;
- seguir as regras que garantem que nenhum objeto existe pela metade;
- usar `self` e prever a ordem em que um objeto é criado;
- explicar por que a Cinza não tem herança, e o que usar no lugar.

**Pré-requisitos.** A Parte II, em especial o capítulo 9 (structs).

---

Uma classe junta **estado e comportamento**: campos que só os seus métodos podem ler e alterar, e
métodos que mantêm esse estado coerente. Na Cinza os campos são **sempre** privados, os métodos
públicos ficam num bloco `pub { }`, e um objeto, ao contrário de um struct, é **compartilhado** —
atribuí-lo ou passá-lo a uma função não o copia. Não há herança: o reaproveitamento é por
composição, e o polimorfismo, pelas interfaces do capítulo 13.

## 12.1 Uma classe

```cinza conta.cinza
class Conta {
  string titular;
  i64 saldo = 0;
  list<string> historico;

  fn registra(string s) {
    historico.add(s);
  }

  pub {
    Conta(string t) {
      titular = t;
      registra("aberta");
    }
    fn deposita(i64 valor) {
      saldo += valor;
      registra("depósito de " + valor);
    }
    fn saldo_atual() -> i64 {
      return saldo;
    }
    fn extrato() -> list<string> {
      return historico;
    }
  }
}

fn main() {
  Conta c = new Conta("Ana");
  c.deposita(100);
  c.deposita(50);
  print(c.saldo_atual(), c.extrato(), c, type(c));
}
```

```saida
150 ["aberta", "depósito de 100", "depósito de 50"] <Conta object> Conta
```

- Os **campos** (`titular`, `saldo`, `historico`) são privados. Dentro de um método, um campo se
  usa pelo nome.
- Um **método fora do `pub`** (`registra`) é privado: só a própria classe o chama.
- O **bloco `pub { }`** tem os métodos públicos e o **construtor** — o método com o nome da classe,
  que roda no `new`.
- `print` mostra um objeto como `<Conta object>`: o que há dentro dele é assunto da classe.

## 12.2 Campos privados, sempre

De fora da classe, nenhum campo é visível — nem para ler:

```cinza saldo.cinza
class Conta {
  i64 saldo = 0;
  pub {
    fn deposita(i64 valor) { saldo += valor; }
  }
}
fn main() {
  Conta c = new Conta();
  c.saldo = 1000000;
}
```

```erro
saldo.cinza:9:4: SemanticError: Campo 'saldo' é privado na classe 'Conta' e não pode ser acessado externamente
```

Para expor um valor, a classe declara um método que o devolve (`saldo_atual()`). A privacidade é da
**classe**, não do objeto: um método pode ler os campos de outro objeto da mesma classe.

```cinza
class Ponto {
  i32 x = 0;
  pub {
    Ponto(i32 v) { x = v; }
    fn distancia(Ponto outro) -> i32 { return outro.x - x; }
  }
}

fn main() {
  print(new Ponto(2).distancia(new Ponto(7)));
}
```

```saida
5
```

> **Por quê?** Um campo público é uma promessa que não pode mais mudar: qualquer código pode
> depender dele. Com os campos sempre privados, a classe controla cada mudança do seu estado — um
> saldo só muda por `deposita`, que registra no histórico —, e a representação interna pode mudar
> sem quebrar quem a usa. Quando o dado deve ficar à vista, a ferramenta é o struct (seção 9.7).

## 12.3 Métodos

Um método é uma função declarada dentro da classe, que enxerga os campos do objeto. Dentro de um
método, `m()` chama o método `m` do próprio objeto — com precedência sobre uma função global de
mesmo nome. Um campo e um método podem ter o mesmo nome, porque se usam de jeitos diferentes:

```cinza
class Caixa {
  i32 tamanho = 3;
  pub {
    fn tamanho() -> i32 {
      return tamanho * 2;
    }
  }
}

fn main() {
  print(new Caixa().tamanho());
}
```

```saida
6
```

Declarar o mesmo método duas vezes — inclusive um dentro e outro fora do `pub` — é erro: não há
sobrecarga também aqui.

## 12.4 O construtor

O construtor é opcional e, no máximo, um. Ele fica dentro do `pub { }`, e os argumentos de `new`
precisam casar com ele (sem construtor, `new` não recebe argumentos):

```cinza
class Contador {
  i32 n = 0;
  pub {
    fn inc() { n += 1; }
    fn valor() -> i32 { return n; }
  }
}

fn main() {
  Contador c = new Contador();
  c.inc();
  c.inc();
  print(c.valor());
}
```

```saida
2
```

Os argumentos de um construtor vão **em ordem** — os argumentos pelo nome existem só na criação de
struct (seção 9.2).

## 12.5 Campos obrigatórios

Um campo sem valor inicial que não é `list`, `dict` nem `T?` é **obrigatório**, como no struct. Numa
classe, quem o atribui é o construtor — e as regras garantem que **nenhum objeto existe com um campo
sem valor**:

- a classe precisa de um construtor que atribua cada campo obrigatório;
- a atribuição fica **no nível de cima** do corpo do construtor — fora de `if`, laço e `try` —, e
  **antes** de ler um campo, chamar um método, usar `self` ou retornar.

```cinza titular.cinza
class Conta {
  string titular;
  pub {
    Conta(string t) {
      if (t != "") {
        titular = t;
      } else {
        titular = "?";
      }
    }
  }
}
fn main() { }
```

```erro
titular.cinza:4:5: SemanticError: O construtor de 'Conta' não atribui o campo obrigatório 'titular' no nível de cima do corpo (fora de if, laço e try): todo objeto precisa sair do construtor com ele
```

A regra é mais estrita do que o necessário neste exemplo (os dois lados do `if` atribuem), e é de
propósito: é uma regra que se lê de olho, sem análise de fluxo. Calcule o valor antes, numa variável
local, e atribua o campo uma vez:

```cinza
class Conta {
  string titular;
  pub {
    Conta(string t) {
      string nome = t;
      if (t == "") {
        nome = "?";
      }
      titular = nome;
    }
    fn quem() -> string { return titular; }
  }
}

fn main() {
  print(new Conta("").quem(), new Conta("Ana").quem());
}
```

```saida
? Ana
```

Ler o campo antes de atribuí-lo também é recusado:

```cinza antes.cinza
class Conta {
  string titular;
  pub {
    Conta(string t) {
      print(titular);
      titular = t;
    }
  }
}
fn main() { }
```

```erro
antes.cinza:5:13: SemanticError: O construtor de 'Conta' não pode ler o campo 'titular' antes de atribuir o campo obrigatório 'titular' (no nível de cima do corpo, fora de if, laço e try)
```

Um campo obrigatório **do próprio tipo** seria impossível: o primeiro objeto precisaria de outro
para existir. O que pode faltar se declara opcional:

```cinza
class No {
  i32 valor = 0;
  No? proximo;
  pub {
    No(i32 v) { valor = v; }
    fn liga(No n) { proximo = n; }
    fn conta() -> i32 {
      if (No p = proximo) {
        return 1 + p.conta();
      }
      return 1;
    }
  }
}

fn main() {
  No a = new No(1);
  No b = new No(2);
  a.liga(b);
  b.liga(new No(3));
  print(a.conta());
}
```

```saida
3
```

> **Vindo de outra linguagem.** Em Java e em C#, um campo não atribuído no construtor fica `null`
> (ou `0`), e o objeto existe meio montado até alguém usar o campo. Na Cinza esse objeto não chega a
> existir: o compilador confere o construtor.

## 12.6 `self`

`self` é o objeto atual, disponível nos métodos, no construtor e nos valores iniciais dos campos.
Ele serve para alcançar um campo escondido por um parâmetro de mesmo nome, para passar o objeto
adiante e para devolvê-lo — o que permite encadear chamadas:

```cinza
class Contador {
  i32 total = 0;
  i32 passo = 1;
  pub {
    Contador(i32 passo) {
      self.passo = passo;
    }
    fn avanca() -> Contador {
      total += passo;
      return self;
    }
    fn valor() -> i32 {
      return total;
    }
  }
}

fn main() {
  Contador c = new Contador(5);
  print(c.avanca().avanca().avanca().valor());
}
```

```saida
15
```

No construtor, o parâmetro `passo` esconde o campo `passo`; `self.passo` é o campo. `self` não pode
ser reatribuído — ele é sempre o objeto atual. E quem vem do Java ou do C# recebe a indicação:

```cinza this.cinza
class C {
  i32 x = 0;
  pub {
    fn f() -> i32 { return this.x; }
  }
}
fn main() { }
```

```erro
this.cinza:4:28: SemanticError: 'this' não existe na Cinza: o objeto atual é self (self.campo, self.metodo())
```

## 12.7 A ordem da criação

Num `new`, **primeiro os campos recebem os valores iniciais, na ordem em que estão declarados**, e
**depois roda o construtor**:

```cinza
fn log(string s, i32 v) -> i32 {
  print("valor inicial:", s);
  return v;
}

class C {
  i32 a = log("a", 1);
  i32 b = log("b", 2) + a;
  pub {
    C() {
      print("construtor: a =", a, "b =", b);
    }
  }
}

fn main() {
  C c = new C();
}
```

```saida
valor inicial: a
valor inicial: b
construtor: a = 1 b = 3
```

Um valor inicial pode usar os campos anteriores (`b` usa `a`) e `self` — **a menos que a classe
tenha um campo obrigatório**: os valores iniciais rodam antes do construtor, quando o obrigatório
ainda não tem valor.

Um valor inicial que cria um objeto da própria classe nunca terminaria (cada objeto criaria outro),
e é erro de compilação:

```cinza no.cinza
class No {
  No proximo = new No();
}
fn main() { }
```

```erro
no.cinza:2:16: SemanticError: 'No' cria a si mesmo nos valores iniciais dos campos (No → No): a criação nunca terminaria. Para 'nenhum ou alguns', use list<No> (vazia: nenhum).
```

## 12.8 O objeto é compartilhado

Esta é a diferença central entre uma classe e um struct: **atribuir um objeto não o copia**. As
variáveis passam a ver o mesmo objeto, e a igualdade (`==`) é a **identidade** — o mesmo objeto, não
"os mesmos campos":

```cinza
class Conta {
  i64 saldo = 0;
  pub {
    fn deposita(i64 valor) { saldo += valor; }
    fn saldo_atual() -> i64 { return saldo; }
  }
}

fn bonus(Conta c) {
  c.deposita(5);
}

fn main() {
  Conta a = new Conta();
  Conta b = a;
  b.deposita(10);
  bonus(a);
  Conta c = new Conta();
  c.deposita(15);
  print(a.saldo_atual(), c.saldo_atual(), a == b, a == c);
}
```

```saida
15 15 true false
```

`a` e `b` são a mesma conta; `bonus` recebeu a mesma conta também. `c` tem o mesmo saldo, mas é
outra conta: `a == c` é `false`.

Pelo mesmo motivo, o iterador de um `for` sobre uma lista de objetos é o próprio objeto — ao
contrário de uma lista de structs (seção 9.5), alterar o iterador altera o que está na lista:

```cinza
class Conta {
  i64 saldo = 0;
  pub {
    fn deposita(i64 valor) { saldo += valor; }
    fn saldo_atual() -> i64 { return saldo; }
  }
}

fn main() {
  list<Conta> contas = [new Conta(), new Conta()];
  for (Conta c in contas) {
    c.deposita(10);
  }
  print(contas[0].saldo_atual(), contas[1].saldo_atual());
}
```

```saida
10 10
```

## 12.9 Sem herança

A Cinza **não tem herança**: depois do `:` de uma classe vêm só interfaces.

```cinza cachorro.cinza
class Animal {
  pub {
    fn som() -> string { return "..."; }
  }
}
class Cachorro : Animal {
}
fn main() { }
```

```erro
cachorro.cinza:6:18: SemanticError: 'Animal' não é uma interface: depois de ':' vêm só interfaces (a Cinza não tem herança).
```

O reaproveitamento de código é por **composição**: a classe tem um campo com um objeto da outra e
delega a ele o que precisa.

```cinza
class Motor {
  i32 potencia = 0;
  pub {
    Motor(i32 p) { potencia = p; }
    fn descricao() -> string { return potencia + " cv"; }
  }
}

class Carro {
  string modelo;
  Motor motor;
  pub {
    Carro(string m, i32 cv) {
      modelo = m;
      motor = new Motor(cv);
    }
    fn ficha() -> string {
      return modelo + ", motor de " + motor.descricao();
    }
  }
}

fn main() {
  print(new Carro("Fusca", 65).ficha());
}
```

```saida
Fusca, motor de 65 cv
```

> **Por quê?** A herança junta duas coisas diferentes — reaproveitar código e ser usado no lugar de
> outro tipo — e as amarra para sempre: uma mudança na classe base pode quebrar todas as
> derivadas, e entender um método exige subir a hierarquia. A Cinza separa as duas: o
> reaproveitamento é por composição, explícito no campo, e o "ser usado no lugar de" é por
> interfaces (capítulo 13), que não têm código nem campos.

### Os modificadores de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `public` | o bloco `pub { ... }` |
| `private`, `protected` | todo campo é privado; um método fora do `pub` é privado |
| `this` | `self` |
| `extends`, `: Base`, `super` | composição (um campo); para polimorfismo, interfaces |
| `implements I` | `class C : I` |
| `static` | uma função ou um `const` no nível de cima |
| `abstract`, `virtual`, `override` | interfaces |
| vários construtores | um construtor; variações em funções que criam o objeto |

O compilador reconhece cada um e aponta o jeito da Cinza — um erro só por caso.

## Programa do capítulo: a classe `Agenda`

Até a versão 4, as tarefas viviam numa lista solta, e qualquer código podia pôr nela uma tarefa com
o título vazio, ou duas com o mesmo título. Na versão 5, **a lista mora dentro de uma classe**: só os
métodos da `Agenda` mexem nela, e eles garantem as regras.

```cinza tarefas.cinza
// tarefas.cinza — versão 5: a classe Agenda guarda as tarefas e as suas regras
import Lists;

enum Prioridade { Alta, Normal, Baixa }

struct Tarefa {
  string titulo;
  Prioridade prioridade = Prioridade.Normal;
  bool feita = false;
}

class Agenda {
  string dono;
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
    Agenda(string d) {
      dono = d;
    }

    fn adiciona(string titulo, Prioridade p) -> bool {
      if (titulo == "" or busca(titulo) != null) {
        return false;
      }
      tarefas.add(new Tarefa(titulo: titulo, prioridade: p));
      return true;
    }

    fn conclui(string titulo) -> bool {
      i32 i = busca(titulo) else { return false; };
      tarefas[i].feita = true;
      return true;
    }

    fn pendentes() -> i32 {
      i32 n = 0;
      for (Tarefa t in tarefas) {
        if (not t.feita) {
          n += 1;
        }
      }
      return n;
    }

    fn mostra() {
      printf("Agenda de {dono}: {pendentes()} pendente(s)");
      for (Tarefa t in tarefas) {
        string marca = "[ ]";
        if (t.feita) {
          marca = "[x]";
        }
        printf("  {marca} {t.titulo} ({t.prioridade})");
      }
    }
  }
}

fn main() {
  Agenda a = new Agenda("Ana");
  print(a.adiciona("comprar pão", Prioridade.Normal));
  print(a.adiciona("pagar a conta de luz", Prioridade.Alta));
  print(a.adiciona("comprar pão", Prioridade.Baixa));
  print(a.adiciona("", Prioridade.Alta));
  print(a.conclui("pagar a conta de luz"), a.conclui("lavar o carro"));
  a.mostra();
}
```

```saida
true
true
false
false
true false
Agenda de Ana: 1 pendente(s)
  [ ] comprar pão (Prioridade.Normal)
  [x] pagar a conta de luz (Prioridade.Alta)
```

Repare em:

- `tarefas` é privada: o único jeito de pôr uma tarefa na agenda é `adiciona`, que recusa o título
  vazio e o repetido.
- `busca` é um método **privado** — um detalhe da agenda, que ninguém de fora precisa conhecer.
- `pendentes()` é chamado dentro de `mostra()` sem `self.`: é o método do próprio objeto.
- A `Tarefa` continua um struct: ela é um dado, e quem guarda as regras é a `Agenda`.

`adiciona` responde `false` sem dizer **por quê** — título vazio ou repetido? O capítulo 14 troca o
`bool` por erros com tipo e mensagem. E o capítulo 13 usa interfaces para filtrar as tarefas por
critérios diferentes.

## Resumo

- `class Nome { campos; métodos privados; pub { construtor; métodos públicos } }`. Campos sempre
  privados (para a classe, não para o objeto); `print` mostra `<Nome object>`.
- No máximo um construtor, dentro do `pub`; os argumentos de `new` casam com ele, em ordem.
- Campo obrigatório: o construtor o atribui no nível de cima, antes de usar o objeto. Campo do
  próprio tipo: opcional (`No?`).
- `self` é o objeto atual; serve para o campo escondido, para passar e para devolver o objeto.
- Criação: valores iniciais na ordem da declaração, depois o construtor.
- O objeto é compartilhado: atribuição, argumento e `for` veem o mesmo objeto; `==` é identidade.
- Sem herança: composição para reaproveitar, interfaces para o polimorfismo.

## Exercícios

1. Escreva `class Termometro` com `fn registra(f64 graus)`, `fn media() -> f64?` (`null` sem
   leituras) e `fn maxima() -> f64?`. Registre 20,5, 23 e 19,25 graus e imprima a média e a máxima.
2. Escreva `class Pilha` de inteiros, com `fn empilha(i32 x)`, `fn desempilha() -> i32?` (`null`
   quando vazia), `fn topo() -> i32?` e `fn tamanho() -> i32`, guardando os elementos numa lista
   privada.
3. Escreva um `struct ContadorS { i32 n = 0; }` e uma `class ContadorC` com o mesmo campo e um
   método que soma 1. Passe cada um para uma função que "incrementa" e imprima os dois depois.
   Explique a diferença.
4. Escreva `class Retangulo` com largura e altura obrigatórias, `fn area() -> f64` e `fn escala(f64
   fator) -> Retangulo`, que multiplica as duas medidas e devolve `self`, para permitir
   `r.escala(2.ff).escala(1.5ff).area()`.
5. Escreva `class Biblioteca` que guarda uma lista privada de `struct Livro { string titulo; bool
   emprestado = false; }`, com `fn empresta(string titulo) -> bool` (falso se o livro não existe ou
   já está emprestado), `fn devolve(string titulo) -> bool` e `fn disponiveis() -> list<string>`.

As soluções estão em [`solucoes/12-classes.md`](solucoes/12-classes.md).

## Na spec

- A classe, os campos obrigatórios, a criação e `self`: [4.6](../spec/04-declaracoes.md)
- O objeto compartilhado: [5.1](../spec/05-execucao.md)
- A igualdade por identidade: [5.3](../spec/05-execucao.md)
- A herança e o que fica de fora: [README](../spec/README.md)
