# 11. O opcional: `T?` e `null`

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar um valor que pode faltar (`T?`) e entender por que nenhum outro tipo admite `null`;
- resolver a ausência com `else`, com um bloco `else { ... yield ... }` e com `if`/`while` com
  declaração;
- reconhecer os opcionais da biblioteca e decidir quando um opcional é melhor que um erro.

**Pré-requisitos.** Os capítulos 6 a 10 (o opcional já apareceu neles, adiantado).

---

Em muitas linguagens, qualquer referência pode ser `null` — e o "erro de um bilhão de dólares",
como o próprio criador do `null` o chamou, é o programa que cai porque um valor que "sempre
existe" não existia. Na Cinza, **um valor de um tipo comum nunca é `null`**: um `string` é sempre um
texto, uma lista é sempre uma lista. A ausência existe só num tipo que a declara, o **opcional**
`T?` — e o compilador não deixa usar um `T?` como `T` sem que a ausência seja resolvida antes.

## 11.1 `null` só onde o tipo diz

```cinza
fn main() {
  string? apelido;
  string? cidade = "Recife";
  print(apelido, cidade, type(apelido), apelido == null, cidade == "Recife");
  apelido = "Bia";
  cidade = null;
  print(apelido, cidade);
}
```

```saida
null Recife string? true true
Bia null
```

- `T?` é "um `T` ou `null`". Qualquer tipo de valor pode ser opcional: `i32?`, `list<i32>?`,
  `Tarefa?`.
- Um `T?` declarado sem valor **nasce `null`**.
- Um `T` cabe num `T?` (`string? cidade = "Recife"`); `null` só cabe num `T?`.
- Sobre um `T?` valem `==` e `!=` — com `null`, com outro `T?` do mesmo tipo ou com um valor do
  tipo dele.

O sentido contrário é o que importa: **um `T?` nunca cabe num `T`** sem a ausência resolvida.

```cinza apelido.cinza
fn main() {
  string? apelido = "Bia";
  string nome = apelido;
}
```

```erro
apelido.cinza:3:10: SemanticError: Tipo incompatível na declaração de 'nome': declarado 'string', mas atribuído 'string?'. O valor é opcional ('string?'): resolva a ausência antes, com else (ex.: valor else padrão) ou com if (T v = valor) { ... }; um x != null não muda o tipo de x
```

Também não se chama método, não se lê campo, não se indexa, não se percorre e não se faz conta com
um `T?`: tudo isso exige o valor. E `null` não cabe num tipo comum:

```cinza nome.cinza
fn main() {
  string nome = null;
}
```

```erro
nome.cinza:2:10: SemanticError: Tipo incompatível na declaração de 'nome': declarado 'string', mas atribuído 'null'. null só cabe num tipo opcional: declare 'string?'
```

> **Vindo de outra linguagem.** É o `String?` do Kotlin, do Swift e do C# com tipos anuláveis, e o
> `Optional<String>` do Java — mas conferido sempre, sem exceção: não existe um `T` que possa ser
> `null` "por baixo". `nil`, `None`, `NULL`, `undefined` e `nullptr` não existem; o compilador
> aponta o `null`.

## 11.2 `valor else alternativa`

`x else y` é o valor de `x`, ou `y` quando `x` é `null`. O resultado é um `T` comum:

```cinza
fn main() {
  string? apelido;
  string nome = apelido else "sem apelido";
  i32? n = 5;
  print(nome, n else 0, type(n else 0));
}
```

```saida
sem apelido 5 i32
```

**A alternativa só é avaliada quando o valor é `null`** — ela pode ser uma conta cara ou uma
chamada com efeito:

```cinza
fn padrao() -> string {
  print("calculou o padrão");
  return "?";
}

fn main() {
  string? a = "tem";
  string? b;
  print(a else padrao());
  print(b else padrao());
}
```

```saida
tem
calculou o padrão
?
```

A alternativa pode ser **outro opcional**, para encadear: o resultado continua `T?` até a última
alternativa ser um `T`. O `else` associa à direita: `a else b else c` é `a else (b else c)`.

```cinza
fn main() {
  string? do_usuario;
  string? do_arquivo = "azul";
  print(do_usuario else do_arquivo else "cinza");
  string? talvez = do_usuario else do_arquivo;
  print(type(talvez));
}
```

```saida
azul
string?
```

O compilador confere os dois lados: o valor antes do `else` precisa ser opcional, a alternativa
precisa ser do tipo de dentro (ou outro `T?` dele), e `else null` não resolve nada:

```cinza zero.cinza
fn main() {
  i32 n = 1;
  i32 m = n else 0;
}
```

```erro
zero.cinza:3:13: SemanticError: O valor antes de 'else' não é opcional ('i32'): ele sempre existe, e não há ausência a resolver
```

## 11.3 `else` com um bloco

