# crud_contatos

Uma agenda de contatos: um CRUD no console que grava num arquivo de texto com `Files`. Serve
para exercitar, num projeto de várias pastas, os imports a partir da raiz do projeto (spec 4.11).

```
crud_contatos/
  czm.mod                  marca a raiz do projeto (vazio)
  main.cinza               ponto de entrada: o menu sobre um arquivo
  modelo/contato.cinza     struct Contato, regras de validação, error ContatoInvalido
  dados/formato.cinza      uma linha por contato (id;nome;telefone;email), error LinhaInvalida
  dados/repositorio.cinza  interface Repositorio e a classe ArquivoTexto
  servicos/agenda.cinza    class Agenda: cria, todos, busca, pega, atualiza, remove
  ui/console.cinza         o menu (input, printf), enum Opcao
  util/listas.cinza        funções genéricas: maior_ou[T], junta[T]
  testes/                  os testes da suíte (rodam o CRUD com a entrada simulada)
```

Todo import parte da raiz, em qualquer arquivo: `servicos/agenda.cinza` usa
`import modelo.contato as m;` e `import dados.repositorio as repo;`, e os programas de
`testes/`, uma subpasta, importam do mesmo jeito, porque o `czm.mod` fica acima deles.

Para usar, da raiz do repositório:

```
./cinza.exe tests/projetos/crud_contatos/main.cinza contatos.txt
```

Na suíte (`make test`), só os arquivos com `// expect` desta pasta são testes; o resto é o
código do projeto.
