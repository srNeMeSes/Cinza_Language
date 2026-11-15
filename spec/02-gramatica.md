# 2. Gramática

A sintaxe completa da Cinza, em EBNF (convenções no [README](README.md)). `nome` é um
identificador (seção 1.3); `inteiro`, `flutuante` e `texto` são os literais da seção 1.5.

Regras que a gramática sozinha não expressa estão nos comentários `(* ... *)` e são detalhadas
nos capítulos seguintes.

## 2.1 Programa e declarações

```ebnf
programa      = { import } { declaracao } ;

import        = "import" nome { "." nome } [ "as" nome ] ";" ;
                (* todos os imports vêm antes de qualquer declaração *)

declaracao    = [ "pub" ] ( funcao | classe | struct | enum | interface | erro | constGlobal ) ;

constGlobal   = "const" tipo nome "=" expressao ";" ;
                (* no nível superior só há const: não existem variáveis globais mutáveis *)

funcao        = "fn" nome [ paramsTipo ] "(" [ parametros ] ")" [ "->" tipo ] bloco ;
                (* paramsTipo só em função do nível superior: capítulo 8 *)
paramsTipo    = "[" nome { "," nome } "]" ;
parametros    = parametro { "," parametro } ;
parametro     = [ "const" ] tipo nome ;

classe        = "class" nome [ ":" nomeTipo { "," nomeTipo } ] "{" { membroClasse } "}" ;
                (* depois de ":" só interfaces *)
membroClasse  = campo
              | funcao                                   (* método privado *)
              | "pub" "{" { funcao | construtor } "}" ;  (* métodos públicos e construtor *)
construtor    = nome "(" [ parametros ] ")" bloco ;
                (* nome igual ao da classe; no máximo um construtor *)
campo         = tipo nome [ "=" expressao ] ";" ;
                (* na classe, sem "=" só para list<T> e dict<K, V>, que nascem vazios; no
                   struct, qualquer campo: sem "=" ele é informado na criação (seção 4.7) *)

struct        = "struct" nome [ paramsTipo ] "{" { campo } "}" ;   (* só campos; capítulo 8 *)

enum          = "enum" nome "{" nome { "," nome } "}" ;

interface     = "interface" nome "{" assinatura { assinatura } "}" ;
assinatura    = "fn" nome "(" [ parametros ] ")" [ "->" tipo ] ";" ;

erro          = "error" nome ";" ;
```

## 2.2 Tipos

```ebnf
tipo          = "i32" | "i64" | "f32" | "f64" | "string" | "str" | "bool"
              | "void"                                   (* só como tipo de retorno *)
              | "var"                                    (* só em variável local e no for *)
              | "list" "<" tipo ">"
              | "dict" "<" tipo "," tipo ">"
              | "pair" "<" tipo "," tipo ">"
              | nomeTipo ;

nomeTipo      = nome                    (* class, struct, enum, interface, erro, parâmetro de tipo *)
              | nome "." nome                            (* apelido de módulo . nome *)
              | ( nome | nome "." nome ) argsTipo ;      (* struct genérico: Par[i32, string] *)
```

## 2.3 Instruções

```ebnf
bloco         = "{" { instrucao } "}" ;

instrucao     = declVar
              | atribuicao
              | expressao ";"
              | se
              | enquanto
              | para
              | "return" [ expressao ] ";"
              | "break" ";"
              | "continue" ";"
              | tente
              | "throw" expressao ";"
              | bloco ;

declVar       = [ "const" ] tipo nome [ "=" expressao ] ";" ;
                (* sem "=" só para list<T> e dict<K, V> *)

atribuicao    = alvo opAtribuicao expressao ";" ;
alvo          = expressao ;
                (* precisa ser uma variável, um campo (x.campo) ou um índice (x[i]) *)
opAtribuicao  = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;

se            = "if" "(" expressao ")" bloco [ "else" ( se | bloco ) ] ;
enquanto      = "while" "(" expressao ")" bloco ;
para          = "for" "(" tipo nome "in" expressao ")" bloco ;

tente         = "try" bloco { "except" "(" nomeTipo nome ")" bloco } [ "finally" bloco ] ;
                (* pelo menos um except ou o finally *)
```