Quando a ausência pede mais que um valor — um aviso, uma conta, sair —, a alternativa é um bloco.
Ele roda só se o valor for `null`, e **todo caminho dele termina**: com `yield valor` (o valor que a
expressão recebe) ou saindo do fluxo com `return`, `throw`, `break` ou `continue`.

```cinza
fn main() {
  string? nada;
  string nome = nada else {
    print("sem nome: usando o padrão");
    yield "anônimo";
  };
  print(nome);
}
```

```saida
sem nome: usando o padrão
anônimo
```

As saídas do fluxo fazem do `else` uma guarda no começo de uma função ou de uma volta de laço — o
valor fica disponível, com o tipo comum, no resto do código:

```cinza
import Convert;

fn tamanho(string? s) -> i32 {
  string t = s else { return -1; };
  return t.size();
}

fn main() {
  print(tamanho("abc"), tamanho(null));
  i32 soma = 0;
  for (string s in ["1", "x", "3"]) {
    i32 n = Convert.to_i32(s) else {
      print("ignorando:", s);
      continue;
    };
    soma += n;
  }
  print(soma);
}
```

```saida
3 -1
ignorando: x
4
```

Um bloco que pode chegar ao `}` sem `yield` nem saída é erro — senão a expressão ficaria sem valor:

```cinza bloco.cinza
fn main() {
  string? s;
  string t = s else { print("sem valor"); };
}
```

```erro
bloco.cinza:3:16: SemanticError: O bloco do 'else' precisa terminar em todos os caminhos: com yield valor (o valor da expressão) ou saindo (return, throw, break, continue)
```

`yield` só existe dentro do bloco de um `else`, e não sai de dentro de um `try` (guarde o valor
numa variável e use `yield` depois do `try`).

## 11.4 `if` e `while` com declaração

`if (T nome = opcional) { ... }` roda o bloco **quando há valor**, com ele em `nome` — do tipo de
dentro, sem o `?`. O `else` roda quando não há:

```cinza
import Strings;

fn main() {
  string s = "chave=valor";
  if (i32 pos = Strings.find(s, "=")) {
    print("achou na posição", pos);
  } else {
    print("não achou");
  }
  if (var pos = Strings.find(s, ":")) {
    print(pos);
  } else {
    print("não tem dois-pontos");
  }
}
```

```saida
achou na posição 5
não tem dois-pontos
```

- O tipo é o **exato** de dentro (`i32` para um `i32?`; `var` também vale) — nem mais largo, nem
  outro.
- `nome` existe só no primeiro bloco; no `else`, não há valor para ele.
- O valor precisa ser opcional: para um valor que sempre existe, declare a variável antes do `if`.

`while (T nome = opcional) { ... }` repete enquanto houver valor, **avaliando o opcional de novo a
cada volta**. O uso clássico é ler a entrada até o fim — `input` devolve `string?`, e `null` no fim
da entrada:

<!-- stdin: um -->
<!-- stdin: dois -->
```cinza leitura.cinza
fn main() {
  while (string linha = input("")) {
    print("[" + linha + "]");
  }
  print("fim da entrada");
}
```

```console
$ printf "um\ndois\n" | cinza leitura.cinza
```

```saida
[um]
[dois]
fim da entrada
```

## 11.5 Sem estreitamento

Em Kotlin e em TypeScript, depois de `if (s != null)` o compilador passa a tratar `s` como não
nulo. **A Cinza não faz isso**: `s != null` é um `bool` como outro qualquer, e `s` continua `string?`.

```cinza estreita.cinza
fn main() {
  string? s = "abc";
  if (s != null) {
    print(s.size());
  }
}
```

```erro
estreita.cinza:4:12: SemanticError: Tipo 'string?' não possui métodos: o valor é opcional; resolva a ausência antes, com else (ex.: valor else padrão) ou com if (T v = valor) { ... }; um x != null não muda o tipo de x
```

A forma da Cinza dá um nome ao valor que existe:

```cinza
fn main() {
  string? s = "abc";
  if (string v = s) {
    print(v.size());
  }
}
```

```saida
3
```

> **Por quê?** O estreitamento depende de o compilador provar que nada mudou `s` entre o teste e o
> uso — uma regra com exceções (um campo alterado por um método no meio, uma variável capturada).
> Com o `if` com declaração não há o que provar: `v` é outra variável, de outro tipo, que só existe
> onde o valor existe.

## 11.6 Opcionais em coleções, campos e parâmetros

Um elemento de lista, o valor de um dicionário, um campo e um parâmetro podem ser opcionais:

