# 5. Funções

**Objetivos.** Ao fim deste capítulo você sabe:

- declarar funções com parâmetros e retorno, e entender o que o compilador confere numa chamada;
- prever o que muda para quem chama quando uma função altera um parâmetro;
- escrever funções recursivas e reconhecer o limite de profundidade;
- usar as funções embutidas e saber o que a Cinza deixa de fora de propósito: sobrecarga, valores
  padrão, funções como valor.

**Pré-requisitos.** Os capítulos 2 a 4.

---

Uma função da Cinza é declarada no nível de cima do arquivo, com os tipos de todos os parâmetros e
o do retorno escritos. Ela é chamada pelo nome, com os argumentos em ordem, e só enxerga o que
recebe. É uma forma simples de propósito: o que uma chamada faz está todo na assinatura e no
corpo, sem valores escondidos, sem várias versões com o mesmo nome e sem funções guardadas em
variáveis.

## 5.1 Declarar e chamar

```cinza
fn area(f64 largura, f64 altura) -> f64 {
  return largura * altura;
}

fn saudacao(string nome) {
  print("Olá,", nome);
}

fn main() {
  print(area(3.ff, 4.5ff));
  saudacao("Ana");
}
```

```saida
13.5
Olá, Ana
```

- `fn nome(tipo1 p1, tipo2 p2) -> tipo` declara a função. Os parâmetros têm o tipo sempre escrito
  (`var` não vale aqui).
- Sem `-> tipo`, a função é `void`: não devolve nada.
- A ordem das funções no arquivo não importa: uma pode chamar outra declarada mais abaixo (seção
  1.4).

Cada chamada é conferida contra a assinatura — a quantidade de argumentos e o tipo de cada um:

```cinza soma.cinza
fn soma(i32 a, i32 b) -> i32 {
  return a + b;
}
fn main() {
  print(soma(1));
}
```

```erro
soma.cinza:5:9: SemanticError: Aridade incorreta em 'soma': esperados 2 argumento(s), mas recebeu 1
```

```cinza dobro.cinza
fn dobro(i32 x) -> i32 {
  return x * 2;
}
fn main() {
  print(dobro("dois"));
}
```

```erro
dobro.cinza:5:9: SemanticError: Argumento 1 de 'dobro': esperado 'i32', mas recebeu 'string'. Converta com cast<i32>(valor)
```

Um argumento segue as mesmas regras de uma atribuição (seção 2.5): um `i32` entra num parâmetro
`i64` e um `f32` num `f64`, alargados; o resto precisa de `cast`.

## 5.2 O retorno

`return valor;` encerra a função e devolve o valor, que precisa ser do tipo declarado. Numa função
`void`, `return;` (sem valor) sai mais cedo. O compilador confere os dois lados — e, como o capítulo
4 mostrou, que uma função com retorno retorna em todos os caminhos.

O resultado de uma função `void` **não é um valor**: não pode ser guardado, passado nem comparado.

```cinza vazio.cinza
fn saudacao(string nome) {
  print("Olá,", nome);
}
fn main() {
  string s = saudacao("Ana");
}
```

```erro
vazio.cinza:5:10: SemanticError: Tipo incompatível na declaração de 's': declarado 'string', mas atribuído 'void'. A expressão é 'void' e não tem valor (a função não retorna nada)
```

Para devolver mais de um valor, devolva um par — ou, com mais campos e nomes, um struct (capítulo 9):

```cinza
fn divide(i32 a, i32 b) -> pair<i32, i32> {
  return {a / b, a % b};
}
fn main() {
  pair<i32, i32> r = divide(17, 5);
  print(r.first, r.second, r);
}
```

```saida
3 2 {3, 2}
```

## 5.3 O que acontece com os argumentos

Ao chamar uma função, cada parâmetro recebe o argumento **como numa atribuição**. O que isso
significa depende do tipo — é a tabela do mapa do sistema de tipos (seção 2.8), agora em ação:

