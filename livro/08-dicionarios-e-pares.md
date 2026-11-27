# 8. Dicionários e pares

**Objetivos.** Ao fim deste capítulo você sabe:

- criar e usar um `dict<K, V>`: ler, inserir, trocar e remover entradas;
- prever a ordem em que as entradas aparecem e por que ela é a de inserção;
- escolher o tipo da chave e entender o que não pode ser chave;
- usar `pair<A, B>` e o módulo `Dicts`.

**Pré-requisitos.** O capítulo 7.

---

Um `dict<K, V>` associa chaves de tipo `K` a valores de tipo `V`, cada chave uma vez só. É o
"dicionário" do Python, o `Map` do Java, o objeto do JavaScript. Duas decisões da Cinza o tornam
previsível: as entradas ficam **na ordem em que entraram**, e ler ou trocar uma chave que não
existe é **um erro**, não um valor inventado. Um `pair<A, B>` é a dupla de valores que um
dicionário guarda em cada entrada — e que serve também por si só.

## 8.1 Criar e acessar

Um literal de dicionário é uma lista de pares entre chaves — cada par `{chave, valor}`:

```cinza
fn main() {
  dict<string, i32> idades = {{"Ana", 30}, {"Bia", 25}};
  print(idades["Ana"], idades.size(), idades);
  idades.add({"Caio", 41});
  idades["Bia"] = 26;
  print(idades, idades.has("Caio"), idades.has("Davi"));
  idades.remove("Ana");
  print(idades, idades.keys(), idades.values());
}
```

```saida
30 2 {"Ana": 30, "Bia": 25}
{"Ana": 30, "Bia": 26, "Caio": 41} true false
{"Bia": 26, "Caio": 41} ["Bia", "Caio"] [26, 41]
```

| Operação | O que faz |
|----------|-----------|
| `d[k]` | o valor da chave `k` |
| `d.add({k, v})` | **insere** a entrada — ou troca o valor, se a chave já existir |
| `d[k] = v` | **troca** o valor de uma chave que já existe |
| `d.remove(k)` | remove a chave |
| `d.has(k)` | `true` se a chave existe |
| `d.size()` | o número de entradas |
| `d.keys()`, `d.values()` | as chaves e os valores, em listas novas, na ordem do dicionário |

Um dicionário declarado sem valor nasce vazio — o `{}` vazio não existe, como o `[]` das listas:

```cinza
fn main() {
  dict<string, i32> d;
  print(d, d.size());
}
```

```saida
{} 0
```

## 8.2 A chave que não existe

Ler, trocar ou remover uma chave que não está no dicionário é um `KeyError`:

```cinza leitura.cinza
fn main() {
  dict<string, i32> idades = {{"Ana", 30}};
  print(idades["Zé"]);
}
```

```erro
leitura.cinza:3:15: KeyError: chave 'Zé' não encontrada no dicionário
    em main (leitura.cinza:3)
```

Inclusive na escrita: **`d[k] = v` só troca**, e a mensagem diz como inserir:

```cinza escrita.cinza
fn main() {
  dict<string, i32> idades = {{"Ana", 30}};
  idades["Zé"] = 40;
}
```

```erro
escrita.cinza:3:9: KeyError: chave 'Zé' não existe no dicionário. Use .add({"Zé", valor}) para inserir novas entradas.
    em main (escrita.cinza:3)
```

> **Por quê?** No Python, `d["Ze"] = 40` cria a chave quando ela não existe — e o erro de digitar
> o nome de uma chave que você queria trocar vira uma entrada nova, em silêncio. Na Cinza, inserir
> (`add`) e trocar (`d[k] = v`) são operações diferentes, e o engano aparece onde aconteceu.

Quando a ausência da chave é um caso esperado, há dois caminhos: perguntar antes com `d.has(k)`, ou
usar `Dicts.get(d, k)`, que devolve um opcional — o valor, ou `null` (capítulo 11):

```cinza
import Dicts;
fn main() {
  dict<string, i32> estoque = {{"pera", 3}};
  print(Dicts.get(estoque, "pera"), Dicts.get(estoque, "kiwi"));
  print(Dicts.get(estoque, "kiwi") else 0);
  if (estoque.has("pera")) {
    print("tem pera:", estoque["pera"]);
  }
}
```

```saida
3 null
0
tem pera: 3
```

## 8.3 A ordem de inserção

As entradas de um dicionário ficam **na ordem em que entraram**. É a ordem do `print`, do `for`, de
`keys()` e `values()` e de tudo o que o módulo `Dicts` devolve. Duas regras completam a ordem:

- **trocar o valor** de uma chave que já existe não a move;
- **remover** uma chave e inseri-la de novo a põe no fim.

