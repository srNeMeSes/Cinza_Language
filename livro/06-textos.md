# 6. Textos

**Objetivos.** Ao fim deste capítulo você sabe:

- percorrer, indexar e fatiar textos contando caracteres, não bytes;
- montar textos com `printf` e `format`, com largura e casas decimais;
- usar o módulo `Strings` para buscar, partir, limpar, montar e testar textos;
- lidar com uma busca que pode não achar nada.

**Pré-requisitos.** A Parte I (capítulos 1 a 5).

---

Uma `string` da Cinza é um texto em UTF-8, imutável, e tudo o que se conta nela — tamanho,
índices, fatias, posições de busca — conta **caracteres**. "coração" tem 7, e o `ç` é um só, mesmo
ocupando dois bytes na memória. O que se faz com textos está em três lugares: a indexação e as
fatias, direto na `string`; a interpolação, com `printf` e `format`; e o resto, no módulo `Strings`.

## 6.1 Caracteres, não bytes

```cinza
fn main() {
  string s = "coração";
  print(s.size(), s[0], s[4], s[-1], s[-2]);
}
```

```saida
7 c ç o ã
```

- `s.size()` é o número de caracteres (um `i32`).
- `s[i]` é o caractere do índice `i`, contando de 0 — e ele é uma `string` de tamanho 1. A Cinza
  não tem um tipo `char` separado.
- **Índices negativos contam do fim**: `s[-1]` é o último, `s[-2]` o penúltimo.

Um índice fora do texto é um erro de execução:

```cinza curto.cinza
fn main() {
  string s = "abc";
  print(s[3]);
}
```

```erro
curto.cinza:3:10: IndexError: índice 3 fora dos limites de uma string de tamanho 3
    em main (curto.cinza:3)
```

Qualquer caractere do Unicode conta como um, inclusive os que ficam fora do plano básico, como os
emojis:

```cinza
fn main() {
  string s = "olá 🙂!";
  print(s.size(), s[4], s[-1]);
}
```

```saida
6 🙂 !
```

> **Vindo de outra linguagem.** Em C, `strlen("coração")` é 9 (bytes); em Java e em JavaScript,
> `"🙂".length` é 2 (unidades UTF-16). Na Cinza é sempre o número de caracteres que você vê, como
> no Python 3.

## 6.2 Fatias

`s[ini:fim]` é o trecho de `ini` até `fim` (exclusive). Sem `ini`, desde o começo; sem `fim`, até o
fim. Um terceiro número é o passo, e um passo negativo anda para trás:

```cinza
fn main() {
  string s = "coração";
  print(s[0:4], s[4:], s[:3], s[-3:], s[::2], s[::-1], s[1:6:2]);
}
```

```saida
cora ção cor ção crço oãçaroc oaã
```

`s[::-1]` inverte o texto. Os índices das fatias também aceitam negativos (`s[-3:]` são os três
últimos).

**As fatias não ajustam os limites**: um trecho que passa do fim do texto é erro, não um texto mais
curto:

```cinza fatia.cinza
fn main() {
  string s = "abc";
  print(s[1:10]);
}
```

```erro
fatia.cinza:3:10: IndexError: fatia '[1:10]' fora dos limites de uma string de tamanho 3
    em main (fatia.cinza:3)
```

> **Vindo de outra linguagem.** No Python, `"abc"[1:10]` dá `"bc"` sem aviso — o que esconde o
> engano de um índice calculado errado. Na Cinza o trecho pedido precisa existir; para "até no
> máximo 10 caracteres", use `Strings.left(s, 10)`, que é explícito sobre isso.

Índices negativos e fatias são só de `string`. Numa lista o índice é sempre não negativo, e o
trecho de uma lista é `Lists.slice` (capítulo 7).

## 6.3 Textos são imutáveis

Uma `string` não muda depois de criada. Para "mudar" um texto, monte outro:

```cinza gato.cinza
fn main() {
  string s = "gato";
  s[0] = "p";
}
```