| Tipo do argumento | O parâmetro recebe | Se a função o alterar |
|-------------------|--------------------|-----------------------|
| números, `string`, `bool`, enum | uma cópia | quem chamou não vê |
| `struct`, `pair` | uma cópia (rasa) | quem chamou não vê |
| `list`, `dict` | a mesma coleção | quem chamou vê |
| objeto de classe | o mesmo objeto | quem chamou vê |

```cinza
struct Ponto { i32 x = 0; i32 y = 0; }

fn mexe(i32 n, Ponto p, list<i32> l) {
  n = 99;
  p.x = 99;
  l.add(99);
}

fn main() {
  i32 n = 1;
  Ponto p = new Ponto();
  list<i32> l = [1];
  mexe(n, p, l);
  print(n, p, l);
}
```

```saida
1 Ponto{x: 0, y: 0} [1, 99]
```

`n` e `p` chegaram como cópias: o que `mexe` fez com eles ficou dentro dela. A lista chegou como a
mesma lista, e o `99` acrescentado aparece em `main`. Uma função que precisa mudar um número ou um
struct de quem chama devolve o valor novo; quem chama decide guardá-lo.

Para garantir que uma função **não** altera uma coleção recebida, declare o parâmetro `const`
(capítulo 15):

```cinza leitura.cinza
fn soma(const list<i32> l) -> i32 {
  l.add(1);
  return 0;
}
fn main() {
  print(soma([1, 2]));
}
```

```erro
leitura.cinza:2:4: SemanticError: Não é possível chamar '.add()' em lista const. Listas const não podem ser modificadas.
```

> **Vindo de outra linguagem.** É o modelo do Java e do Python para coleções e objetos (o que se
> passa é uma referência), com uma diferença: na Cinza um **struct** é um valor, copiado como um
> número — como um `struct` do C# ou do Go. O capítulo 9 explora essa diferença.

## 5.4 O que uma função enxerga

Uma função enxerga seus parâmetros, suas variáveis locais, as constantes globais (`const`) e as
outras funções. **Nunca enxerga as variáveis de quem a chamou**:

```cinza contador.cinza
fn mostra() {
  print(contador);
}
fn main() {
  i32 contador = 3;
  mostra();
}
```

```erro
contador.cinza:2:9: SemanticError: Variável 'contador' não declarada
```

Como não há variáveis globais mutáveis (seção 1.4), tudo o que uma função usa e muda chega pelos
parâmetros ou sai pelo retorno. Para entender uma chamada, basta ler a assinatura.

Funções também não se declaram dentro de outras: toda `fn` fica no nível de cima (ou é um método,
dentro de uma classe, no capítulo 12).

## 5.5 Recursão

Uma função pode chamar a si mesma — e duas funções podem chamar uma à outra:

```cinza
fn fatorial(i64 n) -> i64 {
  if (n <= 1) {
    return 1;
  }
  return n * fatorial(n - 1);
}

fn par(i32 n) -> bool {
  if (n == 0) {
    return true;
  }
  return impar(n - 1);
}

fn impar(i32 n) -> bool {
  if (n == 0) {
    return false;
  }
  return par(n - 1);
}

fn main() {
  print(fatorial(20), par(10), impar(7));
}
```

```saida
2432902008176640000 true true
```

`fatorial(20)` é o maior que cabe em `i64`; `fatorial(21)` estoura — e a mensagem mostra a conta e
onde ela foi feita, com o caminho das chamadas:

```cinza fatorial.cinza
fn fatorial(i64 n) -> i64 {
  if (n <= 1) {
    return 1;
  }
  return n * fatorial(n - 1);
}
fn main() {
  print(fatorial(21));
}
```

```erro
fatorial.cinza:5:12: OverflowError: 21 * 2432902008176640000 excede a faixa de i64 (-9223372036854775808 a 9223372036854775807)
    em fatorial (fatorial.cinza:5)
    em main (fatorial.cinza:8)
```

### O limite de profundidade

Cada chamada em andamento ocupa um nível. **Acima de 2000 níveis, a chamada lança
`StackOverflowError`** — um erro comum da linguagem, com mensagem e stack trace, em vez de o
programa cair:

```cinza conta.cinza
fn conta(i32 n) -> i32 {
  if (n == 0) {
    return 0;
  }
  return 1 + conta(n - 1);
}
fn main() {
  print(conta(1000));
  print(conta(100000));
}
```