O `for` sobre um dicionário recebe, a cada volta, um `pair<K, V>`: a chave em `first` e o valor em
`second`.

```cinza
fn main() {
  dict<string, i32> d = {{"pera", 3}, {"maçã", 1}, {"uva", 2}};
  d["pera"] = 30;
  d.remove("maçã");
  d.add({"maçã", 10});
  print(d);
  for (pair<string, i32> p in d) {
    print(p.first, "->", p.second);
  }
}
```

```saida
{"pera": 30, "uva": 2, "maçã": 10}
pera -> 30
uva -> 2
maçã -> 10
```

Para percorrer pela ordem das chaves, ordene as chaves:

```cinza
import Lists;
fn main() {
  dict<string, i32> d = {{"pera", 3}, {"maçã", 1}, {"uva", 2}};
  for (string k in Lists.sorted(d.keys())) {
    print(k, d[k]);
  }
}
```

```saida
maçã 1
pera 3
uva 2
```

A **igualdade** de dois dicionários não liga para a ordem: são iguais quando têm as mesmas chaves
com os mesmos valores. Já as listas de chaves, que são listas, comparam a ordem:

```cinza
fn main() {
  dict<string, i32> a = {{"x", 1}, {"y", 2}};
  dict<string, i32> b = {{"y", 2}, {"x", 1}};
  print(a == b, a.keys() == b.keys());
}
```

```saida
true false
```

> **Vindo de outra linguagem.** É a ordem do `dict` do Python (desde a 3.7) e do `LinkedHashMap`
> do Java. O `HashMap` do Java não garante ordem nenhuma, e o `map` do Go a embaralha de propósito
> a cada execução. Na Cinza a saída de um programa é sempre a mesma.

## 8.4 O que pode ser chave

A chave de um dicionário pode ser `i32`, `i64`, `string`, `bool` ou um enum (capítulo 10):

```cinza
enum Cor { Vermelho, Azul }
fn main() {
  dict<i32, string> a = {{1, "um"}};
  dict<bool, string> b = {{true, "sim"}};
  dict<Cor, i32> c = {{Cor.Azul, 2}};
  print(a, b, c);
}
```

```saida
{1: "um"} {true: "sim"} {Cor.Azul: 2}
```

**Ponto flutuante não pode ser chave**, nem coleções, nem objetos:

```cinza notas.cinza
fn main() {
  dict<f64, string> conceitos;
}
```

```erro
notas.cinza:2:21: SemanticError: Chave de dict não pode ser 'f64': comparação exata de ponto flutuante não é confiável. Use i32, i64, string, bool ou enum.
```

> **Por quê?** Uma chave é achada por igualdade exata, e `0.1 + 0.2` não é igual a `0.3` (seção
> 2.4): uma chave calculada não acharia a guardada. Para chavear por valores com casas decimais,
> use um inteiro em outra escala (centavos, milímetros) ou o texto do número já arredondado. E uma
> lista como chave poderia mudar depois de guardada, e a entrada não seria mais achada pelo valor
> novo.

Um literal com a mesma chave duas vezes é erro, em vez de ficar só com uma delas:

```cinza repetida.cinza
fn main() {
  dict<string, i32> d = {{"a", 1}, {"a", 2}};
}
```

```erro
repetida.cinza:2:37: SemanticError: Chave duplicada 'a' no literal de dicionário. Cada chave deve aparecer exatamente uma vez.
```

## 8.5 Valores que são coleções

O valor pode ser de qualquer tipo, inclusive outra coleção. `d[k]` devolve a coleção guardada — a
mesma, não uma cópia —, então ela pode ser alterada no lugar:

```cinza
fn main() {
  dict<string, list<string>> grupos;
  grupos.add({"frutas", ["maçã"]});
  grupos["frutas"].add("uva");
  grupos.add({"cores", ["azul"]});
  print(grupos);
}
```

```saida
{"frutas": ["maçã", "uva"], "cores": ["azul"]}
```

O próprio dicionário também é compartilhado, como a lista (seção 7.3): atribuí-lo ou passá-lo a uma
função não o copia. Para uma cópia, `Dicts.copy` (rasa, como `Lists.copy`).

## 8.6 Pares: `pair<A, B>`

Um `pair<A, B>` junta dois valores, de tipos que podem ser diferentes: `first` e `second`.

```cinza
fn main() {
  pair<string, i32> p = {"Ana", 30};
  print(p, p.first, p.second, type(p));
  pair<string, i32> q = p;
  q = {"Bia", 25};
  print(p, q, p == {"Ana", 30});
}
```

