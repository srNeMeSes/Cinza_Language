# Cinza Language

Suporte à linguagem Cinza no VS Code.

## Destaque de sintaxe

- Palavras reservadas, tipos (`i32`, `i64`, `f32`, `f64`, `string`, `bool`, `list`, `dict`,
  `pair`), `cast<T>`, funções e structs genéricos (`fn maior[T]`, `struct Par[A, B]`,
  `Caixa[i32]`), criação pelo nome (`new Ponto(y: 4)`), enums, interfaces, erros declarados,
  `import ... as`, os módulos nativos e as funções embutidas.
- `printf` e `format`: o texto como string e cada `{expr}` (ou `{expr:8.2f}`) como código.
- As diretivas dos testes nos comentários (`// expect:`, `// expect-error:`, `// args:`...).
- Marca como inválido o que o compilador recusa já na leitura do arquivo:
  - identificador com acento (`ação`: só a-z, A-Z, dígitos e `_`);
  - número com ponto sem a precisão (`1.5`; o certo é `1.5f` ou `1.5ff`), sufixo inválido
    (`2.5fff`, `1f`) e literal fora da faixa (inteiro acima de i64; `f32`/`f64` grandes
    demais, contados pelos dígitos antes do ponto);
  - escape desconhecido numa string (`\q`) e `}` sem `{` num `printf`;
  - caracteres que não existem na Cinza: `` ~ ^ ? ' \ ` # $ @ `` e `&` ou `|` sozinhos.

## Servidor de linguagem

A extensão usa o servidor de linguagem do próprio compilador (`cinza --lsp`): enquanto se digita,
o léxico, a sintaxe, os imports e a semântica rodam sobre o texto do editor (sem executar nada), e
os erros aparecem sublinhados e no painel Problemas — os mesmos da compilação, com a mesma
mensagem; um erro num módulo importado aparece no arquivo dele. **Formatar Documento**
(Shift+Alt+F) usa o `cinza fmt` (o estilo da Cinza, sem opções).

O executável vem da configuração `cinza.caminho` (padrão: `cinza`, procurado no `PATH`). Sem ele,
a extensão avisa e segue só com o destaque de sintaxe.

## Erros no painel Problemas (tarefas)

O problem matcher `$cinza` lê os erros do `cinza` (CVM) e do `cinzac` (compilador nativo), no
formato `arquivo:linha:coluna: Tipo: mensagem`. Um `.vscode/tasks.json` de exemplo, na pasta do
compilador:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Cinza: rodar",
      "type": "process",
      "command": "${workspaceFolder}/cinza.exe",
      "args": ["${relativeFile}"],
      "problemMatcher": "$cinza",
      "group": "test"
    },
    {
      "label": "Cinza: compilar nativo (-O2)",
      "type": "process",
      "command": "${workspaceFolder}/llvm/bin/cinzac.exe",
      "args": ["-O2", "${relativeFile}", "-o", "${fileDirname}/${fileBasenameNoExtension}.exe"],
      "problemMatcher": "$cinza",
      "group": "build"
    }
  ]
}
```

O stack trace de um erro de runtime (`em f (arquivo:linha)`) aparece no terminal, abaixo do erro.

## Desenvolvimento

A extensão vive no repositório da Cinza, em `editor/vscode/`. Para o VS Code carregá-la sem
empacotar, uma junção do Windows aponta para esta pasta (o VS Code está registrado para
`C:\cinza-vscode`):

```
mklink /J C:\cinza-vscode <repositório>\editor\vscode
```

O cliente do servidor de linguagem (`extension.js`) usa o `vscode-languageclient`: na pasta de
desenvolvimento, `npm install` em `editor/vscode` (sem ele, a extensão fica só com o destaque); o
`.vsix` do CI já o leva. O servidor em si é testado pela suíte do compilador (`tests/lsp`, com um
cliente mínimo).

Depois de mudar a gramática: Developer: Reload Window no VS Code, e `make test-vscode` na raiz
do repositório. O teste (`teste/conferir.py`) tokeniza os `.cinza` de `tests/` e `exemplos/` com
o motor de cores do próprio VS Code e compara os trechos marcados como inválidos com
`teste/invalidos.esperado` — cada um é um erro léxico de propósito num teste. Um trecho novo é
um falso positivo (algo válido ficou vermelho); um que some, uma marca perdida. Depois de
conferir à mão uma mudança de propósito, `python editor/vscode/teste/conferir.py --atualizar`
grava a lista nova.

## Histórico

- 0.8.0 — o servidor de linguagem do cinza (`cinza --lsp`): os erros enquanto se digita e a
  formatação do documento (`cinza fmt`); a configuração `cinza.caminho`.
- 0.7.0 — identificadores com acento, literais fora da faixa e os caracteres recusados pelo
  léxico marcados como inválidos; `e.kind`, `e.line` e `e.column` como campos; a interpolação
  sem `}` termina nas aspas; o problem matcher `$cinza`.
- 0.6.0 — structs genéricos, criação pelo nome, tipos escritos como valor.
