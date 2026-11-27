# Soluções do capítulo 12

[Voltar ao capítulo](../12-classes.md)

## Exercício 1

Sem leituras não há média nem máxima: os dois métodos devolvem um opcional, e quem chama decide.

```cinza
import Lists;

class Termometro {
  list<f64> leituras;
  pub {
    fn registra(f64 graus) {
      leituras.add(graus);
    }
    fn media() -> f64? {
      if (leituras.size() == 0) {
        return null;
      }
      return Lists.average(leituras);
    }
    fn maxima() -> f64? {
      if (leituras.size() == 0) {
        return null;
      }
      return Lists.max(leituras);
    }
  }
}

fn main() {
  Termometro t = new Termometro();
  print(t.media());
  t.registra(20.5ff);
  t.registra(23.ff);
  t.registra(19.25ff);
  print(t.media(), t.maxima());
}
```

```saida
null
20.916666666666668 23
```

## Exercício 2

```cinza
class Pilha {
  list<i32> itens;
  pub {
    fn empilha(i32 x) {
      itens.add(x);
    }
    fn desempilha() -> i32? {
      if (itens.size() == 0) {
        return null;
      }
      i32 x = itens[itens.size() - 1];
      itens.remove(itens.size() - 1);
      return x;
    }
    fn topo() -> i32? {
      if (itens.size() == 0) {
        return null;
      }
      return itens[itens.size() - 1];
    }
    fn tamanho() -> i32 {
      return itens.size();
    }
  }
}

fn main() {
  Pilha p = new Pilha();
  p.empilha(1);
  p.empilha(2);
  p.empilha(3);
  print(p.desempilha(), p.topo(), p.tamanho());
  print(p.desempilha(), p.desempilha(), p.desempilha());
}
```

```saida
3 2 2
2 1 null
```

## Exercício 3

```cinza
struct ContadorS {
  i32 n = 0;
}

class ContadorC {
  i32 n = 0;
  pub {
    fn soma() { n += 1; }
    fn valor() -> i32 { return n; }
  }
}

fn incrementa_s(ContadorS c) {
  c.n += 1;
}

fn incrementa_c(ContadorC c) {
  c.soma();
}

fn main() {
  ContadorS s = new ContadorS();
  ContadorC c = new ContadorC();
  incrementa_s(s);
  incrementa_c(c);
  print(s.n, c.valor());
}
```

```saida
0 1
```

O struct é um valor: `incrementa_s` recebeu uma **cópia** e somou nela. O objeto de classe é
compartilhado: `incrementa_c` recebeu **o mesmo** contador, e a soma ficou.

## Exercício 4

```cinza
class Retangulo {
  f64 largura;
  f64 altura;
  pub {
    Retangulo(f64 l, f64 a) {
      largura = l;
      altura = a;
    }
    fn area() -> f64 {
      return largura * altura;
    }
    fn escala(f64 fator) -> Retangulo {
      largura *= fator;
      altura *= fator;
      return self;
    }
  }
}

fn main() {
  Retangulo r = new Retangulo(2.ff, 3.ff);
  print(r.area(), r.escala(2.ff).escala(1.5ff).area(), r.area());
}
```

```saida
6 54 54
```

`escala` muda o próprio objeto e o devolve: a terceira chamada a `area()` mostra que `r` foi
escalado, não uma cópia.

## Exercício 5

```cinza
struct Livro {
  string titulo;
  bool emprestado = false;
}

class Biblioteca {
  list<Livro> livros;

  fn busca(string titulo) -> i32? {
    for (i32 i in range(0, livros.size())) {
      if (livros[i].titulo == titulo) {
        return i;
      }
    }
    return null;
  }

  pub {
    fn acrescenta(string titulo) {
      livros.add(new Livro(titulo: titulo));
    }
    fn empresta(string titulo) -> bool {
      i32 i = busca(titulo) else { return false; };
      if (livros[i].emprestado) {
        return false;
      }
      livros[i].emprestado = true;
      return true;
    }
    fn devolve(string titulo) -> bool {
      i32 i = busca(titulo) else { return false; };
      if (not livros[i].emprestado) {
        return false;
      }
      livros[i].emprestado = false;
      return true;
    }
    fn disponiveis() -> list<string> {
      list<string> r;
      for (Livro l in livros) {
        if (not l.emprestado) {
          r.add(l.titulo);
        }
      }
      return r;
    }
  }
}

fn main() {
  Biblioteca b = new Biblioteca();
  b.acrescenta("Dom Casmurro");
  b.acrescenta("Vidas Secas");
  print(b.empresta("Dom Casmurro"), b.empresta("Dom Casmurro"), b.empresta("Iracema"));
  print(b.disponiveis());
  print(b.devolve("Dom Casmurro"), b.disponiveis());
}
```

```saida
true false false
["Vidas Secas"]
true ["Dom Casmurro", "Vidas Secas"]
```