```erro
gato.cinza:3:4: SemanticError: Strings são imutáveis: 's[...]' não pode ser alterado. Monte um texto novo (concatenação, fatias, Strings.replace).
```

```cinza
fn main() {
  string s = "gato";
  string t = "p" + s[1:];
  print(s, t);
}
```

```saida
gato pato
```

Para montar um texto aos pedaços num laço, `+=` funciona; quando as partes já estão numa lista,
`Strings.join` diz melhor o que se quer:

```cinza
import Strings;
fn main() {
  string s = "";
  list<string> partes;
  for (i32 i in range(0, 5)) {
    s += cast<string>(i);
    partes.add(cast<string>(i));
  }
  print(s, Strings.join(partes, "-"));
}
```

```saida
01234 0-1-2-3-4
```

## 6.4 Literais de texto

Um texto se escreve **entre aspas duplas**, e os escapes são `\n` (quebra de linha), `\t`
(tabulação), `\r`, `\"` e `\\`:

```cinza
fn main() {
  print("coluna1\tcoluna2");
  print("ele disse \"oi\"");
  print("C:\\pasta\\arquivo");
}
```

```saida
coluna1	coluna2
ele disse "oi"
C:\pasta\arquivo
```

Um texto pode ocupar várias linhas: a quebra de linha faz parte dele.

```cinza
fn main() {
  string poema = "Batatinha quando nasce
se esparrama pelo chão";
  print(poema);
  print(poema.size());
}
```

```saida
Batatinha quando nasce
se esparrama pelo chão
45
```

Aspas simples não existem — nem para texto, nem para caractere:

```cinza aspas.cinza
fn main() {
  print('a');
}
```

```erro
aspas.cinza:2:9: LexicalError: Texto entre aspas simples ('a'): na Cinza os textos se escrevem entre aspas duplas ("a"), e um caractere é uma string de tamanho 1
```

Também não há escapes de código (`\u00e9`, `\x41`): escreva o caractere direto no texto — o arquivo
é UTF-8 —, ou crie-o pelo código com `Strings.chr(233)`.

## 6.5 Interpolação: `printf` e `format`

`printf` escreve um texto com expressões entre chaves substituídas pelo valor, e pula a linha;
`format` faz o mesmo e **devolve** o texto, em vez de escrever:

```cinza
fn main() {
  string nome = "Ana";
  i32 idade = 30;
  f64 altura = 1.6543ff;
  printf("{nome} tem {idade} anos e {altura:.2f} m");
  printf("ano que vem: {idade + 1}; letras: {nome.size()}; é Ana? {nome == \"Ana\"}");
  string etiqueta = format("R$ {altura * 10.ff:.2f}");
  print(etiqueta, etiqueta.size());
}
```

```saida
Ana tem 30 anos e 1.65 m
ano que vem: 31; letras: 3; é Ana? true
R$ 16.54 8
```

Dentro das chaves vale **qualquer expressão** — conta, chamada, método, comparação —, conferida na
compilação como qualquer outra. Um nome que não existe é erro antes de rodar, não um texto com
`{nome}` impresso:

```cinza ola.cinza
fn main() {
  printf("Olá, {nome}!");
}
```

```erro
ola.cinza:2:17: SemanticError: Variável 'nome' não declarada
```

Por isso o texto do `printf` precisa ser um **literal** entre aspas: o compilador lê as expressões
dele. Uma variável com o texto não serve.

### Formatos

Depois da expressão, um `:` e o formato:

| Formato | Efeito |
|---------|--------|
| `{x:.2f}` | exatamente 2 casas decimais, arredondando (metade se afasta do zero); só para números |
| `{x:8}` | largura mínima de 8 caracteres: números alinham à direita, o resto à esquerda |
| `{x:8.2f}` | as duas coisas |
| `{{` e `}}` | uma chave, escrita como está |

```cinza
fn main() {
  list<string> nomes = ["Ana", "Bernardo", "Cy"];
  list<f64> notas = [9.5ff, 7.25ff, 10.ff];
  for (i32 i in range(0, 3)) {
    printf("[{nomes[i]:10}] [{notas[i]:6.1f}]");
  }
  printf("{{chaves}} e {1 + 1}");
}
```

