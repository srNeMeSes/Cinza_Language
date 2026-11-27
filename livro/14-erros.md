# 14. Erros e exceções

**Objetivos.** Ao fim deste capítulo você sabe:

- capturar um erro de execução com `try`/`except`, e ler o tipo, a mensagem e a posição dele;
- declarar os seus próprios tipos de erro e lançá-los com `throw`;
- usar `finally` e prever o que acontece quando um erro surge no meio de um `finally`;
- decidir entre lançar um erro e devolver um opcional.

**Pré-requisitos.** Os capítulos 11 (o opcional) e 12 (classes).

---

Todo erro de execução da Cinza tem um **tipo** e uma **mensagem**. O que o compilador confere antes
de rodar não vira erro de execução; o que sobra — o índice fora da lista, a divisão por zero, o texto
que não é número — interrompe o caminho normal do programa e sobe pelas funções em andamento até
alguém capturá-lo com `try`/`except`. Se ninguém captura, o programa termina, com a mensagem e o
stack trace. Os seus programas lançam erros do mesmo jeito, com `throw`.

## 14.1 Os erros da linguagem

| Tipo | Quando a linguagem o lança |
|------|----------------------------|
| `ValueError` | um valor inválido para a operação: `cast<i32>("x")`, `range` com passo 0, `Strings.split` com separador vazio, a média de uma lista vazia |
| `IndexError` | um índice de lista ou de texto fora dos limites; o primeiro de uma lista vazia |
| `KeyError` | uma chave que não existe, ao ler, trocar ou remover |
| `ZeroDivisionError` | divisão ou resto por zero |
| `OverflowError` | um resultado fora da faixa do tipo; um `cast` que não cabe |
| `IOError` | uma falha ao ler ou gravar arquivos (capítulo 20) |
| `StackOverflowError` | mais de 2000 chamadas aninhadas (seção 5.5) |
| `TypeError` | a linguagem não lança (os tipos são conferidos na compilação); existe para o seu `throw` |
| `Error` | a raiz de todos: `except (Error e)` captura qualquer um |

## 14.2 Capturar: `try` e `except`

```cinza
fn main() {
  list<i32> l = [1, 2];
  try {
    print(l[5]);
    print("nunca");
  } except (IndexError e) {
    print("capturado:", e.message);
  }
  print("o programa segue");
}
```

```saida
capturado: índice 5 fora dos limites (tamanho: 2)
o programa segue
```

O erro interrompe o `try` no ponto em que surgiu (o `print("nunca")` não roda), e o primeiro
`except` do tipo dele recebe o erro numa variável — que existe só dentro do bloco do `except`.

Um erro tem quatro campos, só de leitura: `kind` (o nome do tipo), `message`, e `line` e `column` (a
posição onde ele surgiu). No `print`, aparece como `Tipo: mensagem`:

```cinza
fn main() {
  try {
    i32 zero = 0;
    print(10 / zero);
  } except (ZeroDivisionError e) {
    print(e.kind, "|", e.message, "|", e.line, e.column, "|", e);
  }
}
```

```saida
ZeroDivisionError | divisão por zero | 4 14 | ZeroDivisionError: divisão por zero
```

## 14.3 Vários `except`, e o erro que sobe

Um `try` pode ter vários `except`. O erro vai para **o primeiro** cujo tipo é o dele, ou para um
`except (Error ...)`, que captura qualquer erro:

```cinza
error Meu;

fn tenta(i32 c) {
  try {
    if (c == 0) {
      throw Meu("meu");
    }
    if (c == 1) {
      throw ValueError("valor");
    }
    throw KeyError("chave");
  } except (Meu e) {
    print("Meu:", e.message);
  } except (ValueError e) {
    print("ValueError:", e.message);
  } except (Error e) {
    print("qualquer outro:", e.kind);
  }
}

fn main() {
  tenta(0);
  tenta(1);
  tenta(2);
}
```

```saida
Meu: meu
ValueError: valor
qualquer outro: KeyError
```

Por isso o `except (Error ...)` precisa ser **o último**, e repetir um tipo é erro: os `except`
depois dele nunca seriam alcançados.

