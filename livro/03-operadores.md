# 3. Operadores e expressões

**Objetivos.** Ao fim deste capítulo você sabe:

- usar os operadores aritméticos, de comparação, de igualdade e lógicos, e prever o tipo do
  resultado;
- explicar a divisão inteira e o sinal do resto;
- ler uma expressão pela tabela de precedência;
- dizer em que ordem as partes de uma expressão e de uma atribuição são avaliadas.

**Pré-requisitos.** O capítulo 2 (os tipos e as duas famílias de números).

---

Os operadores da Cinza são os que você já conhece de C, Java ou Python, com menos variações e
nenhuma conversão escondida. Cada um vale para um conjunto definido de tipos, e o compilador
recusa o resto — inclusive os operadores que outras linguagens têm e a Cinza não (o fim da seção
3.7 lista quais).

## 3.1 Aritmética: `+ - * / %`

Os dois lados são da mesma família (seção 2.5), e o resultado tem o tipo mais largo dos dois:

```cinza
fn main() {
  print(7 + 2, 7 - 2, 7 * 2, 7 / 2, 7 % 2);
  print(7.ff / 2.ff, 7.5f / 2.f);
}
```

```saida
9 5 14 3 1
3.5 3.75
```

### Divisão inteira e resto

Entre inteiros, **`/` é a divisão inteira, e trunca em direção ao zero**; o **`%` dá o resto, com
o sinal do dividendo** (o número da esquerda). Os dois sempre combinam: `a == (a / b) * b + a % b`.

```cinza
fn main() {
  print(-7 / 2, 7 / -2, -7 % 2, 7 % -2);
}
```

```saida
-3 -3 -1 1
```

> **Vindo de outra linguagem.** É a regra do C, do Java, do C# e do Go. O Python é diferente: lá
> `-7 // 2` é `-4` (arredonda para baixo) e `-7 % 2` é `1` (o sinal do divisor). Na Cinza, `-7 /
> 2` é `-3` e `-7 % 2` é `-1`.

Para a divisão real entre inteiros, converta **antes** de dividir — depois, a parte fracionária já
se perdeu:

```cinza
fn main() {
  print(cast<f64>(7) / 2.ff, cast<f64>(7 / 2));
}
```

```saida
3.5 3
```

O `%` só existe entre inteiros:

```cinza resto.cinza
fn main() {
  print(7.5ff % 2.ff);
}
```

```erro
resto.cinza:2:15: SemanticError: Operador '%' só existe entre inteiros (i32, i64), mas recebeu 'f64' e 'f64'
```

### O que para o programa

Uma divisão ou um resto por zero lança `ZeroDivisionError` — entre inteiros e também em ponto
flutuante (seção 2.4):

```cinza divide.cinza
fn main() {
  i32 zero = 0;
  print(10 / zero);
}
```

```erro
divide.cinza:3:12: ZeroDivisionError: divisão por zero
    em main (divide.cinza:3)
```

Um resultado inteiro fora da faixa do tipo lança `OverflowError` (seção 2.3). Isso inclui dois
casos que costumam passar despercebidos: o `-x` e o `x / -1` com o **menor** valor do tipo, cujo
oposto não cabe (em `i32`, -2147483648 existe e 2147483648 não). O `x % -1`, ao contrário, é
sempre `0`, que cabe:

```cinza menor.cinza
fn main() {
  i32 menor = -2147483648;
  print(menor % -1);
  print(-menor);
}
```

```saida
0
```

```erro
menor.cinza:4:9: OverflowError: -(-2147483648) excede a faixa de i32 (-2147483648 a 2147483647)
    em main (menor.cinza:4)
```

O programa imprimiu `0` antes de parar: o erro só acontece quando a conta é feita.

## 3.2 Comparação: `< <= > >=`

Valem entre números da mesma família e entre textos:

