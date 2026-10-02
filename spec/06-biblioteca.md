# 6. Biblioteca padrão

## 6.1 Funções embutidas

Disponíveis em qualquer arquivo, sem `import`. Uma função do programa com o mesmo nome tem
precedência.

| Função                                        | Descrição |
|-----------------------------------------------|-----------|
| `print(valores...)`                           | escreve os valores na saída, separados por um espaço, e pula linha. Aceita qualquer número de argumentos de qualquer tipo (nenhum: só pula linha). Usa a forma textual da seção 5.4. |
| `input(string prompt) -> string`              | escreve `prompt` sem pular linha e lê uma linha da entrada, sem o fim de linha. Fim da entrada lança `IOError`. |
| `range(int ini, int fim) -> list<int>`        | os inteiros de `ini` (inclusive) até `fim` (exclusive). |
| `range(int ini, int fim, int passo) -> list<int>` | idem, de `passo` em `passo`; passo negativo conta para baixo; passo `0` lança `ValueError`. |
| `type(x) -> type`                             | o tipo real de `x` (seção 3.9). |

Uma expressão `void` não pode ser argumento de nenhuma delas.

```cinza
fn main() {
  print();
  print(1, 2.5, "a", true, [1, 2]);
  print(range(0, 3), range(3, 0, -1), range(5, 0));
}
// expect:
// expect: 1 2.5 a true [1, 2]
// expect: [0, 1, 2] [3, 2, 1] []
```

## 6.2 Módulos nativos

Importados como qualquer módulo e usados pelo apelido:

```
import Math;
import Strings as st;
```

Nas assinaturas abaixo, **`número`** é `int` ou `decimal`, **`comparável`** é `int`, `decimal` ou
`string`, e **`T`** é qualquer tipo. Quando o mesmo nome aparece mais de uma vez numa assinatura,
os argumentos precisam ser do mesmo tipo (o primeiro fixa o tipo) e o retorno tem esse tipo.

Índices e tamanhos de texto contam **caracteres** (code points UTF-8), não bytes.

### `Strings`

| Função | Descrição |
|--------|-----------|
| `upper(string s) -> string` | maiúsculas (ASCII e letras acentuadas Latin-1: `ação` → `AÇÃO`) |
| `lower(string s) -> string` | minúsculas (idem) |
| `trim(string s) -> string` | sem espaços, tabulações e quebras de linha no início e no fim |
| `split(string s, string sep) -> list<string>` | partes separadas por `sep`; separadores seguidos geram partes vazias; `sep` vazio lança `ValueError` |
| `join(list<string> partes, string sep) -> string` | as partes unidas por `sep` |
| `contains(string s, string trecho) -> bool` | `true` se `trecho` aparece em `s` |
| `replace(string s, string de, string para) -> string` | troca todas as ocorrências; `de` vazio lança `ValueError` |
| `substr(string s, int inicio, int tamanho) -> string` | `tamanho` caracteres a partir de `inicio`; o trecho precisa caber em `s` (senão `IndexError`) |
| `find(string s, string trecho) -> int` | índice da primeira ocorrência, ou `-1` |
| `starts_with(string s, string prefixo) -> bool` | `true` se `s` começa com `prefixo` |

```cinza
import Strings as st;
fn main() {
  print(st.upper("ação"), st.trim("  a b  "), st.split("a,b,,c", ","));
  print(st.join(["x", "y"], "-"), st.replace("banana", "an", "AN"), st.substr("coração", 2, 3));
  print(st.find("coração", "ção"), st.find("abc", "z"), st.contains("abc", "b"), st.starts_with("cinza", "ci"));
}
// expect: AÇÃO a b ["a", "b", "", "c"]
// expect: x-y bANANa raç
// expect: 4 -1 true true
```

### `Files`

| Função | Descrição |
|--------|-----------|
| `read(string caminho) -> string` | o conteúdo inteiro do arquivo |
| `write(string caminho, string conteudo)` | grava o arquivo, substituindo o conteúdo anterior |

Caminhos relativos partem da pasta em que o interpretador foi executado. Não conseguir abrir,
ler ou gravar lança `IOError`.

### `Math`

