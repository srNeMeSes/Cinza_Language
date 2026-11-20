# Cinza — compilador em C++20
Pipeline: Lexer → Parser → SemanticAnalyzer → dois backends, ambos permanentes: a CVM
(`cinza prog.cinza`, roda na hora; cvm/DESENHO.md) e o LLVM (`cinzac prog.cinza -o prog.exe`;
llvm/DESENHO.md, seção P do REVISAO_V2.md). Toda mudança da linguagem entra nos dois, com as
regras comuns em operacoes.cpp. Sem compatibilidade com versões antigas da
linguagem (ela está em desenvolvimento): o que muda, muda de vez.
Filosofia: disciplinada; sem null, sem herança, sem closures.
Build: `make`  |  Debug com sanitizers: `make debug`  |  Testes: `make test`
Backend nativo: `make cinzac` (llvm/bin/, com a runtime/; no Windows compilado pelo clang do
CLANG64, com o LLVM e o lld estáticos) | `make test-llvm` (a suíte pelo cinzac; com -O2:
`python tests/run_tests.py --cinzac --O2`)
Versão: versao.h (a mesma da spec) | Pacotes: distribuicao/empacota.sh; release numa tag v<versão>
CI: .github/workflows/ci.yml (Windows e Linux, GCC e Clang; os resultados pelo gh)
Extensão do VS Code: editor/vscode (C:\cinza-vscode aponta para ela) | `make test-vscode` (a gramática)
Plano da v2 e bugs confirmados: REVISAO_V2.md
Especificação: spec/ (os exemplos com // expect rodam no make test; mudou a linguagem, atualize a spec)
Regras: comentários e mensagens de erro em português; não mudar a
semântica da linguagem sem perguntar; toda correção vem com um teste em tests/.