# Especificação da linguagem Cinza

Versão 2 · outubro de 2026

Este documento define a linguagem Cinza: o que é um programa válido e o que ele faz ao
executar. É a referência para quem implementa a linguagem — o interpretador atual e a futura
máquina virtual (CVM) precisam se comportar exatamente como descrito aqui. Não é um tutorial:
para aprender a linguagem, o livro (a ser reescrito) é o caminho.

## Capítulos

1. [Léxico](01-lexico.md) — caracteres, comentários, identificadores, palavras-chave, literais
2. [Gramática](02-gramatica.md) — a sintaxe completa em EBNF
3. [Tipos](03-tipos.md) — os tipos, as regras de atribuição, `op<...>`, interfaces, `const`
4. [Declarações](04-declaracoes.md) — programa, `main`, funções, classes, struct, enum,
   interface, erros, módulos
5. [Execução](05-execucao.md) — valores, operadores, ordem de avaliação, instruções, exceções
6. [Biblioteca padrão](06-biblioteca.md) — funções embutidas e os módulos nativos
7. [Erros e diagnósticos](07-erros.md) — tipos de erro, quando ocorrem, formato das mensagens

## Filosofia

A Cinza é **disciplinada**: tipagem estática, sem `null`, sem herança e sem closures. Tudo o
que pode ser conferido antes de executar é conferido na compilação — inclusive o tipo de um
valor `op<...>`, que trava na inicialização e nunca muda. Os erros que restam para a execução
(índice fora da lista, divisão por zero, overflow...) são erros da linguagem, capturáveis com
`try`/`except`.

## Convenções deste documento

- **Erro de compilação**: o programa é rejeitado antes de executar qualquer coisa (erros
  léxicos, de sintaxe, de import ou semânticos). O interpretador sai com código 1.
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
