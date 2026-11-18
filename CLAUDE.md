# Cinza — compilador em C++20
Pipeline: Lexer → Parser → SemanticAnalyzer → compilador → CVM (máquina virtual, o único
runtime; cvm/DESENHO.md). Backend LLVM em construção: `cinzac` (llvm/DESENHO.md; etapas na
seção P do REVISAO_V2.md). Sem compatibilidade com versões antigas da
linguagem (ela está em desenvolvimento): o que muda, muda de vez.
Filosofia: disciplinada; sem null, sem herança, sem closures.
Build: `make`  |  Debug com sanitizers: `make debug`  |  Testes: `make test`
Backend nativo: `make cinzac` (llvm/bin/) | `make test-llvm` (a suíte pelo cinzac)
Plano da v2 e bugs confirmados: REVISAO_V2.md
Especificação: spec/ (os exemplos com // expect rodam no make test; mudou a linguagem, atualize a spec)
Regras: comentários e mensagens de erro em português; não mudar a
semântica da linguagem sem perguntar; toda correção vem com um teste em tests/.