```saida
1000
```

```erro
conta.cinza:1:1: StackOverflowError: profundidade máxima de chamadas (2000) excedida ao chamar 'conta'
    em conta (conta.cinza:1)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    em conta (conta.cinza:5)
    ... (1989 chamadas omitidas)
    em main (conta.cinza:9)
```

Três coisas para ler nessa mensagem:

- A posição (`conta.cinza:1:1`) é a da **função chamada** — a chamada que passaria do limite é a
  que começaria `conta` mais uma vez.
- Um stack trace muito fundo mostra as **10 chamadas mais internas**, uma linha com quantas foram
  omitidas e a **mais externa**: o começo e o fim do caminho, que é o que importa para achar o
  problema.
- Como qualquer erro de execução, `StackOverflowError` pode ser capturado (capítulo 14) — o
  programa decide o que fazer.

Recursão de até algumas centenas de níveis é o uso típico (percorrer uma árvore, dividir um
problema ao meio); para repetir milhares de vezes, um laço.

> **Por quê?** Sem limite, uma recursão infinita esgota a pilha do sistema e o processo cai com uma
> falha de segmentação (ou, no Windows, um código de saída estranho), sem dizer onde. Com o limite,
> o erro é da linguagem: tem tipo, mensagem e o caminho das chamadas, igual nos dois compiladores.

## 5.6 Um nome, uma função

A Cinza não tem **sobrecarga**: dois itens do nível de cima não podem ter o mesmo nome, mesmo com
parâmetros diferentes.

```cinza area.cinza
fn area(f64 lado) -> f64 {
  return lado * lado;
}
fn area(f64 a, f64 b) -> f64 {
  return a * b;
}
fn main() { }
```

```erro
area.cinza:4:1: SemanticError: Declaração duplicada: 'area' já foi declarado neste escopo (na linha 1): não há sobrecarga, e dois itens do nível superior não têm o mesmo nome
```

Pelo mesmo motivo não há **valores padrão** nos parâmetros nem **argumentos pelo nome** numa
chamada (eles existem só na criação de structs, capítulo 9). Cada um tem a sua mensagem:

```cinza padrao.cinza
fn saudacao(string nome = "mundo") {
  print("Olá,", nome);
}
fn main() { }
```

```erro
padrao.cinza:1:25: SyntaxError: A Cinza não tem valor padrão nos parâmetros: todo argumento é passado na chamada (para variações, declare funções com nomes diferentes) (token: '=')
```

> **Por quê?** Com sobrecarga, valores padrão e argumentos pelo nome, ler `area(x)` exige saber
> qual das versões será escolhida, e com quais valores escondidos. Sem eles, cada nome é uma
> função, e cada chamada mostra todos os seus argumentos. Variações ganham nomes que as descrevem:
> `area_quadrado` e `area_retangulo`.

## 5.7 Funções não são valores

O nome de uma função só aparece numa chamada. Ela não pode ser guardada numa variável, passada como
argumento nem devolvida — e não há funções anônimas (lambdas):

```cinza aplica.cinza
fn dobro(i32 x) -> i32 {
  return x * 2;
}
fn main() {
  var f = dobro;
}
```

```erro
aplica.cinza:5:11: SemanticError: 'dobro' é uma função: chame-a com dobro(...). Na Cinza funções não são valores.
```

```cinza lambda.cinza
fn main() {
  var f = fn(i32 x) -> i32 { return x * 2; };
}
```

```erro
lambda.cinza:2:11: SyntaxError: A Cinza não tem funções anônimas (lambdas) nem funções como valor: declare a função no nível de cima e chame-a pelo nome. Para passar um comportamento adiante, use um objeto de uma classe que cumpre uma interface (token: 'fn')
```

O que outras linguagens fazem passando uma função adiante — um comparador para ordenar, um filtro,
uma ação para cada elemento —, a Cinza faz de dois jeitos:

- **um objeto de uma classe que cumpre uma interface** (capítulo 13): a interface diz qual método
  existe (`fn aceita(Tarefa t) -> bool`), e cada classe que a cumpre é um "filtro" diferente;