```cinza
struct Pessoa {
  string nome;
  i32? idade;
  string? email;
}

fn saudacao(string? nome) -> string {
  return "Olá, " + (nome else "visitante") + "!";
}

fn main() {
  list<i32?> notas = [8, null, 10];
  i32 soma = 0;
  for (i32? n in notas) {
    soma += n else 0;
  }
  Pessoa p = new Pessoa(nome: "Ana", idade: 30);
  print(notas, soma);
  print(p);
  print(p.email else "(sem email)", saudacao("Bia"), saudacao(null));
}
```

```saida
[8, null, 10] 18
Pessoa{nome: "Ana", idade: 30, email: null}
(sem email) Olá, Bia! Olá, visitante!
```

Um campo `T?` nasce `null` e **não é obrigatório** na criação do struct. A chave de um dicionário
não pode ser opcional, e não existe opcional de opcional (`T??`): um `T?` já tem o seu `null`.

Uma função que pode não ter resposta devolve um `T?`, e quem chama decide o que fazer:

```cinza
fn primeiro_par(list<i32> l) -> i32? {
  for (i32 x in l) {
    if (x % 2 == 0) {
      return x;
    }
  }
  return null;
}

fn main() {
  print(primeiro_par([1, 3, 4, 6]), primeiro_par([1, 3]));
  print(primeiro_par([5, 8]) else -1);
}
```

```saida
4 null
8
```

## 11.7 Os opcionais da biblioteca

O que pode faltar de verdade, na biblioteca, devolve um opcional — nada devolve um `-1` ou um texto
vazio fingindo ser resposta:

| Função | Devolve `null` quando |
|--------|------------------------|
| `Strings.find`, `find_last`, `find_from` | o trecho não aparece |
| `Lists.index_of`, `last_index_of` | o elemento não está na lista |
| `Dicts.get(d, k)` | a chave não existe |
| `Convert.to_i32`, `to_i64`, `to_f32`, `to_f64`, `to_bool` | o texto não é aceito |
| `Env.get(nome)` | a variável de ambiente não existe |
| `input(prompt)` | a entrada acabou |
| `Regex.find`, e os grupos de `Regex.groups` | o padrão não casa, ou o grupo não participou |

```cinza
import Convert;
import Env;

fn main() {
  print(Convert.to_i32("42"), Convert.to_i32("4x2"), Convert.to_bool("talvez"));
  i32 idade = Convert.to_i32("abc") else 0;
  string tema = Env.get("TEMA_DO_LIVRO_CINZA") else "cinza";
  print(idade, tema);
}
```

```saida
42 null null
0 cinza
```

## 11.8 Opcional ou erro?

A regra da biblioteca serve também para os seus programas:

- **o que pode faltar de verdade** — uma busca sem resultado, uma configuração que o usuário não
  deu, um texto que pode não ser número — é um `T?`: faltar é um resultado normal, e quem chama
  decide;
- **o que quem chama controla** — um índice fora da lista, uma chave que deveria existir, o
  primeiro elemento de uma lista vazia — é um **erro**: se aconteceu, há um engano no programa, e ele
  deve parar ali (capítulo 14).

```cinza erro.cinza
import Lists;
fn main() {
  list<i32> l = [1, 2];
  print(Lists.index_of(l, 5));
  print(l[5]);
}
```

```saida
null
```

```erro
erro.cinza:5:10: IndexError: índice 5 fora dos limites (tamanho: 2)
    em main (erro.cinza:5)
```

Procurar o 5 e não achar é uma resposta (`null`); ler a posição 5 de uma lista de dois elementos é
um engano.

## 11.9 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `x ?? y` (C#, JS), `x ?: y` (Kotlin), `opt.orElse(y)` (Java) | `x else y` |
| `if let v = x` (Swift, Rust), `x?.let { }` (Kotlin) | `if (T v = x) { }` |
| `if (x != null) { x.f() }` (Kotlin, TS) | `if (T v = x) { v.f() }` |
| `x!!` (Kotlin), `x!` (Swift), `x.unwrap()` (Rust) | `x else { throw ... }` — a falha, escrita |
| `nil`, `None`, `NULL`, `undefined`, `nullptr` | `null` |
| `x?.campo` (encadeamento opcional) | `if (T v = x) { ... v.campo ... }` |

## Programa do capítulo: buscar uma tarefa pelo título

Na versão 3 do `tarefas` (capítulo 10), uma função que não achava a tarefa imprimia "não achei"
por conta própria. Na versão 4, **buscar devolve um opcional** — a posição, ou `null` —, e cada quem
chama decide o que fazer com a ausência. A tarefa também ganha dois campos que podem faltar: o
responsável e o prazo em dias.

