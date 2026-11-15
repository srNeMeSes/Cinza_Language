# 6. Biblioteca padrão

## 6.1 Funções embutidas

Disponíveis em qualquer arquivo, sem `import`. Uma função do programa com o mesmo nome tem
precedência.

| Função                                        | Descrição |
|-----------------------------------------------|-----------|
| `print(valores...)`                           | escreve os valores na saída, separados por um espaço, e pula linha. Aceita qualquer número de argumentos de qualquer tipo (nenhum: só pula linha). Usa a forma textual da seção 5.4. |
| `printf("texto")`                             | escreve o texto com as expressões `{...}` substituídas (abaixo) e pula linha, como `print` |
| `format("texto") -> string`                   | o mesmo texto do `printf`, devolvido em vez de escrito |
| `input(string prompt) -> string`              | escreve `prompt` sem pular linha e lê uma linha da entrada, sem o fim de linha. Fim da entrada lança `IOError`. |
| `range(inteiro ini, inteiro fim) -> list<inteiro>` | os inteiros de `ini` (inclusive) até `fim` (exclusive), no tipo dos argumentos: `range(0, 10)` é `list<i32>`; com um `i64`, `list<i64>`. |
| `range(inteiro ini, inteiro fim, inteiro passo) -> list<inteiro>` | idem, de `passo` em `passo`; passo negativo conta para baixo; passo `0` lança `ValueError`. Como lista, no máximo 100 milhões de elementos (senão `ValueError`); no `for` a lista não é criada e não há limite. |
| `type(x) -> type`                             | o tipo real de `x` (seção 3.8). |
| `exit([inteiro codigo])`                      | encerra o programa com o código de saída (padrão `0`, sucesso; de `0` a `255`, senão `ValueError`), depois de rodar os `finally` pendentes (abaixo). |

Uma expressão `void` não pode ser argumento de nenhuma delas.

```cinza
fn main() {
  print();
  print(1, 2.5ff, "a", true, [1, 2]);
  print(range(0, 3), range(3, 0, -1), range(5, 0));
}
// expect:
// expect: 1 2.5 a true [1, 2]
// expect: [0, 1, 2] [3, 2, 1] []
```

### `exit`

`exit(codigo)` encerra o programa de qualquer ponto — inclusive de dentro de funções chamadas pela
`main`. O que já foi escrito na saída fica. Antes de encerrar, rodam os `finally` pendentes, do
mais interno para fora, em todas as funções da pilha; **nenhum `except` o captura**, nem
`except (Error e)`. Se um `finally` lançar um erro, esse erro segue como qualquer outro.

Para a análise de fluxo, `exit(...)` encerra o caminho como `throw`: uma função com retorno pode
terminar num `exit` sem `return` depois, e uma instrução logo depois de `exit(...)` é código
inalcançável (erro de compilação, seção 5.5). Como `print`, uma função do programa chamada `exit`
tem precedência — e é uma chamada comum.

```cinza
fn le_idade(i64 n) -> i64 {
  if (n >= 0) { return n; }
  print("idade inválida:", n);
  exit(2);
}
fn main() {
  try {
    print(le_idade(30));
    print(le_idade(-1));
  } finally {
    print("limpeza");
  }
}
// expect: 30
// expect: idade inválida: -1
// expect: limpeza
// expect-exit: 2
```

### Interpolação: `printf` e `format`

O argumento de `printf` e `format` é **um texto literal** entre aspas (uma variável não serve: o
texto é analisado na compilação). Cada `{expr}` dentro dele é substituído pelo valor de `expr`,
na forma textual da seção 5.4. `expr` é **qualquer expressão** — variável, conta, chamada, campo,
índice — conferida na compilação como as demais: um nome inexistente ou um erro de tipo é erro
de compilação, não de execução. As expressões são avaliadas da esquerda para a direita.

Depois da expressão, um formato opcional após `:`:

| Formato | Efeito |
|---------|--------|
| `{x:.2f}` | exatamente 2 casas decimais, arredondando como `Math.round_to`; só para números (outro tipo é erro de compilação); `f32` arredonda a forma decimal do próprio `f32` |
| `{x:8}` | largura mínima de 8 caracteres: números alinham à direita, o resto à esquerda; texto mais longo fica inteiro |
| `{x:8.2f}` | os dois |

Largura de até 1000 e de 0 a 100 casas. Largura começando com `0` (`{n:05}`) é erro de
compilação. `{{` e `}}` escrevem uma chave; uma chave sem par, `{}` vazio, uma expressão
`void` ou mal escrita são erros de compilação.

Como `print`, uma função do programa chamada `printf` ou `format` tem precedência — e recebe o
texto sem interpolação.

```cinza
fn main() {
  string nome = "Ana";
  i64 idade = 30;
  f64 preco = 2.5ff;
  printf("Olá, {nome}! Você tem {idade} anos; ano que vem, {idade + 1}.");
  printf("R$ {preco:.2f} | [{idade:5}] [{nome:5}] | {{chaves}}");
  string s = format("{nome.size()} letras");
  print(s);
}
// expect: Olá, Ana! Você tem 30 anos; ano que vem, 31.
// expect: R$ 2.50 | [   30] [Ana  ] | {chaves}
// expect: 3 letras
```

## 6.2 Módulos nativos

Importados como qualquer módulo e usados pelo apelido:

```
import Math;
import Strings as st;
```

Nas assinaturas abaixo, **`número`** é qualquer tipo numérico, **`inteiro`** é `i32` ou `i64`,
**`flutuante`** é `f32` ou `f64`, **`comparável`** é um número ou `string`, e **`T`** é qualquer
tipo. Quando o mesmo nome aparece mais de uma vez numa assinatura, os argumentos precisam ser da
mesma família e o mais estreito é alargado (`Math.max(i32, i64)` é `i64`); o retorno tem esse
tipo. Um parâmetro `i64` aceita `i32` (seção 3.5); `f64` aceita `f32`.

Posições, tamanhos e contagens (`find`, `count`, `index_of`, `ord`, `compare`, `size()`...) são
`i32`; grandezas que pedem 64 bits (`Files.size`, os instantes de `Time`) são `i64`.

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
| `find(string s, string trecho) -> i32` | índice da primeira ocorrência, ou `-1` |
| `find_last(string s, string trecho) -> i32` | índice da última ocorrência, ou `-1` |
| `find_from(string s, string trecho, i64 inicio) -> i32` | como `find`, a partir do índice `inicio` (de `0` ao tamanho; senão `IndexError`) |
| `count(string s, string trecho) -> i32` | ocorrências sem sobreposição (`count("aaaa", "aa")` = `2`); trecho vazio lança `ValueError` |

**Partes**

| Função | Descrição |
|--------|-----------|
| `char_at(string s, i64 i) -> string` | o caractere do índice `i` (`IndexError` se inválido) |
| `substr(string s, i64 inicio, i64 tamanho) -> string` | `tamanho` caracteres a partir de `inicio`; o trecho precisa caber em `s` (senão `IndexError`) |
| `slice(string s, i64 ini, i64 fim) -> string` | de `ini` até `fim` (exclusive); exige `0 ≤ ini ≤ fim ≤ tamanho` (senão `IndexError`) |
| `left(string s, i64 n) -> string`, `right(...)` | os `n` primeiros / últimos caracteres; `n` maior que o tamanho dá o texto todo; `n` negativo lança `ValueError` |
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
| `repeat(string s, i64 n) -> string` | `s` repetido `n` vezes; `n` negativo, ou resultado acima de 1 GiB, lança `ValueError` |
| `reverse(string s) -> string` | os caracteres em ordem inversa |
| `pad_left(string s, i64 largura[, string p]) -> string` | completa à esquerda com `p` (padrão: espaço) até `largura` caracteres; texto já maior fica como está; largura acima de 2²⁸ (1 GiB) lança `ValueError` |
| `pad_right(string s, i64 largura[, string p]) -> string` | idem, à direita |
| `center(string s, i64 largura[, string p]) -> string` | idem, dos dois lados (a sobra ímpar fica à direita) |
| `fixed(flutuante x, i64 casas) -> string` | o número com **exatamente** `casas` casas decimais (`fixed(2.5ff, 2)` → `"2.50"`); arredonda como `Math.round_to`; `casas` de 0 a 100 (senão `ValueError`); `nan` e infinito saem `nan`, `inf`, `-inf` |

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
| `ord(string c) -> i32` | o código Unicode de um único caractere (`ord("A")` = `65`); outro tamanho lança `ValueError` |
| `chr(i64 n) -> string` | o caractere do código Unicode `n`; código inválido lança `ValueError` |
| `equals_ignore_case(string a, string b) -> bool` | iguais sem diferenciar maiúsculas de minúsculas |
| `compare(string a, string b) -> i32` | `-1`, `0` ou `1`, na ordem de `<` entre strings |

