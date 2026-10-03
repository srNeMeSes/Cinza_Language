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

Cada função faz a operação inteira: não há arquivo "aberto". Caminhos relativos partem da pasta
em que o programa foi executado; caminhos devolvidos usam `/` como separador em qualquer sistema.
Toda falha lança `IOError` — nada falha em silêncio: apagar o que não existe, copiar por cima de
algo existente sem pedir ou apagar uma pasta com conteúdo pela função errada são erros.

**Ler e gravar**

| Função | Descrição |
|--------|-----------|
| `read(string caminho) -> string` | o conteúdo inteiro do arquivo |
| `lines(string caminho) -> list<string>` | as linhas, sem o fim de linha (`\n` ou `\r\n`); o `\n` no fim do arquivo não gera uma linha vazia; arquivo vazio dá `[]` |
| `write(string caminho, string conteudo)` | grava o arquivo, substituindo o conteúdo anterior |
| `append(string caminho, string conteudo)` | acrescenta ao fim; cria o arquivo se não existir |
| `write_lines(string caminho, list<string> linhas)` | grava cada linha seguida de `\n`, substituindo o conteúdo anterior |
| `append_line(string caminho, string linha)` | acrescenta a linha seguida de `\n`; cria o arquivo se não existir |

**Consultar**

| Função | Descrição |
|--------|-----------|
| `exists(string caminho) -> bool` | `true` se existe (arquivo ou pasta) |
| `is_file(string caminho) -> bool` | `true` se é um arquivo |
| `is_dir(string caminho) -> bool` | `true` se é uma pasta |
| `size(string caminho) -> int` | tamanho do arquivo em **bytes** (não em caracteres) |

**Arquivos e pastas**

| Função | Descrição |
|--------|-----------|
| `copy(string origem, string destino, bool substituir)` | copia um arquivo; destino existente só é trocado com `substituir` = `true`, e nunca se for uma pasta |
| `move(string origem, string destino, bool substituir)` | move ou renomeia um arquivo ou uma pasta; mesma regra do destino |
| `delete(string caminho)` | apaga um arquivo (uma pasta é erro) |
| `make_dir(string caminho)` | cria a pasta e as intermediárias; já existir não é erro (um arquivo com o nome é) |
| `list_dir(string caminho) -> list<string>` | os nomes do que há na pasta, em ordem alfabética |
| `delete_dir(string caminho)` | apaga uma pasta **vazia** |
| `delete_tree(string caminho)` | apaga uma pasta e todo o seu conteúdo |
| `current_dir() -> string` | a pasta em que o programa foi executado (caminho completo) |

**Caminhos** — só manipulam o texto, sem consultar o disco:

| Função | Descrição |
|--------|-----------|
| `join(string a, string b) -> string` | `a` e `b` unidos por `/` (`b` absoluto substitui `a`) |
| `name(string caminho) -> string` | o último componente: `"dados/a.txt"` → `"a.txt"` |
| `extension(string caminho) -> string` | a extensão com o ponto: `"a.tar.gz"` → `".gz"`; sem extensão, `""` |
| `parent(string caminho) -> string` | o caminho sem o último componente: `"dados/a.txt"` → `"dados"`; sem pasta, `""` |
| `absolute(string caminho) -> string` | o caminho completo, com `.` e `..` resolvidos |

```cinza
import Files;
fn main() {
  str d = "tests/_tmp_spec_files";
  if (Files.exists(d)) { Files.delete_tree(d); }
  Files.make_dir(d);
  str a = Files.join(d, "lista.txt");
  Files.write_lines(a, ["pão", "leite"]);
  Files.append_line(a, "café");
  print(Files.lines(a), Files.size(a));
  Files.copy(a, Files.join(d, "copia.txt"), false);
  print(Files.list_dir(d), Files.extension(a), Files.name(a));
  try { Files.delete_dir(d); } except (IOError e) { print(e.message); }
  Files.delete_tree(d);
  print(Files.exists(d));
}
// expect: ["pão", "leite", "café"] 17
// expect: ["copia.txt", "lista.txt"] .txt lista.txt
// expect: a pasta não está vazia (use delete_tree): 'tests/_tmp_spec_files'
// expect: false
```

### `Math`

Nenhuma função produz infinito nem NaN: fora do domínio lança `ValueError`; resultado grande
demais lança `OverflowError`. Argumentos `int` em parâmetros `decimal` são convertidos.

**Constantes** (`decimal`): `pi` (π), `e`, `tau` (2π).

**Potências, raízes e logaritmos**