O corpo de `if`, `else`, `while`, `for`, `try`, `except` e `finally` é **sempre um bloco** entre
chaves; `else if` encadeia sem chaves extras.

`fn`, `class`, `struct`, `enum`, `interface` e `error` só aparecem no nível superior (métodos,
dentro de `class`).

## 2.4 Expressões

Da menor para a maior precedência; todos os operadores binários associam à esquerda.

```ebnf
expressao     = ou ;
ou            = e { ( "||" | "or" ) e } ;
e             = igualdade { ( "&&" | "and" ) igualdade } ;
igualdade     = comparacao { ( "==" | "!=" ) comparacao } ;
comparacao    = soma { ( "<" | "<=" | ">" | ">=" ) soma } ;
soma          = produto { ( "+" | "-" ) produto } ;
produto       = unario { ( "*" | "/" | "%" ) unario } ;
unario        = ( "-" | "!" | "not" ) unario | posfixo ;
posfixo       = primario { "." nome [ argumentos ] | "[" expressao "]" | fatia } ;
fatia         = "[" [ expressao ] ":" [ expressao ] [ ":" [ expressao ] ] "]" ;   (* só string *)

primario      = literal
              | nome [ [ argsTipo ] argumentos ]         (* variável ou chamada *)
              | nome "." nome [ [ argsTipo ] argumentos ] (* nome de módulo importado *)
              | "self"
              | "new" nomeTipo ( argumentos | camposNomeados )
              | "cast" "<" tipo ">" "(" expressao ")"   (* i32, i64, f32, f64 ou string; seção 3.1 *)
              | tipoValor
              | "(" expressao ")"
              | lista | par | dicionario ;

argumentos    = "(" [ expressao { "," expressao } ] ")" ;
camposNomeados = "(" nome ":" expressao { "," nome ":" expressao } ")" ;
                (* só na criação de struct: seção 4.7 *)
argsTipo      = "[" tipo { "," tipo } "]" ;
                (* numa expressão, só quando o "]" é seguido de "(": senão é índice ou
                   fatia; seção 8.3 *)
lista         = "[" expressao { "," expressao } "]" ;    (* não há "[]" vazio *)
par           = "{" expressao "," expressao "}" ;
dicionario    = "{" entrada { "," entrada } "}" ;        (* não há "{}" vazio *)
entrada       = "{" expressao "," expressao "}" ;
tipoValor     = ( "i32" | "i64" | "f32" | "f64" | "string" | "str" | "bool"
                | "list" | "dict" | "pair" ) …           (* um tipo escrito como valor *)
              | ( nome | nome "." nome ) argsTipo ;      (* struct genérico: seção 8.6 *)
literal       = inteiro | flutuante | texto | "true" | "false" ;   (* flutuante: 1.5f, 1.5ff *)
```

| Precedência | Operadores                       |
|-------------|----------------------------------|
| 1 (menor)   | `\|\|` `or`                      |
| 2           | `&&` `and`                       |
| 3           | `==` `!=`                        |
| 4           | `<` `<=` `>` `>=`                |
| 5           | `+` `-`                          |
| 6           | `*` `/` `%`                      |
| 7           | `-` `!` `not` (unários)          |
| 8 (maior)   | `.campo`, `.metodo(...)`, `[i]`, `[a:b]`, chamada |

```cinza
fn main() {
  print(1 + 2 * 3, (1 + 2) * 3, -2 * 3, not true or true, 10 - 4 - 3);
}
// expect: 7 9 -6 true 3
```

Observações:

- `-` seguido de um literal inteiro forma o literal negativo; por isso
  `-9223372036854775808` é válido.
- Comparações encadeadas como `a < b < c` são sintaticamente válidas, mas comparam um `bool`
  com um número e por isso são erro de tipo (seção 5.3).
- Um **tipo escrito como valor** (`i64`, `list<i64>`, o nome de uma classe) só serve para
  comparar com `type(x)` (seção 3.8).
- Uma lista ou dicionário vazio se escreve omitindo o inicializador: `list<i64> l;`.
- `{a, b}` é um par; `{{k, v}, ...}` é um dicionário.