```saida
[Ana       ] [   9.5]
[Bernardo  ] [   7.3]
[Cy        ] [  10.0]
{chaves} e 2
```

O `7.25` com uma casa vira `7.3`: o arredondamento é o do número **como ele é escrito** (o capítulo
21 explica a diferença), e a metade se afasta do zero.

Não há preenchimento com zeros (o `%03d` do C): a mensagem diz como fazer.

```cinza zeros.cinza
fn main() {
  i32 n = 7;
  printf("{n:03}");
}
```

```erro
zeros.cinza:3:10: SemanticError: Formato inválido '03' em '{n:03}': a largura não começa com 0 (não há preenchimento com zeros; use Strings.pad_left(cast<string>(n), 5, "0")). Use a largura ({x:8}), as casas decimais ({x:.2f}) ou as duas ({x:8.2f})
```

## 6.6 O módulo `Strings`

As demais funções de texto ficam no módulo `Strings`, importado com `import Strings;` (ou com um
apelido: `import Strings as st;`). Elas não são métodos — e quem escreve `s.upper()` recebe a
indicação:

```cinza caixa.cinza
fn main() {
  string s = "abc";
  print(s.upper());
}
```

```erro
caixa.cinza:3:10: SemanticError: Método 'upper' não existe em 'string'. As funções de texto ficam no módulo Strings: Strings.upper(s), com import Strings;
```

> **Por quê?** A `string` tem um só método, `size()`, como as listas e os dicionários têm só os
> poucos que mexem na própria estrutura. O resto é função de módulo, com o texto como primeiro
> argumento: a biblioteca fica num lugar só, cada função tem uma página na spec, e nada se esconde
> num tipo embutido.

Como textos são imutáveis, **toda função devolve um texto novo**. As principais, por tarefa (a
lista completa está na [spec, seção 6.2](../spec/06-biblioteca.md)):

### Buscar

```cinza
import Strings as st;
fn main() {
  string s = "banana";
  print(st.contains(s, "nan"), st.starts_with(s, "ba"), st.ends_with(s, "na"));
  print(st.find(s, "na"), st.find_last(s, "na"), st.find(s, "x"), st.count(s, "a"), st.positions(s, "a"));
}
```

```saida
true true true
2 4 null 3 [1, 3, 5]
```

Repare no `null`: `find` devolve a posição **se achar**. O tipo dele é `i32?` — um `i32` que pode
faltar —, e o compilador não deixa usar o resultado como número sem resolver a falta antes:

```cinza busca.cinza
import Strings as st;
fn main() {
  string s = "a=b";
  i32 i = st.find(s, "=");
}
```

```erro
busca.cinza:4:7: SemanticError: Tipo incompatível na declaração de 'i': declarado 'i32', mas atribuído 'i32?'. O valor é opcional ('i32?'): resolva a ausência antes, com else (ex.: valor else padrão) ou com if (T v = valor) { ... }; um x != null não muda o tipo de x
```

O capítulo 11 é todo sobre isso. Por ora, as duas formas mais usadas: `else` dá um valor para o caso
de não achar, e `if` com uma declaração roda o bloco só quando achou:

```cinza
import Strings as st;
fn main() {
  string s = "chave=valor";
  if (i32 pos = st.find(s, "=")) {
    print("antes:", s[:pos], "depois:", s[pos + 1:]);
  }
  print(st.find(s, ":") else -1);
}
```

```saida
antes: chave depois: valor
-1
```

> **Por quê?** Em quase toda linguagem, "não achei" é um `-1` (C, Java, JavaScript) que parece
> uma posição — e `s[-1]`, na Cinza, é o último caractere. O `null` num tipo opcional não tem como
> ser confundido com uma resposta.

### Partir

```cinza
import Strings as st;
fn main() {
  print(st.split("a,b,,c", ","), st.words("  um dois  três "));
  print(st.lines("linha 1\nlinha 2\n"), st.chars("pé"));
  print(st.left("Cinza", 3), st.right("Cinza", 2), st.substr("coração", 2, 3), st.slice("coração", 2, 5));
}
```