| Função | Descrição |
|--------|-----------|
| `sqrt(decimal x) -> decimal` | raiz quadrada; `x` negativo lança `ValueError` |
| `cbrt(decimal x) -> decimal` | raiz cúbica (aceita negativos: `cbrt(-8)` = `-2`) |
| `pow(decimal b, decimal x) -> decimal` | `b` elevado a `x`; sem resultado real lança `ValueError` |
| `exp(decimal x) -> decimal` | *e* elevado a `x` |
| `log(decimal x) -> decimal` | logaritmo natural |
| `log10(decimal x) -> decimal`, `log2(...)` | logaritmo na base 10 / na base 2 |
| `log_base(decimal x, decimal base) -> decimal` | logaritmo na base dada; a base precisa ser positiva e diferente de 1 |
| `hypot(decimal x, decimal y) -> decimal` | √(x² + y²), sem estourar no meio do cálculo |

Os logaritmos lançam `ValueError` para `x` ≤ 0.

**Trigonometria** (ângulos em radianos)

| Função | Descrição |
|--------|-----------|
| `sin`, `cos`, `tan(decimal x) -> decimal` | seno, cosseno, tangente |
| `asin`, `acos(decimal x) -> decimal` | arco seno e arco cosseno; `x` fora de -1 a 1 lança `ValueError` |
| `atan(decimal x) -> decimal` | arco tangente |
| `atan2(decimal y, decimal x) -> decimal` | ângulo do ponto (x, y), de -π a π (note a ordem: `y` primeiro) |
| `sinh`, `cosh`, `tanh(decimal x) -> decimal` | funções hiperbólicas |
| `degrees(decimal rad) -> decimal`, `radians(decimal graus)` | conversão entre radianos e graus |

**Arredondamento**

| Função | Descrição |
|--------|-----------|
| `floor(decimal x) -> int` | maior inteiro ≤ `x` |
| `ceil(decimal x) -> int` | menor inteiro ≥ `x` |
| `round(decimal x) -> int` | inteiro mais próximo; metade se afasta do zero (`2.5` → `3`, `-2.5` → `-3`) |
| `trunc(decimal x) -> int` | corta as casas, em direção ao zero (`-2.7` → `-2`) |
| `round_to(decimal x, int casas) -> decimal` | arredonda para `casas` casas decimais (metade se afasta do zero); `casas` negativo lança `ValueError` |

`floor`, `ceil`, `round` e `trunc` lançam `OverflowError` se o resultado não couber em `int`.

`round_to` arredonda o número **como ele é escrito** (a forma da seção 5.4), não a sua representação
binária: `round_to(2.675, 2)` é `2.68`, embora `2.675` não seja exato em binário. O resultado é um
`decimal`, mostrado pela forma da seção 5.4: `round_to(2.5, 2)` é `2.5`, não `2.50`.

**Inteiros e sinais**

| Função | Descrição |
|--------|-----------|
| `abs(número x) -> número` | valor absoluto; `abs` do menor `int` lança `OverflowError` |
| `sign(número x) -> int` | `-1`, `0` ou `1` |
| `min(número a, número b) -> número`, `max(...)` | o menor / o maior (para uma lista, `Lists`) |
| `clamp(número x, número min, número max) -> número` | `x` limitado ao intervalo; `min > max` lança `ValueError` |
| `gcd(int a, int b) -> int`, `lcm(...)` | máximo divisor comum / mínimo múltiplo comum, sempre ≥ 0; `gcd(0, 0)` = `0`, `lcm` com zero = `0` |
| `is_even(int n) -> bool`, `is_odd(...)` | par / ímpar |
| `factorial(int n) -> int` | n!; negativo lança `ValueError`; acima de `20` não cabe em `int` (`OverflowError`) |
| `is_prime(int n) -> bool` | `true` se `n` é primo (exato em toda a faixa de `int`; menores que 2 não são) |

**Comparar decimais**

| Função | Descrição |
|--------|-----------|
| `is_close(decimal a, decimal b) -> bool` | iguais a menos de erro de arredondamento: diferença relativa até 10⁻⁹, ou absoluta até 10⁻¹² (perto de zero). `0.1 + 0.2 == 0.3` é `false`; `is_close(0.1 + 0.2, 0.3)` é `true` |

```cinza
import Math;
fn main() {
  print(Math.sqrt(16.0), Math.pow(2, 10), Math.abs(-3), Math.abs(-2.5));
  print(Math.floor(2.7), Math.ceil(2.1), Math.round(2.5), Math.round(-2.5), Math.min(3, 7));
  print(Math.pi);
  print(Math.round_to(2.675, 2), Math.trunc(-2.7), Math.hypot(3, 4), Math.log_base(8, 2));
  print(Math.gcd(12, 18), Math.lcm(4, 6), Math.factorial(5), Math.is_prime(97), Math.clamp(15, 0, 10));
  print(0.1 + 0.2 == 0.3, Math.is_close(0.1 + 0.2, 0.3));
}
// expect: 4 1024 3 2.5
// expect: 2 3 3 -3 3
// expect: 3.141592653589793
// expect: 2.68 -2 5 3
// expect: 6 12 120 true 10
// expect: false true
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