- **para ordenar por um critério**, `Lists.sort_by` (capítulo 7), que recebe a lista e a lista das
  chaves já calculadas.

> **Por quê?** Funções como valor trazem junto as closures — funções que capturam variáveis do
> lugar onde foram criadas e as mantêm vivas. É onde o estado se esconde e muda longe dos olhos. A
> Cinza prefere o estado à vista: num objeto, com campos e métodos declarados.

## 5.8 As funções embutidas

Algumas funções estão disponíveis em qualquer arquivo, sem `import`:

| Função | O que faz | Detalhes |
|--------|-----------|----------|
| `print(valores...)` | escreve os valores, separados por espaço, e pula a linha | seção 1.2 |
| `printf("texto")`, `format("texto")` | escreve (ou devolve) um texto com `{expressões}` substituídas | capítulo 6 |
| `input(string prompt) -> string?` | lê uma linha da entrada; no fim, `null` | capítulo 11 |
| `range(ini, fim[, passo])` | a faixa de números | seção 4.3 |
| `type(x)` | o tipo de `x` | seções 2.1 e 10 |
| `exit([código])` | encerra o programa com o código (de 0 a 255; o padrão é 0) | abaixo |

Elas **não são palavras reservadas**: uma função do programa com o mesmo nome tem precedência — e
passa a ser essa que todo o arquivo chama:

```cinza
fn range(i32 a, i32 b) -> i32 {
  return a * 100 + b;
}
fn main() {
  print(range(1, 2));
}
```

```saida
102
```

### `exit`

`exit(código)` encerra o programa de qualquer ponto, inclusive de dentro de uma função chamada pela
`main`, com o código de saída pedido. O que já foi escrito fica:

<!-- exit: 3 -->
```cinza sai.cinza
fn confere(i32 n) {
  if (n < 0) {
    print("valor inválido:", n);
    exit(3);
  }
}
fn main() {
  confere(5);
  confere(-1);
  print("nunca chega aqui");
}
```

```saida
valor inválido: -1
```

Antes de encerrar, o `exit` roda os blocos `finally` pendentes (capítulo 14), e nenhum `except` o
captura. Um código fora de 0 a 255 é `ValueError`. Para o compilador, depois do `exit` o caminho
termina, como depois de um `return` (seção 4.6).

## Programa do capítulo: a Torre de Hanói

Mover uma pilha de discos de uma haste para outra, um disco por vez, sem nunca pôr um disco maior
sobre um menor, usando uma terceira haste de apoio. A solução recursiva é a definição do problema:
para mover `n` discos de A para C, mova os `n - 1` de cima para B, mova o maior para C, e mova os
`n - 1` de B para C. A função devolve quantos movimentos fez, e só os mostra quando são poucos:

```cinza hanoi.cinza
// hanoi.cinza: cinza hanoi.cinza [discos]

fn move(i32 n, string origem, string destino, string apoio, bool mostra) -> i64 {
  if (n == 0) {
    return 0;
  }
  i64 antes = move(n - 1, origem, apoio, destino, mostra);
  if (mostra) {
    print("disco", n, ":", origem, "->", destino);
  }
  i64 depois = move(n - 1, apoio, destino, origem, mostra);
  return antes + 1 + depois;
}

fn main(list<string> args) {
  i32 discos = 3;
  if (args.size() > 0) {
    discos = cast<i32>(args[0]);
  }
  i64 total = move(discos, "A", "C", "B", discos <= 4);
  print(discos, "discos:", total, "movimentos");
}
```

```saida
disco 1 : A -> C
disco 2 : A -> B
disco 1 : C -> B
disco 3 : A -> C
disco 1 : B -> A
disco 2 : B -> C
disco 1 : A -> C
3 discos: 7 movimentos
```

Com mais discos, só a contagem — `2ⁿ - 1` movimentos:

