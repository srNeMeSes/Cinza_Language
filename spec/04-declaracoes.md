# 4. Declarações

## 4.1 Programa

Um programa é um arquivo principal mais os módulos que ele importa (seção 4.11). Em cada
arquivo, os `import` vêm primeiro e depois só **declarações**: `fn`, `class`, `struct`, `enum`,
`interface`, `error` e `const`. Não há comandos soltos nem variáveis globais mutáveis no nível
superior.

```cinza
print(1);
fn main() { }
// expect-error: Comando solto no nível superior
```

```cinza
i64 contador = 0;
fn main() { }
// expect-error: precisa ser 'const': não há variáveis globais mutáveis
```

As declarações do nível superior podem ser usadas antes de onde aparecem no arquivo (uma função
pode chamar outra declarada mais abaixo; uma classe pode usar outra declarada depois). A exceção
são os `const` globais, que só enxergam `const` declarados antes deles (seção 4.3).

## 4.2 A função `main`

O arquivo principal precisa ter **exatamente uma** `main`, numa destas formas:

```
fn main() { }
fn main() -> void { }
fn main(list<string> args) { }
fn main(list<string> args) -> void { }
```

O nome do parâmetro é livre. `args` recebe os argumentos da linha de comando escritos depois do
nome do arquivo (`cinza prog.cinza a b` → `["a", "b"]`). Qualquer outra assinatura é erro de
compilação.

A `main` é o ponto de entrada: o programa não pode chamá-la, e um módulo importado não pode
declarar uma `main`.

**Ordem de execução**: primeiro são avaliados os `const` globais de cada módulo (os módulos
importados antes de quem os importa, o principal por último; dentro de um arquivo, na ordem em
que aparecem); depois a `main` é chamada. Um erro de runtime ao avaliar um `const` encerra o
programa antes da `main`.

## 4.3 `const` global

```
const i64 LIMITE = 100;
const f64 TAXA = LIMITE * 0.5ff;
```

O valor inicial de um `const` global precisa ser uma **expressão constante**: literais, `null`,
valores de enum, operadores (aritméticos, de comparação, lógicos e o `else`), `cast<T>`,
parênteses e outros `const` globais **já declarados** — também os de um módulo importado
(`util.LIMITE`) e os nativos (`Math.pi`). Chamadas de função, `new` e literais de `list`, `dict` e
`pair` não são permitidos.

O valor é **calculado na compilação**, com as mesmas regras das contas da execução (seção 5.3):
na execução não há ordem de inicialização, e um `const` vale o mesmo em qualquer módulo. Uma conta
que falharia (um estouro, uma divisão por zero, um `cast` que não cabe) é erro de compilação no
`const`.

```cinza
const i32 X = 2147483647 + 1;
fn main() { print(X); }
// expect-error: O valor do const 'X' não pode ser calculado: OverflowError
```

```cinza
const i64 A = 2;
const i64 B = A * 3;
fn main() { print(B); }
// expect: 6
```

```cinza
const i64 B = A * 3;
const i64 A = 2;
fn main() { print(B); }
// expect-error: Variável 'A' não declarada
```

## 4.4 Funções

```
fn nome(tipo1 p1, tipo2 p2) -> tipoRetorno { ... }
```

- Sem `-> tipo`, a função é `void`. Uma função `void` pode usar `return;` para sair antes; uma
  função que retorna valor precisa **retornar em todos os caminhos** (seção 5.5).
- Os parâmetros recebem cópias dos argumentos com a semântica da seção 5.1 (valores primitivos e
  struct são copiados; list, dict e objetos são compartilhados). Um parâmetro `const` não pode
  ser reatribuído (nem alterado, se for um recipiente).
- Não há sobrecarga: dois itens do nível superior com o mesmo nome são erro.
- Funções **não são valores**: o nome de uma função só pode ser usado numa chamada.
- Uma função do nível superior pode ser **genérica** (`fn maior[T](T a, T b) -> T`): capítulo 8.
- Uma função do programa com o nome de uma embutida (`print`, `range`...) tem precedência sobre
  ela.

```cinza
fn par(i64 n) -> bool {
  if (n == 0) { return true; }
  return impar(n - 1);
}
fn impar(i64 n) -> bool {
  if (n == 0) { return false; }
  return par(n - 1);
}
fn main() { print(par(10), impar(7)); }
// expect: true true
```

```cinza
fn f() -> i64 { return 1; }
fn main() { print(f); }
// expect-error: 'f' é uma função
```

## 4.5 Variáveis locais e escopo

