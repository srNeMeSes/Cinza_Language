# 1. Léxico

## 1.1 Arquivo-fonte

Um arquivo-fonte Cinza é texto em **UTF-8**, com extensão `.cinza`. Uma marca de ordem de bytes
(BOM) no início do arquivo é ignorada. Fins de linha podem ser `\n` ou `\r\n`.

Espaço, tabulação, retorno de carro e quebra de linha separam tokens e, fora isso, não têm
significado.

## 1.2 Comentários

Um comentário começa com `//` e vai até o fim da linha. Não há comentário de bloco.

Comentários e strings podem conter qualquer caractere UTF-8, inclusive acentos.

```cinza
// comentário: ação, pão e céu são permitidos aqui
fn main() {
  print("e aqui também: ação"); // e depois do código
}
// expect: e aqui também: ação
```

## 1.3 Identificadores

```
identificador = ( letra | "_" ) { letra | dígito | "_" } ;
letra         = "a" … "z" | "A" … "Z" ;
dígito        = "0" … "9" ;
```

Identificadores usam **só ASCII**. Uma letra acentuada num identificador é erro léxico.
Maiúsculas e minúsculas são diferentes (`nome` e `Nome` são identificadores distintos).

```cinza
fn main() {
  i64 ação = 1;
}
// expect-error: Identificadores não podem conter 'ç'
```

## 1.4 Palavras reservadas

Palavras-chave:

```
fn  return  if  else  while  for  in  true  false
struct  class  pub  new  const  self  enum  interface  error
break  continue  try  except  finally  throw  import  as
and  or  not  cast
```

Nomes de tipo, também reservados:

```
i32  i64  f32  f64  string  str  bool  void  var  list  dict  pair
```

`and`, `or` e `not` são sinônimos exatos de `&&`, `||` e `!`; `str` é sinônimo exato de
`string`.

`print`, `printf`, `format`, `input`, `range`, `type` e `exit` **não** são palavras reservadas: são funções embutidas
(capítulo 6), e uma função do programa com o mesmo nome tem precedência sobre elas.

Logo depois de `apelido.` (nome qualificado de um módulo importado, seção 4.11), uma palavra
reservada é aceita como nome — é o que permite `Random.i32(1, 6)` e `Random.f64()`.

## 1.5 Literais

### Inteiro

Uma sequência de dígitos decimais: `0`, `42`, `9223372036854775807`. Não há prefixos de base
(hexadecimal, binário) nem separadores.

O tipo do literal é `i32` se o valor couber em 32 bits com sinal (-2147483648 a 2147483647) e
`i64` se não couber. O valor precisa caber em `i64`: o literal `9223372036854775808` só é aceito
logo depois de um `-` unário, formando o menor `i64`, `-9223372036854775808`; fora da faixa é erro
de compilação. Onde se espera `i64`, um literal `i32` é alargado (seção 3.5).

```cinza
fn main() {
  i64 maior = 9223372036854775807;
  i64 menor = -9223372036854775808;
  print(maior, menor, type(5), type(3000000000), type(-2147483648));
}
// expect: 9223372036854775807 -9223372036854775808 i32 i64 i32
```

### Ponto flutuante

Dígitos, um ponto, dígitos opcionais e o **sufixo de precisão**, obrigatório: `f` para `f32` e
`ff` para `f64`.

| Literal              | Tipo  |
|----------------------|-------|
| `1.5f`, `1.f`, `0.25f` | `f32` |
| `1.5ff`, `1.ff`, `0.25ff` | `f64` |

Um literal com ponto e sem sufixo (`1.5`) é erro de compilação, assim como o sufixo sem ponto
(`1f`) e qualquer outro sufixo (`1.5fff`). `.5` não é literal (falta o dígito antes do ponto) e
**não há notação com expoente** (`1e10` não é literal). Um literal `f32` é lido direto na precisão
de 32 bits (o valor mais próximo em `float`, sem passar por `f64`).

Um literal que não cabe no seu tipo (`f32` vai até cerca de 3.4e38, `f64` até cerca de 1.8e308) é
erro léxico.

```cinza
fn main() {
  print(1.5f, 1.ff, type(1.5f), type(1.5ff), 0.1f + 0.2f, 0.1ff + 0.2ff);
}
// expect: 1.5 1 f32 f64 0.3 0.30000000000000004
```

```cinza
fn main() { print(1.5); }
// expect-error: Literal '1.5' sem a precisão: escreva 1.5f (f32) ou 1.5ff (f64)
```

### Texto (string)

Entre aspas duplas. Pode conter qualquer caractere UTF-8 e pode ocupar várias linhas (a quebra
de linha faz parte do texto). Sequências de escape:

| Escape | Significado      |
|--------|------------------|
| `\n`   | quebra de linha  |
| `\t`   | tabulação        |
| `\r`   | retorno de carro |
| `\\`   | barra invertida  |
| `\"`   | aspas duplas     |

Qualquer outra sequência começando com `\` é erro léxico, assim como uma string sem as aspas
de fechamento.

```cinza
fn main() {
  print("a\tb", "aspas: \"x\"", "barra: \\");
}
// expect: a	b aspas: "x" barra: \
```

```cinza
fn main() {
  print("\q");
}
// expect-error: Escape desconhecido '\q' em string
```

### Booleano

`true` e `false`.

## 1.6 Operadores e pontuação

```
+   -   *   /   %          aritméticos
==  !=  <   <=  >   >=     comparação
&&  ||  !                  lógicos (sinônimos: and, or, not)
=   +=  -=  *=  /=  %=     atribuição
->                         tipo de retorno
.   ,   ;   :              ponto, vírgula, ponto e vírgula, dois-pontos
(   )   {   }   [   ]      agrupadores
```

Qualquer outro caractere fora de comentários e strings (`@`, `$`, `#`...) é erro léxico.
Todos os erros léxicos de um arquivo são relatados juntos, antes da análise sintática.

```cinza
fn main() {
  i64 a = 1 @ 2;
}
// expect-error: LexicalError
```
