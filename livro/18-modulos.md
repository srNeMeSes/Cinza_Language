# 18. Módulos e projetos

**Objetivos.** Ao fim deste capítulo você sabe:

- criar um projeto com `cinza init` e dividir um programa em vários arquivos;
- importar um módulo pelo caminho a partir da raiz do projeto, com ou sem apelido;
- decidir o que um módulo exporta, com `pub`, e prever o que cada arquivo enxerga;
- organizar as pastas de um projeto.

**Pré-requisitos.** Os capítulos 5 (funções) e 12 a 17 (os tipos que um módulo exporta).

---

Até aqui, todo programa coube num arquivo. Um programa de verdade não cabe: as tarefas, a coleção,
os filtros e a `main` são assuntos diferentes, e cada um merece o seu arquivo. Na Cinza, cada
arquivo é um **módulo**; os módulos de um programa formam um **projeto**; e um arquivo usa outro com
`import`.

## 18.1 O projeto

Um projeto é uma pasta com um arquivo `czm.mod`. `cinza init` cria esse arquivo:

```console
$ mkdir loja
$ cinza init loja
projeto criado: loja/czm.mod
```

```text
// czm.mod: esta pasta é a raiz de um projeto Cinza (os imports partem dela)
projeto loja
cinza 0.6
```

O `czm.mod` diz o nome do projeto e a versão mínima da linguagem (o capítulo 19 acrescenta as
dependências). A pasta dele é a **raiz do projeto**: todo `import` parte dela.

Sem `czm.mod`, não há projeto: o programa é de um arquivo só, e só importa os módulos da biblioteca
(`import Strings;`). O primeiro `import` de um arquivo seu explica isso:

```cinza main.cinza
import precos;

fn main() {
  print(precos.DESCONTO);
}
```

```erro
main.cinza:1:1: ImportError: 'import precos': este programa não está num projeto (não há um czm.mod na pasta dele nem acima). Sem projeto, só se importam os módulos nativos; crie o projeto com 'cinza init' na pasta dele
```

## 18.2 O primeiro módulo

O projeto `loja` tem três arquivos:

```text
loja/
  czm.mod
  main.cinza
  precos.cinza
  util/texto.cinza
```

<!-- projeto: loja -->
```cinza precos.cinza
// precos.cinza — as contas de preço da loja
module precos;

import Math;
import util.texto;

pub const f64 DESCONTO = 0.1ff;

pub fn com_desconto(f64 preco) -> f64 {
  return arredonda(preco * (1.ff - DESCONTO));
}

pub fn etiqueta(string nome, f64 preco) -> string {
  return texto.alinha(nome, 8) + format("R$ {preco:.2f}");
}

fn arredonda(f64 x) -> f64 {
  return Math.round_to(x, 2);
}
```

<!-- projeto: loja -->
```cinza util/texto.cinza
// util/texto.cinza — ajudas para textos
module util.texto;

import Strings;

pub fn alinha(string s, i32 largura) -> string {
  return Strings.pad_right(s, largura);
}
```

<!-- projeto: loja -->
```cinza main.cinza
import precos;
import util.texto;

fn main() {
  print(precos.etiqueta("café", precos.com_desconto(7.5ff)));
  print(precos.etiqueta("pão", 12.ff));
  print(texto.alinha("total", 8) + "|", precos.DESCONTO);
}
```

```console
$ cd loja
$ cinza main.cinza
```

```saida
café    R$ 6.75
pão     R$ 12.00
total   | 0.1
```

Cada coisa nesse projeto tem uma regra:

- **`module precos;`** — todo arquivo importado começa dizendo o próprio nome (seção 18.3).
- **`import util.texto;`** — o caminho a partir da raiz, com `.` no lugar das pastas: o arquivo
  `util/texto.cinza`. O apelido é o último trecho, `texto` (seção 18.4).
- **`pub`** — só o que é `pub` sai do módulo: a `main` usa `precos.com_desconto`, mas não
  `precos.arredonda` (seção 18.5).
