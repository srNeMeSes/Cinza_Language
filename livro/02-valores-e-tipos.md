# 2. Valores, tipos e variáveis

**Objetivos.** Ao fim deste capítulo você sabe:

- escolher entre `i32`, `i64`, `f32` e `f64` e escrever os literais de cada um;
- declarar variáveis com o tipo escrito ou com `var`;
- converter entre tipos com `cast<T>` e prever quando a conversão falha;
- explicar o que a Cinza converte sozinha (quase nada) e o que ela confere antes de rodar.

**Pré-requisitos.** O capítulo 1.

---

Na Cinza todo valor tem um tipo conhecido antes de o programa rodar, e esse tipo nunca muda. O
compilador usa essa informação para recusar o que não faz sentido — somar texto com número,
guardar um decimal num inteiro, usar um valor que pode não existir — antes que vire um erro na
mão de quem usa o programa. Este capítulo apresenta os tipos básicos e as regras que os ligam; o
último trecho, o *mapa do sistema de tipos*, mostra onde cada um dos tipos restantes entra no
livro.

## 2.1 Os tipos primitivos

| Tipo | Valores | Exemplo de literal |
|------|---------|--------------------|
| `i32` | inteiro de 32 bits com sinal: de -2147483648 a 2147483647 | `42`, `-7` |
| `i64` | inteiro de 64 bits com sinal: até cerca de ±9,2 × 10¹⁸ | `3000000000` |
| `f32` | ponto flutuante de 32 bits (IEEE 754, precisão simples) | `1.5f` |
| `f64` | ponto flutuante de 64 bits (IEEE 754, precisão dupla) | `1.5ff` |
| `string` | texto em UTF-8 (sinônimo: `str`) | `"Olá"` |
| `bool` | `true` ou `false` | `true` |

A função embutida `type` mostra o tipo de qualquer valor — útil para conferir o que o compilador
entendeu:

```cinza
fn main() {
  print(type(42), type(3000000000), type(1.5f), type(1.5ff), type(1.f), type("a"), type(true));
}
```

```saida
i32 i64 f32 f64 f32 string bool
```

Repare no `3000000000`: ele não cabe em `i32`, então o literal já é `i64`. **Um literal inteiro é
`i32` se couber, e `i64` se não couber.**

## 2.2 Variáveis

Uma variável se declara com o tipo, o nome e o valor inicial:

```cinza
fn main() {
  i32 idade = 30;
  string nome = "Ana";
  bool ativo = true;
  idade = idade + 1;
  print(nome, idade, ativo);
}
```

```saida
Ana 31 true
```

Duas regras valem para toda variável:

- **O valor inicial é obrigatório.** Não existe variável "ainda sem valor", nem um valor padrão
  escondido (o `0` de um campo do Java, o `undefined` do JavaScript, o lixo da memória em C):

  ```cinza idade.cinza
  fn main() {
    i32 idade;
  }
  ```

  ```erro
  idade.cinza:2:7: SyntaxError: 'idade' deve ser inicializado na declaração. Somente list<T> e dict<K,V> (que nascem vazios) e um opcional T? (que nasce null) podem omitir o inicializador. Use: i32 idade = <valor>; (token: 'idade')
  ```

  A mensagem já adianta as exceções: as coleções nascem vazias (capítulos 7 e 8) e o opcional
  nasce `null` (capítulo 11) — os únicos casos em que "sem valor" tem um significado claro.

- **O tipo é fixo.** Uma variável `i32` guarda sempre um `i32`:

  ```cinza idade.cinza
  fn main() {
    i32 idade = 30;
    idade = "trinta";
  }
  ```

  ```erro
  idade.cinza:3:3: SemanticError: Tipo incompatível na atribuição de 'idade': esperado 'i32', mas recebeu 'string'. Converta com cast<i32>(valor)
  ```

### Inferência com `var`

Quando o tipo é óbvio pelo valor, `var` o deixa implícito. O tipo continua fixo — só não é
escrito:

```cinza
fn main() {
  var n = 10;
  var nome = "Ana";
  var pi = 3.14ff;
  print(type(n), type(nome), type(pi));
  n = n + 1;
  print(n);
}
```

