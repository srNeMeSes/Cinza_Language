# Soluções do capítulo 18

[Voltar ao capítulo](../18-modulos.md)

## Exercício 1

A função nova vai no fim de `util/texto.cinza`, com `pub`:

<!-- projeto: loja_centraliza -->
```cinza util/texto.cinza
// util/texto.cinza — ajudas para textos
module util.texto;

import Strings;

pub fn alinha(string s, i32 largura) -> string {
  return Strings.pad_right(s, largura);
}

pub fn centraliza(string s, i32 largura) -> string {
  i32 sobra = largura - s.size();
  if (sobra <= 0) {
    return s;
  }
  i32 esquerda = sobra / 2;
  return Strings.repeat(" ", esquerda) + s + Strings.repeat(" ", sobra - esquerda);
}
```

<!-- projeto: loja_centraliza -->
```cinza main.cinza
import precos;
import util.texto;

fn main() {
  print("[" + texto.centraliza("loja", 16) + "]");
  print(precos.etiqueta("café", precos.com_desconto(7.5ff)));
  print(precos.etiqueta("pão", 12.ff));
}
```

```saida
[      loja      ]
café    R$ 6.75
pão     R$ 12.00
```

Nenhum `import` muda: a `main` já importava `util.texto`, e o import traz o módulo inteiro — tudo
o que é `pub` nele fica disponível pelo apelido.

## Exercício 2

Muda o nome do arquivo e tudo o que se refere ao caminho dele:

- a primeira linha do arquivo movido: `module apoio.texto;`;
- o `import` em quem o usa — `main.cinza` e `precos.cinza`: `import apoio.texto;`.

O apelido continua `texto` (o último trecho), então `texto.alinha(...)` não muda. Cada esquecimento
tem a sua mensagem, as da seção 18.3 e da seção 18.4: o `import util.texto;` que sobrou dá "módulo
'util.texto' não encontrado (procurado em: util/texto.cinza)"; o `module util.texto;` que sobrou no
arquivo movido dá "declara 'module util.texto;', mas o nome dele é 'apoio.texto'". É para isso que o
`module` repete o caminho: o arquivo movido não passa despercebido.

## Exercício 3

Porque a coleção não precisa saber o que guarda: `Colecao[T]` e `Filtro[T]` servem a qualquer `T`
(capítulo 17), e quem escolhe o `T` é quem cria a coleção. Se `colecao.cinza` importasse o modelo,

- ela deixaria de ser reaproveitável: outro programa que quisesse uma coleção de outra coisa
  levaria junto as tarefas;
- e as dependências deixariam de ir numa direção só: o modelo é a camada de baixo, e uma mudança
  nele passaria a obrigar a conferir a coleção também.

## Exercício 4

<!-- projeto: estatistica -->
```cinza estatistica.cinza
// estatistica.cinza — contas sobre listas de números
module estatistica;

pub fn media(const list<f64> l) -> f64 {
  return soma(l) / cast<f64>(l.size());
}

fn soma(const list<f64> l) -> f64 {
  f64 s = 0.ff;
  for (f64 x in l) {
    s += x;
  }
  return s;
}
```

<!-- projeto: estatistica -->
```cinza main.cinza
import estatistica;

fn main() {
  print(estatistica.media([7.ff, 8.5ff, 10.ff]));
}
```

```saida
8.5
```

A `soma` é do módulo — a `media` a usa, a `main` não:

<!-- projeto: estatistica -->
<!-- variante -->
```cinza main.cinza
import estatistica;

fn main() {
  print(estatistica.soma([7.ff, 8.5ff, 10.ff]));
}
```

```erro
main.cinza:4:9: SemanticError: 'soma' é privado no módulo 'estatistica': declare-o com 'pub' para exportá-lo.
```

## Exercício 5

O que os dois dividem vai para um terceiro módulo, que os dois importam; `a` e `b` deixam de
depender um do outro:

<!-- projeto: limites -->
```cinza config.cinza
// config.cinza — o que a e b dividem
module config;

pub const i32 LIMITE = 3;
```

<!-- projeto: limites -->
```cinza a.cinza
// a.cinza
module a;

import config;

pub fn corta(list<i32> l) -> list<i32> {
  list<i32> r;
  for (i32 x in l) {
    if (r.size() < config.LIMITE) {
      r.add(x);
    }
  }
  return r;
}
```

<!-- projeto: limites -->
```cinza b.cinza
// b.cinza
module b;

import config;

pub fn cabe(i32 n) -> bool {
  return n <= config.LIMITE;
}
```

<!-- projeto: limites -->
```cinza main.cinza
import a;
import b;

fn main() {
  print(a.corta([5, 6, 7, 8]), b.cabe(2), b.cabe(4));
}
```

```saida
[5, 6, 7] true false
```

`config` não importa ninguém: está na camada de baixo. É o mesmo desenho do programa do capítulo,
em que o modelo não importa a coleção nem os filtros.