- **`precos.DESCONTO`** — os nomes de um módulo se usam sempre pelo apelido.

## 18.3 `module`: o nome no começo do arquivo

Todo arquivo importado começa com `module` e o caminho dele a partir da raiz, o mesmo do `import`
que o traz. Sem a linha, o `import` explica:

<!-- projeto: loja -->
<!-- variante -->
<!-- roda: main.cinza -->
```cinza util/texto.cinza
// util/texto.cinza — ajudas para textos
import Strings;

pub fn alinha(string s, i32 largura) -> string {
  return Strings.pad_right(s, largura);
}
```

```erro
precos.cinza:5:1: ImportError: o arquivo 'util/texto.cinza' é importado como 'util.texto', mas não declara o nome: comece-o com 'module util.texto;'
```

(O erro aponta o `import` de `precos.cinza`, o primeiro a pedir o módulo.) Um nome diferente do
caminho também é erro, e a mensagem diz o certo:

<!-- projeto: loja -->
<!-- variante -->
<!-- roda: main.cinza -->
```cinza util/texto.cinza
// util/texto.cinza — ajudas para textos
module texto;

import Strings;

pub fn alinha(string s, i32 largura) -> string {
  return Strings.pad_right(s, largura);
}
```

```erro
util/texto.cinza:2:1: ImportError: o arquivo 'util/texto.cinza' declara 'module texto;', mas o nome dele é 'util.texto' (o caminho a partir da raiz do projeto dele): troque para 'module util.texto;'
```

> **Por quê?** O `module` repete o que o caminho já diz — de propósito. Quem abre um arquivo sabe,
> pela primeira linha, como importá-lo; quem lê um `import` sabe qual arquivo abrir; e um arquivo
> movido de pasta sem ajustar o nome é apontado na hora, em vez de mudar em silêncio o que os
> outros importam. O arquivo principal (o que tem a `main`) não precisa da linha.

## 18.4 `import`: o caminho a partir da raiz

O caminho de um `import` parte **sempre da raiz do projeto**, não da pasta de quem importa:
`precos.cinza` e `main.cinza` escrevem `import util.texto;` igual, e escreveriam igual se estivessem
em outras pastas. Não há import relativo (`./`, `../`), nem o caminho entre aspas:

<!-- projeto: loja -->
<!-- variante -->
```cinza main.cinza
import precos;
import ./util/texto;

fn main() {
  print(texto.alinha("total", 8) + "|");
}
```

```erro
main.cinza:2:8: SyntaxError: O import parte da raiz do projeto, com '.' entre as pastas e sem a extensão: import util.texto; (não há import relativo, com ./ ou ../, nem o caminho entre aspas) (token: '.')
```

Um caminho que não existe diz onde procurou:

<!-- projeto: loja -->
<!-- variante -->
```cinza main.cinza
import precos;
import utils.texto;

fn main() {
  print(texto.alinha("total", 8) + "|");
}
```

```erro
main.cinza:2:1: ImportError: módulo 'utils.texto' não encontrado (procurado em: utils/texto.cinza). O caminho do import parte da raiz do projeto (a pasta do czm.mod), não da pasta de quem importa, e o primeiro trecho não é um pacote do czm.mod
```

A raiz é a pasta do `czm.mod` mais próximo, procurado na pasta do arquivo que você roda e nas de
cima. Por isso um arquivo de uma subpasta, rodado direto (`cinza util/teste.cinza`), continua com a
raiz do projeto: os imports dele são os mesmos.

**O apelido.** `import util.texto;` dá ao módulo o apelido `texto`; `as` escolhe outro, para
encurtar ou para evitar dois apelidos iguais (`import modelo.tarefa as t;`). Dois imports com o
mesmo apelido são erro, e um nome de variável igual a um apelido também.

> **Por quê?** Com o caminho sempre a partir da raiz, cada módulo tem **um** nome: `util.texto` é
> o mesmo arquivo em qualquer lugar do projeto. Com imports relativos, o mesmo arquivo seria
> `./texto` num lugar e `../util/texto` em outro, e mover um arquivo quebraria os imports *dele*.

