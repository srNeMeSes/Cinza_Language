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
int contador = 0;
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
const int LIMITE = 100;
const decimal TAXA = LIMITE * 0.5;
```

O valor inicial de um `const` global precisa ser uma **expressão constante**: literais, valores
de enum, operadores (aritméticos, de comparação e lógicos), parênteses e outros `const` globais
**já declarados**. Chamadas de função, `new` e literais de `list`, `dict` e `pair` não são
permitidos.

```cinza
const int A = 2;
const int B = A * 3;
fn main() { print(B); }
// expect: 6
```

```cinza
const int B = A * 3;
const int A = 2;
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
- Uma função do programa com o nome de uma embutida (`print`, `range`...) tem precedência sobre
  ela.

```cinza
fn par(int n) -> bool {
  if (n == 0) { return true; }
  return impar(n - 1);
}
fn impar(int n) -> bool {
  if (n == 0) { return false; }
  return par(n - 1);
}
fn main() { print(par(10), impar(7)); }
// expect: true true
```

```cinza
fn f() -> int { return 1; }
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
  int x = 1;
  if (true) {
    int x = 2;
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

**Criação**: `new Nome(argumentos)`. Primeiro os campos recebem seus valores iniciais, na ordem
em que estão declarados (um valor inicial pode usar campos anteriores, `self` e `const`
globais); depois roda o construtor, se houver. Os argumentos de `new` precisam casar com o
construtor (sem construtor, nenhum argumento).

**`self`** é o objeto atual, disponível em métodos, no construtor e nos valores iniciais dos
campos. Pode ser lido, passado e retornado, mas não reatribuído.

```cinza
class Contador {
  int total = 0;
  int passo = 1;
  pub {
    Contador(int p) { passo = p; }
    fn avanca() -> Contador {
      total += passo;
      return self;
    }
    fn valor() -> int { return total; }
  }
}
fn main() {
  Contador c = new Contador(5);
  print(c.avanca().avanca().valor());
}
// expect: 10
```

```cinza
class A { int x = 1; }
fn main() {
  A a = new A();
  print(a.x);
}
// expect-error: é privado na classe 'A'
```

## 4.7 `struct`

```
struct Ponto { int x = 0; int y = 0; }
```

Um struct só tem campos, todos públicos — não tem métodos, construtor nem `pub`. Cada campo
precisa de valor inicial (exceto `list` e `dict`).

**Criação**: `new Ponto()` usa os valores iniciais; `new Ponto(3, 4)` informa **todos** os
campos, na ordem da declaração. Um número parcial de argumentos é erro.

Um struct é um **valor**: cada atribuição, argumento e retorno copia os campos (seção 5.1). Por
isso um struct não pode conter a si mesmo por valor, direta ou indiretamente (por um campo, um
`pair` ou um `op`); dentro de uma `list`, pode.

Atribuir a um campo de um struct temporário — o resultado de uma chamada ou de `new` — é erro,
porque a alteração iria para uma cópia que ninguém vê.

```cinza
struct Ponto { int x = 0; int y = 0; }
fn main() {
  Ponto a = new Ponto(1, 2);
  Ponto b = a;
  b.x = 10;
  print(a, b);
}
// expect: Ponto{x: 1, y: 2} Ponto{x: 10, y: 2}
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
  dict<Cor, int> contagem = {{Cor.Verde, 2}, {Cor.Azul, 1}};
  print(c, c == Cor.Verde, contagem[Cor.Verde], type(c));
}
// expect: Cor.Verde true 2 Cor
```

## 4.9 `interface`

```
interface Forma {
  fn area() -> decimal;
  fn nome() -> string;
}
```

Uma interface só tem assinaturas de métodos, terminadas em `;` — sem corpo e sem campos. Precisa
de pelo menos um método, sem repetir nomes. Uma interface não pode ser instanciada.

Uma classe declara as interfaces que cumpre depois de `:` (várias, separadas por vírgula); só
interfaces podem aparecer ali — a Cinza não tem herança. As regras de uso estão na seção 3.8.

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

`import a.b.c;` procura o arquivo `a/b/c.cinza`, primeiro na pasta do arquivo que importa e
depois em cada pasta da variável de ambiente `CINZA_PATH` (separadas por `;` no Windows e `:` nos
demais sistemas). Cada arquivo é carregado uma só vez, mesmo que vários módulos o importem.

- Os `import` vêm antes de qualquer declaração; um `import` depois é erro.
- Dois imports com o mesmo apelido são erro (use `as`).
- Importação circular (`a` importa `b`, que importa `a`) é erro, com a cadeia completa.
- Um arquivo que não existe é `ImportError`.
- Uma variável ou parâmetro com o nome de um apelido é erro.

### Usar

Os nomes de um módulo são usados sempre pelo apelido: `tx.soma(1, 2)`, `tx.LIMITE`,
`new tx.Pessoa()`, `tx.Pessoa p`, `except (tx.Falha e)`. Sem o apelido, o nome não é visível —
por isso o arquivo principal pode ter uma função com o mesmo nome de uma do módulo, sem conflito.

### Exportar

Só o que é declarado com `pub` no nível superior é visível para quem importa:

```
pub fn soma(int a, int b) -> int { return a + b; }
fn auxiliar() -> int { return 0; }       // privado do módulo
```

`pub` vale antes de `fn`, `class`, `struct`, `enum`, `interface`, `error` e `const`. Usar um nome
sem `pub` de outro módulo é erro.

### Módulos nativos

`Strings`, `Files`, `Math`, `Random`, `Convert`, `Lists` e `Time` são módulos da biblioteca padrão,
importados como qualquer outro (`import Math;`, `import Strings as st;`) e descritos no capítulo
6. Esses nomes são reservados: um arquivo do programa com o nome de um módulo nativo
(`Math.cinza`...) é erro, inclusive se estiver no caminho de busca de um `import Math;`.