```saida
i32 string f64
11
```

`var` vale para os tipos primitivos, em variáveis locais e no `for` (capítulo 4). Para uma lista,
um objeto ou um parâmetro de função, o tipo é sempre escrito: é onde ele mais ajuda quem lê.

```cinza lista.cinza
fn main() {
  var l = [1, 2];
}
```

```erro
lista.cinza:2:7: SemanticError: Inferência 'var' só é permitida para tipos primitivos (números, string, bool), mas recebeu 'list<i32>'. Declare o tipo explicitamente.
```

### Constantes locais

`const` antes do tipo faz da variável uma constante: ela não pode ser reatribuída. O capítulo 15
mostra o que mais o `const` garante (em coleções, em parâmetros e no nível de cima).

```cinza taxa.cinza
fn main() {
  const f64 TAXA = 0.05ff;
  TAXA = 0.1ff;
}
```

```erro
taxa.cinza:3:3: SemanticError: Constante 'TAXA' não pode ser reatribuída. Remova o 'const' na declaração se precisar de variável mutável.
```

## 2.3 Inteiros: `i32` e `i64`

Use **`i32`** para contagens, índices e quantidades do dia a dia — é o tipo dos literais pequenos,
de `size()` e das posições em textos e listas. Use **`i64`** quando o valor pode passar de dois
bilhões: identificadores grandes, somas acumuladas, instantes de tempo (capítulo 23), tamanhos de
arquivo.

Uma conta entre dois `i32` continua `i32`. E aqui está a primeira decisão importante da Cinza:
**um resultado que não cabe no tipo é um erro, não uma volta silenciosa ao começo da faixa**:

```cinza estouro.cinza
fn main() {
  i32 maior = 2147483647;
  print(maior + 1);
}
```

```erro
estouro.cinza:3:15: OverflowError: 2147483647 + 1 excede a faixa de i32 (-2147483648 a 2147483647)
    em main (estouro.cinza:3)
```

Com `i64`, a mesma conta cabe:

```cinza
fn main() {
  i64 maior = 2147483647;
  print(maior + 1);
}
```

```saida
2147483648
```

> **Por quê?** Em Java, C# e Go, `2147483647 + 1` dá `-2147483648` sem aviso nenhum (em C é
> comportamento indefinido, que na prática costuma dar o mesmo) — um saldo que vira negativo, um
> contador que volta a zero. O erro aparece longe da causa, quando aparece. A
> Cinza confere cada conta inteira e para no ponto exato do estouro, com a conta na mensagem. O
> mesmo vale para a divisão por zero e para `menor / -1` (capítulo 3).

> **Vindo de outra linguagem.** Não há literais em outra base (`0xFF`, `0b101`) nem separadores
> (`1_000`). Para converter de e para outras bases existe o módulo `Convert` (capítulo 21).

```cinza cor.cinza
fn main() {
  i32 cor = 0xFF;
}
```

```erro
cor.cinza:2:13: LexicalError: Literal '0xFF' inválido: um número não pode vir colado em letras (não há prefixo de base, como 0x)
```

## 2.4 Ponto flutuante: `f32` e `f64`

Um literal de ponto flutuante tem sempre um ponto **e** um sufixo que diz a precisão: `f` para
`f32` e `ff` para `f64`. Sem o sufixo, é erro:

```cinza preco.cinza
fn main() {
  f64 preco = 9.99;
}
```

```erro
preco.cinza:2:15: LexicalError: Literal '9.99' sem a precisão: escreva 9.99f (f32) ou 9.99ff (f64)
```

O ponto pode vir sem dígitos depois (`1.f`, `2.ff`), mas o dígito antes dele é obrigatório:
`0.5ff`, não `.5ff`. Use **`f64`** como padrão para contas; **`f32`** ocupa metade da memória e
tem cerca de 7 dígitos de precisão, contra cerca de 16 do `f64` — serve para grandes volumes de
dados em que essa precisão basta.