| Nome | Descrição |
|------|-----------|
| `pi`, `e` | as constantes π e *e* (`decimal`) |
| `sqrt(decimal x) -> decimal` | raiz quadrada; `x` negativo lança `ValueError` |
| `pow(decimal b, decimal x) -> decimal` | `b` elevado a `x`; sem resultado real lança `ValueError`, resultado infinito lança `OverflowError` |
| `abs(número x) -> número` | valor absoluto; `abs` do menor `int` lança `OverflowError` |
| `floor(decimal x) -> int` | maior inteiro ≤ `x` |
| `ceil(decimal x) -> int` | menor inteiro ≥ `x` |
| `round(decimal x) -> int` | inteiro mais próximo; metade se afasta do zero (`2.5` → `3`, `-2.5` → `-3`) |
| `min(número a, número b) -> número`, `max(...)` | o menor / o maior |
| `sin(decimal x) -> decimal`, `cos(...)` | seno e cosseno (`x` em radianos) |
| `log(decimal x) -> decimal` | logaritmo natural; `x` ≤ 0 lança `ValueError` |

`floor`, `ceil` e `round` lançam `OverflowError` se o resultado não couber em `int`. Argumentos
`int` em parâmetros `decimal` são convertidos.

```cinza
import Math;
fn main() {
  print(Math.sqrt(16.0), Math.pow(2, 10), Math.abs(-3), Math.abs(-2.5));
  print(Math.floor(2.7), Math.ceil(2.1), Math.round(2.5), Math.round(-2.5), Math.min(3, 7));
  print(Math.pi);
}
// expect: 4 1024 3 2.5
// expect: 2 3 3 -3 3
// expect: 3.141592653589793
```

### `Random`

| Função | Descrição |
|--------|-----------|
| `seed(int s)` | reinicia o gerador; a mesma semente produz a mesma sequência (na mesma implementação) |
| `int(int a, int b) -> int` | inteiro de `a` até `b`, **os dois inclusive**; `a > b` lança `ValueError` |
| `decimal() -> decimal` | de `0.0` (inclusive) até `1.0` (exclusive) |
| `choice(list<T> l) -> T` | um elemento de `l`; lista vazia lança `IndexError` |

A sequência exata gerada para uma semente não faz parte da especificação.

```cinza
import Random;
fn main() {
  Random.seed(7);
  int a = Random.int(1, 100);
  Random.seed(7);
  print(a == Random.int(1, 100), Random.int(5, 5));
}
// expect: true 5
```

### `Convert`

| Função | Descrição |
|--------|-----------|
| `to_int(string s) -> int` | sinal opcional (`+` ou `-`) seguido de dígitos, sem espaços; texto inválido ou fora da faixa de `int` lança `ValueError` |
| `to_decimal(string s) -> decimal` | `[sinal] dígitos [. dígitos] [e [sinal] dígitos]`; texto inválido ou fora da faixa lança `ValueError` |
| `to_string(T x) -> string` | a forma textual da seção 5.4 |
| `to_bool(string s) -> bool` | só `"true"` ou `"false"`; outro texto lança `ValueError` |

`to_decimal` aceita expoente (`"1e2"`), embora os literais da linguagem não aceitem.

```cinza
import Convert as cv;
fn main() {
  print(cv.to_int("42") + 1, cv.to_decimal("2.5"), cv.to_string(10) + "!", cv.to_bool("true"));
  try { print(cv.to_int("12a")); } except (ValueError e) { print(e.message); }
}
// expect: 43 2.5 10! true
// expect: '12a' não é um int válido
```

### `Lists`

| Função | Descrição |
|--------|-----------|
| `sort(list<comparável> l)` | ordena `l` no lugar, em ordem crescente (estável) |
| `reverse(list<T> l)` | inverte `l` no lugar |
| `contains(list<T> l, T x) -> bool` | `true` se algum elemento é igual a `x` |
| `index_of(list<T> l, T x) -> int` | índice do primeiro elemento igual a `x`, ou `-1` |
| `slice(list<T> l, int ini, int fim) -> list<T>` | **nova** lista com os elementos de `ini` até `fim` (exclusive); exige `0 ≤ ini ≤ fim ≤ tamanho` (senão `IndexError`) |
| `sum(list<número> l) -> número` | soma; lista vazia dá `0`; estouro lança `OverflowError` |

`sort` e `reverse` alteram a lista: são erro numa lista `const` e numa coleção de `op` de tipo
desconhecido (seção 3.7).

```cinza
import Lists as L;
fn main() {
  list<int> n = [5, 3, 9, 1];
  L.sort(n);
  print(n, L.contains(n, 9), L.index_of(n, 5), L.slice(n, 1, 3), L.sum(n));
  L.reverse(n);
  print(n);
}
// expect: [1, 3, 5, 9] true 2 [3, 5] 18
// expect: [9, 5, 3, 1]
```
