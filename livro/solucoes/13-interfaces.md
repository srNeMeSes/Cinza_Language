# Soluções do capítulo 13

[Voltar ao capítulo](../13-interfaces.md)

## Exercício 1

```cinza
interface Animal {
  fn som() -> string;
  fn nome() -> string;
}

class Cachorro : Animal {
  pub {
    fn som() -> string { return "au"; }
    fn nome() -> string { return "cachorro"; }
  }
}

class Gato : Animal {
  pub {
    fn som() -> string { return "miau"; }
    fn nome() -> string { return "gato"; }
  }
}

class Vaca : Animal {
  pub {
    fn som() -> string { return "muu"; }
    fn nome() -> string { return "vaca"; }
  }
}

fn main() {
  list<Animal> bichos = [new Cachorro(), new Gato(), new Vaca()];
  for (Animal a in bichos) {
    printf("{a.nome()} faz {a.som()}");
  }
}
```

```saida
cachorro faz au
gato faz miau
vaca faz muu
```

## Exercício 2

```cinza
import Strings;

interface Regra {
  fn valida(string s) -> bool;
}

class NaoVazia : Regra {
  pub {
    fn valida(string s) -> bool { return s != ""; }
  }
}

class TamanhoMinimo : Regra {
  i32 n;
  pub {
    TamanhoMinimo(i32 n) { self.n = n; }
    fn valida(string s) -> bool { return s.size() >= n; }
  }
}

class ComecaCom : Regra {
  string prefixo;
  pub {
    ComecaCom(string p) { prefixo = p; }
    fn valida(string s) -> bool { return Strings.starts_with(s, prefixo); }
  }
}

fn confere(string s, list<Regra> regras) -> bool {
  for (Regra r in regras) {
    if (not r.valida(s)) {
      return false;
    }
  }
  return true;
}

fn main() {
  list<Regra> regras = [new NaoVazia(), new TamanhoMinimo(8), new ComecaCom("cz-")];
  print(confere("cz-segredo", regras), confere("cz-abc", regras), confere("segredo123", regras), confere("", regras));
}
```

```saida
true false false false
```

`TamanhoMinimo(i32 n)` usa `self.n = n;` para atribuir o campo obrigatório com o parâmetro de mesmo
nome (seção 12.6).

## Exercício 3

```cinza
interface Reducao {
  fn passo(i32 x);
  fn resultado() -> i32;
}

class Soma : Reducao {
  i32 total = 0;
  pub {
    fn passo(i32 x) { total += x; }
    fn resultado() -> i32 { return total; }
  }
}

class Maximo : Reducao {
  i32? maior;
  pub {
    fn passo(i32 x) {
      i32 atual = maior else x;
      if (x >= atual) {
        maior = x;
      }
    }
    fn resultado() -> i32 { return maior else 0; }
  }
}

fn reduz(list<i32> l, Reducao r) -> i32 {
  for (i32 x in l) {
    r.passo(x);
  }
  return r.resultado();
}

fn main() {
  list<i32> l = [4, -2, 9, 3];
  print(reduz(l, new Soma()), reduz(l, new Maximo()));
}
```

```saida
14 9
```

`Maximo` guarda o maior num `i32?`: antes do primeiro elemento não há maior, e um valor inicial
como `0` estaria errado para uma lista só de negativos.

## Exercício 4

`f` é uma `Forma`, e um valor de interface nunca volta a ser a classe (seção 13.3): `Quadrado q = f`
não compila, nem com `cast`. Primeiro jeito: se "diagonal" faz sentido para toda forma, ela vira um
método da interface, e cada classe responde a seu modo (aqui, o círculo responde 0):

```cinza
interface Forma {
  fn diagonal() -> f64;
}

class Quadrado : Forma {
  f64 lado;
  pub {
    Quadrado(f64 l) { lado = l; }
    fn diagonal() -> f64 { return lado * 1.4142135623730951ff; }
  }
}

class Circulo : Forma {
  pub {
    fn diagonal() -> f64 { return 0.ff; }
  }
}

fn main() {
  list<Forma> formas = [new Quadrado(1.ff), new Circulo(), new Quadrado(2.ff)];
  f64 soma = 0.ff;
  for (Forma f in formas) {
    soma += f.diagonal();
  }
  printf("{soma:.4f}");
}
```

```saida
4.2426
```

Segundo jeito: quem precisa saber que um objeto é um quadrado guarda os quadrados como `Quadrado`,
numa lista própria, além de (ou em vez de) guardá-los como `Forma`:

```cinza
interface Forma {
  fn area() -> f64;
}

class Quadrado : Forma {
  f64 lado;
  pub {
    Quadrado(f64 l) { lado = l; }
    fn area() -> f64 { return lado * lado; }
    fn diagonal() -> f64 { return lado * 1.4142135623730951ff; }
  }
}

fn main() {
  list<Quadrado> quadrados = [new Quadrado(1.ff), new Quadrado(2.ff)];
  list<Forma> formas;
  for (Quadrado q in quadrados) {
    formas.add(q);
  }
  f64 soma = 0.ff;
  for (Quadrado q in quadrados) {
    soma += q.diagonal();
  }
  printf("{soma:.4f} {formas.size()}");
}
```

```saida
4.2426 2
```

## Exercício 5

`Nao` contém o filtro que inverte — o mesmo padrão do `E` do capítulo:

```cinza
import Strings;

struct Tarefa {
  string titulo;
}

interface Filtro {
  fn aceita(Tarefa t) -> bool;
}

class ComTexto : Filtro {
  string trecho;
  pub {
    ComTexto(string t) { trecho = t; }
    fn aceita(Tarefa t) -> bool { return Strings.contains(t.titulo, trecho); }
  }
}

class Nao : Filtro {
  Filtro f;
  pub {
    Nao(Filtro f) { self.f = f; }
    fn aceita(Tarefa t) -> bool { return not f.aceita(t); }
  }
}

fn main() {
  list<Tarefa> tarefas = [new Tarefa("comprar pão"), new Tarefa("pagar a luz"), new Tarefa("pagar o aluguel")];
  Filtro sem_pagar = new Nao(new ComTexto("pagar"));
  for (Tarefa t in tarefas) {
    if (sem_pagar.aceita(t)) {
      print(t.titulo);
    }
  }
}
```

```saida
comprar pão
```
