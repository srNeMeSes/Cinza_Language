# Testes

`make test` roda tudo: os testes de unidade em C++ (`unit_*.cpp`) e, pelo `run_tests.py`, cada
programa `.cinza` das pastas abaixo e os exemplos executáveis da especificação (`spec/`).

Cada programa diz o resultado esperado em comentários (`// expect:`, `// expect-error:`,
`// expect-not:`, `// expect-exit:`, `// args:`, `// stdin:`, `// env:`), descritos no início
do `run_tests.py`. Para rodar só uma parte: `python tests/run_tests.py cinza.exe genericos/`
(o filtro é um trecho do caminho).

`make test-llvm` (ou `python tests/run_tests.py --cinzac`) roda a mesma suíte pelo backend
nativo: cada teste é compilado pelo `cinzac` e o executável roda com os mesmos argumentos,
entrada e ambiente; o que o `cinzac` ainda não compila é contado à parte, com os motivos.
Com `--O2` (`python tests/run_tests.py --cinzac --O2`), cada teste é compilado com otimização.

Todo teste também confere a memória: o `run_tests.py` roda os programas com
`CINZA_VAZAMENTOS=1`, e um programa que termina com algum objeto do runtime ainda vivo sai com
código 70 e falha como "vazamento de memória" (`--sem-vazamentos` desliga a conferência).

## Pastas

| Pasta | Assunto |
|-------|---------|
| `lexico_e_sintaxe/` | léxico, gramática, recuperação de erros, aninhamento |
| `diagnosticos/` | formato das mensagens de erro |
| `programa_e_main/` | nível superior, `main`, argumentos |
| `tipos/` | regras de atribuição, `void`, `var`, `type()` |
| `numeros/` | `i32`/`i64`/`f32`/`f64`, literais, `cast<T>`, estouro, divisão por zero |
| `operadores/` | `and`/`or`/`not`, atribuição composta |
| `fluxo/` | `if`, laços, `break`/`continue`, `range`, retorno, código morto, `exit` |
| `funcoes/` | chamadas, escopo, recursão |
| `const/` | `const` global e local, recipientes `const` |
| `classes/` | campos, métodos, `self`, lugares atribuíveis |
| `structs/` | `struct`: cópia, criação (posicional e pelo nome), campos obrigatórios, recursão |
| `enum/` | `enum` |
| `interfaces/` | `interface` |
| `excecoes/` | `error`, `throw`, `try`/`except`/`finally` |
| `strings_e_printf/` | índices e fatias de texto, `printf`/`format` |
| `importacao/` | `import`, módulos, `pub`, pacotes (`czm.mod`), nomes de módulos nativos |
| `biblioteca/` | funções embutidas e módulos nativos (`Strings`, `Files`, `Math`...) |
| `genericos/` | funções e structs genéricos (spec, capítulo 8) |
| `cvm/` | casos específicos da máquina virtual |
| `nativo/` | o backend nativo (`cinzac`): o que cada etapa da seção P do REVISAO passa a compilar; rodam também na CVM |
| `gc/` | memória: coleta de ciclos, contagem de referências, cadeias longas |
| `programas/` | programas maiores (rede neural, lista ligada) |
| `pacotes/` | pacotes de apoio, dependências locais de `tests/` (`requer ./pacotes/saudacao` no `tests/czm.mod`); os pacotes git são testados pelo `run_tests.py`, com repositórios temporários |
| `projetos/` | projetos de várias pastas, cada um com `czm.mod` próprio (`crud_contatos/`: CRUD de contatos em arquivo) |
| `fuzz/` | o fuzzer (`python tests/fuzz/fuzz.py --tempo 300`: mutações dos programas da suíte contra os dois backends) e o gerador de programas válidos (`python tests/fuzz/gera.py --tempo 300`: o teste diferencial CVM × `cinzac -O0` × `cinzac -O2`); não fazem parte do `make test` |

O prefixo do nome (`a5_`, `k3_`, `gen_`...) é o item do `REVISAO_V2.md` que criou o teste.

Numa pasta com `czm.mod` próprio (os `projetos/`), só os arquivos com `// expect` são testes; o
resto é o código do projeto, importado por eles.

## Módulos de apoio

`modulos/` não tem testes: são os módulos importados por eles (`import modulos.geom;`). O arquivo
`tests/czm.mod` (vazio) faz de `tests/` a raiz do projeto (spec 4.11), então o mesmo import vale
em qualquer subpasta, também ao rodar um teste à mão:

```
./cinza.exe tests/genericos/gen_modulo.cinza
```

`spec_exemplos/` é gerada a cada execução (fora do git); `tests_adm/` é local e não entra na
suíte.