> **Por quê?** Em muitas linguagens `1.5` é um `double` e `1.5f` um `float`, e a diferença passa
> despercebida até um resultado sair com sete dígitos certos em vez de dezesseis. Na Cinza a
> precisão está sempre escrita no literal, à vista de quem lê.

Também não há notação com expoente. Um número muito grande ou muito pequeno se escreve por
extenso, ou se converte de um texto:

```cinza avogadro.cinza
fn main() {
  f64 avogadro = 6.022e23;
}
```

```erro
avogadro.cinza:2:18: LexicalError: Literal '6.022e23' inválido: a Cinza não tem notação com expoente; escreva o número por extenso ou converta o texto: cast<f64>("6.022e23")
```

### Sem infinito e sem NaN

A Cinza segue o IEEE 754 nas contas, com uma diferença deliberada: **infinito e NaN não existem**.
Uma divisão por zero lança `ZeroDivisionError` — também em ponto flutuante, inclusive `0 / 0` —,
e um resultado além do maior valor do tipo lança `OverflowError`:

```cinza divisao.cinza
fn main() {
  f64 zero = 0.ff;
  print(1.ff / zero);
}
```

```erro
divisao.cinza:3:14: ZeroDivisionError: divisão por zero
    em main (divisao.cinza:3)
```

> **Por quê?** Um `NaN` se espalha em silêncio: toda conta que o toca vira `NaN`, e a comparação
> `NaN == NaN` é falsa. Quando ele aparece no fim de um cálculo, a origem já se perdeu. Na Cinza o
> programa para onde a conta deixou de ter resultado.

O que é pequeno demais vira `0`, e o zero negativo existe (`-0.ff` aparece como `-0`), como no
IEEE 754.

### Como os números aparecem

`print` (e a concatenação, e o `cast<string>`) mostra um número de ponto flutuante com **o menor
número de dígitos que volta ao mesmo valor no próprio tipo**. Valores inteiros saem sem `.0`, e a
notação científica aparece só nos extremos — a mesma regra do `repr` do Python:

```cinza
fn main() {
  print(0.1ff + 0.2ff, 1.ff / 3.ff, 1.f / 3.f, 2.5ff * 4.ff);
  print(100000.ff, 0.00001ff, cast<f64>("1e16"), -0.ff);
}
```

```saida
0.30000000000000004 0.3333333333333333 0.33333334 10
100000 1e-05 1e+16 -0
```

O `0.30000000000000004` é o valor real da soma em binário — a Cinza não o arredonda para parecer
`0.3`. Para comparar resultados de ponto flutuante, use `Math.is_close` (capítulo 21); para
mostrar com um número fixo de casas, `printf("{x:.2f}")` (capítulo 6). Repare também que `1.f /
3.f` sai com menos dígitos que `1.ff / 3.ff`: é a precisão de cada tipo.

## 2.5 As duas famílias

Os quatro tipos numéricos formam **duas famílias que não se misturam**: os inteiros (`i32`, `i64`)
e os de ponto flutuante (`f32`, `f64`).

**Dentro da família**, o mais estreito é promovido ao mais largo, sem perda: numa conta, `i32 +
i64` dá `i64` e `f32 * f64` dá `f64`; numa atribuição, um `i32` cabe num `i64` e um `f32` num
`f64`:

```cinza
fn main() {
  i32 a = 2000000000;
  i64 b = a;
  f32 c = 0.1f;
  f64 d = c;
  print(a + b, type(a + b), d, type(c * 2.ff));
}
```

```saida
4000000000 i64 0.10000000149011612 f64
```

O `d` mostra o valor exato que o `f32` guardava: `0.1f` não é exatamente 0,1, e em `f64` os
dígitos a mais aparecem. Alargar é sempre exato; o que já estava arredondado continua.

**Entre famílias, nada é implícito** — nem numa conta, nem numa comparação, nem com um literal:

```cinza media.cinza
fn main() {
  i32 n = 3;
  f64 x = 1.5ff;
  print(n * x);
}
```

```erro
media.cinza:4:11: SemanticError: Operador '*' entre 'i32' e 'f64': inteiro e ponto flutuante não se misturam. Converta um dos lados com cast<T>(valor), ex.: cast<f64>(n)
```