```cinza
fn main() {
  print(3 < 5, 5 <= 5, cast<i64>(3) > 2, 1.5ff >= 1.5ff);
  print("abc" < "abd", "Z" < "a", "maçã" < "mato", "" < "a");
}
```

```saida
true true true true
true true false true
```

Os textos se comparam **pela ordem dos code points** do Unicode, caractere a caractere. É uma ordem
exata e previsível, mas não é a do dicionário: as maiúsculas vêm antes das minúsculas (`"Z" <
"a"`), e as letras acentuadas depois de todas as sem acento — `ç` vem depois de `t`, por isso
`"maçã" < "mato"` é `false`.

Inteiro com ponto flutuante não se compara, como em qualquer conta (seção 2.5), e `bool` não tem
ordem:

```cinza limite.cinza
fn main() {
  i32 n = 3;
  f64 limite = 2.5ff;
  print(n > limite);
}
```

```erro
limite.cinza:4:11: SemanticError: Operador '>' entre 'i32' e 'f64': inteiro e ponto flutuante não se misturam. Converta um dos lados com cast<T>(valor), ex.: cast<f64>(n)
```

As comparações **não se encadeiam**:

```cinza faixa.cinza
fn main() {
  i32 x = 5;
  print(1 < x < 10);
}
```

```erro
faixa.cinza:3:15: SemanticError: A Cinza não encadeia comparações: 'a < b < c' compara o resultado de 'a < b' (um bool) com c. Escreva 'a < b and b < c'
```

> **Vindo de outra linguagem.** No Python, `1 < x < 10` é o mesmo que `1 < x and x < 10`. Em C ele
> compila e dá sempre `1`, porque compara o resultado de `1 < x` (0 ou 1) com 10. A Cinza não
> faz nenhum dos dois: é um erro, e a mensagem mostra como escrever.

## 3.3 Igualdade: `==` e `!=`

Os dois lados precisam ser do mesmo tipo, ou números da mesma família. O que "igual" quer dizer
depende do tipo:

| Tipo | Igualdade |
|------|-----------|
| primitivos, enum, `type` | pelo valor |
| `list`, `dict`, `pair`, `struct` | estrutural: os mesmos elementos (ou campos), iguais um a um |
| objeto de classe, interface, erro | identidade: o mesmo objeto |
| opcional (`T?`) | `null` só é igual a `null`; com valor, a igualdade do `T` |

```cinza
struct Ponto { i32 x = 0; i32 y = 0; }
class Conta {
  i32 saldo = 0;
}
fn main() {
  print(1 == cast<i64>(1), "a" == "a", [1, 2] == [1, 2], {{"a", 1}} == {{"a", 1}});
  print(new Ponto(1, 2) == new Ponto(1, 2));
  Conta a = new Conta();
  Conta b = new Conta();
  Conta c = a;
  print(a == b, a == c, -0.ff == 0.ff);
}
```

```saida
true true true true
true
false true true
```

Duas listas com os mesmos elementos são iguais; dois structs com os mesmos campos também. Já dois
objetos de classe só são iguais se forem **o mesmo objeto**: `a` e `b` foram criados separados,
`c` é outro nome para `a`. Os capítulos 9 e 12 explicam essa diferença entre valores e objetos.

Tipos diferentes não se comparam — inclusive inteiro com ponto flutuante:

```cinza tipos.cinza
fn main() {
  print(1 == "1");
}
```

```erro
tipos.cinza:2:11: SemanticError: Comparação '==' não é suportada entre 'i32' e 'string'
```

> **Por quê?** Em JavaScript `1 == "1"` é `true`; em Python é `False`, mas compila e roda. Em
> nenhum dos dois a comparação faz sentido no programa — é quase sempre um engano (um número que
> veio como texto, por exemplo). A Cinza o aponta antes de rodar.

## 3.4 Lógicos: `and`, `or`, `not`

