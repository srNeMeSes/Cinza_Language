# A linguagem Cinza

Um livro para quem já programa e quer aprender a Cinza a fundo: a linguagem, a biblioteca, os
dois compiladores e o porquê de cada decisão. Cobre a versão 0.6 da linguagem.

Todo programa com saída mostrada neste livro roda na suíte de testes do compilador, na máquina
virtual (`cinza`) e no compilador nativo (`cinzac`): se a linguagem mudar e um exemplo deixar de
valer, a suíte acusa. A regra exata de cada assunto está na [especificação](../spec/README.md); o
livro ensina, a spec define.

## Sumário

**Parte I — Primeiros passos**

1. [Começando](01-comecando.md)
2. [Valores, tipos e variáveis](02-valores-e-tipos.md)
3. [Operadores e expressões](03-operadores.md)
4. [Controle de fluxo](04-controle-de-fluxo.md)
5. [Funções](05-funcoes.md)

**Parte II — Dados**

6. [Textos](06-textos.md)
7. [Listas](07-listas.md)
8. Dicionários e pares
9. Structs
10. Enums e tipos como valores
11. O opcional: `T?` e `null`

**Parte III — Organizando o programa**

12. Classes
13. Interfaces
14. Erros e exceções
15. `const` e imutabilidade
16. Genéricos: funções
17. Genéricos: structs, classes e interfaces
18. Módulos e projetos
19. Pacotes

**Parte IV — A biblioteca na prática**

20. Arquivos, ambiente e processos
21. Números: `Math`, `Random` e `Convert`
22. Expressões regulares e JSON
23. Datas e tempo

**Parte V — Indo além**

24. Os dois compiladores
25. Ferramentas e hábitos
26. Projeto final: o `tarefas` completo

**Apêndices**

A. Gramática · B. Precedência e associatividade · C. Palavras reservadas · D. Referência rápida
da biblioteca · E. Mensagens de erro · F. Vindo de outra linguagem · G. Glossário

As soluções dos exercícios estão em [`solucoes/`](solucoes/).