```cinza dobro.cinza
fn main() {
  f64 x = 1.5ff;
  print(x * 2);
}
```

```erro
dobro.cinza:3:13: SemanticError: O literal 2 é inteiro, mas aqui se espera 'f64': inteiro e ponto flutuante não se misturam. Escreva 2.ff (ou converta com cast<f64>(...))
```

E **o mais largo nunca vira o mais estreito sozinho**, mesmo que o valor coubesse:

```cinza estreita.cinza
fn main() {
  i64 grande = 5;
  i32 pequeno = grande;
}
```

```erro
estreita.cinza:3:7: SemanticError: Tipo incompatível na declaração de 'pequeno': declarado 'i32', mas atribuído 'i64'. Converta com cast<i32>(valor)
```

> **Por quê?** Cada conversão implícita de outras linguagens é um lugar onde um valor muda sem
> ninguém ter escrito isso: o inteiro que vira `double` e perde precisão acima de 2⁵³, o `long`
> que vira `int` e é cortado, a divisão inteira que acontece antes da conversão (`7 / 2` dando `3`
> num `double`). Na Cinza o que alarga sem perda é automático; todo o resto é escrito, com `cast`.

## 2.6 Converter com `cast<T>`

`cast<T>(valor)` converte entre `i32`, `i64`, `f32`, `f64` e `string`:

```cinza
fn main() {
  print(cast<f64>(7) / 2.ff, cast<i32>(-2.9ff), cast<i64>("42") + 1, cast<string>(3.5ff) + "!", cast<f32>(16777217));
}
```

```saida
3.5 -2 43 3.5! 16777216
```

As regras principais (a tabela completa está na [spec, seção 3.1](../spec/03-tipos.md)):

- **ponto flutuante → inteiro** corta as casas, em direção ao zero: `cast<i32>(-2.9ff)` é `-2`
  (para arredondar, `Math.round`, capítulo 21);
- **inteiro → ponto flutuante** dá o valor mais próximo: `16777217` não existe em `f32`, e vira
  `16777216`;
- **número → `string`** é a mesma forma do `print`;
- **`string` → número** aceita só o número escrito, sem espaços em volta.

O `cast` confere o valor na execução. **Um valor que não cabe, ou um texto que não é número, é um
erro** — de novo, nada de resultado silenciosamente errado:

```cinza grande.cinza
fn main() {
  i64 g = 3000000000;
  print(cast<i32>(g));
}
```

```erro
grande.cinza:3:9: OverflowError: cast<i32>(3000000000): o valor não cabe em i32
    em main (grande.cinza:3)
```

```cinza texto.cinza
fn main() {
  print(cast<i32>("12a"));
}
```

```erro
texto.cinza:2:9: ValueError: cast<i32>("12a"): não é um número inteiro (só sinal e dígitos)
    em main (texto.cinza:2)
```

Esses erros podem ser capturados com `try`/`except` (capítulo 14). Quando um texto que não é
número é um caso esperado — o que o usuário digitou, por exemplo —, o módulo `Convert` tem a
versão que devolve um opcional em vez de lançar erro: `Convert.to_i32(texto) else 0` (capítulos 11
e 21).

O `cast` converte o valor **depois** de calculado. Por isso `cast<f32>(2 + 3.f)` não resolve a
mistura — a conta dentro dos parênteses já é inválida:

```cinza mistura.cinza
fn main() {
  print(cast<f32>(2 + 3.f));
}
```

```erro
mistura.cinza:2:19: SemanticError: O literal 2 é inteiro, mas aqui se espera 'f32': inteiro e ponto flutuante não se misturam. Escreva 2.f (ou converta com cast<f32>(...))
```

`bool` e as coleções não se convertem com `cast`: um `bool` vira número com um `if`, e uma lista
vira texto com `Strings.join` (capítulo 6).

## 2.7 Textos e booleanos

O capítulo 6 é todo sobre textos; por ora, o essencial. Uma `string` se escreve entre aspas
duplas, e os escapes são `\n` (quebra de linha), `\t` (tabulação), `\r`, `\"` e `\\`. `str` é
sinônimo de `string`:

```cinza
fn main() {
  string s = "linha 1\nlinha 2\t(tab) \"aspas\" \\ barra";
  print(s);
  str curto = "str é o mesmo que string";
  print(curto, type(curto));
}
```

```saida
linha 1
linha 2	(tab) "aspas" \ barra
str é o mesmo que string string
```

Um `bool` é `true` ou `false`, e não se confunde com número: `bool b = 1;` é erro, e um `if`
exige um `bool` de verdade (capítulo 4).

## 2.8 O mapa do sistema de tipos

Os tipos deste capítulo são a base. O resto do sistema de tipos aparece ao longo do livro; este
mapa mostra onde, e as regras que valem para todos.

**Os tipos, pelo que acontece numa atribuição** — se o valor é copiado ou se as duas variáveis
passam a ver o mesmo valor (detalhes no capítulo 5):

| Tipos | O que são | Na atribuição | Capítulo |
|-------|-----------|---------------|----------|
| `i32`, `i64`, `f32`, `f64`, `string`, `bool` | primitivos | copiados | 2 |
| `list<T>`, `dict<K, V>` | coleções | compartilhados (a mesma coleção) | 7, 8 |
| `pair<A, B>` | par imutável | copiado | 8 |
| `struct` | agrupamento de campos, um valor | copiado (cópia rasa) | 9 |
| `enum` | um conjunto fixo de nomes | copiado | 10 |
| `T?` | o opcional: um `T` ou `null` | conforme o `T` | 11 |
| `class` | objeto com campos privados e métodos | compartilhado (o mesmo objeto) | 12 |
| `interface` | um contrato que classes cumprem | compartilhado | 13 |
| erros (`ValueError`, `error Nome;`) | o que se lança com `throw` | compartilhado, imutável | 14 |

**As regras que atravessam todos eles:**

- **O tipo é conhecido na compilação e nunca muda.** Não há tipo dinâmico; um valor de interface
  nunca volta a ser tratado como a classe (capítulo 13).
- **Implícito, só o que alarga sem perda**: `i32` → `i64`, `f32` → `f64`, e uma classe onde se
  espera uma interface que ela declara cumprir. Todo o resto é escrito com `cast`.
- **`null` só existe no opcional `T?`**, e o compilador exige que a ausência seja resolvida antes
  de usar o valor (capítulo 11). Um `string`, um objeto, uma lista nunca são `null`.
- **Coleções são invariantes**: `list<i32>` não é `list<i64>` (capítulo 7).
- **`const` é somente leitura**: pelo nome const nada muda, em qualquer profundidade, e ele para no
  objeto de classe (capítulo 15).
- **Genéricos** são conferidos em cada uso, com os tipos concretos (capítulos 16 e 17).

**O que é conferido antes de rodar** — os tipos de cada expressão, as conversões, o `null`, os
nomes, a função que esquece de retornar, o código que nunca roda, o `const` — e **o que sobra para
a execução**, sempre como um erro com tipo e mensagem: o estouro de uma conta, a divisão por zero,
o índice fora da lista, a chave que não existe, o `cast` de um valor que não cabe.

## Programa do capítulo: conversor de distâncias

Um conversor que recebe uma distância em quilômetros, como texto na linha de comando, e a mostra
em milhas e em metros. Ele usa as duas famílias de números e as conversões nos dois sentidos:

<!-- args: 42.195 -->
```cinza conversor.cinza
// conversor.cinza: cinza conversor.cinza <quilômetros>

const f64 MILHAS_POR_KM = 0.621371ff;

fn main(list<string> args) {
  if (args.size() != 1) {
    print("Uso: cinza conversor.cinza <quilômetros>");
    exit(2);
  }
  f64 km = cast<f64>(args[0]);           // o texto vira número (ValueError se não for)
  f64 milhas = km * MILHAS_POR_KM;
  i64 metros = cast<i64>(km * 1000.ff);  // corta as casas: metros inteiros
  print(km, "km =", milhas, "milhas =", metros, "metros");
}
```

