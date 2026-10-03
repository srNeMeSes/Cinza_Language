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

Strings são imutáveis: toda função devolve um texto novo. **Letras** são as do ASCII e as
acentuadas do Latin-1 (`á`, `ç`, `õ`, `Ü`...); **espaço em branco** é espaço, tabulação e
quebras de linha (`\t \n \r \f \v`).

**Buscar**

| Função | Descrição |
|--------|-----------|
| `contains(string s, string trecho) -> bool` | `true` se `trecho` aparece em `s` |
| `starts_with(string s, string prefixo) -> bool`, `ends_with(...)` | `true` se `s` começa / termina com o trecho |
| `find(string s, string trecho) -> int` | índice da primeira ocorrência, ou `-1` |
| `find_last(string s, string trecho) -> int` | índice da última ocorrência, ou `-1` |
| `find_from(string s, string trecho, int inicio) -> int` | como `find`, a partir do índice `inicio` (de `0` ao tamanho; senão `IndexError`) |
| `count(string s, string trecho) -> int` | ocorrências sem sobreposição (`count("aaaa", "aa")` = `2`); trecho vazio lança `ValueError` |

**Partes**

| Função | Descrição |
|--------|-----------|
| `char_at(string s, int i) -> string` | o caractere do índice `i` (`IndexError` se inválido) |
| `substr(string s, int inicio, int tamanho) -> string` | `tamanho` caracteres a partir de `inicio`; o trecho precisa caber em `s` (senão `IndexError`) |
| `slice(string s, int ini, int fim) -> string` | de `ini` até `fim` (exclusive); exige `0 ≤ ini ≤ fim ≤ tamanho` (senão `IndexError`) |
| `left(string s, int n) -> string`, `right(...)` | os `n` primeiros / últimos caracteres; `n` maior que o tamanho dá o texto todo; `n` negativo lança `ValueError` |
| `chars(string s) -> list<string>` | os caracteres, um por elemento |
| `split(string s, string sep) -> list<string>` | partes separadas por `sep`; separadores seguidos geram partes vazias; `sep` vazio lança `ValueError` |
| `lines(string s) -> list<string>` | as linhas, como `Files.lines` (`\n` ou `\r\n`; o `\n` final não gera linha vazia) |
| `words(string s) -> list<string>` | as palavras, separadas por qualquer espaço em branco, sem partes vazias |

**Limpar e trocar**

| Função | Descrição |
|--------|-----------|
| `trim(string s) -> string` | sem espaço em branco no início e no fim |
| `trim_start(string s) -> string`, `trim_end(...)` | só no início / só no fim |
| `replace(string s, string de, string para) -> string` | troca todas as ocorrências |
| `replace_first(string s, string de, string para) -> string` | troca só a primeira |
| `remove(string s, string trecho) -> string` | apaga todas as ocorrências |

O trecho procurado por `replace`, `replace_first` e `remove` não pode ser vazio (`ValueError`).

**Montar**

| Função | Descrição |
|--------|-----------|
| `join(list<string> partes, string sep) -> string` | as partes unidas por `sep` |
| `repeat(string s, int n) -> string` | `s` repetido `n` vezes; `n` negativo, ou resultado acima de 1 GiB, lança `ValueError` |
| `reverse(string s) -> string` | os caracteres em ordem inversa |
| `pad_left(string s, int largura[, string p]) -> string` | completa à esquerda com `p` (padrão: espaço) até `largura` caracteres; texto já maior fica como está |
| `pad_right(string s, int largura[, string p]) -> string` | idem, à direita |
| `center(string s, int largura[, string p]) -> string` | idem, dos dois lados (a sobra ímpar fica à direita) |
| `fixed(decimal x, int casas) -> string` | o número com **exatamente** `casas` casas decimais (`fixed(2.5, 2)` → `"2.50"`); arredonda como `Math.round_to`; `casas` de 0 a 100 (senão `ValueError`) |

O preenchimento `p` precisa ter exatamente 1 caractere (senão `ValueError`).

**Maiúsculas e minúsculas**

| Função | Descrição |
|--------|-----------|
| `upper(string s) -> string`, `lower(...)` | maiúsculas / minúsculas (`ação` → `AÇÃO`) |
| `capitalize(string s) -> string` | primeira letra maiúscula, o resto minúsculo (`"ana MARIA"` → `"Ana maria"`) |
| `title(string s) -> string` | cada palavra com a primeira letra maiúscula e o resto minúsculo (`"ana maria"` → `"Ana Maria"`) |

**Testar** — o texto inteiro; string vazia dá `false` (menos em `is_empty`)

| Função | Descrição |
|--------|-----------|
| `is_empty(string s) -> bool` | `true` se não tem caracteres |
| `is_digit(string s) -> bool` | só dígitos `0`–`9` (`"-1"` não é) |
| `is_alpha(string s) -> bool`, `is_alnum(...)` | só letras / só letras e dígitos |
| `is_space(string s) -> bool` | só espaço em branco |
| `is_upper(string s) -> bool`, `is_lower(...)` | tem letras, todas maiúsculas / minúsculas (`"ABC 1"` é maiúsculo) |

**Caracteres e comparação**

| Função | Descrição |
|--------|-----------|
| `ord(string c) -> int` | o código Unicode de um único caractere (`ord("A")` = `65`); outro tamanho lança `ValueError` |
| `chr(int n) -> string` | o caractere do código Unicode `n`; código inválido lança `ValueError` |
| `equals_ignore_case(string a, string b) -> bool` | iguais sem diferenciar maiúsculas de minúsculas |
| `compare(string a, string b) -> int` | `-1`, `0` ou `1`, na ordem de `<` entre strings |

