# Soluções do capítulo 10

[Voltar ao capítulo](../10-enums-e-tipos.md)

## Exercício 1

Como o enum não lista os seus valores (seção 10.7), a lista dos sete dias é escrita uma vez:

```cinza
enum Dia { Seg, Ter, Qua, Qui, Sex, Sab, Dom }

fn fim_de_semana(Dia d) -> bool {
  return d == Dia.Sab or d == Dia.Dom;
}

fn main() {
  list<Dia> semana = [Dia.Seg, Dia.Ter, Dia.Qua, Dia.Qui, Dia.Sex, Dia.Sab, Dia.Dom];
  for (Dia d in semana) {
    print(d, fim_de_semana(d));
  }
}
```

```saida
Dia.Seg false
Dia.Ter false
Dia.Qua false
Dia.Qui false
Dia.Sex false
Dia.Sab true
Dia.Dom true
```

## Exercício 2

```cinza
enum Sinal { Verde, Amarelo, Vermelho }

fn proximo(Sinal s) -> Sinal {
  if (s == Sinal.Verde) {
    return Sinal.Amarelo;
  } else if (s == Sinal.Amarelo) {
    return Sinal.Vermelho;
  }
  return Sinal.Verde;
}

fn main() {
  Sinal s = Sinal.Verde;
  for (i32 i in range(0, 6)) {
    s = proximo(s);
    print(s);
  }
}
```

```saida
Sinal.Amarelo
Sinal.Vermelho
Sinal.Verde
Sinal.Amarelo
Sinal.Vermelho
Sinal.Verde
```

## Exercício 3

```cinza
import Dicts;

enum Cor { Vermelho, Verde, Azul }

fn main() {
  list<Cor> cores = [Cor.Azul, Cor.Verde, Cor.Azul, Cor.Vermelho, Cor.Azul];
  dict<Cor, i32> contagem;
  for (Cor c in cores) {
    contagem.add({c, (Dicts.get(contagem, c) else 0) + 1});
  }
  print(contagem);
}
```

```saida
{Cor.Azul: 3, Cor.Verde: 1, Cor.Vermelho: 1}
```

As chaves aparecem na ordem em que cada cor apareceu pela primeira vez na lista.

## Exercício 4

```cinza
enum Cor { Vermelho, Verde, Azul }

fn main() {
  print(type(1 + cast<i64>(1)), type([1.f]), type({"a", 1}), type(Cor.Verde), type(type(1)));
}
```

```saida
i64 list<f32> pair<string, i32> Cor type
```

`i32 + i64` alarga para `i64`; `{"a", 1}` é um par; e o tipo de um tipo é `type`.

## Exercício 5

O dicionário diz o mesmo que a função, numa linha — e uma prioridade nova esquecida nele seria um
`KeyError` na primeira tarefa com ela, em vez de cair calada no último `return` da função:

```cinza
import Lists;

enum Prioridade { Alta, Normal, Baixa }

struct Tarefa {
  string titulo;
  Prioridade prioridade = Prioridade.Normal;
}

fn main() {
  dict<Prioridade, i32> peso = {{Prioridade.Alta, 1}, {Prioridade.Normal, 2}, {Prioridade.Baixa, 3}};
  list<Tarefa> tarefas = [
    new Tarefa(titulo: "b", prioridade: Prioridade.Baixa),
    new Tarefa(titulo: "a", prioridade: Prioridade.Alta),
    new Tarefa(titulo: "n")
  ];
  list<i32> chaves;
  for (Tarefa t in tarefas) {
    chaves.add(peso[t.prioridade]);
  }
  Lists.sort_by(tarefas, chaves);
  for (Tarefa t in tarefas) {
    print(t.titulo, t.prioridade);
  }
}
```

```saida
a Prioridade.Alta
n Prioridade.Normal
b Prioridade.Baixa
```