```cinza
import Strings as st;
fn main() {
  print(st.upper("ação"), st.trim("  a b  "), st.split("a,b,,c", ","));
  print(st.join(["x", "y"], "-"), st.replace("banana", "an", "AN"), st.substr("coração", 2, 3));
  print(st.find("coração", "ção"), st.find("abc", "z"), st.contains("abc", "b"), st.starts_with("cinza", "ci"));
  print(st.char_at("ação", 1), st.words("  ola   mundo "), st.title("ana maria"), st.count("banana", "a"));
  print("R$ " + st.fixed(2.5ff, 2), st.pad_left("7", 3, "0"), "[" + st.center("ok", 6) + "]", st.ord("A"), st.chr(231));
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
| `read_bytes(string caminho) -> list<i32>` | o conteúdo como bytes, cada um de `0` a `255` (sem interpretar como texto) — para formatos binários |
| `write_bytes(string caminho, list<inteiro> bytes)` | grava os bytes, substituindo o conteúdo anterior; valor fora de `0` a `255` lança `ValueError` |

**Consultar**

| Função | Descrição |
|--------|-----------|
| `exists(string caminho) -> bool` | `true` se existe (arquivo ou pasta) |
| `is_file(string caminho) -> bool` | `true` se é um arquivo |
| `is_dir(string caminho) -> bool` | `true` se é uma pasta |
| `size(string caminho) -> i64` | tamanho do arquivo em **bytes** (não em caracteres) |

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
| `modified(string caminho) -> i64` | o instante da última modificação (um instante de `Time`) |

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

As funções de ponto flutuante seguem o **IEEE 754** e a **largura do argumento**: `sqrt(2.f)` é
`f32`, `sqrt(2.ff)` é `f64` (a conta é feita em `f64` e arredondada uma vez para `f32`). Nenhuma
lança erro: fora do domínio dá `nan` (`sqrt(-1.ff)`, `log(-1.ff)`, `asin(2.ff)`), um polo dá `±inf`
(`log(0.ff)` é `-inf`, `pow(0.ff, -1.ff)` é `inf`) e o que passa do maior valor dá `inf`
(`exp(1000.ff)`). Inteiro não entra onde se espera ponto flutuante: `sqrt(2)` é erro de compilação
(escreva `sqrt(2.ff)` ou `sqrt(cast<f64>(n))`).

**Constantes** (`f64`): `pi` (π), `e`, `tau` (2π), `inf` (infinito) e `nan`.

**Potências, raízes e logaritmos**

| Função | Descrição |
|--------|-----------|
| `sqrt(flutuante x) -> flutuante` | raiz quadrada (`nan` para negativo) |
| `cbrt(flutuante x) -> flutuante` | raiz cúbica (aceita negativos: `cbrt(-8.ff)` = `-2`) |
| `pow(flutuante b, flutuante x) -> flutuante` | `b` elevado a `x` (`nan` sem resultado real) |
| `exp(flutuante x) -> flutuante` | *e* elevado a `x` |
| `log(flutuante x) -> flutuante` | logaritmo natural (`log(0)` = `-inf`, negativo dá `nan`) |
| `log10(flutuante x) -> flutuante`, `log2(...)` | logaritmo na base 10 / na base 2 |
| `log_base(flutuante x, flutuante base) -> flutuante` | `log(x) / log(base)` |
| `hypot(flutuante x, flutuante y) -> flutuante` | √(x² + y²), sem estourar no meio do cálculo |

**Trigonometria** (ângulos em radianos)

| Função | Descrição |
|--------|-----------|
| `sin`, `cos`, `tan(flutuante x) -> flutuante` | seno, cosseno, tangente |
| `asin`, `acos(flutuante x) -> flutuante` | arco seno e arco cosseno (`nan` fora de -1 a 1) |
| `atan(flutuante x) -> flutuante` | arco tangente |
| `atan2(flutuante y, flutuante x) -> flutuante` | ângulo do ponto (x, y), de -π a π (note a ordem: `y` primeiro) |
| `sinh`, `cosh`, `tanh(flutuante x) -> flutuante` | funções hiperbólicas |
| `degrees(flutuante rad) -> flutuante`, `radians(flutuante graus)` | conversão entre radianos e graus |

**Arredondamento**

| Função | Descrição |
|--------|-----------|
| `floor(flutuante x) -> i64` | maior inteiro ≤ `x` |
| `ceil(flutuante x) -> i64` | menor inteiro ≥ `x` |
| `round(flutuante x) -> i64` | inteiro mais próximo; metade se afasta do zero (`2.5` → `3`, `-2.5` → `-3`) |
| `trunc(flutuante x) -> i64` | corta as casas, em direção ao zero (`-2.7` → `-2`) |
| `round_to(flutuante x, i64 casas) -> flutuante` | arredonda para `casas` casas decimais (metade se afasta do zero); `casas` negativo lança `ValueError`; `nan` e infinito voltam como estão |

`floor`, `ceil`, `round` e `trunc` lançam `ValueError` para `nan` e infinito (não têm valor
inteiro) e `OverflowError` se o resultado não couber em `i64`.

`round_to` arredonda o número **como ele é escrito** (a forma da seção 5.4, no próprio tipo), não a
sua representação binária: `round_to(2.675ff, 2)` é `2.68`, embora `2.675` não seja exato em
binário. O resultado é mostrado pela forma da seção 5.4: `round_to(2.5ff, 2)` é `2.5`, não `2.50`.

**Inteiros e sinais**

| Função | Descrição |
|--------|-----------|
| `abs(número x) -> número` | valor absoluto; `abs` do menor `i32`/`i64` lança `OverflowError` |
| `sign(número x) -> i32` | `-1`, `0` ou `1` (`sign(nan)` é `0`) |
| `min(número a, número b) -> número`, `max(...)` | o menor / o maior (para uma lista, `Lists`); com `nan` em qualquer lado, `nan` |
| `clamp(número x, número min, número max) -> número` | `x` limitado ao intervalo; `min > max` lança `ValueError` |
| `gcd(inteiro a, inteiro b) -> inteiro`, `lcm(...)` | máximo divisor comum / mínimo múltiplo comum, sempre ≥ 0; `gcd(0, 0)` = `0`, `lcm` com zero = `0`; resultado que não cabe no tipo lança `OverflowError` |
| `is_even(inteiro n) -> bool`, `is_odd(...)` | par / ímpar |
| `factorial(i64 n) -> i64` | n!; negativo lança `ValueError`; acima de `20` não cabe em `i64` (`OverflowError`) |
| `is_prime(i64 n) -> bool` | `true` se `n` é primo (exato em toda a faixa de `i64`; menores que 2 não são) |

**Ponto flutuante**

| Função | Descrição |
|--------|-----------|
| `is_close(flutuante a, flutuante b) -> bool` | iguais a menos de erro de arredondamento: em `f64`, diferença relativa até 10⁻⁹ ou absoluta até 10⁻¹² (perto de zero); em `f32`, relativa até 10⁻⁵ ou absoluta até 10⁻⁷. `0.1ff + 0.2ff == 0.3ff` é `false`; `is_close(0.1ff + 0.2ff, 0.3ff)` é `true` |
| `is_nan(flutuante x) -> bool` | `true` se `x` é `nan` (o único valor diferente de si mesmo) |
| `is_inf(flutuante x) -> bool` | `true` se `x` é `inf` ou `-inf` |

```cinza
import Math;
fn main() {
  print(Math.sqrt(16.0ff), Math.pow(2.ff, 10.ff), Math.abs(-3), Math.abs(-2.5ff));
  print(Math.floor(2.7ff), Math.ceil(2.1ff), Math.round(2.5ff), Math.round(-2.5ff), Math.min(3, 7));
  print(Math.pi);
  print(Math.round_to(2.675ff, 2), Math.trunc(-2.7ff), Math.hypot(3.ff, 4.ff), Math.log_base(8.ff, 2.ff));
  print(Math.gcd(12, 18), Math.lcm(4, 6), Math.factorial(5), Math.is_prime(97), Math.clamp(15, 0, 10));
  print(0.1ff + 0.2ff == 0.3ff, Math.is_close(0.1ff + 0.2ff, 0.3ff));
  print(Math.sqrt(2.f), type(Math.sqrt(2.f)), Math.sqrt(-1.ff), Math.log(0.ff), Math.is_nan(Math.nan));
}
// expect: 4 1024 3 2.5
// expect: 2 3 3 -3 3
// expect: 3.141592653589793
// expect: 2.68 -2 5 3
// expect: 6 12 120 true 10
// expect: false true
// expect: 1.4142135 f32 nan -inf true
```

### `Random`

| Função | Descrição |
|--------|-----------|
| `seed(i64 s)` | reinicia o gerador; a mesma semente produz a mesma sequência (na mesma implementação) |
| `i32(i32 a, i32 b) -> i32` | inteiro de `a` até `b`, **os dois inclusive**; `a > b` lança `ValueError` |
| `i64(i64 a, i64 b) -> i64` | idem, em `i64` |
| `f64() -> f64` | de `0.0` (inclusive) até `1.0` (exclusive) |
| `f64_range(f64 a, f64 b) -> f64` | de `a` (inclusive) até `b` (exclusive); `a` ≥ `b` lança `ValueError` |
| `bool() -> bool` | `true` ou `false`, com a mesma chance |
| `chance(f64 p) -> bool` | `true` com probabilidade `p`, de `0` (nunca) a `1` (sempre); fora disso, `ValueError` |
| `gauss(f64 media, f64 desvio) -> f64` | distribuição normal; desvio `0` devolve a média; desvio negativo lança `ValueError` |
| `choice(list<T> l) -> T` | um elemento de `l`; lista vazia lança `IndexError` |
| `choices(list<T> l, i64 k) -> list<T>` | `k` sorteios **com** repetição (`k` pode passar do tamanho); com `k` > 0, lista vazia lança `IndexError` |
| `sample(list<T> l, i64 k) -> list<T>` | `k` elementos de posições **distintas**, em ordem aleatória; `k` maior que o tamanho lança `ValueError` |
| `shuffle(list<T> l)` | embaralha `l` no lugar |

`k` negativo lança `ValueError`; `k` = 0 dá `[]`, mesmo com lista vazia. `shuffle` altera a
lista: é erro numa lista `const`, como `Lists.sort`. A sequência exata gerada para uma semente não faz parte da especificação.

```cinza
import Random;
fn main() {
  Random.seed(7);
  i64 a = Random.i64(1, 100);
  Random.seed(7);
  print(a == Random.i64(1, 100), Random.i64(5, 5));
}
// expect: true 5
```

### `Convert`

Texto ↔ número é `cast<T>` (seção 3.1): `cast<i64>("42")`, `cast<f64>("2.5")`, `cast<string>(7)`.
O `Convert` fica com o que o `cast` não faz: `bool`, validação, conversão com padrão e bases.

| Função | Descrição |
|--------|-----------|
| `to_bool(string s) -> bool` | só `"true"` ou `"false"`; outro texto lança `ValueError` |
| `is_i32(string s) -> bool`, `is_i64(...)`, `is_f32(...)`, `is_f64(...)` | `true` se `cast<T>(s)` aceitaria o texto (formato e faixa) |
| `is_bool(string s) -> bool` | `true` se `to_bool` aceitaria o texto |
| `to_i32_or(string s, i32 padrao) -> i32`, `to_i64_or(string s, i64 padrao) -> i64` | o valor convertido como no `cast`, ou `padrao` se o texto não for aceito |
| `to_f32_or(string s, f32 padrao) -> f32`, `to_f64_or(string s, f64 padrao) -> f64` | idem |
| `to_bool_or(string s, bool padrao) -> bool` | idem |

Para aceitar vírgula decimal, troque-a antes: `cast<f64>(Strings.replace(s, ",", "."))`.

**Bases numéricas** — dígitos `0`–`9` e `a`–`z`, sem prefixo (`0x`, `0b`); negativo com `-`

| Função | Descrição |
|--------|-----------|
| `to_base(i64 n, i64 base) -> string` | `n` na base dada, de 2 a 36 (senão `ValueError`), em minúsculas |
| `to_hex(i64 n) -> string`, `to_binary(...)`, `to_octal(...)` | atalhos para as bases 16, 2 e 8 |
| `from_base(string s, i64 base) -> i64` | o inverso: sinal opcional e dígitos da base (maiúsculas ou minúsculas); dígito inválido lança `ValueError`; fora da faixa de `i64`, `OverflowError` |

```cinza
import Convert as cv;
fn main() {
  print(cast<i64>("42") + 1, cast<f64>("2.5"), cast<string>(10) + "!", cv.to_bool("true"));
  try { print(cast<i64>("12a")); } except (ValueError e) { print(e.message); }
  print(cv.is_i64("12a"), cv.to_i64_or("12a", 0), cv.to_i64_or("12", 0));
  print(cv.to_hex(255), cv.to_binary(5), cv.from_base("ff", 16), cv.to_base(35, 36));
}
// expect: 43 2.5 10! true
// expect: cast<i64>("12a"): não é um número inteiro (só sinal e dígitos)
// expect: false 0 12
// expect: ff 101 255 z
```

### `Lists`

"Igual" é a igualdade `==` (seção 5.3). Os métodos `l.size()`, `l.add(x)`, `l.remove(i)` e
`l.has(i)` continuam na própria lista (seção 5.7).

**Ordenar** — sempre estável (elementos de mesma chave mantêm a ordem relativa)

| Função | Descrição |
|--------|-----------|
| `sort(list<comparável> l)` | ordena `l` no lugar, em ordem crescente (com ponto flutuante, `nan` fica depois de todos, inclusive de `inf`) |
| `sort_desc(list<comparável> l)` | idem, em ordem decrescente |
| `sort_by(list<T> l, list<comparável> chaves)` | ordena `l` no lugar pela chave da **mesma posição**; as chaves são reorganizadas junto; tamanhos diferentes lançam `ValueError` |
| `sorted(list<comparável> l) -> list<comparável>` | **nova** lista ordenada; `l` não muda |
| `reverse(list<T> l)` | inverte `l` no lugar |
| `reversed(list<T> l) -> list<T>` | **nova** lista invertida; `l` não muda |

`sort_by` é a forma de ordenar objetos e structs: monte a lista de chaves com um laço.

**Estatística**

| Função | Descrição |
|--------|-----------|
| `sum(list<número> l) -> número` | soma, no tipo dos elementos; lista vazia dá `0`; estouro de inteiro lança `OverflowError` (ponto flutuante segue o IEEE) |
| `product(list<número> l) -> número` | produto; lista vazia dá `1`; idem |
| `average(list<número> l) -> f64` | média |
| `min(list<comparável> l) -> comparável`, `max(...)` | o menor / o maior (o primeiro, se houver empate); com algum `nan`, `nan` |

`average`, `min` e `max` de uma lista vazia lançam `ValueError`.

**Buscar**

| Função | Descrição |
|--------|-----------|
| `contains(list<T> l, T x) -> bool` | `true` se algum elemento é igual a `x` |
| `index_of(list<T> l, T x) -> i32` | índice do primeiro elemento igual a `x`, ou `-1` |
| `last_index_of(list<T> l, T x) -> i32` | índice do último, ou `-1` |
| `count(list<T> l, T x) -> i32` | quantos elementos são iguais a `x` |
| `first(list<T> l) -> T`, `last(...)` | o primeiro / o último elemento; lista vazia lança `IndexError` |
| `is_empty(list<T> l) -> bool` | `true` se não tem elementos |

**Alterar no lugar**

| Função | Descrição |
|--------|-----------|
| `insert(list<T> l, i64 i, T x)` | insere `x` na posição `i`, de `0` ao tamanho (no tamanho = acrescentar); senão `IndexError` |
| `pop(list<T> l) -> T` | remove e devolve o último; lista vazia lança `IndexError` |
| `remove_value(list<T> l, T x)` | remove o primeiro elemento igual a `x`; ausente lança `ValueError` |
| `clear(list<T> l)` | remove todos os elementos |
| `extend(list<T> l, list<T> outra)` | acrescenta os elementos de `outra` ao fim de `l` (`outra` pode ser `l`) |
| `swap(list<T> l, i64 i, i64 j)` | troca dois elementos de lugar (`IndexError` se um índice for inválido) |

**Listas novas** — a original não muda

| Função | Descrição |
|--------|-----------|
| `slice(list<T> l, i64 ini, i64 fim) -> list<T>` | os elementos de `ini` até `fim` (exclusive); exige `0 ≤ ini ≤ fim ≤ tamanho` (senão `IndexError`) |
| `concat(list<T> a, list<T> b) -> list<T>` | os elementos de `a` seguidos dos de `b` |
| `copy(list<T> l) -> list<T>` | uma cópia (rasa: objetos dentro continuam compartilhados; structs, como sempre, são valores) |
| `unique(list<T> l) -> list<T>` | sem repetidos, mantendo a primeira ocorrência e a ordem |
| `repeat(T x, i64 n) -> list<T>` | `n` cópias de `x`; `n` negativo, ou acima de 100 milhões, lança `ValueError` |

As funções que alteram a lista (`sort`, `sort_desc`, `reverse`, as de "alterar no lugar" e, em
`sort_by`, também as chaves) são erro numa lista `const`.

```cinza
import Lists as L;
fn main() {
  list<i64> n = [5, 3, 9, 1];
  L.sort(n);
  print(n, L.contains(n, 9), L.index_of(n, 5), L.slice(n, 1, 3), L.sum(n));
  L.reverse(n);
  print(n);
  print(L.max(n), L.average(n), L.unique([1, 2, 1]), L.repeat("-", 3));
  list<string> nomes = ["Caio", "Ana", "Bia"];
  list<i64> idades = [30, 25, 18];
  L.sort_by(nomes, idades);
  print(nomes, idades);
}
// expect: [1, 3, 5, 9] true 2 [3, 5] 18
// expect: [9, 5, 3, 1]
// expect: 9 4.5 [1, 2] ["-", "-", "-"]
// expect: ["Bia", "Ana", "Caio"] [18, 25, 30]
```

### `Time`

Um **instante** é um `i64`: os segundos desde 1970-01-01 00:00 UTC (negativo antes disso). Por ser
um `i64`, instantes se comparam, se ordenam, servem de chave de `dict` e se somam. A faixa de
datas vai do ano 1 ao 9999; um instante fora dela, nas funções que leem as partes, lança
`ValueError`.

As partes, `make`, `format` e `parse` usam o **horário local** do computador (inclusive horário de
verão); as variantes terminadas em `_utc` usam o UTC.

**Relógio**

| Função | Descrição |
|--------|-----------|
| `now() -> i64` | o instante atual |
| `clock() -> i64` | milissegundos de um relógio que só avança — use a diferença entre duas leituras para medir duração |
| `sleep(i64 ms)` | espera `ms` milissegundos; negativo lança `ValueError` |

**Montar e desmontar**

| Função | Descrição |
|--------|-----------|
| `make(i64 ano, i64 mes, i64 dia[, i64 hora, i64 minuto, i64 segundo]) -> i64` | o instante da data local; as partes de hora que faltam valem `0`; data inválida (`31/02`, mês `13`, hora `24`...) lança `ValueError` |
| `year(i64 t) -> i64`, `month`, `day`, `hour`, `minute`, `second` | as partes de `t` |
| `weekday(i64 t) -> i64` | dia da semana, padrão ISO: `1` = segunda ... `7` = domingo |
| `day_of_year(i64 t) -> i64` | de `1` a `366` |
| `make_utc(...)`, `year_utc(i64 t)`, `month_utc`, ..., `day_of_year_utc` | as mesmas, em UTC |

**Texto** — campos do padrão: `yyyy` (ano, 4 dígitos), `MM` (mês), `dd` (dia), `HH` (hora, 0 a
23), `mm` (minuto), `ss` (segundo), todos com 2 dígitos; o resto do padrão é texto literal

| Função | Descrição |
|--------|-----------|
| `format(i64 t, string padrao) -> string` | `format(t, "dd/MM/yyyy HH:mm")` → `"03/10/2026 14:30"` |
| `parse(string texto, string padrao) -> i64` | o inverso: o texto precisa casar **exatamente** com o padrão (campos com o número exato de dígitos); o que o padrão não traz vale ano 1970, mês 1, dia 1, hora 0; texto fora do padrão ou data inválida lança `ValueError` |
| `format_utc(...)`, `parse_utc(...)` | as mesmas, em UTC |

**Contas e calendário**

| Função | Descrição |
|--------|-----------|
| `add_days(i64 t, i64 n) -> i64`, `add_hours`, `add_minutes`, `add_seconds` | `t` mais `n` dias (de 86400 segundos), horas, minutos ou segundos; `n` pode ser negativo; estouro lança `OverflowError` |
| `days_between(i64 a, i64 b) -> i64` | dias de **calendário** (local) de `a` até `b`: de 23h de um dia a 1h do seguinte é `1`; negativo se `b` vem antes |
| `is_leap_year(i64 ano) -> bool` | ano bissexto |
| `days_in_month(i64 ano, i64 mes) -> i64` | dias do mês (`28` a `31`); mês inválido lança `ValueError` |

```cinza
import Time;
fn main() {
  i64 t = Time.make_utc(2026, 10, 3, 14, 30, 0);
  print(t, Time.format_utc(t, "dd/MM/yyyy HH:mm"), Time.weekday_utc(t), Time.day_of_year_utc(t));
  i64 p = Time.parse_utc("25/12/2026", "dd/MM/yyyy");
  print(Time.days_between(Time.make(2026, 10, 3), Time.make(2026, 12, 25)), Time.format_utc(Time.add_days(p, 7), "dd/MM"));
  print(Time.is_leap_year(2028), Time.days_in_month(2026, 2), Time.make(2026, 1, 1) < Time.now());
}
// expect: 1791037800 03/10/2026 14:30 6 276
// expect: 83 01/01
// expect: true 28 true
```