Os operadores lógicos se escrevem de duas formas, à escolha: `and` ou `&&`, `or` ou `||`, `not`
ou `!`. Valem **só sobre `bool`**:

```cinza
fn main() {
  bool chovendo = true;
  bool frio = false;
  print(chovendo and frio, chovendo or frio, not frio);
  print(chovendo && frio, chovendo || frio, !frio);
}
```

```saida
false true true
false true true
```

Não há "verdadeiro por conveniência": um número, um texto ou uma lista não viram `bool`. O `if
(n)` de C e de Python é um erro:

```cinza condicao.cinza
fn main() {
  i32 n = 3;
  if (n) { print(n); }
}
```

```erro
condicao.cinza:3:3: SemanticError: Condição do 'if' deve ser 'bool', mas recebeu 'i32'
```

Escreva a comparação que você quer dizer: `n != 0`, `nome != ""`, `lista.size() > 0`.

### Curto-circuito

`and` e `or` avaliam o lado direito **só se precisarem**: em `a and b`, se `a` é `false` o
resultado já é `false`, e `b` nem é avaliado; em `a or b`, idem com `true`.

```cinza
fn confere(string nome, bool valor) -> bool {
  print("avaliou", nome);
  return valor;
}
fn main() {
  print(confere("a", false) and confere("b", true));
  print(confere("c", true) or confere("d", true));
}
```

```saida
avaliou a
false
avaliou c
true
```

Isso permite usar o lado esquerdo como guarda do direito. Aqui, `l[0]` só é lido se a lista tiver
um elemento — sem o curto-circuito, a lista vazia seria um `IndexError`:

```cinza
fn main() {
  list<i32> l;
  print(l.size() > 0 and l[0] == 5);
}
```

```saida
false
```

## 3.5 Concatenação com `+`

Com uma `string` de um dos lados, `+` junta textos. O outro lado pode ser uma `string` ou
qualquer primitivo, que entra na forma do `print`:

```cinza
fn main() {
  i32 idade = 30;
  print("idade: " + idade, "a" + 1 + 2, 1 + 2 + "a", "x" + true, "v" + 2.5ff);
}
```

```saida
idade: 30 a12 3a xtrue v2.5
```

O `+` associa à esquerda: em `"a" + 1 + 2`, primeiro `"a" + 1` dá `"a1"`, e depois `"a1" + 2` dá
`"a12"`; em `1 + 2 + "a"`, primeiro `1 + 2` dá `3`. Coleções e objetos não se concatenam — para
mostrar uma lista dentro de um texto, use a interpolação do `printf` (capítulo 6):

```cinza itens.cinza
fn main() {
  print("itens: " + [1, 2]);
}
```

```erro
itens.cinza:2:19: SemanticError: Operador '+' não é suportado entre 'string' e 'list<i32>'. O + junta texto com números, textos e bool; para pôr outro valor num texto, use a interpolação: format("... {x}")
```

## 3.6 Atribuição composta: `+= -= *= /= %=`

`a op= b` é o mesmo que `a = a op b`, com `a` avaliado uma vez só:

```cinza
fn main() {
  i32 n = 10;
  n += 5;
  n -= 3;
  n *= 2;
  n /= 4;
  n %= 4;
  string s = "a";
  s += "b";
  s += 1;
  print(n, s);
}
```

```saida
2 ab1
```

O resultado da conta precisa caber no tipo de `a`. Como `i32 + i64` dá `i64`, somar um `i64` a um
`i32` com `+=` é erro — seria estreitar sem escrever:

```cinza total.cinza
fn main() {
  i32 total = 0;
  i64 parcela = 5;
  total += parcela;
}
```

```erro
total.cinza:4:3: SemanticError: Operador '+=' em 'total' produz 'i64', que não cabe no tipo 'i32' do alvo
```

No sentido contrário vale: `i64 += i32` e `f64 *= f32` alargam o lado direito.