```cinza
import Strings as st;
fn main() {
  print(st.upper("ação"), st.trim("  a b  "), st.split("a,b,,c", ","));
  print(st.join(["x", "y"], "-"), st.replace("banana", "an", "AN"), st.substr("coração", 2, 3));
  print(st.find("coração", "ção"), st.find("abc", "z"), st.contains("abc", "b"), st.starts_with("cinza", "ci"));
  print(st.char_at("ação", 1), st.words("  ola   mundo "), st.title("ana maria"), st.count("banana", "a"));
  print("R$ " + st.fixed(2.5, 2), st.pad_left("7", 3, "0"), "[" + st.center("ok", 6) + "]", st.ord("A"), st.chr(231));
}
// expect: AÇÃO a b ["a", "b", "", "c"]
// expect: x-y bANANa raç
// expect: 4 -1 true true
// expect: ç ["ola", "mundo"] Ana Maria 3
// expect: R$ 2.50 007 [  ok  ] 65 ç
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
| `decimal_range(decimal a, decimal b) -> decimal` | de `a` (inclusive) até `b` (exclusive); `a` ≥ `b` lança `ValueError` |
| `bool() -> bool` | `true` ou `false`, com a mesma chance |
| `chance(decimal p) -> bool` | `true` com probabilidade `p`, de `0` (nunca) a `1` (sempre); fora disso, `ValueError` |
| `gauss(decimal media, decimal desvio) -> decimal` | distribuição normal; desvio `0` devolve a média; desvio negativo lança `ValueError` |
| `choice(list<T> l) -> T` | um elemento de `l`; lista vazia lança `IndexError` |
| `choices(list<T> l, int k) -> list<T>` | `k` sorteios **com** repetição (`k` pode passar do tamanho); com `k` > 0, lista vazia lança `IndexError` |
| `sample(list<T> l, int k) -> list<T>` | `k` elementos de posições **distintas**, em ordem aleatória; `k` maior que o tamanho lança `ValueError` |
| `shuffle(list<T> l)` | embaralha `l` no lugar |

`k` negativo lança `ValueError`; `k` = 0 dá `[]`, mesmo com lista vazia. `shuffle` altera a
lista: é erro numa lista `const` e numa coleção de `op` de tipo desconhecido (seção 3.7), como
`Lists.sort`. A sequência exata gerada para uma semente não faz parte da especificação.

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

**Converter texto** — estrito: nada de espaços, vírgula decimal ou prefixos

| Função | Descrição |
|--------|-----------|
| `to_int(string s) -> int` | sinal opcional (`+` ou `-`) seguido de dígitos; texto inválido ou fora da faixa de `int` lança `ValueError` |
| `to_decimal(string s) -> decimal` | `[sinal] dígitos [. dígitos] [e [sinal] dígitos]`; texto inválido ou grande demais para `decimal` lança `ValueError`; pequeno demais vira `0` |
| `to_bool(string s) -> bool` | só `"true"` ou `"false"`; outro texto lança `ValueError` |
| `to_string(T x) -> string` | a forma textual da seção 5.4 |

`to_decimal` aceita expoente (`"1e2"`), embora os literais da linguagem não aceitem. Para aceitar
vírgula decimal, troque-a antes: `to_decimal(Strings.replace(s, ",", "."))`.

**Validar e converter com padrão** — aceitam exatamente os mesmos textos que `to_int`,
`to_decimal` e `to_bool`, sem lançar erro

| Função | Descrição |
|--------|-----------|
| `is_int(string s) -> bool`, `is_decimal(...)`, `is_bool(...)` | `true` se o `to_*` correspondente aceitaria o texto |
| `to_int_or(string s, int padrao) -> int` | o valor convertido, ou `padrao` se o texto não for aceito |
| `to_decimal_or(string s, decimal padrao) -> decimal` | idem |
| `to_bool_or(string s, bool padrao) -> bool` | idem |

**Bases numéricas** — dígitos `0`–`9` e `a`–`z`, sem prefixo (`0x`, `0b`); negativo com `-`

| Função | Descrição |
|--------|-----------|
| `to_base(int n, int base) -> string` | `n` na base dada, de 2 a 36 (senão `ValueError`), em minúsculas |
| `to_hex(int n) -> string`, `to_binary(...)`, `to_octal(...)` | atalhos para as bases 16, 2 e 8 |
| `from_base(string s, int base) -> int` | o inverso: sinal opcional e dígitos da base (maiúsculas ou minúsculas); dígito inválido ou fora da faixa de `int` lança `ValueError` |

```cinza
import Convert as cv;
fn main() {
  print(cv.to_int("42") + 1, cv.to_decimal("2.5"), cv.to_string(10) + "!", cv.to_bool("true"));
  try { print(cv.to_int("12a")); } except (ValueError e) { print(e.message); }
  print(cv.is_int("12a"), cv.to_int_or("12a", 0), cv.to_int_or("12", 0));
  print(cv.to_hex(255), cv.to_binary(5), cv.from_base("ff", 16), cv.to_base(35, 36));
}
// expect: 43 2.5 10! true
// expect: '12a' não é um int válido
// expect: false 0 12
// expect: ff 101 255 z
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
