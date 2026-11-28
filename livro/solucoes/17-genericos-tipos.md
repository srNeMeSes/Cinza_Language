# Soluções do capítulo 17

[Voltar ao capítulo](../17-genericos-tipos.md)

## Exercício 1

```cinza
struct Intervalo[T] {
  T inicio;
  T fim;
}

fn contem[T](Intervalo[T] i, T x) -> bool {
  return x >= i.inicio and x <= i.fim;
}

fn main() {
  Intervalo[i32] nota = new Intervalo[i32](0, 10);
  Intervalo[string] letras = new Intervalo[string]("b", "m");
  print(contem(nota, 7), contem(nota, 11), contem(letras, "casa"), contem(letras, "a"));
}
```

```saida
true false true false
```

`T` é deduzido do `Intervalo[i32]` (exato, como dentro de uma coleção) e do `x`: os dois precisam
combinar. Os textos comparam pela ordem alfabética.

## Exercício 2

```cinza
class Fila[T] {
  list<T> itens;
  pub {
    fn entra(T x) {
      itens.add(x);
    }
    fn sai() -> T? {
      if (itens.size() == 0) {
        return null;
      }
      T x = itens[0];
      itens.remove(0);
      return x;
    }
    fn vazia() -> bool {
      return itens.size() == 0;
    }
  }
}

fn main() {
  Fila[string] f = new Fila[string]();
  f.entra("ana");
  f.entra("bia");
  f.entra("caio");
  while (not f.vazia()) {
    print(f.sai() else "?");
  }
}
```

```saida
ana
bia
caio
```

(`itens.remove(0)` desloca todos os elementos; para filas grandes, guardar o índice do primeiro e
avançá-lo é mais rápido.)

## Exercício 3

O corpo de `maior` passa a ser conferido na cópia `Colecao[Tarefa]`, e o `x > m` não vale para um
struct:

```cinza
struct Tarefa {
  string titulo;
}

class Colecao[T] {
  list<T> itens;
  pub {
    fn poe(T x) {
      itens.add(x);
    }
    fn maior() -> T? {
      if (itens.size() == 0) {
        return null;
      }
      T m = itens[0];
      for (T x in itens) {
        if (x > m) {
          m = x;
        }
      }
      return m;
    }
  }
}

fn main() {
  Colecao[Tarefa] tarefas = new Colecao[Tarefa]();
  tarefas.poe(new Tarefa("comprar pão"));
  print(tarefas.maior());
}
```

```erro
prog.cinza:17:15: SemanticError: Comparação '>' não é suportada entre 'Tarefa' e 'Tarefa'
    em Colecao[Tarefa], usada em prog.cinza:27
```

Sem a chamada, o corpo de `maior` nunca é conferido para `Tarefa`: numa classe genérica, cada
método é conferido só quando é usado.

## Exercício 4

```cinza
class Contador[T] {
  dict<T, i32> vezes;
  pub {
    fn registra(T x) {
      if (vezes.has(x)) {
        vezes[x] += 1;
      } else {
        vezes.add({x, 1});
      }
    }
    fn quantas(T x) -> i32 {
      if (vezes.has(x)) {
        return vezes[x];
      }
      return 0;
    }
  }
}

fn main() {
  Contador[string] c = new Contador[string]();
  for (string p in ["sim", "não", "sim", "sim"]) {
    c.registra(p);
  }
  print(c.quantas("sim"), c.quantas("não"), c.quantas("talvez"));
}
```

```saida
3 1 0
```

Um `Contador[f64]` não chega a existir: o campo seria um `dict<f64, i32>`, e `f64` não é chave. Os
campos são conferidos já na cópia, mesmo sem chamar nenhum método:

```cinza notas.cinza
class Contador[T] {
  dict<T, i32> vezes;
  pub {
    fn quantas(T x) -> i32 {
      return 0;
    }
  }
}

fn main() {
  Contador[f64] c = new Contador[f64]();
}
```

```erro
notas.cinza:2:3: SemanticError: Chave de dict não pode ser 'f64': comparação exata de ponto flutuante não é confiável. Use i32, i64, string, bool ou enum.
    em Contador[f64], usada em notas.cinza:11
```

## Exercício 5

```cinza
interface Transforma[A, B] {
  fn aplica(A x) -> B;
}

class Tamanho : Transforma[string, i32] {
  pub {
    fn aplica(string s) -> i32 {
      return s.size();
    }
  }
}

fn mapeia[A, B](const list<A> l, Transforma[A, B] t) -> list<B> {
  list<B> r;
  for (A x in l) {
    r.add(t.aplica(x));
  }
  return r;
}

fn main() {
  print(mapeia[string, i32](["ana", "beatriz", "caio"], new Tamanho()));
}
```

```saida
[3, 7, 4]
```

Os tipos vão na chamada porque não são deduzidos pela interface que `Tamanho` cumpre:

```cinza tamanhos.cinza
interface Transforma[A, B] {
  fn aplica(A x) -> B;
}

class Tamanho : Transforma[string, i32] {
  pub {
    fn aplica(string s) -> i32 {
      return s.size();
    }
  }
}

fn mapeia[A, B](const list<A> l, Transforma[A, B] t) -> list<B> {
  list<B> r;
  for (A x in l) {
    r.add(t.aplica(x));
  }
  return r;
}

fn main() {
  print(mapeia(["ana"], new Tamanho()));
}
```

```erro
tamanhos.cinza:22:9: SemanticError: Argumento 2 de 'mapeia': 'Tamanho' cumpre 'Transforma[string, i32]', mas os tipos de 'mapeia' não são deduzidos pela interface que uma classe cumpre: informe-os na chamada, mapeia[...](...)
```

É a interface no lugar da função passada como argumento (seção 13.5): `mapeia` aplica qualquer
transformação, e cada classe que cumpre `Transforma` é uma.