**Uma vez só, e sem círculos.** Um módulo importado por vários arquivos é carregado uma vez. Um
módulo que importa outro que o importa de volta é erro, com a cadeia:

<!-- projeto: loja -->
<!-- variante -->
<!-- roda: main.cinza -->
```cinza util/texto.cinza
// util/texto.cinza — ajudas para textos
module util.texto;

import Strings;
import precos;

pub fn alinha(string s, i32 largura) -> string {
  return Strings.pad_right(s, largura);
}
```

```erro
util/texto.cinza:5:1: ImportError: import circular: precos.cinza → util/texto.cinza → precos.cinza
```

O círculo costuma dizer que duas coisas estão no lugar errado: o que os dois módulos usam um do
outro vai para um terceiro, que os dois importam.

## 18.5 `pub`: o que sai do módulo

No nível de cima de um módulo, só o que é declarado com `pub` é visível para quem importa:
`pub fn`, `pub const`, `pub struct`, `pub class`, `pub enum`, `pub interface`, `pub error`. O resto
é do módulo:

<!-- projeto: loja -->
<!-- variante -->
```cinza main.cinza
import precos;

fn main() {
  print(precos.arredonda(7.555ff));
}
```

```erro
main.cinza:4:9: SemanticError: 'arredonda' é privado no módulo 'precos': declare-o com 'pub' para exportá-lo.
```

O `pub` do módulo é o mesmo `pub` da classe (capítulo 12), em outra escala: a classe esconde os
campos e expõe os métodos; o módulo esconde as funções de apoio e expõe o que os outros usam. Os
campos de um struct `pub` são todos visíveis (um struct é só dados, capítulo 9); os de uma classe
`pub` continuam privados.

> **Por quê?** Privado por padrão inverte o custo do esquecimento: esquecer um `pub` dá um erro
> claro na primeira vez que alguém tenta usar o nome; esquecer de esconder (como em Python, em que
> tudo é visível) faz o código de fora depender de detalhes que você queria poder mudar.

## 18.6 O que um módulo enxerga

- Os nomes de outro módulo, **sempre pelo apelido**: `precos.com_desconto(...)`,
  `precos.DESCONTO`, `new modelo.Tarefa(...)`, `t.Tarefa x`, `except (t.TituloInvalido e)`. Sem o
  apelido, o nome não existe — e o compilador diz qual módulo o tem:

<!-- projeto: loja -->
<!-- variante -->
```cinza main.cinza
import precos;

fn main() {
  print(com_desconto(7.5ff));
}
```

```erro
main.cinza:4:9: SemanticError: Função 'com_desconto' não declarada: o módulo 'precos' tem uma; os nomes de um módulo se usam pelo apelido: precos.com_desconto(...)
```

- Por isso não há conflito de nomes: a `main` pode ter a sua própria `arredonda`, e `precos` pode
  ter a dele.
- Os nomes do corpo de um módulo são resolvidos **no módulo**: quando `precos.com_desconto` chama
  `arredonda`, é a de `precos`, qualquer que seja quem chamou.
- Não há estado global para dividir: no nível de cima só há constantes (capítulo 15), e o que muda
  passa por parâmetros e objetos. Um módulo não tem como guardar um contador que outro altere.
- Os módulos da biblioteca (`Strings`, `Math`, `Lists`...) são importados do mesmo jeito, e os
  nomes deles são reservados: um arquivo `Math.cinza` no projeto é erro.

## 18.7 Como organizar as pastas

O compilador não impõe nada além do caminho igual ao nome. Algumas regras que funcionam:

- **Um módulo por assunto**, não por tipo de declaração: `precos.cinza` tem a constante, as funções
  e os tipos de preço juntos; não um `constantes.cinza` com as constantes de todos os assuntos.