`tipo nome = valor;` declara uma variável local. O escopo de uma variável vai da declaração ao
fim do bloco em que ela foi declarada. Um bloco interno pode declarar uma variável com o mesmo
nome de outra externa (a interna a esconde até o fim do bloco); no mesmo bloco, repetir um nome é
erro. Os parâmetros estão no mesmo escopo do corpo da função.

```cinza
fn main() {
  i64 x = 1;
  if (true) {
    i64 x = 2;
    print(x);
  }
  print(x);
}
// expect: 2
// expect: 1
```

O iterador do `for` existe só dentro do corpo do laço; a variável do `except`, só dentro do bloco
do `except`.

Uma função nunca enxerga as variáveis de quem a chamou: só seus parâmetros e locais, os campos do
objeto atual (em métodos) e os `const` globais.

## 4.6 Classes

```
class Nome : Interface1, Interface2 {
  tipo campo = valor;            // campos: sempre privados
  tipo obrigatorio;              // sem valor inicial: o construtor atribui
  fn auxiliar() { ... }          // método privado
  pub {
    Nome(parâmetros) { ... }     // construtor (opcional, no máximo um)
    fn metodo() -> tipo { ... }  // métodos públicos
  }
}
```

**Campos** são sempre privados: só os métodos da própria classe os acessam (inclusive em outro
objeto da mesma classe). Dentro de um método, um campo é usado pelo nome (`total`) ou por
`self.total`. Um parâmetro ou variável local com o nome de um campo esconde o campo; use
`self.campo` para alcançá-lo.

**Métodos** declarados fora do `pub { }` são privados; dentro, públicos. Declarar o mesmo método
duas vezes (inclusive um dentro e outro fora do `pub`) é erro. Um campo e um método podem ter o
mesmo nome, porque são usados de formas diferentes (`x` e `x()`). Dentro de um método, `m()`
chama o método `m` do próprio objeto, com precedência sobre uma função global de mesmo nome.

**Campo sem valor inicial.** `list` e `dict` nascem vazios e um opcional (`T?`) nasce `null`
(seção 3.10). Qualquer outro campo sem valor inicial é **obrigatório**, e então:

- a classe precisa de um construtor, que atribui cada campo obrigatório (`campo = valor;`, ou
  `self.campo = valor;` quando um parâmetro tem o nome do campo) **no nível de cima** do corpo —
  fora de `if`, laço e `try` — **antes** de ler um campo, chamar um método, usar `self` de outro
  jeito ou retornar; um caminho que não atribua é erro de compilação;
- os valores iniciais dos outros campos não podem usar o objeto (`self`, os campos, os métodos),
  porque rodam antes do construtor;
- um campo obrigatório do próprio tipo (`class No { No prox; }`), ou um ciclo deles entre classes
  (`A` exige um `B` e `B` exige um `A`), é erro de compilação: o primeiro objeto nunca poderia ser
  criado. O que pode faltar é um opcional: `No? prox;`.

Assim nenhum objeto existe com um campo sem valor.

**Criação**: `new Nome(argumentos)`. Primeiro os campos recebem seus valores iniciais, na ordem
em que estão declarados (um valor inicial pode usar campos anteriores, `self` e `const`
globais, se a classe não tiver campo obrigatório); depois roda o construtor, se houver. Os
argumentos de `new` precisam casar com o construtor (sem construtor, nenhum argumento).

```cinza
class Conta {
  string titular;
  i64 saldo;
  list<string> historico;
  pub {
    Conta(string t, i64 inicial) {
      titular = t;
      saldo = inicial;
      registra("aberta");
    }
    fn registra(string s) { historico.add(s); }
    fn resumo() -> string { return titular + " " + cast<string>(saldo) + " " + cast<string>(historico.size()); }
  }
}
fn main() { print(new Conta("ana", 10).resumo()); }
// expect: ana 10 1
```

```cinza
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
// expect-error: O construtor de 'Conta' não pode ler o campo 'titular' antes de atribuir o campo obrigatório 'titular'
```

Os valores iniciais rodam a cada criação, então um `new` escrito neles que volta ao próprio tipo
— direto (`No prox = new No();`) ou por outros tipos (`A` cria `B` e `B` cria `A`, também pelos
valores padrão de um struct) — é erro de compilação: a criação nunca terminaria. O "próximo" que
pode não existir é um `No?`, que nasce `null` (seção 3.10). Por um construtor, um método ou uma função, a
criação sem fim passa por chamadas e para no limite delas (seção 5.8).

```cinza
class No {
  i32 valor = 0;
  No prox = new No();
}
fn main() { }
// expect-error: 'No' cria a si mesmo nos valores iniciais dos campos (No → No)
```