```cinza ordem.cinza
fn main() {
  try {
    print(1);
  } except (Error e) {
    print("qualquer");
  } except (ValueError e) {
    print("valor");
  }
}
```

```erro
ordem.cinza:6:5: SemanticError: 'except (Error e)' captura qualquer erro e precisa ser o último: os 'except' seguintes nunca seriam alcançados.
```

Um erro que nenhum `except` do `try` captura **continua subindo**: sai da função em que surgiu e vai
para quem a chamou, e assim por diante, até um `try` que o capture.

```cinza
fn le(list<i32> l, i32 i) -> i32 {
  try {
    return l[i];
  } except (ValueError e) {
    return -1;
  }
}

fn main() {
  try {
    print(le([1], 3));
  } except (IndexError e) {
    print("subiu até a main:", e.message);
  }
}
```

```saida
subiu até a main: índice 3 fora dos limites (tamanho: 1)
```

## 14.4 Os seus erros: `error`

`error Nome;`, no nível de cima, declara um novo tipo de erro, filho de `Error`. Um erro se cria com
`Nome("mensagem")` — ou `Nome()`, com a mensagem vazia — e se lança com `throw`:

```cinza
error SaldoInsuficiente;

fn saca(i64 saldo, i64 valor) -> i64 {
  if (valor > saldo) {
    throw SaldoInsuficiente("faltam " + (valor - saldo));
  }
  return saldo - valor;
}

fn main() {
  try {
    print(saca(100, 30));
    print(saca(100, 130));
  } except (SaldoInsuficiente e) {
    print(e.kind, e.message);
  }
}
```

```saida
70
SaldoInsuficiente faltam 30
```

O nome do tipo é a informação principal: é por ele que quem chama escolhe o que capturar. Um erro é
um valor como os outros — pode ser guardado numa variável do tipo `Error` (ou do próprio tipo) e
lançado depois —, mas não tem campos além dos quatro, e os quatro não mudam.

> **Vindo de outra linguagem.** Não há hierarquia de erros além de "filho de `Error`" — um
> `error` não herda de outro (não há herança, seção 12.9) —, nem campos próprios num erro, nem a
> declaração de quais erros uma função lança (o `throws` do Java). Para dados além da mensagem,
> ponha-os na mensagem, ou devolva um struct com o resultado.

## 14.5 `throw`, e lançar de novo

`throw` lança um erro — e só um erro: `throw "falhou"` é recusado na compilação. Dentro de um
`except`, `throw e;` **lança de novo** o erro capturado, preservando o tipo, a mensagem, a posição e
o stack trace originais — o jeito de registrar um erro sem escondê-lo de quem está acima:

```cinza
fn interna() {
  throw KeyError("k");
}

fn main() {
  try {
    try {
      interna();
    } except (Error e) {
      print("registrando:", e.kind);
      throw e;
    }
  } except (KeyError e) {
    print("de novo:", e.message, "linha", e.line);
  }
}
```

```saida
registrando: KeyError
de novo: k linha 2
```

A linha continua sendo a 2, onde o erro surgiu dentro de `interna` — não a do `throw e`.

## 14.6 `finally`

O bloco `finally` **sempre roda** ao sair do `try`: depois do fluxo normal, de um `return`, de um
`break` ou `continue`, de um erro capturado ou não capturado, e de um erro lançado dentro de um
`except`. Depois dele, o que estava pendente continua — o `return` acontece, o erro sobe:

```cinza
fn divide(i64 a, i64 b) -> i64 {
  try {
    return a / b;
  } except (ZeroDivisionError e) {
    print("capturado:", e.message);
    return 0;
  } finally {
    print("finally");
  }
}

fn main() {
  print(divide(10, 2));
  print(divide(1, 0));
}
```

```saida
finally
5
capturado: divisão por zero
finally
0
```

Repare na ordem: o `finally` roda **antes** de o valor do `return` chegar a quem chamou. É o lugar
das limpezas que precisam acontecer de qualquer jeito.

Um erro lançado **dentro do próprio `finally`** substitui o que estava pendente — o erro que subia
se perde, e o novo sobe no lugar dele:

```cinza
fn f() -> i32 {
  try {
    throw ValueError("primeiro");
  } finally {
    throw IndexError("segundo");
  }
}

fn main() {
  try {
    f();
  } except (Error e) {
    print(e.kind, e.message);
  }
}
```