- **Subpastas quando os módulos se agrupam**: `modelo/tarefa.cinza`, `modelo/agenda.cinza`;
  `util/` para o que não é do domínio do programa.
- **A `main` fina**: o arquivo principal monta as peças e trata a entrada e a saída; as regras ficam
  nos módulos, onde outro programa (ou um teste) pode usá-las.
- **As dependências numa direção só**: o modelo não importa a interface com o usuário; os filtros
  importam o modelo, e não o contrário. Um círculo (seção 18.4) é o sinal de que isso se quebrou.

## 18.8 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `from modelo import Tarefa` (Python), `import { Tarefa } from "./modelo"` (JS) | `import modelo;` e `modelo.Tarefa` |
| `import ./x`, `require("../x")`, `#include "x.h"` | `import caminho.a.partir.da.raiz;` |
| `import java.util.*` | não há import de tudo: o apelido e os nomes por ele |
| `export`, `public` | `pub` |
| `package x` (Java, Go), `mod x` (Rust) | `module x;` no começo do arquivo |
| `go.mod`, `Cargo.toml`, `package.json` | `czm.mod` |
| `__init__.py`, `index.js` | não há: cada arquivo é um módulo |
| `using System.Text` (C#) | `import Strings;` |

O compilador reconhece as formas mais comuns:

```cinza main.cinza
from precos import com_desconto;

fn main() {
  print(precos.com_desconto(10.ff));
}
```

```erro
main.cinza:1:1: SyntaxError: A Cinza escreve 'import precos;', e usa os nomes do módulo pelo apelido: precos.Nome (token: 'from')
```

```cinza precos.cinza
export fn com_desconto(f64 preco) -> f64 {
  return preco * 0.9ff;
}

fn main() {}
```

```erro
precos.cinza:1:1: SyntaxError: A Cinza escreve 'pub', não 'export': pub fn ... (token: 'export')
```

## Programa do capítulo: o `tarefas` em módulos

A versão 10 do `tarefas` divide a versão 9 (capítulo 17) em arquivos: o modelo (a tarefa, a
prioridade e o erro do título), a coleção genérica com o filtro, os filtros de tarefas e a `main`.

```text
tarefas/
  czm.mod
  main.cinza
  colecao.cinza
  filtros.cinza
  modelo/tarefa.cinza
```

<!-- projeto: tarefas -->
```cinza modelo/tarefa.cinza
// modelo/tarefa.cinza — o que é uma tarefa
module modelo.tarefa;

pub enum Prioridade { Baixa, Media, Alta }

pub struct Tarefa {
  string titulo;
  Prioridade prioridade = Prioridade.Media;
  bool feita = false;
}

pub error TituloInvalido;

pub fn nova(string titulo, Prioridade p) -> Tarefa {
  if (titulo == "") {
    throw TituloInvalido("o título não pode ser vazio");
  }
  return new Tarefa(titulo: titulo, prioridade: p);
}
```

<!-- projeto: tarefas -->
```cinza colecao.cinza
// colecao.cinza — uma coleção de qualquer coisa, com filtros
module colecao;

pub interface Filtro[T] {
  fn aceita(T x) -> bool;
}

pub class Colecao[T] {
  list<T> itens;

  pub {
    fn poe(T x) {
      itens.add(x);
    }

    fn filtra(Filtro[T] f) -> list<T> {
      list<T> r;
      for (T x in itens) {
        if (f.aceita(x)) {
          r.add(x);
        }
      }
      return r;
    }

    fn tamanho() -> i32 {
      return itens.size();
    }
  }
}
```

<!-- projeto: tarefas -->
```cinza filtros.cinza
// filtros.cinza — os filtros de tarefas
module filtros;

import colecao;
import modelo.tarefa;

pub class Pendente : colecao.Filtro[tarefa.Tarefa] {
  pub {
    fn aceita(tarefa.Tarefa t) -> bool {
      return not t.feita;
    }
  }
}

pub class Urgente : colecao.Filtro[tarefa.Tarefa] {
  pub {
    fn aceita(tarefa.Tarefa t) -> bool {
      return t.prioridade == tarefa.Prioridade.Alta;
    }
  }
}
```

<!-- projeto: tarefas -->
```cinza main.cinza
// main.cinza — tarefas, versão 10: o programa dividido em módulos
import colecao;
import filtros;
import modelo.tarefa as t;

fn main() {
  colecao.Colecao[t.Tarefa] tarefas = new colecao.Colecao[t.Tarefa]();
  tarefas.poe(t.nova("comprar pão", t.Prioridade.Alta));
  tarefas.poe(t.nova("estudar a Cinza", t.Prioridade.Media));
  try {
    tarefas.poe(t.nova("", t.Prioridade.Baixa));
  } except (t.TituloInvalido e) {
    print("recusada:", e.message);
  }
  for (t.Tarefa x in tarefas.filtra(new filtros.Urgente())) {
    print("urgente:", x.titulo);
  }
  print(tarefas.filtra(new filtros.Pendente()).size(), "de", tarefas.tamanho(), "pendentes");
}
```

```saida
recusada: o título não pode ser vazio
urgente: comprar pão
2 de 2 pendentes
```

Repare em:

- **As dependências numa direção**: `modelo/tarefa` não importa ninguém; `colecao` também não (ela
  serve a qualquer tipo); `filtros` importa os dois; a `main` importa os três.
- **A coleção não sabe de tarefas**: `colecao.cinza` não importa o modelo. É a mesma `Colecao[T]`
  do capítulo 17, agora reaproveitável por qualquer programa.
- **Os tipos atravessam os módulos pelo apelido**: `colecao.Filtro[tarefa.Tarefa]`,
  `colecao.Colecao[t.Tarefa]`, `except (t.TituloInvalido e)`. O genérico de um módulo recebe o tipo
  de outro.
- **O mesmo módulo, dois apelidos**: `filtros.cinza` chama o modelo de `tarefa`; a `main`, de `t`.
  O apelido é de quem importa.
- **`nova` concentra a regra**: quem quer uma tarefa a cria por `t.nova`, que recusa o título vazio
  com o erro do próprio módulo.

## Resumo

- Um projeto é uma pasta com `czm.mod` (`cinza init`); sem ele, só se importam os módulos da
  biblioteca.
- Cada arquivo é um módulo; o importado começa com `module caminho;`, igual ao caminho dele a partir
  da raiz.
- `import a.b;` parte sempre da raiz do projeto; o apelido é o último trecho, ou o do `as`. Não há
  import relativo, nem de tudo, nem circular.
- Só o que é `pub` sai do módulo, e os nomes de outro módulo se usam sempre pelo apelido.
- Um módulo por assunto, a `main` fina, as dependências numa direção só.

## Exercícios

1. No projeto `loja`, acrescente a `util/texto.cinza` uma função `pub fn centraliza(string s, i32
   largura) -> string` e use-a na `main`. Precisa mudar algum `import`?
2. Mova `util/texto.cinza` para `apoio/texto.cinza`. O que precisa mudar, e em quais arquivos? O
   que o compilador diz de cada coisa que você esquecer?
3. Por que `colecao.cinza`, no programa do capítulo, não importa `modelo/tarefa.cinza`? O que se
   perderia se importasse?
4. Escreva um módulo `estatistica.cinza` com `pub fn media(const list<f64> l) -> f64` e uma função
   privada `soma`, e use-o num `main.cinza` do mesmo projeto. Tente chamar `estatistica.soma` da
   `main` e leia a mensagem.
5. Dois módulos, `a` e `b`, precisam de uma constante `LIMITE` e se importam um ao outro para isso.
   Reorganize-os para não haver círculo.

As soluções estão em [`solucoes/18-modulos.md`](solucoes/18-modulos.md).

## Na spec

- Módulos, `import`, `module`, `pub` e o `czm.mod`: [4.11](../spec/04-declaracoes.md)
- Os módulos nativos: [6.2](../spec/06-biblioteca.md)