Não há `++` nem `--` — `x += 1` diz o mesmo, e não tem as variantes pré e pós:

```cinza contador.cinza
fn main() {
  i32 i = 0;
  i++;
}
```

```erro
contador.cinza:3:5: SyntaxError: A Cinza não tem '++' nem '--': escreva x += 1 (ou x -= 1) (token: '+')
```

E **uma atribuição não é uma expressão**: ela não tem valor, e não pode aparecer dentro de outra
expressão (o `if (x = ler())` do C, ou o `b = (a = 5)`):

```cinza dupla.cinza
fn main() {
  i32 a = 0;
  i32 b = (a = 5);
}
```

```erro
dupla.cinza:3:14: SyntaxError: Uma atribuição não é uma expressão na Cinza (não tem valor): faça a atribuição numa instrução própria (token: '=')
```

> **Por quê?** O `if (x = 0)` no lugar de `if (x == 0)` é um dos enganos mais antigos do C. Sem
> atribuição dentro de expressão, ele não tem como acontecer; e cada instrução muda no máximo uma
> coisa, o que facilita ler.

## 3.7 Precedência

Da **maior** para a **menor** precedência (quem está mais acima se liga primeiro):

| Nível | Operadores | Associatividade |
|-------|------------|-----------------|
| posfixos | chamada `f(x)`, membro `a.b`, índice `a[i]`, fatia `s[i:j]` | à esquerda |
| unários | `-x`, `!x`, `not x` | — |
| multiplicativos | `*` `/` `%` | à esquerda |
| aditivos | `+` `-` | à esquerda |
| comparação | `<` `<=` `>` `>=` | (não encadeia) |
| igualdade | `==` `!=` | à esquerda |
| "e" lógico | `&&` `and` | à esquerda |
| "ou" lógico | `\|\|` `or` | à esquerda |
| opcional | `x else y` (capítulo 11) | **à direita** |

```cinza
fn main() {
  print(2 + 3 * 4, (2 + 3) * 4, 10 - 4 - 3, 2 * 3 % 4, -2 * -3);
  print(1 + 2 == 3 and 4 > 5 or true, not true == false);
}
```

```saida
14 20 3 2 6
true true
```

Na segunda linha: `1 + 2 == 3 and 4 > 5 or true` se lê `((1 + 2) == 3 and (4 > 5)) or true`; e
`not true == false` se lê `(not true) == false`, porque o `not` é unário e se liga antes do `==`.

Esse último ponto pega quem vem do Python, onde `not` tem precedência baixa e `not a == 1`
significa `not (a == 1)`. Na Cinza o `not` tem a precedência do `!` — e com um operando que não é
`bool` o compilador explica:

```cinza negacao.cinza
fn main() {
  i32 a = 1;
  print(not a == 1);
}
```

```erro
negacao.cinza:3:9: SemanticError: Operador 'not' requer 'bool', mas recebeu 'i32'. O 'not' se aplica só ao que vem logo depois: 'not a == 1' é '(not a) == 1'; para negar a comparação, use parênteses ('not (a == 1)') ou '!='
```

Na dúvida, parênteses: eles não custam nada e deixam a intenção à vista.

### O que a Cinza não tem

Alguns operadores comuns em outras linguagens ficaram de fora, cada um com o equivalente direto. O
compilador reconhece a tentativa e diz o que usar:

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `x++`, `--x` | `x += 1`, `x -= 1` |
| `2 ** 10` | `Math.pow(2.ff, 10.ff)` (capítulo 21) |
| `cond ? a : b` | um `if`/`else` (capítulo 4) |
| `a & b`, `a \| b`, `a ^ b`, `~a` | não há operadores de bits |
| `a = b = 0`, `if (x = f())` | uma atribuição por instrução |

```cinza escolha.cinza
fn main() {
  i32 n = 5;
  string s = n > 3 ? "grande" : "pequeno";
}
```

