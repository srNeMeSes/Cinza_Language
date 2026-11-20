Prévia da linguagem Cinza. A linguagem ainda pode mudar antes da 1.0 (as classes genéricas e o
sistema de módulos, REVISAO_V2.md, seção Q).

## Instalar

Baixe o pacote do seu sistema, descompacte em qualquer pasta e ponha a pasta `bin/` no `PATH`. Não
é preciso instalar mais nada: o `cinzac` leva o LLVM, o ligador e as bibliotecas que os programas
usam.

- **Windows (x64):** `cinza-<versão>-windows-x64.zip`
- **Linux (x86-64, qualquer distribuição):** `cinza-<versão>-linux-x64.tar.gz` — executáveis
  estáticos; os programas gerados também saem estáticos.

```
cinza programa.cinza          roda na hora (a CVM)
cinzac programa.cinza -o prog gera o executável (-O2 para otimizar)
```

A extensão do VS Code (destaque de sintaxe e os erros no painel Problemas) é o arquivo `.vsix`:
no VS Code, *Extensões → … → Instalar do VSIX*.

A especificação da linguagem vem na pasta `spec/` de cada pacote.