**`self`** é o objeto atual, disponível em métodos, no construtor e nos valores iniciais dos
campos. Pode ser lido, passado e retornado, mas não reatribuído.

```cinza
class Contador {
  i64 total = 0;
  i64 passo = 1;
  pub {
    Contador(i64 p) { passo = p; }
    fn avanca() -> Contador {
      total += passo;
      return self;
    }
    fn valor() -> i64 { return total; }
  }
}
fn main() {
  Contador c = new Contador(5);
  print(c.avanca().avanca().valor());
}
// expect: 10
```

```cinza
class A { i64 x = 1; }
fn main() {
  A a = new A();
  print(a.x);
}
// expect-error: é privado na classe 'A'
```

## 4.7 `struct`

```
struct Ponto { i64 x = 0; i64 y = 0; }
```

Um struct só tem campos, todos públicos — não tem métodos, construtor nem `pub`.

Um campo pode ter **valor inicial** (`i64 x = 0;`) ou não. `list` e `dict` sem valor inicial
nascem vazios, e um opcional (`i32? idade;`), `null`. Qualquer outro campo sem valor inicial é
**obrigatório**: quem cria o struct precisa informá-lo. Assim nenhum struct existe com um campo
sem valor.

**Criação**, de três formas:

- `new Ponto()` — todos os campos com o valor inicial. Só vale se o struct não tiver campo
  obrigatório.
- `new Ponto(3, 4)` — **todos** os campos, na ordem da declaração. Um número parcial de
  argumentos é erro.
- `new Ponto(y: 4)` — **pelo nome** (`campo: valor`), em qualquer ordem. Os campos omitidos
  recebem o valor inicial; os obrigatórios não podem ser omitidos. Um nome que não é campo, um
  campo repetido ou a mistura com argumentos sem nome são erro.

Na criação pelo nome, os valores informados são avaliados na ordem em que estão escritos, e
depois os valores iniciais dos campos omitidos, na ordem da declaração.

Um campo cujo tipo é um struct com campos obrigatórios também não tem valor padrão: ele mesmo é
obrigatório, a menos que receba um valor inicial completo (`Contato c = new Contato(nome: "");`).

Um struct é um **valor**: cada atribuição, argumento e retorno copia os campos (seção 5.1). Por
isso um struct não pode conter a si mesmo por valor, direta ou indiretamente (por um campo ou um
`pair`); dentro de uma `list`, pode.

Atribuir a um campo de um struct temporário — o resultado de uma chamada ou de `new` — é erro,
porque a alteração iria para uma cópia que ninguém vê.

```cinza
struct Ponto { i64 x = 0; i64 y = 0; }
fn main() {
  Ponto a = new Ponto(1, 2);
  Ponto b = a;
  b.x = 10;
  print(a, b);
}
// expect: Ponto{x: 1, y: 2} Ponto{x: 10, y: 2}
```

```cinza
struct Contato {
  string nome;            // obrigatório
  string telefone = "";
  i32 prioridade = 1;
}
fn main() {
  Contato a = new Contato(nome: "ana");
  Contato b = new Contato(prioridade: 3, nome: "bia");
  Contato c = new Contato("caio", "1199", 2);
  print(a, b, c.telefone);
}
// expect: Contato{nome: "ana", telefone: "", prioridade: 1} Contato{nome: "bia", telefone: "", prioridade: 3} 1199
```

```cinza
struct Contato { string nome; string telefone = ""; }
fn main() {
  Contato c = new Contato();
}
// expect-error: o campo 'nome' não tem valor inicial
```

```cinza
struct Contato { string nome; string telefone = ""; }
fn main() {
  Contato c = new Contato(telefone: "1199");
}
// expect-error: falta o campo obrigatório 'nome'
```

## 4.8 `enum`

```
enum Cor { Vermelho, Verde, Azul }
```

Um enum declara um tipo com um conjunto fixo de valores. Pelo menos um valor; sem repetir nomes.

- Um valor se escreve sempre **qualificado**: `Cor.Verde` (de um módulo: `cr.Cor.Verde`).
- Os valores são **só nomes**: não há número por trás. `Cor c = 1;` e `Cor.Verde + 1` são erros.
- Só `==` e `!=` se aplicam; valores de enums diferentes não se comparam.
- Um enum pode ser chave de `dict`; no `print`, um valor aparece como `Cor.Verde`.