```erro
escolha.cinza:3:20: SyntaxError: A Cinza não tem o operador ternário (condição ? a : b): use um if/else (token: '?')
```

## 3.8 Ordem de avaliação

As partes de uma expressão são avaliadas **da esquerda para a direita**, sempre — operandos,
argumentos de uma chamada e elementos de um literal. A precedência decide como as partes se
agrupam, não a ordem em que são calculadas:

```cinza
fn v(string nome, i32 n) -> i32 {
  print("avaliou", nome);
  return n;
}
fn main() {
  print(v("a", 1) + v("b", 2) * v("c", 3));
}
```

```saida
avaliou a
avaliou b
avaliou c
7
```

A multiplicação se liga antes da soma (`1 + (2 * 3)`), mas `a`, `b` e `c` são avaliados nessa
ordem. Em C e C++ essa ordem não é definida; na Cinza é, e os dois compiladores a seguem.

Numa **atribuição**, primeiro as partes do alvo (a variável, os índices), depois o valor:

```cinza
fn v(string nome, i32 n) -> i32 {
  print("avaliou", nome);
  return n;
}
fn main() {
  list<i32> l = [0, 0, 0];
  l[v("índice", 1)] = v("valor", 9);
  print(l);
}
```

```saida
avaliou índice
avaliou valor
[0, 9, 0]
```

Em `a op= b`, o alvo é avaliado uma vez, e o valor atual dele é lido **depois** de avaliar `b`.
Por isso, se `b` mudar o próprio alvo, a mudança entra na conta:

```cinza
fn muda(list<i32> l) -> i32 {
  l[0] = 100;
  return 1;
}
fn main() {
  list<i32> l = [5];
  l[0] += muda(l);
  print(l);
}
```

```saida
[101]
```

`muda` trocou `l[0]` por `100` antes de a soma ler o valor; o resultado é `100 + 1`. (Escrever um
programa que dependa disso não é boa ideia — mas, se acontecer, o resultado é definido e o mesmo
nos dois compiladores.)

## Programa do capítulo: dividir a conta

Uma conta de restaurante dividida entre amigos, em centavos — sem perder nenhum: quando a divisão
não é exata, alguns pagam um centavo a mais. O programa usa a divisão inteira e o resto, e guarda
dinheiro em `i64` de centavos, o jeito certo de fazer contas com dinheiro (o capítulo 2 mostrou
por que `0.1 + 0.2` não dá `0.3`):

<!-- args: 10000 3 -->
```cinza conta.cinza
// conta.cinza: cinza conta.cinza <total em centavos> <pessoas>

fn reais(i64 centavos) -> string {
  i64 inteiro = centavos / 100;
  i64 resto = centavos % 100;
  string zero = "";
  if (resto < 10) {
    zero = "0";
  }
  return "R$ " + inteiro + "," + zero + resto;
}

fn main(list<string> args) {
  if (args.size() != 2) {
    print("Uso: cinza conta.cinza <total em centavos> <pessoas>");
    exit(2);
  }
  i64 total = cast<i64>(args[0]);
  i64 pessoas = cast<i64>(args[1]);
  i64 parte = total / pessoas;
  i64 sobra = total % pessoas;
  print("Total:", reais(total), "para", pessoas, "pessoas");
  print(sobra, "pagam", reais(parte + 1), "e", pessoas - sobra, "pagam", reais(parte));
  print("Conferência:", reais(sobra * (parte + 1) + (pessoas - sobra) * parte));
}
```

```console
$ cinza conta.cinza 10000 3
```

```saida
Total: R$ 100,00 para 3 pessoas
1 pagam R$ 33,34 e 2 pagam R$ 33,33
Conferência: R$ 100,00
```

Com zero pessoas, a divisão para o programa — no lugar exato da conta:

<!-- args: 10000 0 -->
```cinza conta.cinza
// conta.cinza: cinza conta.cinza <total em centavos> <pessoas>

fn reais(i64 centavos) -> string {
  i64 inteiro = centavos / 100;
  i64 resto = centavos % 100;
  string zero = "";
  if (resto < 10) {
    zero = "0";
  }
  return "R$ " + inteiro + "," + zero + resto;
}

fn main(list<string> args) {
  if (args.size() != 2) {
    print("Uso: cinza conta.cinza <total em centavos> <pessoas>");
    exit(2);
  }
  i64 total = cast<i64>(args[0]);
  i64 pessoas = cast<i64>(args[1]);
  i64 parte = total / pessoas;
  i64 sobra = total % pessoas;
  print("Total:", reais(total), "para", pessoas, "pessoas");
  print(sobra, "pagam", reais(parte + 1), "e", pessoas - sobra, "pagam", reais(parte));
  print("Conferência:", reais(sobra * (parte + 1) + (pessoas - sobra) * parte));
}
```

```console
$ cinza conta.cinza 10000 0
```

```erro
conta.cinza:20:21: ZeroDivisionError: divisão por zero
    em main (conta.cinza:20)
```

Repare em:

- `centavos / 100` e `centavos % 100`: a divisão inteira separa os reais, o resto os centavos.
- `"R$ " + inteiro + "," + zero + resto`: a concatenação, da esquerda para a direita; cada `i64`
  entra como texto.
- `total % pessoas`: quantos centavos sobram — e quantas pessoas pagam um a mais.
- A "Conferência" refaz a soma: a soma das partes é o total, sem centavo perdido.

## Resumo

- `+ - * /` entre números da mesma família, com o resultado no tipo mais largo; `%` só entre
  inteiros. A divisão inteira trunca em direção ao zero, e o resto tem o sinal do dividendo.
- Divisão por zero, estouro, `-menor` e `menor / -1` são erros de execução.
- `< <= > >=` entre números da mesma família ou entre textos (pelos code points); não encadeiam.
  `==` compara valores — e, em objetos de classe, a identidade.
- `and`/`&&`, `or`/`||`, `not`/`!` só sobre `bool`, com curto-circuito.
- `+` com texto concatena; `op=` exige que o resultado caiba no alvo. Sem `++`, `**`, ternário,
  operadores de bits nem atribuição dentro de expressão.
- Tudo é avaliado da esquerda para a direita; numa atribuição, o alvo antes do valor.

## Exercícios

1. Sem rodar, diga o resultado de cada expressão — e depois confira:
   `-7 / 2`, `-7 % 2`, `7 % -2`, `"1" + 2 + 3`, `1 + 2 + "3"`, `2 + 3 * 4 % 5`, `not false and false`.
2. Escreva a função `fn bissexto(i32 ano) -> bool`: um ano é bissexto se é divisível por 4 e não
   por 100, ou se é divisível por 400. Teste com 1900, 2000, 2024 e 2026.
3. O teste abaixo não compila. Explique o erro e corrija-o.

   ```text
   fn main() {
     i32 nota = 7;
     print(0 <= nota <= 10);
   }
   ```

4. Por que o trecho abaixo é um erro? Corrija-o de dois jeitos diferentes.

   ```text
   i32 total = 0;
   i64 x = 5;
   total += x;
   ```

5. Sem rodar, diga o que o programa imprime, e em que ordem:

   ```text
   fn f(string s, i32 n) -> i32 {
     print(s);
     return n;
   }
   fn main() {
     print(f("x", 1) * f("y", 2) == f("z", 2) or f("w", 0) > 0);
   }
   ```

As soluções estão em [`solucoes/03-operadores.md`](solucoes/03-operadores.md).

## Na spec

- A tabela de precedência e a gramática das expressões: [2.4](../spec/02-gramatica.md)
- A ordem de avaliação: [5.2](../spec/05-execucao.md)
- Os operadores e as regras de cada um: [5.3](../spec/05-execucao.md)