```saida
IndexError segundo
```

`return`, `break` e `continue` não podem sair de um `finally` (é erro de compilação): o `finally`
termina normalmente ou com um erro. E o `exit(código)` roda os `finally` pendentes antes de
encerrar, mas nenhum `except` o captura, nem o `except (Error e)`:

<!-- exit: 4 -->
```cinza
fn main() {
  try {
    exit(4);
  } except (Error e) {
    print("não captura");
  } finally {
    print("finally roda");
  }
}
```

```saida
finally roda
```

Um `try` precisa de pelo menos um `except` ou do `finally`.

## 14.7 O erro que ninguém captura

Um erro que sobe até a `main` sem ser capturado **termina o programa** com o código de saída 1, a
mensagem e o stack trace — da chamada mais interna para fora (seção 1.5):

```cinza fundo.cinza
error Falha;

fn c() {
  throw Falha("lá no fundo");
}

fn b() {
  c();
}

fn a() {
  b();
}

fn main() {
  print("antes");
  a();
}
```

```saida
antes
```

```erro
fundo.cinza:4:9: Falha: lá no fundo
    em c (fundo.cinza:4)
    em b (fundo.cinza:8)
    em a (fundo.cinza:12)
    em main (fundo.cinza:17)
```

## 14.8 Erro ou opcional?

A regra do capítulo 11 decide (seção 11.8): o que **pode faltar de verdade** é um opcional, e quem
chama resolve; o que é **um engano** — um dado que viola a regra do programa, um índice que não
devia estar fora — é um erro. Os dois se combinam bem: um opcional resolvido com um `else` que lança
transforma "não havia valor" em "isso é um erro, aqui":

```cinza
import Convert;

fn idade(string s) -> i32 {
  i32 n = Convert.to_i32(s) else { throw ValueError("idade inválida: " + s); };
  if (n < 0) {
    throw ValueError("idade negativa: " + n);
  }
  return n;
}

fn main() {
  for (string s in ["30", "x", "-2"]) {
    try {
      print(idade(s));
    } except (ValueError e) {
      print(e.message);
    }
  }
}
```

```saida
30
idade inválida: x
idade negativa: -2
```

> **Por quê?** Um erro que pode ser ignorado acaba ignorado: um `-1` que ninguém confere, um
> `null` que passa adiante. Na Cinza, o que pode faltar é opcional — e o compilador exige que a
> ausência seja resolvida —, e o engano é um erro — que, se ninguém o capturar, para o programa no
> lugar certo, com o tipo e a mensagem. Não há o caminho do meio, o valor errado que segue em
> silêncio.

## 14.9 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `catch (Exception e)`, `rescue` | `except (Error e)` |
| `raise`, `throw new X(...)` | `throw X("...")` |
| `class MeuErro extends Exception` | `error MeuErro;` |
| `e.getMessage()`, `str(e)` | `e.message`, `print(e)` |
| `throws X` na assinatura | não há: qualquer função pode lançar |
| `panic` / `recover` (Go) | `throw` / `except` |
| `Result<T, E>` (Rust) | um opcional, ou um erro |

O compilador reconhece o `catch` e o `raise`:

```cinza captura.cinza
fn main() {
  try {
    print(1);
  } catch (Error e) {
    print(e.message);
  }
}
```

```erro
captura.cinza:4:5: SyntaxError: A Cinza escreve 'except', não 'catch': except (ValueError e) { ... } (token: 'catch')
```

## Programa do capítulo: as regras viram erros

Na versão 5 do `tarefas` (capítulo 12), `adiciona` devolvia `false` sem dizer por quê. Na versão 7,
**cada regra violada é um erro com tipo**: o título vazio, a tarefa repetida, a tarefa que não
existe. O programa processa uma lista de comandos e, para cada um que falha, mostra o motivo e
segue com o próximo:

```cinza tarefas.cinza
// tarefas.cinza — versão 7: as regras da agenda viram erros com tipo
import Strings;

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
      if (busca(t) != null) {
        throw TarefaRepetida("já existe a tarefa '" + t + "'");
      }
      tarefas.add(new Tarefa(titulo: t));
    }

    fn conclui(string titulo) {
      i32 i = busca(titulo) else { throw TarefaNaoEncontrada("não há a tarefa '" + titulo + "'"); };
      tarefas[i].feita = true;
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

fn main() {
  Agenda a = new Agenda();
  list<string> comandos = ["add comprar pão", "add  ", "add estudar a Cinza", "add comprar pão",
                           "fim comprar pão", "fim lavar o carro", "del comprar pão"];
  i32 falhas = 0;
  for (string c in comandos) {
    try {
      executa(a, c);
    } except (TituloInvalido e) {
      print("título inválido:", e.message);
      falhas += 1;
    } except (Error e) {
      printf("{e.kind}: {e.message}");
      falhas += 1;
    }
  }
  print(a.resumo(), "|", falhas, "comandos falharam");
}
```

```saida
título inválido: o título não pode ser vazio
TarefaRepetida: já existe a tarefa 'comprar pão'
TarefaNaoEncontrada: não há a tarefa 'lavar o carro'
ValueError: comando desconhecido: del
1 de 2 feitas | 4 comandos falharam
```

Repare em:

- Os erros são lançados dentro da `Agenda` e capturados na `main`, duas chamadas acima: `executa`
  não captura nada, e o erro passa por ela.
- O `except (TituloInvalido e)` trata um tipo à parte; o `except (Error e)` final pega os outros —
  inclusive o `ValueError` que o próprio `executa` lança.
- `conclui` usa o `else` que lança (seção 14.8): a busca devolve um opcional, e a ausência, aqui,
  é um erro.
- Um comando que falha não interrompe os seguintes: o `try` está **dentro** do laço.

## Resumo

- Erros de execução têm tipo (`ValueError`, `IndexError`, `KeyError`...) e sobem pelas chamadas até
  um `try` que os capture; sem captura, o programa termina com o código 1 e o stack trace.
- `try { } except (Tipo e) { }`: o primeiro `except` do tipo do erro; `except (Error e)` captura
  qualquer um e fica por último. `e.kind`, `e.message`, `e.line`, `e.column`, só de leitura.
- `error Nome;` declara um tipo; `throw Nome("mensagem")` lança; `throw e;` relança preservando a
  origem.
- `finally` sempre roda; um erro nele substitui o pendente; `return`, `break` e `continue` não saem
  dele; o `exit` roda os `finally` e não é capturado.
- O que pode faltar é opcional; o engano é erro.

## Exercícios

1. Escreva `fn divide_seguro(i32 a, i32 b) -> i32?`, que devolve `null` em vez de deixar o
   `ZeroDivisionError` subir. (Depois, compare com testar `b == 0` antes: qual é mais claro?)
2. Declare `error SenhaFraca;` e escreva `fn valida(string senha)`, que lança `SenhaFraca` com uma
   mensagem diferente para cada regra quebrada: menos de 8 caracteres, ou sem nenhum dígito. Teste
   uma lista de senhas, mostrando o resultado de cada uma.
3. Sem rodar, diga o que este programa imprime, e em que ordem:

   ```text
   fn f() -> i32 {
     try {
       print("try");
       return 1;
     } finally {
       print("finally");
     }
   }
   fn main() {
     print("resultado", f());
   }
   ```

4. Escreva `fn registra_e_relanca(list<i32> l, i32 i) -> i32`, que devolve `l[i]` e, se der erro,
   imprime `"erro registrado: <tipo>"` e relança o mesmo erro. Capture-o na `main` e mostre a linha.
5. Converta `["10", "abc", "7", "", "3"]` em números com `cast<i32>`, guardando os válidos numa lista
   e os inválidos noutra, sem parar no primeiro erro.

As soluções estão em [`solucoes/14-erros.md`](solucoes/14-erros.md).

## Na spec

- A declaração `error`: [4.10](../spec/04-declaracoes.md)
- `throw`, `try`, `except` e `finally`: [5.6](../spec/05-execucao.md)
- Os tipos de erro e o formato das mensagens: [7.1 e 7.3](../spec/07-erros.md)
- `exit` e os `finally`: [6.1](../spec/06-biblioteca.md)
