# Cinza — compilador/interpretador em C++20
Pipeline: Lexer → Parser → SemanticAnalyzer → Executor (tree-walk).
Filosofia: disciplinada; sem null, sem herança, sem closures.
Build: `make`  |  Debug com sanitizers: `make debug`  |  Testes: `make test`
Plano da v2 e bugs confirmados: REVISAO_V2.md
Especificação: spec/ (os exemplos com // expect rodam no make test; mudou a linguagem, atualize a spec)
Regras: comentários e mensagens de erro em português; não mudar a
semântica da linguagem sem perguntar; toda correção vem com um teste em tests/.