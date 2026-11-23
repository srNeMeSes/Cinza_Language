# Especificação da linguagem Cinza

Versão 0.6.0 · novembro de 2026 (o opcional: `T?`, `null`, `else` e `yield`)

Este documento define a linguagem Cinza: o que é um programa válido e o que ele faz ao
executar. É a referência para quem implementa a linguagem — a máquina virtual (CVM) e o
compilador nativo (`cinzac`, pelo LLVM) precisam se comportar exatamente como descrito aqui. Não é um tutorial:
para aprender a linguagem, o livro (a ser reescrito) é o caminho.

## Capítulos

1. [Léxico](01-lexico.md) — caracteres, comentários, identificadores, palavras-chave, literais
2. [Gramática](02-gramatica.md) — a sintaxe completa em EBNF
3. [Tipos](03-tipos.md) — os tipos, as regras de atribuição, interfaces, `const`
4. [Declarações](04-declaracoes.md) — programa, `main`, funções, classes, struct, enum,
   interface, erros, módulos
5. [Execução](05-execucao.md) — valores, operadores, ordem de avaliação, instruções, exceções
6. [Biblioteca padrão](06-biblioteca.md) — funções embutidas e os módulos nativos
7. [Erros e diagnósticos](07-erros.md) — tipos de erro, quando ocorrem, formato das mensagens
8. [Genéricos](08-genericos.md) — funções e structs genéricos: declaração, conferência no uso,
   inferência, erros com rastro

## Filosofia

A Cinza é **disciplinada**: tipagem estática, `null` só onde o tipo diz (`T?`, resolvido antes
do uso), sem herança e sem closures. Tudo o
que pode ser conferido antes de executar é conferido na compilação. Os erros que restam para a execução
(índice fora da lista, divisão por zero, overflow...) são erros da linguagem, capturáveis com
`try`/`except`.

### O que fica fora

Decisões do autor, não lacunas a preencher depois.

**Nunca, por filosofia:**

- **Herança**: o polimorfismo é pelas interfaces (seção 3.7), que não têm código nem campos.
- **Closures e funções como valor**: o nome de uma função só aparece numa chamada (seção 4.4). O
  que outras linguagens fazem passando uma função adiante, a Cinza faz com um objeto de uma classe
  que cumpre uma interface; para ordenar por um critério, `Lists.sort_by`.
- **Variáveis globais mutáveis**: no nível de cima só há `const` (seção 4.3).
- **Sobrecarga**, de funções (dois itens com o mesmo nome, seção 4.4) e de operadores (os operadores
  valem só para os tipos da linguagem, seção 5.3).
- **Conversões implícitas entre números**: a troca de tipo é sempre escrita, com `cast<T>` (seção
  3.5).
- **Tipo dinâmico**: um valor nunca muda de tipo, e um valor de interface nunca volta a ser tratado
  como a classe (seção 3.7).
- **Reflexão**: em execução, de um valor só se sabe o tipo (`type()`, seção 3.8), não os campos nem
  os métodos.
- **Ponteiros e memória manual**: a memória é sempre do runtime, pela contagem de referências e pelo
  coletor de ciclos (seção 5.9).
- **Métodos genéricos e restrições nos genéricos** (`T: Forma`): genéricos são só os itens do nível
  de cima, conferidos no uso de cada cópia (capítulo 8).
- **Infinito e NaN** (seção 3.1) e **import relativo** (seção 4.11).

**Fora da 1.0, a rever com o uso real:**

- **Concorrência** (threads, `async`, canais): o paralelismo de hoje é por processos (`Process`,
  capítulo 6).
- **Macros e metaprogramação**.

## Convenções deste documento

- **Erro de compilação**: o programa é rejeitado antes de executar qualquer coisa (erros
  léxicos, de sintaxe, de import ou semânticos). O `cinza` sai com código 1.
- **Erro de runtime**: um erro lançado durante a execução (`IndexError`, `OverflowError`...).
  Pode ser capturado com `try`/`except`; se ninguém capturar, o programa termina com código 1.
- A gramática usa EBNF: `{ x }` repete zero ou mais vezes, `[ x ]` é opcional, `( a | b )`
  escolhe um, e textos entre aspas são escritos literalmente.
- "Deve" e "não pode" descrevem regras conferidas pela implementação; violá-las é sempre erro
  (de compilação ou de runtime, conforme indicado).

## Exemplos executáveis

Os exemplos em blocos ` ```cinza ` que trazem linhas `// expect:` ou `// expect-error:` são
**testes**: o `make test` os extrai e os executa. Assim a especificação não fica
desatualizada — se um exemplo deixar de se comportar como descrito, a suíte falha.

- `// expect: texto` — uma linha exata da saída, na ordem;
- `// expect-error: trecho` — o programa termina com erro (código 1) e a mensagem contém o
  trecho.

```cinza
fn main() {
  print("olá, Cinza");
}
// expect: olá, Cinza
```

Blocos sem essas linhas são apenas ilustrativos.