```saida
{"Ana", 30} Ana 30 pair<string, i32>
{"Ana", 30} {"Bia", 25} true
```

Um par é **imutável** e **copiado** como um número: para "mudar" um par, crie outro.

```cinza par.cinza
fn main() {
  pair<string, i32> p = {"Ana", 30};
  p.second = 31;
}
```

```erro
par.cinza:3:4: SemanticError: pair é imutável: não é possível atribuir a 'p.second'. Crie um novo par, ex.: p = {a, b};
```

O uso típico do par é o retorno de dois valores (seção 5.2) e as entradas de um dicionário. Para
mais de dois valores, ou quando os nomes `first` e `second` não dizem o que eles são, um struct
(capítulo 9) é mais claro.

## 8.7 O módulo `Dicts`

As funções de dicionário ficam no módulo `Dicts`. Elas seguem a ordem do dicionário, e nada falha
em silêncio: uma chave ausente onde ela precisa existir é `KeyError`, e uma chave repetida ao montar
um dicionário é `ValueError`.

### Ler

```cinza
import Dicts;
fn main() {
  dict<string, i32> estoque = {{"pera", 3}, {"maçã", 0}, {"uva", 3}};
  print(Dicts.contains_value(estoque, 0), Dicts.keys_of(estoque, 3), Dicts.is_empty(estoque));
  print(Dicts.items(estoque));
}
```

```saida
true ["pera", "uva"] false
[{"pera", 3}, {"maçã", 0}, {"uva", 3}]
```

`get` (seção 8.2) devolve o valor ou `null`; `keys_of` devolve as chaves com um valor; `items`, as
entradas como uma lista de pares.

### Alterar no lugar

```cinza
import Dicts;
fn main() {
  dict<string, i32> estoque = {{"pera", 3}, {"uva", 3}};
  Dicts.update(estoque, {{"kiwi", 5}, {"uva", 1}});
  print(estoque);
  i32 uvas = Dicts.pop(estoque, "uva");
  print(uvas, estoque);
  Dicts.clear(estoque);
  print(estoque);
}
```

```saida
{"pera": 3, "uva": 1, "kiwi": 5}
1 {"pera": 3, "kiwi": 5}
{}
```

`update` traz as entradas do outro dicionário (numa chave dos dois, fica o valor do outro); `pop`
remove e devolve o valor.

### Criar

```cinza
import Dicts;
fn main() {
  dict<string, i32> a = {{"x", 1}, {"y", 2}};
  print(Dicts.merge(a, {{"y", 20}, {"z", 3}}), a);
  print(Dicts.from_lists(["a", "b"], [1, 2]), Dicts.from_pairs([{1, "um"}, {2, "dois"}]));
  print(Dicts.invert(a));
}
```

```saida
{"x": 1, "y": 20, "z": 3} {"x": 1, "y": 2}
{"a": 1, "b": 2} {1: "um", 2: "dois"}
{1: "x", 2: "y"}
```

`merge` é como `update`, mas cria um dicionário novo. `invert` troca chaves e valores — e se dois
valores forem iguais, não há como escolher qual chave fica, então é erro:

```cinza inverte.cinza
import Dicts;
fn main() {
  print(Dicts.invert({{"a", 1}, {"b", 1}}));
}
```

```erro
inverte.cinza:3:9: ValueError: 'invert': o valor '1' aparece em mais de uma chave
    em main (inverte.cinza:3)
```

## 8.8 Os nomes de outras linguagens

| Em outras linguagens | Na Cinza |
|----------------------|----------|
| `{"a": 1}` | `{{"a", 1}}` |
| `{}` | `dict<K, V> d;` |
| `d["novo"] = 1` (inserir) | `d.add({"novo", 1})` |
| `d.get(k, padrão)` | `Dicts.get(d, k) else padrão` |
| `k in d`, `d.containsKey(k)` | `d.has(k)` |
| `d.put(k, v)` | `d.add({k, v})` |
| `for k, v in d.items()` | `for (pair<K, V> p in d)` |

A forma do Python e do JSON é reconhecida:

```cinza python.cinza
fn main() {
  dict<string, i32> d = {"a": 1, "b": 2};
}
```

```erro
python.cinza:2:29: SyntaxError: Um dict se escreve com pares entre chaves, separados por vírgula: {{"a", 1}, {"b", 2}}; não há a forma {chave: valor} (token: ':')
```

## Programa do capítulo: as palavras mais frequentes

O programa conta as palavras de um texto, sem diferenciar maiúsculas e sem a pontuação, e mostra as
cinco mais frequentes. A contagem é um `dict<string, i32>`; para ordenar pela contagem, as chaves
vão para uma lista e o `sort_by` as ordena pelos valores:

```cinza frequencia.cinza
// frequencia.cinza: as palavras mais frequentes de um texto
import Strings as st;
import Dicts;
import Lists;

const string TEXTO = "O rato roeu a roupa do rei de Roma, e a rainha, com raiva, roeu o resto. O rei riu.";

fn limpa(string palavra) -> string {
  return st.remove(st.remove(st.lower(palavra), ","), ".");
}

fn main() {
  dict<string, i32> contagem;
  for (string bruta in st.words(TEXTO)) {
    string p = limpa(bruta);
    contagem.add({p, (Dicts.get(contagem, p) else 0) + 1});
  }
  list<string> palavras = contagem.keys();
  list<i32> chaves;
  for (string p in palavras) {
    chaves.add(-contagem[p]);
  }
  Lists.sort_by(palavras, chaves);
  printf("{contagem.size()} palavras diferentes; as mais frequentes:");
  for (i32 i in range(0, 5)) {
    printf("  {palavras[i]:6}{contagem[palavras[i]]:3}");
  }
}
```

```saida
15 palavras diferentes; as mais frequentes:
  o       3
  roeu    2
  a       2
  rei     2
  rato    1
```

Repare em:

- `contagem.add({p, (Dicts.get(contagem, p) else 0) + 1})` é o contador inteiro numa linha: o
  valor atual (ou 0, se a palavra ainda não está lá) mais um, inserido ou trocado pelo `add`.
- As chaves do `sort_by` são as contagens **com sinal trocado**: ele ordena do menor para o maior,
  e assim as mais frequentes ficam primeiro.
- O `sort_by` é estável, e as chaves de `keys()` vêm na ordem de inserção: entre palavras com a
  mesma contagem (`roeu`, `a` e `rei`), fica primeiro a que apareceu antes no texto. O resultado é
  sempre o mesmo, a cada execução.

## Resumo

- `dict<K, V>`: `d[k]` lê, `d.add({k, v})` insere ou troca, `d[k] = v` só troca, `remove`, `has`,
  `size`, `keys`, `values`. Chave ausente onde precisa existir é `KeyError`; `Dicts.get` devolve um
  opcional.
- As entradas ficam na ordem de inserção (trocar não move, remover e inserir põe no fim); `for`
  recebe um `pair<K, V>`; `==` ignora a ordem.
- Chaves: `i32`, `i64`, `string`, `bool` ou enum — nunca ponto flutuante, coleções ou objetos.
- O dicionário é compartilhado; `Dicts.copy` copia. Os valores podem ser coleções, alteradas no lugar.
- `pair<A, B>`: `first` e `second`, imutável e copiado.
- `Dicts`: `get`, `contains_value`, `keys_of`, `items`, `update`, `pop`, `clear`, `merge`,
  `from_lists`, `from_pairs`, `invert`, `copy`.

## Exercícios

1. Dado `dict<string, string> telefones = {{"Ana", "1111"}, {"Bia", "2222"}}`, monte o dicionário
   inverso (do número para o nome) com um `for`, e confira que ele é igual ao de `Dicts.invert`.
2. Agrupe as palavras de `"banana abacate cereja amora berinjela caju"` pela primeira letra, num
   `dict<string, list<string>>`: `{"b": ["banana", "berinjela"], "a": [...], "c": [...]}`.
3. O contador abaixo para com um erro na primeira palavra. Explique por quê e corrija-o de dois
   jeitos: com `has` e com `Dicts.get`.

   ```text
   dict<string, i32> contagem;
   for (string p in ["a", "b", "a"]) {
     contagem[p] += 1;
   }
   ```

4. Escreva `fn junta(dict<string, i32> a, dict<string, i32> b) -> dict<string, i32>`, que soma os
   estoques: numa chave dos dois, o valor é a soma (o `Dicts.merge` ficaria com o de `b`).
5. Escreva `fn extremos(list<i32> l) -> pair<i32, i32>`, que devolve o menor e o maior valor da
   lista num par, percorrendo a lista uma vez só.

As soluções estão em [`solucoes/08-dicionarios-e-pares.md`](solucoes/08-dicionarios-e-pares.md).

## Na spec

- Os tipos `dict<K, V>` e `pair<A, B>`, a ordem e as chaves: [3.3](../spec/03-tipos.md)
- O compartilhamento e a cópia: [5.1](../spec/05-execucao.md)
- Os métodos e o `KeyError`: [5.7](../spec/05-execucao.md)
- O `for` sobre um dicionário: [5.5](../spec/05-execucao.md)
- O módulo `Dicts`: [6.2](../spec/06-biblioteca.md)