<!-- args: 20 -->
```cinza hanoi.cinza
// hanoi.cinza: cinza hanoi.cinza [discos]

fn move(i32 n, string origem, string destino, string apoio, bool mostra) -> i64 {
  if (n == 0) {
    return 0;
  }
  i64 antes = move(n - 1, origem, apoio, destino, mostra);
  if (mostra) {
    print("disco", n, ":", origem, "->", destino);
  }
  i64 depois = move(n - 1, apoio, destino, origem, mostra);
  return antes + 1 + depois;
}

fn main(list<string> args) {
  i32 discos = 3;
  if (args.size() > 0) {
    discos = cast<i32>(args[0]);
  }
  i64 total = move(discos, "A", "C", "B", discos <= 4);
  print(discos, "discos:", total, "movimentos");
}
```

```console
$ cinza hanoi.cinza 20
```

```saida
20 discos: 1048575 movimentos
```

Repare em:

- A recursão desce só `n` níveis (cada chamada tira um disco), então 20 discos ficam longe do
  limite de 2000 — embora a função seja chamada mais de dois milhões de vezes.
- A contagem é `i64`: com 40 discos ela passa de um trilhão, e um `i32` estouraria.
- `mostra` é decidido uma vez, em `main`, e passado adiante: sem variáveis globais, é assim que uma
  configuração chega às chamadas de dentro.

## Resumo

- `fn nome(tipo p, ...) -> tipo { ... }`; sem `-> tipo`, `void`. Toda chamada é conferida (quantos
  argumentos e de que tipo), com o alargamento dentro da família.
- O resultado de uma função `void` não é um valor. Para vários valores, um `pair` ou um struct.
- Números, textos, structs e pares chegam como cópias; listas, dicionários e objetos, como os
  mesmos. `const` no parâmetro impede alterar a coleção.
- Uma função enxerga só parâmetros, locais, `const` globais e outras funções.
- Recursão até 2000 níveis; depois, `StackOverflowError`, com o stack trace resumido.
- Sem sobrecarga, valores padrão, argumentos pelo nome, funções como valor ou lambdas: cada nome é
  uma função, e o comportamento que se passa adiante é um objeto.
- As embutidas (`print`, `range`, `type`, `exit`...) cedem a vez a uma função do programa com o
  mesmo nome.

## Exercícios

1. Escreva `fn maior(i32 a, i32 b) -> i32` e, usando-a, `fn maior_de_tres(i32 a, i32 b, i32 c) ->
   i32`. Teste com `(3, 9, 5)`, `(9, 3, 5)` e `(4, 4, 4)`.
2. Escreva duas versões de `fib(i32 n) -> i64`, o n-ésimo número de Fibonacci (`fib(0) = 0`,
   `fib(1) = 1`): uma recursiva e uma com um laço. Imprima `fib(0)` a `fib(10)` com as duas.
3. Escreva `fn potencia(i64 base, i32 expoente) -> i64` recursiva (com `expoente >= 0`). Confira
   `potencia(2, 10)` e `potencia(3, 4)`, e veja o que acontece com `potencia(2, 63)`.
4. O programa abaixo imprime `1`, e não `2`. Explique por quê e corrija-o sem mudar a `main` além
   da linha da chamada.

   ```text
   fn incrementa(i32 n) {
     n += 1;
   }
   fn main() {
     i32 x = 1;
     incrementa(x);
     print(x);
   }
   ```

5. Escreva `fn mdc(i64 a, i64 b) -> i64` pelo algoritmo de Euclides, recursivo: `mdc(a, 0) = a` e
   `mdc(a, b) = mdc(b, a % b)`. Teste com `(48, 18)`, `(17, 5)` e `(0, 9)`.

As soluções estão em [`solucoes/05-funcoes.md`](solucoes/05-funcoes.md).

## Na spec

- Funções, sobrecarga, funções como valor e as embutidas com o mesmo nome: [4.4](../spec/04-declaracoes.md)
- Escopo e o que uma função enxerga: [4.5](../spec/04-declaracoes.md)
- Cópia e compartilhamento: [5.1](../spec/05-execucao.md)
- Recursão e o limite: [5.8](../spec/05-execucao.md)
- As funções embutidas e o `exit`: [6.1](../spec/06-biblioteca.md)
- O formato do stack trace: [7.3](../spec/07-erros.md)
