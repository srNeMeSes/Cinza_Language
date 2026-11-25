# 1. Começando

**Objetivos.** Ao fim deste capítulo você sabe:

- instalar a Cinza e conferir que os dois comandos funcionam;
- escrever, rodar e compilar um programa;
- ler uma mensagem de erro do compilador e um erro de execução;
- receber argumentos da linha de comando.

**Pré-requisitos.** Nenhum: é o ponto de partida.

---

A Cinza é uma linguagem de tipagem estática com dois jeitos de executar o mesmo programa: a
**CVM**, uma máquina virtual que roda o arquivo na hora (`cinza`), e um **compilador nativo**
baseado no LLVM, que gera um executável (`cinzac`). Os dois aceitam exatamente a mesma
linguagem, produzem a mesma saída e relatam os mesmos erros, com as mesmas mensagens. Você pode
desenvolver com um e entregar com o outro.

A linguagem é *disciplinada*: o que pode ser conferido antes de executar é conferido na
compilação — os tipos, o `null`, os caminhos que esquecem de retornar, o código que nunca roda.
O que sobra para a execução (um índice fora da lista, uma divisão por zero, um estouro) é erro
da linguagem, com tipo e mensagem, e não comportamento indefinido.

## 1.1 Instalar

### Pelos pacotes