```console
$ cinza conversor.cinza 42.195
```

```saida
42.195 km = 26.218749345 milhas = 42195 metros
```

Com uma distância que não é número, o `cast` para o programa com a mensagem e a posição:

<!-- args: 42,195 -->
```cinza conversor.cinza
// conversor.cinza: cinza conversor.cinza <quilômetros>

const f64 MILHAS_POR_KM = 0.621371ff;

fn main(list<string> args) {
  if (args.size() != 1) {
    print("Uso: cinza conversor.cinza <quilômetros>");
    exit(2);
  }
  f64 km = cast<f64>(args[0]);           // o texto vira número (ValueError se não for)
  f64 milhas = km * MILHAS_POR_KM;
  i64 metros = cast<i64>(km * 1000.ff);  // corta as casas: metros inteiros
  print(km, "km =", milhas, "milhas =", metros, "metros");
}
```

```console
$ cinza conversor.cinza 42,195
```

```erro
conversor.cinza:10:12: ValueError: cast<f64>("42,195"): não é um número (ex.: 2.5, -3, 1e10)
    em main (conversor.cinza:10)
```

A vírgula decimal não é aceita: o `cast` lê o número no formato do código (com a diferença de que,
num texto, o expoente é aceito: `cast<f64>("1e10")`). Para aceitá-la,
troque-a antes por ponto — `Strings.replace(texto, ",", ".")`, no capítulo 6. E para tratar o
erro em vez de parar o programa, o capítulo 11 mostra o `Convert.to_f64`, que devolve `null` em
vez de lançar.

Repare também em `1000.ff`: a conta é entre `f64`, então o literal precisa ser `f64`. Escrever
`km * 1000` seria misturar as famílias.

## Resumo

- Os primitivos são `i32`, `i64`, `f32`, `f64`, `string` (ou `str`) e `bool`. Um literal inteiro é
  `i32` se couber, senão `i64`; um literal de ponto flutuante tem o ponto e o sufixo `f` (`f32`)
  ou `ff` (`f64`). Não há `0x`, `1_000` nem expoente.
- Toda variável tem valor inicial e um tipo fixo; `var` o deixa implícito (só para primitivos);
  `const` impede a reatribuição.
- Estouro, divisão por zero e `cast` que não cabe são erros de execução. Não há infinito nem NaN.
- As famílias não se misturam: dentro delas o estreito alarga sozinho; entre elas e para estreitar,
  `cast<T>`.
- `print` mostra o ponto flutuante com o menor número de dígitos que volta ao valor; científica só
  nos extremos.

## Exercícios

1. Escreva um programa que declare uma variável de cada tipo primitivo, com o tipo escrito, e
   imprima cada valor ao lado do seu `type()`.
2. Sem rodar, diga o valor e o tipo de cada expressão — depois confira com `print` e `type`:
   `7 / 2`, `cast<f64>(7) / 2.ff`, `cast<f64>(7 / 2)`, `cast<i32>(-7.9ff)`,
   `1 + cast<i64>(2)` e `cast<f32>(0.1ff)`.
3. O programa abaixo para com um erro. Explique por quê e reescreva-o para imprimir `2147483648`
   (sem mudar o valor inicial).

   ```text
   fn main() {
     i32 x = 2147483647;
     x = x + 1;
     print(x);
   }
   ```

4. Escreva um conversor de temperatura: ele recebe os graus Celsius na linha de comando e imprime
   os Fahrenheit (`F = C × 9 / 5 + 32`). Cuidado com as famílias e com a divisão.

As soluções estão em [`solucoes/02-valores-e-tipos.md`](solucoes/02-valores-e-tipos.md).

## Na spec

- Os literais: [1.5](../spec/01-lexico.md)
- Os tipos primitivos, as duas famílias e o `cast`: [3.1](../spec/03-tipos.md)
- `var` e o valor inicial: [3.2 e 3.6](../spec/03-tipos.md)
- As regras de atribuição: [3.5](../spec/03-tipos.md)
- A forma textual dos números: [5.4](../spec/05-execucao.md)