```cinza tarefas.cinza
// tarefas.cinza — versão 4: o que pode faltar é opcional
enum Prioridade { Alta, Normal, Baixa }

struct Tarefa {
  string titulo;
  Prioridade prioridade = Prioridade.Normal;
  bool feita = false;
  string? responsavel;
  i32? prazo_dias;
}

fn busca(list<Tarefa> tarefas, string titulo) -> i32? {
  for (i32 i in range(0, tarefas.size())) {
    if (tarefas[i].titulo == titulo) {
      return i;
    }
  }
  return null;
}

fn conclui(list<Tarefa> tarefas, string titulo) -> bool {
  i32 i = busca(tarefas, titulo) else { return false; };
  tarefas[i].feita = true;
  return true;
}

fn main() {
  list<Tarefa> tarefas = [
    new Tarefa(titulo: "comprar pão", responsavel: "Ana"),
    new Tarefa(titulo: "pagar a conta de luz", prioridade: Prioridade.Alta, prazo_dias: 2),
    new Tarefa(titulo: "estudar a Cinza", prazo_dias: 30)
  ];
  print(conclui(tarefas, "comprar pão"), conclui(tarefas, "lavar o carro"));

  if (i32 i = busca(tarefas, "pagar a conta de luz")) {
    Tarefa t = tarefas[i];
    printf("achei: {t.titulo}, prazo de {t.prazo_dias else 0} dias");
  }

  i32 sem_prazo = 0;
  for (Tarefa t in tarefas) {
    string quem = t.responsavel else "ninguém";
    string marca = "[ ]";
    if (t.feita) {
      marca = "[x]";
    }
    printf("{marca} {t.titulo} — responsável: {quem}");
    if (t.prazo_dias == null) {
      sem_prazo += 1;
    }
  }
  print(sem_prazo, "tarefa(s) sem prazo");
}
```

```saida
true false
achei: pagar a conta de luz, prazo de 2 dias
[x] comprar pão — responsável: Ana
[ ] pagar a conta de luz — responsável: ninguém
[ ] estudar a Cinza — responsável: ninguém
1 tarefa(s) sem prazo
```

Repare em:

- `busca` devolve `i32?`: a posição da tarefa, que serve para alterá-la no lugar (seção 9.5), ou
  `null`. Não há um `-1` que alguém possa usar como índice por engano.
- `conclui` resolve a ausência com uma guarda — `else { return false; }` — e o resto da função usa
  `i` como um `i32` comum.
- `responsavel` e `prazo_dias` são opcionais: não são obrigatórios na criação (nascem `null`), e
  cada uso diz o que fazer quando faltam (`else "ninguém"`, `else 0`, ou `== null` para contar).

As tarefas e as funções que mexem nelas ainda andam separadas — qualquer código pode pôr na lista
uma tarefa com o título vazio, por exemplo. O capítulo 12, que abre a Parte III, junta os dois numa
classe.

## Resumo

- Nenhum tipo comum admite `null`; a ausência existe só em `T?`. Um `T` cabe num `T?`, nunca o
  contrário sem resolver a ausência.
- `x else y` dá o valor ou a alternativa (avaliada só se precisar); encadeia à direita; `else null`
  não vale.
- `x else { ...; yield v; }` — ou `return`, `throw`, `break`, `continue` — para a ausência que pede
  mais que um valor.
- `if (T v = x) { } else { }` e `while (T v = x) { }` dão nome ao valor que existe; não há
  estreitamento por `x != null`.
- Elementos, valores de dicionário, campos (que nascem `null`) e parâmetros podem ser opcionais.
- A biblioteca devolve `T?` para o que pode faltar de verdade, e lança erro para o engano de quem
  chama. Faça o mesmo.

## Exercícios

1. Escreva `fn primeiro_maior(list<i32> l, i32 limite) -> i32?`, o primeiro elemento maior que
   `limite`. Use-a uma vez com `else` e uma vez com `if` com declaração.
2. Dadas as linhas `["nome=Ana", "idade", "cidade=Recife", "=vazio"]`, monte um `dict<string,
   string>` com as que têm a forma `chave=valor` (com a chave não vazia), ignorando as outras com um
   `else { continue; }`.
3. Leia números da entrada, um por linha, até ela acabar, e imprima a soma. Linhas que não são
   números são ignoradas, com um aviso.
4. Com `struct Pessoa { string nome; string? apelido; }`, escreva `fn como_chamar(Pessoa p) ->
   string`, que devolve o apelido, se houver, e o nome, senão.
5. Explique por que o trecho abaixo não compila e reescreva-o de dois jeitos diferentes.

   ```text
   fn iniciais(string? nome) -> string {
     if (nome != null) {
       return nome[0];
     }
     return "?";
   }
   ```

As soluções estão em [`solucoes/11-opcional.md`](solucoes/11-opcional.md).

## Na spec

- O opcional, `null`, `else`, `yield` e o `if`/`while` com declaração: [3.10](../spec/03-tipos.md)
- O que pode faltar na biblioteca: [6.2](../spec/06-biblioteca.md)
- A gramática do `else` e da condição com declaração: [2.3 e 2.4](../spec/02-gramatica.md)