Cada versão da Cinza é publicada com um pacote por sistema, na página de releases do repositório
([github.com/srNeMeSes/Cinza_Language](https://github.com/srNeMeSes/Cinza_Language)):

| Sistema | Pacote |
|---------|--------|
| Windows (x64) | `cinza-<versão>-windows-x64.zip` |
| Linux (x86-64, qualquer distribuição) | `cinza-<versão>-linux-x64.tar.gz` |

Descompacte em qualquer pasta e ponha a pasta `bin/` dele no `PATH`. **Não é preciso instalar
mais nada**: o `cinzac` leva consigo o LLVM, o ligador e as bibliotecas que os programas usam.
No Linux, tanto as ferramentas quanto os programas que o `cinzac` gera são executáveis estáticos
— rodam em qualquer distribuição, sem dependências.

O pacote traz também a especificação da linguagem (`spec/`) e, na página da release, a extensão
do VS Code (o arquivo `.vsix`, seção 1.7).

### Do código-fonte

Para compilar a Cinza você mesmo, precisa de um compilador C++20 com suporte a `#embed` (GCC 15
ou Clang 19 em diante) e, para o `cinzac`, do LLVM 20 (no Windows, o clang, o lld e o LLVM do
MSYS2 CLANG64; no Linux, o pacote `llvm-20-dev`):

```console
$ git clone https://github.com/srNeMeSes/Cinza_Language.git
$ cd Cinza_Language
$ make            # o cinza (a CVM), na raiz
$ make cinzac     # o compilador nativo, em llvm/bin/
$ make test       # a suíte de testes, pela CVM
```

### Conferir

```console
$ cinza --version
cinza 0.6.0 (a CVM, a máquina virtual da Cinza)
$ cinzac --version
cinzac 0.6.0 (LLVM 20.1.8, alvo x86_64-w64-windows-gnu)
```

## 1.2 O primeiro programa

Crie um arquivo `ola.cinza`:

```cinza
fn main() {
  print("Olá, mundo!");
}
// expect: Olá, mundo!
```

Todo programa começa pela função `main`. `fn` declara uma função; o corpo vai entre chaves e
cada instrução termina em `;`. `print` escreve os valores na saída e pula a linha.

> **Convenção deste livro.** As linhas `// expect:` no fim de um exemplo mostram o que ele
> escreve — uma linha de `// expect:` por linha da saída. São comentários comuns para a Cinza; a
> suíte de testes do compilador as usa para conferir que cada exemplo do livro faz exatamente o
> que o texto diz. Do mesmo jeito, `// expect-error:` mostra (um trecho de) a mensagem de erro, e
> `// args:` os argumentos passados ao programa.

`print` aceita qualquer número de valores, de qualquer tipo, e os separa com um espaço. Sem
argumentos, só pula a linha:

```cinza
fn main() {
  print("A soma de", 2, "e", 3, "é", 2 + 3);
  print();
  print("uma lista:", [1, 2, 3], "e um booleano:", true);
}
// expect: A soma de 2 e 3 é 5
// expect:
// expect: uma lista: [1, 2, 3] e um booleano: true
```

Comentários começam com `//` e vão até o fim da linha. A Cinza não tem comentário de bloco
(`/* ... */`).

## 1.3 Rodar na hora ou compilar

Há dois comandos, e você escolhe conforme o momento:

```console
$ cinza ola.cinza
Olá, mundo!

$ cinzac ola.cinza -o ola
$ ./ola
Olá, mundo!
```

- **`cinza arquivo.cinza`** compila para o bytecode da CVM e executa em seguida, sem deixar
  arquivo nenhum. É o comando do dia a dia: o ciclo editar-rodar é imediato.
- **`cinzac arquivo.cinza -o nome`** gera um executável nativo (no Windows, `nome.exe`; sem `-o`,
  o nome do arquivo). Com **`-O2`** o código sai otimizado — a compilação demora mais e o programa
  roda mais rápido. O executável não precisa da Cinza instalada para rodar.

Os dois começam pela mesma parte do compilador — a leitura, a análise e todas as conferências —,
e só então seguem caminhos diferentes. Por isso um erro de compilação é o mesmo nos dois, e um
programa correto escreve a mesma saída nos dois. O capítulo 24 mostra o caminho de cada um por
dentro e quando um vale mais que o outro.

> **Vindo de outra linguagem.** É como ter o `python arquivo.py` e o `gcc arquivo.c` para a
> mesma linguagem — com a garantia de que os dois concordam, porque a suíte de testes da Cinza
> roda inteira nos dois.

## 1.4 A forma de um programa

Um arquivo da Cinza é uma sequência de **declarações**: funções (`fn`), classes (`class`),
structs (`struct`), enums (`enum`), interfaces (`interface`), tipos de erro (`error`) e
constantes (`const`). Não há instruções soltas fora de uma função — só a `main` roda, e é ela
quem chama o resto:

```cinza
fn main() {
  print(dobro(LIMITE));
}

fn dobro(i32 x) -> i32 {
  return x * 2;
}

const i32 LIMITE = 21;
// expect: 42
```

A ordem das declarações não importa: a `main` usa `dobro` e `LIMITE`, que vêm depois dela. O
`-> i32` diz o tipo do retorno; o capítulo 5 trata das funções em detalhe.

Também **não há variáveis globais mutáveis**: no nível de cima, só `const`. Um valor que muda
mora dentro de uma função, ou de um objeto (capítulo 12):

```cinza
i32 contador = 0;
fn main() { }
// expect-error: Variável global 'contador' precisa ser 'const': não há variáveis globais mutáveis
```

> **Por quê?** Estado global mutável é o que torna um programa difícil de entender em partes:
> qualquer função pode mudá-lo a qualquer momento. A Cinza o tira da linguagem; o que precisa
> ser compartilhado é passado explicitamente.

A `main` pode ter uma destas formas, e nenhuma outra:

```text
fn main() { }
fn main() -> void { }
fn main(list<string> args) { }
fn main(list<string> args) -> void { }
```

## 1.5 Quando algo dá errado

Esqueça o `;` do primeiro programa:

```cinza
fn main() {
  print("Olá, mundo!")
}
// expect-error: 2:23: SyntaxError: Esperado ';' após expressão (token: '}')
```

```console
$ cinza ola.cinza
ola.cinza:2:23: SyntaxError: Esperado ';' após expressão (token: '}')
```

Toda mensagem do compilador tem a mesma forma: **`arquivo:linha:coluna: Tipo: mensagem`**. A
coluna conta caracteres a partir de 1 — aqui, logo depois do `)` onde o `;` faltou —, e o final
`(token: '}')` diz o que o compilador encontrou no lugar. Editores que entendem esse formato
(como o VS Code, seção 1.7) levam direto à posição.

O tipo diz em que fase o problema foi achado:

| Tipo | Fase | O que o compilador relata |
|------|------|---------------------------|
| `LexicalError` | a leitura dos caracteres: um símbolo que não existe, uma string sem fim | todos os do arquivo, juntos |
| `SyntaxError` | a gramática: o `;` que falta, um parêntese aberto | os que encontrar (ele se recupera e segue) |
| `ImportError` | os módulos: um arquivo que não existe | o primeiro |
| `SemanticError` | os tipos e os nomes: uma função que não existe, um tipo errado | o primeiro |

Um erro semântico típico — o nome escrito errado:

```cinza
fn main() {
  prnt("Olá");
}
// expect-error: 2:3: SemanticError: Função 'prnt' não declarada
```

**Com qualquer erro de compilação, nada do programa roda** — nem as linhas antes do erro. O
programa abaixo não imprime `antes`:

```cinza
fn main() {
  print("antes");
  i32 x = "texto";
}
// expect-error: SemanticError: Tipo incompatível na declaração de 'x': declarado 'i32', mas atribuído 'string'
// expect-not: antes
```

O que só se descobre executando é um **erro de execução**. Ele também tem tipo e mensagem, e vem
seguido do *stack trace* — as chamadas em andamento, da mais interna para fora:

```cinza
fn main() {
  list<string> nomes = ["Ana", "Bia"];
  print(nomes[2]);
}
// expect-error: 3:14: IndexError: índice 2 fora dos limites (tamanho: 2)
// expect-error:     em main (
```

```console
$ cinza indice.cinza
indice.cinza:3:14: IndexError: índice 2 fora dos limites (tamanho: 2)
    em main (indice.cinza:3)
```

Os erros de execução podem ser capturados com `try`/`except` (capítulo 14). Um erro que ninguém
captura encerra o programa.

O **código de saída** do programa segue a mesma lógica: `0` quando a `main` termina, `1` em
qualquer erro de compilação ou de execução não capturado, e o código que o próprio programa pedir
com `exit(n)` (capítulo 5). Scripts e ferramentas de integração contínua podem confiar nele.

## 1.6 Argumentos da linha de comando

Tudo o que vem depois do nome do arquivo vai para a `main`, numa `list<string>`:

```cinza
fn main(list<string> args) {
  print("recebi", args.size(), "argumentos:", args);
}
// args: ana 42 --verbose
// expect: recebi 3 argumentos: ["ana", "42", "--verbose"]
```

```console
$ cinza args.cinza ana 42 --verbose
recebi 3 argumentos: ["ana", "42", "--verbose"]
```

O nome do parâmetro é livre. As opções do próprio `cinza` (como `--version`) vêm **antes** do
arquivo; depois dele, tudo é do seu programa, mesmo o que começa com `-`. No executável do
`cinzac`, os argumentos são os do executável: `./args ana 42`.

## 1.7 O editor e as ferramentas

**A extensão do VS Code** (o `.vsix` da release: *Extensões → … → Instalar do VSIX*) traz o
destaque de sintaxe e usa o servidor de linguagem do próprio compilador (`cinza --lsp`): enquanto
você digita, os erros aparecem sublinhados e no painel Problemas — os mesmos da compilação, com a
mesma mensagem. Com o mouse sobre um nome aparece o tipo dele; **F12** vai à declaração; depois de
um `.`, o editor sugere os membros. A extensão procura o `cinza` no `PATH` (ou na configuração
`cinza.caminho`).

**O formatador** deixa o código no estilo da Cinza — a indentação e os espaços —, sem opções para
configurar e sem quebrar nem juntar linhas:

```console
$ cinza fmt prog.cinza          # formata no lugar
$ cinza fmt --check prog.cinza  # só confere (código 1 se precisar formatar)
```

No VS Code, *Formatar Documento* (Shift+Alt+F) usa o mesmo formatador.

## Programa do capítulo: saudações

Um programa que cumprimenta cada nome recebido na linha de comando — e explica o uso quando não
recebe nenhum. Ele adianta um pouco do que vem nos próximos capítulos (o `if`, o `for`, uma função
com retorno), mas deve ser legível para quem já programa:

```cinza
// saudacao.cinza: cinza saudacao.cinza <nome>...

fn saudacao(string nome) -> string {
  return "Olá, " + nome + "!";
}

fn main(list<string> args) {
  if (args.size() == 0) {
    print("Uso: cinza saudacao.cinza <nome>...");
    exit(2);
  }
  for (string nome in args) {
    print(saudacao(nome));
  }
  print("Saudei", args.size(), "pessoas.");
}
// args: Ana Bia Caio
// expect: Olá, Ana!
// expect: Olá, Bia!
// expect: Olá, Caio!
// expect: Saudei 3 pessoas.
```

```console
$ cinza saudacao.cinza Ana Bia Caio
Olá, Ana!
Olá, Bia!
Olá, Caio!
Saudei 3 pessoas.
$ cinza saudacao.cinza
Uso: cinza saudacao.cinza <nome>...
$ echo $?
2
```

Repare em três coisas:

- `saudacao` vem antes de `main` só por gosto: a ordem não importa.
- `"Olá, " + nome + "!"` junta textos com `+` (o capítulo 3 mostra as regras).
- `exit(2)` encerra o programa com o código 2, o costume para "uso errado". Para o compilador,
  depois do `exit` o caminho termina — como depois de um `return`.

## Resumo

- `cinza prog.cinza` roda na hora (a CVM); `cinzac prog.cinza -o prog` gera um executável nativo
  (`-O2` para otimizar). Mesma linguagem, mesma saída, mesmos erros.
- Um programa é uma sequência de declarações, em qualquer ordem; só a `main` roda. No nível de
  cima não há instruções soltas nem variáveis mutáveis, só `const`.
- As mensagens têm a forma `arquivo:linha:coluna: Tipo: mensagem`. Com um erro de compilação nada
  roda; um erro de execução vem com o stack trace.
- O código de saída é `0`, `1` (erro) ou o pedido por `exit(n)`.
- `main(list<string> args)` recebe os argumentos escritos depois do arquivo.

## Exercícios

1. Escreva um programa que imprima, numa linha, o seu nome e o ano atual, e na linha seguinte a
   soma de 1234 com 4321.
2. O programa abaixo tem três erros. Rode-o, leia cada mensagem e corrija um de cada vez (o que
   aparece primeiro? por que os outros só aparecem depois?).

   ```text
   fn main() {
     print("Cinza" $ "!");
     print("versão", 0.6f)
     imprime("fim");
   }
   ```

3. Mude o programa do capítulo para que, sem argumentos, ele cumprimente `mundo` em vez de sair
   com erro.
4. Escreva um programa que imprima quantos argumentos recebeu e o primeiro e o último deles (se
   houver pelo menos um).

As soluções estão em [`solucoes/01-*.cinza`](solucoes/).

## Na spec

- A `main` e a forma de um programa: [4.1 e 4.2](../spec/04-declaracoes.md)
- Os erros de compilação e o formato das mensagens: [7.2 e 7.3](../spec/07-erros.md)
- A linha de comando e o código de saída: [7.4](../spec/07-erros.md)
- `print` e `exit`: [6.1](../spec/06-biblioteca.md)
