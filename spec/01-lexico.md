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
  int ação = 1;
}
// expect-error: Identificadores não podem conter 'ç'
```

## 1.4 Palavras reservadas

Palavras-chave:

```
fn  return  if  else  while  for  in  true  false
struct  class  pub  new  const  self  enum  interface  error
break  continue  try  except  finally  throw  import  as
and  or  not
```

Nomes de tipo, também reservados:

```
int  decimal  string  str  bool  void  var  list  dict  pair  op
```

`and`, `or` e `not` são sinônimos exatos de `&&`, `||` e `!`; `str` é sinônimo exato de
`string`.

`print`, `input`, `range` e `type` **não** são palavras reservadas: são funções embutidas
(capítulo 6), e uma função do programa com o mesmo nome tem precedência sobre elas.

Logo depois de `apelido.` (nome qualificado de um módulo importado, seção 4.11), uma palavra
reservada é aceita como nome — é o que permite `Random.int(1, 6)`.

## 1.5 Literais

### Inteiro

Uma sequência de dígitos decimais: `0`, `42`, `9223372036854775807`. Não há prefixos de base
(hexadecimal, binário) nem separadores.

O valor precisa caber em `int` (64 bits com sinal). O literal `9223372036854775808` só é aceito
logo depois de um `-` unário, formando o menor `int`, `-9223372036854775808`. Fora da faixa é
erro de compilação.

```cinza
fn main() {
  int maior = 9223372036854775807;
  int menor = -9223372036854775808;
  print(maior, menor);
}
// expect: 9223372036854775807 -9223372036854775808
```

### Decimal

Dígitos, um ponto e dígitos: `3.14`, `0.5`, `10.0`. Os dois lados do ponto são obrigatórios
(`1.` e `.5` não são literais). **Não há notação com expoente** (`1e10` não é literal).

Um literal que não cabe em `decimal` (ponto flutuante de 64 bits) é erro léxico.

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
  int a = 1 @ 2;
}
// expect-error: LexicalError
```