```saida
["a", "b", "", "c"] ["um", "dois", "três"]
["linha 1", "linha 2"] ["p", "é"]
Cin za raç raç
```

- `split` mantém as partes vazias entre separadores seguidos; `words` separa por qualquer espaço em
  branco e não gera partes vazias.
- `lines` aceita `\n` e `\r\n`, e o `\n` do fim não gera uma linha vazia.
- `substr(s, início, tamanho)` e `slice(s, início, fim)` são o mesmo trecho dito de dois jeitos.

### Limpar e trocar

```cinza
import Strings as st;
fn main() {
  print("[" + st.trim("  oi  ") + "]", "[" + st.trim_start("  oi  ") + "]", "[" + st.trim_end("  oi  ") + "]");
  print(st.replace("a-b-c", "-", "+"), st.replace_first("a-b-c", "-", "+"), st.remove("a-b-c", "-"));
}
```

```saida
[oi] [oi  ] [  oi]
a+b+c a+b-c abc
```

### Montar

```cinza
import Strings as st;
fn main() {
  print(st.join(["a", "b", "c"], ", "), st.repeat("ab", 3), st.reverse("ação"));
  print("[" + st.pad_left("7", 3, "0") + "]", "[" + st.pad_right("ok", 5) + "]", "[" + st.center("x", 5, "*") + "]");
  print(st.fixed(3.14159ff, 2));
}
```

```saida
a, b, c ababab oãça
[007] [ok   ] [**x**]
3.14
```

`fixed(x, casas)` é o `{x:.2f}` como função: o número com exatamente essa quantidade de casas.

### Maiúsculas, testes e caracteres

```cinza
import Strings as st;
fn main() {
  print(st.upper("ação"), st.lower("ÁGUA"), st.capitalize("joão SILVA"), st.title("joão da silva"));
  print(st.is_digit("2026"), st.is_digit("-1"), st.is_alpha("ação"), st.is_space(" \t"), st.is_empty(""));
  print(st.ord("A"), st.chr(231), st.compare("a", "b"), st.equals_ignore_case("CINZA", "cinza"));
}
```

```saida
AÇÃO água João silva João Da Silva
true false true true true
65 ç -1 true
```

As letras que `upper`, `lower` e os testes conhecem são as do ASCII e as acentuadas do Latin-1 (`á`,
`ç`, `õ`, `Ü`...): o português inteiro, e a maior parte das línguas da Europa Ocidental.

### Quando o argumento não serve

Um argumento que não faz sentido é um erro, não um resultado vazio — um separador vazio, um trecho
que passa do fim do texto:

```cinza partes.cinza
import Strings as st;
fn main() {
  print(st.split("abc", ""));
}
```

```erro
partes.cinza:3:9: ValueError: o separador de 'split' não pode ser vazio
    em main (partes.cinza:3)
```

## 6.7 Comparar textos

`==` compara os caracteres, e `<`, `<=`, `>` e `>=` seguem a ordem dos code points (seção 3.2): as
maiúsculas antes das minúsculas, os acentuados depois de todos os sem acento. Para comparar sem
ligar para maiúsculas, `Strings.equals_ignore_case`, ou compare os dois em minúsculas:

```cinza
import Strings as st;
fn main() {
  print("Ana" < "ana", st.lower("Ana") == st.lower("ana"), "b" > "abc");
}
```

```saida
true true true
```

## Programa do capítulo: um recibo alinhado

O programa recebe um pedido como texto, uma linha por produto no formato `produto;quantidade;preço`,
e monta um recibo com as colunas alinhadas. Ele parte o texto, limpa os espaços, converte os
números e usa a largura dos formatos para alinhar:

```cinza recibo.cinza
// recibo.cinza: um recibo alinhado a partir de linhas "produto;quantidade;preço"
import Strings as st;

const string PEDIDO = "Café;2;7.50
Pão de queijo;6;3.25
  Suco de laranja ;1;9.90
Água;3;2.00";

fn main() {
  print(st.pad_right("Produto", 18) + st.pad_left("Qtd", 4) + st.pad_left("Preço", 9) + st.pad_left("Subtotal", 10));
  print(st.repeat("-", 41));
  f64 total = 0.ff;
  for (string linha in st.lines(PEDIDO)) {
    list<string> campos = st.split(linha, ";");
    string produto = st.trim(campos[0]);
    i32 qtd = cast<i32>(campos[1]);
    f64 preco = cast<f64>(campos[2]);
    f64 subtotal = cast<f64>(qtd) * preco;
    total += subtotal;
    printf("{produto:18}{qtd:4}{preco:9.2f}{subtotal:10.2f}");
  }
  print(st.repeat("-", 41));
  print(st.pad_right("Total", 31) + st.pad_left(st.fixed(total, 2), 10));
}
```

```saida
Produto            Qtd    Preço  Subtotal
-----------------------------------------
Café                 2     7.50     15.00
Pão de queijo        6     3.25     19.50
Suco de laranja      1     9.90      9.90
Água                 3     2.00      6.00
-----------------------------------------
Total                               50.40
```

Repare em:

- O pedido é uma constante de várias linhas, e `st.lines` o parte; `st.split(linha, ";")` separa
  os campos de cada uma.
- `st.trim` tira os espaços que sobraram em volta de `Suco de laranja`.
- Nos formatos, o texto (`{produto:18}`) alinha à esquerda e os números (`{qtd:4}`,
  `{preco:9.2f}`) à direita — o que uma tabela pede. Os títulos, que são texto, usam `pad_left`
  para alinhar à direita como os números abaixo deles.
- As larguras contam caracteres: `Pão de queijo` e `Água` alinham com o resto, apesar dos acentos.

## Resumo

- Uma `string` é UTF-8 e imutável; `size()`, os índices e as fatias contam caracteres. `s[i]` é uma
  `string`; índices negativos contam do fim; as fatias aceitam passo e não ajustam os limites.
- Os textos se escrevem entre aspas duplas, com `\n`, `\t`, `\r`, `\"` e `\\`, e podem ocupar
  várias linhas.
- `printf` e `format` substituem `{expressões}`, conferidas na compilação, com `{x:.2f}`, `{x:8}` e
  `{x:8.2f}`.
- O resto está no módulo `Strings`, e toda função devolve um texto novo. As buscas devolvem um
  opcional (`i32?`), resolvido com `else` ou com `if`.
- Argumentos que não fazem sentido são erros, não resultados vazios.

## Exercícios

1. Escreva `fn palindromo(string s) -> bool`, que diz se o texto é o mesmo de trás para frente,
   ignorando maiúsculas e espaços. Teste com `"Socorram me subi no onibus em Marrocos"`, `"ovo"` e
   `"Cinza"`.
2. Escreva `fn vogais(string s) -> i32`, que conta as vogais sem acento (`a e i o u`, maiúsculas ou
   minúsculas) de um texto, percorrendo-o com `for`.
3. Escreva `fn iniciais(string nome) -> string`, que transforma `"joão da silva"` em `"J. D. S."`.
4. Escreva `fn mascara(string cartao) -> string`, que troca todos os dígitos de um número de cartão
   por `*`, menos os quatro últimos: `"1234567812345678"` vira `"************5678"`.
5. Imprima a tabuada do 7, de 1 a 10, com `printf`, alinhada assim (os números à direita):

   ```text
    7 x  1 =   7
    7 x  2 =  14
   ...
    7 x 10 =  70
   ```

As soluções estão em [`solucoes/06-textos.md`](solucoes/06-textos.md).

## Na spec

- Os literais de texto e os escapes: [1.5](../spec/01-lexico.md)
- Índices, fatias e a imutabilidade: [5.7](../spec/05-execucao.md)
- `printf`, `format` e os formatos: [6.1](../spec/06-biblioteca.md)
- O módulo `Strings` e o que pode faltar: [6.2](../spec/06-biblioteca.md)