```cinza
enum Cor { Vermelho, Verde, Azul }
fn main() {
  Cor c = Cor.Verde;
  dict<Cor, i64> contagem = {{Cor.Verde, 2}, {Cor.Azul, 1}};
  print(c, c == Cor.Verde, contagem[Cor.Verde], type(c));
}
// expect: Cor.Verde true 2 Cor
```

## 4.9 `interface`

```
interface Forma {
  fn area() -> f64;
  fn nome() -> string;
}
```

Uma interface só tem assinaturas de métodos, terminadas em `;` — sem corpo e sem campos. Precisa
de pelo menos um método, sem repetir nomes. Uma interface não pode ser instanciada.

Uma classe declara as interfaces que cumpre depois de `:` (várias, separadas por vírgula); só
interfaces podem aparecer ali — a Cinza não tem herança. As regras de uso estão na seção 3.7.

```cinza
class Base { }
class Filha : Base { }
fn main() { }
// expect-error: 'Base' não é uma interface
```

## 4.10 Tipos de erro

```
error SaldoInsuficiente;
```

Declara um novo tipo de erro, filho de `Error`. Um erro é criado com `Tipo("mensagem")` (ou
`Tipo()`, mensagem vazia) e lançado com `throw` (seção 5.6). Os tipos embutidos estão no capítulo
7.

## 4.11 Módulos

### Importar

```
import util.texto;          // apelido: texto
import util.texto as tx;    // apelido: tx
```

`import a.b.c;` procura o arquivo `a/b/c.cinza` **a partir da raiz do projeto** — ou, se `a` for o
nome de um pacote do `czm.mod`, o arquivo `b/c.cinza` a partir da raiz do pacote (veja Pacotes,
abaixo). O caminho é o mesmo em qualquer arquivo do projeto: não depende da pasta de quem importa,
e não há import relativo nem `..`. Assim um módulo tem um nome só. Cada arquivo é carregado uma só
vez, mesmo que vários módulos o importem.

A **raiz do projeto** é a pasta do arquivo `czm.mod` mais próximo: o compilador procura esse
arquivo na pasta do arquivo principal e, se não achar, em cada pasta acima dela. **Sem `czm.mod`
não há projeto**: o programa é de um arquivo só e importa apenas os módulos nativos (um `import`
de arquivo é `ImportError`). `cinza init [pasta]` cria o `czm.mod`, com o nome do projeto (o da
pasta) e a versão da linguagem.

```
projeto/
  czm.mod
  main.cinza                 import servicos.cadastro;
  modelos/usuario.cinza
  servicos/cadastro.cinza    import modelos.usuario;
  servicos/teste.cinza       import modelos.usuario;   (rodado direto: a raiz continua projeto/)
```

- Os `import` vêm antes de qualquer declaração; um `import` depois é erro.
- Dois imports com o mesmo apelido são erro (use `as`).
- Importação circular (`a` importa `b`, que importa `a`) é erro, com a cadeia completa.
- Um arquivo que não existe é `ImportError`.
- Uma variável ou parâmetro com o nome de um apelido é erro.

### Declarar o nome

Todo arquivo importado começa com **`module`** e o seu nome — o caminho dele a partir da raiz do
projeto a que pertence (num pacote, a raiz do pacote), com `.` no lugar das pastas:

```
// servicos/cadastro.cinza
module servicos.cadastro;
import modelos.usuario;
```

Assim quem abre o arquivo sabe como importá-lo, e quem lê um `import` acha o arquivo. O `module`
vem antes de tudo (antes dos `import`). Um arquivo importado sem ele, ou com outro nome, é
`ImportError` — a mensagem diz o nome certo. O arquivo principal não precisa declarar; se declarar,
o nome também é conferido contra o caminho dele.

### Usar

Os nomes de um módulo são usados sempre pelo apelido: `tx.soma(1, 2)`, `tx.LIMITE`,
`new tx.Pessoa()`, `tx.Pessoa p`, `except (tx.Falha e)`. Sem o apelido, o nome não é visível —
por isso o arquivo principal pode ter uma função com o mesmo nome de uma do módulo, sem conflito.

### Exportar

Só o que é declarado com `pub` no nível superior é visível para quem importa:

```
pub fn soma(i64 a, i64 b) -> i64 { return a + b; }
fn auxiliar() -> i64 { return 0; }       // privado do módulo
```

`pub` vale antes de `fn`, `class`, `struct`, `enum`, `interface`, `error` e `const`. Usar um nome
sem `pub` de outro módulo é erro.

### O `czm.mod`

Uma declaração por linha; `//` no começo da linha ou depois de um espaço começa um comentário:

```
projeto loja                              // o nome do projeto (o de um pacote, para quem o usa)
cinza 0.6                                 // a versão mínima da linguagem
requer github.com/ana/json v1.2.0         // um pacote git: o repositório e uma tag
requer github.com/bia/http v0.3.1 as web  // com outro nome
requer ../utilidades                      // uma dependência local: uma pasta com czm.mod
```

- `projeto nome`: letras, dígitos e `_`. Opcional no projeto principal; um pacote sem nome
  precisa de um `as` em quem o usa.
- `cinza x.y`: uma versão da linguagem acima desta é `ImportError`.
- `requer origem [versão] [as nome]`: a origem é um caminho (começa com `./`, `../`, `/` ou uma
  letra de disco) ou um repositório git; o repositório precisa da versão, uma tag `vX.Y.Z`
  (`github.com/ana/json` vira `https://github.com/ana/json`; uma URL com esquema, ou `git@...`,
  fica como está).
- Outra declaração, uma linha mal formada ou a mesma origem duas vezes é `ImportError`, com a linha.

### Pacotes

Um **pacote** é um projeto usado por outro: uma pasta com `czm.mod`, no disco (dependência local)
ou num repositório git. O primeiro trecho do `import` é o nome do pacote; o resto, o caminho dentro
dele; `import pacote;` sozinho é o módulo `pacote.cinza` da raiz do pacote:

```
import json;                  // json.cinza, na raiz do pacote json
import json.leitor;           // leitor.cinza
import web.rotas as rotas;    // rotas.cinza do pacote 'web' (o 'as' do czm.mod)
```

Dentro de um pacote, os imports partem da raiz **dele**, e os pacotes visíveis são os do
`czm.mod` dele: um pacote é compilado igual em qualquer projeto que o use. O nome de um pacote não
pode ser o de um módulo da biblioteca padrão, nem o de uma pasta ou arquivo da raiz do projeto, e
dois pacotes do mesmo `czm.mod` não podem ter o mesmo nome (`as` resolve) — todos `ImportError`.

**Pacotes git.** O compilador nunca acessa a rede: os comandos fazem isso, pelo `git` instalado.

| Comando | O que faz |
|---|---|
| `cinza add <repositório> [versão] [as nome]` | Põe (ou troca) a linha `requer` no `czm.mod`; sem versão, a maior tag `vX.Y.Z` do repositório. Depois, o mesmo que `get` |
| `cinza add <./caminho> [as nome]` | Uma dependência local |
| `cinza remove <nome ou origem>` | Tira a linha do `czm.mod`. Depois, o mesmo que `get` |
| `cinza get` | Escolhe as versões, baixa as que faltam para o cache e grava o `czm.lock` |

- **A escolha das versões.** Cada pacote git entra com uma versão só: a **maior pedida** por
  qualquer `czm.mod` do projeto (o principal, os pacotes e os pacotes deles). Pedidos de majors
  diferentes (`v1.4.0` e `v2.0.0`) são erro, com a cadeia de quem pediu cada um: majors diferentes
  não são compatíveis. Um pacote git não pode ter dependências locais.
- **O cache.** Cada versão é baixada uma vez, em `~/.cinza/pacotes` (ou na pasta da variável
  `CINZA_PACOTES`), sem o histórico do git e sem converter os fins de linha.
- **O `czm.lock`.** Gerado pelo `get`, ao lado do `czm.mod` (e versionado com ele): uma linha
  `pacote <origem> <versão> <commit> sha256-<hash>` por pacote git; o hash é o do conteúdo
  baixado. Se uma tag já travada no lock passa a apontar para outro conteúdo, o `get` recusa.
- **Na compilação**, cada pacote git vem do `czm.lock` do projeto principal e do cache. É
  `ImportError`, com a sugestão de rodar `cinza get`: o pacote fora do lock, o lock com uma versão
  menor que a pedida ou de outra major, a versão fora do cache, e o conteúdo no cache diferente do
  hash do lock.

### Módulos nativos

`Strings`, `Files`, `Math`, `Random`, `Convert`, `Lists`, `Dicts`, `Env`, `Process`, `Regex` e `Time`
são módulos da biblioteca padrão, importados como qualquer outro (`import Math;`, `import Strings as
st;`) e descritos no capítulo 6. `Json` também, mas escrito na própria Cinza: vem embutido no
compilador e é um módulo comum (com `pub class Value` e `pub enum Kind`), que aparece como
`<biblioteca>/Json.cinza` nas mensagens. Esses nomes são reservados: um arquivo do programa com o
nome de um deles (`Math.cinza`, `Json.cinza`...) é erro, inclusive se estiver no caminho de busca
de um `import Math;`.
