# Plano do livro da Cinza (Q7)

Documento de trabalho: o mapa do livro, as convenções e o andamento. Cada capítulo é escrito
numa etapa própria, conferido contra a spec e com os exemplos rodando nos dois backends; ao
terminar, a caixa dele é marcada aqui. Quem retoma o trabalho lê este arquivo primeiro.

## Público e tom

- **Para quem já programa** (decidido pelo autor em 2026-11-25): o leitor conhece alguma
  linguagem — Python, Java, C, C#, JavaScript, Go. O livro não explica o que é uma variável ou um
  laço; explica como a Cinza faz e **por quê** (sem `null` solto, sem herança, sem closures, as
  duas famílias de números, a ordem de inserção do dict...).
- Comparações com outras linguagens quando ajudam ("como o `Optional` do Java, mas conferido na
  compilação"), em quadros curtos, nunca como base da explicação.
- Português do Brasil, direto, segunda pessoa ("você"). Termos da linguagem como na spec.
- O livro **ensina**; a spec **define**. Cada capítulo aponta as seções da spec que cobre, para
  quem quer a regra exata; o livro não repete a spec tabela por tabela.
- **Regra editorial: ensinar a linguagem e o porquê do desenho dela.** Cada recurso vem com a
  decisão por trás (as classes: encapsulamento sempre, composição e interfaces em vez de herança;
  os genéricos: conferidos no uso, sem restrições declaradas; o opcional: a ausência no tipo). O
  que a Cinza deixa de fora de propósito (spec, README: "O que fica fora") é explicado como
  decisão, não como lacuna. Clareza e precisão antes de extensão.

## Convenções

- **Onde:** `livro/`, um arquivo Markdown por capítulo (`01-comecando.md`, ...), mais
  `livro/README.md` com o sumário. Os apêndices em `livro/apendice-a-gramatica.md` etc.
- **Exemplos executados pela suíte** (decidido pelo autor em 2026-11-25: o código limpo, sem as
  linhas `// expect`, e a saída à parte):
  - ` ```cinza nome.cinza ` — o programa, como o leitor o escreveria (o nome é opcional; sem
    ele, `prog.cinza`);
  - logo depois, ` ```saida ` — a saída exata, linha por linha — e/ou ` ```erro ` — a mensagem de
    erro, com o arquivo pelo nome do bloco (`ola.cinza:2:23: SyntaxError: ...`); cada linha é
    conferida como trecho;
  - um ` ```console ` entre os dois, com o comando que o leitor digita, é só ilustrativo;
  - os raros casos com argumentos, entrada, ambiente, código de saída ou um trecho que não pode
    aparecer: comentários HTML logo antes do código, que o leitor não vê — `<!-- args: a b -->`,
    `<!-- stdin: linha -->`, `<!-- env: NOME=valor -->`, `<!-- exit: 2 -->`,
    `<!-- nao: trecho -->`.

  O `tests/run_tests.py` extrai cada exemplo para `tests/spec_exemplos/livro/<capítulo>_<linha>/`
  e o roda de dentro da pasta, com o nome puro, na CVM e no cinzac; um exemplo quebrado quebra o
  `make test`. Um ` ```cinza ` sem saída nem erro é só ilustrativo. Trechos que não são programas
  completos (uma linha de comando, um `czm.mod`, um fragmento) vão em ` ```text ` ou
  ` ```console `.
- **Exemplos com vários arquivos** (módulos, pacotes, arquivos lidos do disco): o projeto de
  exemplo fica em `livro/exemplos/<projeto>/`, com o seu `czm.mod`; os `.cinza` dele com
  expectativas são testes; o capítulo mostra os arquivos e a saída.
- **Exercícios:** de 3 a 5 por capítulo, no fim, do mais simples ao mais aberto. As soluções em
  `livro/solucoes/NN-nome.md`, no mesmo formato dos capítulos, rodadas pela suíte.
- **PDF** (pedido do autor em 2026-11-25): os `.md` são a fonte; ao fim, um `make livro` gera o
  livro inteiro num PDF (e num HTML) pelo pandoc, com o sumário, a numeração e os blocos de saída
  e de erro com rótulo próprio; o PDF entra na release.
- **Estrutura de cada capítulo:** os objetivos (o que o leitor sabe fazer ao fim, 2 a 4 tópicos)
  e os pré-requisitos (os capítulos de que ele depende); as seções, cada uma
  com um exemplo curto que roda; um "Programa do capítulo" que junta tudo num programa de verdade
  (de 30 a 80 linhas); "Resumo" em tópicos; "Exercícios"; "Na spec" (as seções correspondentes).
- **Quadros:** `> **Por quê?**` (a decisão de desenho por trás da regra), `> **Vindo de outra
  linguagem**` (a diferença que costuma pegar), `> **Cuidado**` (o erro comum e a mensagem que
  ele produz).
- **Mensagens de erro:** sempre como o compilador as mostra (copiadas da execução, não
  inventadas), para o leitor reconhecer na tela. Se uma mensagem ficar confusa ao ser explicada,
  ela é melhorada no compilador (com teste), não no livro.

## Fio condutor

Os "Programas do capítulo" das Partes II a IV constroem aos poucos um mesmo programa, o
**gerenciador de tarefas** `tarefas`: a lista de tarefas (cap. 7), as tarefas com prazo e
prioridade (structs e enums, cap. 9 e 10), o que pode faltar (opcional, cap. 11), a classe
`Agenda` (cap. 12), filtros por interface (cap. 13), os erros de entrada (cap. 14), a coleção
genérica (cap. 17), a divisão em módulos (cap. 18), a gravação em JSON (cap. 22) e os prazos com
datas (cap. 23). O capítulo 26 o fecha como um programa de linha de comando completo. Cada
versão roda sozinha: o leitor pode pular para qualquer capítulo.

## Sumário

### Abertura

- [ ] **Prefácio e como ler este livro** — o que é a Cinza (estática, disciplinada, dois
  backends); para quem é o livro; como os capítulos se organizam; as convenções dos exemplos e dos
  quadros; a versão da linguagem coberta (0.6).

### Parte I — Primeiros passos

- [x] **1. Começando** (2026-11-25) — instalar pelos pacotes da release (Windows e Linux, sem dependências) e,
  para quem quiser, compilar do código-fonte; o primeiro programa (`fn main`, `print`,
  comentários); `cinza prog.cinza` (roda na hora) e `cinzac prog.cinza -o prog` (executável
  nativo); a extensão do VS Code e o `cinza fmt`; ler a primeira mensagem de erro (o formato
  `arquivo:linha:coluna: Tipo: mensagem`); argumentos da linha de comando (`main(list<string>
  args)`). *Spec: 4.2, 7.3, 7.4.*
- [x] **2. Valores, tipos e variáveis** (2026-11-25) — `i32`, `i64`, `f32`, `f64`, `string`, `bool`; os
  literais (o tipo do inteiro pelo tamanho; `1.5f` e `1.5ff`: a precisão sempre escrita; sem `0x`
  nem expoente); declarar com tipo e com `var`; o valor inicial obrigatório; as duas famílias que
  não se misturam e a promoção dentro da família; `cast<T>` e as conversões que podem falhar;
  estouro é erro, não volta ao começo; sem infinito nem NaN; como os números aparecem no `print`.
  Fecha com **o mapa do sistema de tipos**: primitivos e compostos, o que é valor (copiado) e o
  que é referência (compartilhado), o implícito (só alargar dentro da família) e o explícito
  (`cast`), o que nunca é `null`, o que é conferido na compilação e o que sobra para a execução —
  cada item com o capítulo que o aprofunda. *Spec: 1.5, 3.1, 3.2, 3.5, 3.6, 5.1, 5.4.*
- [ ] **3. Operadores e expressões** — aritmética (a divisão inteira trunca em direção a zero; o
  sinal do `%` segue o dividendo); comparação, igualdade (estrutural e por identidade, adiantada) e
  os lógicos com `and`/`or`/`not`; concatenação com `+`; a atribuição composta e a regra do tipo
  do alvo; a tabela de precedência; a ordem de avaliação (da esquerda para a direita, o
  curto-circuito). *Spec: 2.4, 5.2, 5.3.*
- [ ] **4. Controle de fluxo** — `if`/`else if`/`else` (sempre com chaves); `while`; `for` sobre
  lista, dict, texto e `range` (os elementos copiados no início); `break` e `continue`; o que o
  compilador confere no fluxo: o retorno em todos os caminhos e o código inalcançável. *Spec: 5.5.*
- [ ] **5. Funções** — declaração, parâmetros e retorno; o que é copiado e o que é compartilhado
  ao passar um valor (a tabela de 5.1, primeira vez); recursão e o limite de 2000 níveis
  (`StackOverflowError`); sem sobrecarga; funções não são valores (e o que se faz no lugar: o
  capítulo 13); as embutidas (`print`, `range`, `type`, `input`, `exit`) e a precedência de uma
  função do programa com o mesmo nome; o `exit` e os `finally`. *Spec: 4.4, 4.5, 5.1, 5.8, 6.1.*

### Parte II — Dados

- [ ] **6. Textos** — `string` em UTF-8: tamanho e índices em caracteres, não em bytes; índices
  negativos e fatias com passo; strings imutáveis; `printf` e `format` com interpolação e
  formatos (`{x:.2f}`, `{nome:10}`); o módulo `Strings` por tarefa (buscar, partir, limpar,
  montar, testar), com os opcionais de `find`. *Spec: 1.5, 5.7, 6.1 (interpolação), 6.2 Strings.*
- [ ] **7. Listas** — `list<T>`: literal, índice, `add`/`remove`/`has`/`size`; a lista é
  compartilhada (o aliasing e quando copiar com `Lists.copy`); a invariância (`list<i32>` não é
  `list<i64>`) e o literal que se adapta; o módulo `Lists`: ordenar (estável; `sort_by` para
  ordenar objetos), buscar, estatística, listas novas e alteração no lugar. Programa: a primeira
  lista de tarefas. *Spec: 3.3, 3.5, 5.7, 6.2 Lists.*
- [ ] **8. Dicionários e pares** — `dict<K, V>` na ordem de inserção; ler (`d[k]` e o
  `KeyError`), inserir (`add`) e trocar (`d[k] = v` só troca); as chaves permitidas e por quê;
  `==` sem ligar para a ordem; o `for` com `pair<K, V>`; `pair<A, B>` imutável; o módulo `Dicts`
  (`get` devolve opcional, `items`, `merge`, `invert`...). Programa: contar palavras de um texto.
  *Spec: 3.3, 5.7, 6.2 Dicts.*
- [ ] **9. Structs** — o agrupamento de campos que é um **valor**: cópia na atribuição, no
  argumento e no retorno (cópia rasa); os três jeitos de criar (`new P()`, em ordem, pelo nome);
  os campos obrigatórios; por que atribuir a um temporário é erro; um struct que contém uma lista
  de si mesmo. Programa: a tarefa vira um struct. *Spec: 4.7, 5.1.*
- [ ] **10. Enums e tipos como valores** — `enum` (valores só nomes, sempre qualificados, chave de
  dict); `type(x)` e os tipos escritos como valor; o que a Cinza não tem (reflexão) e por quê.
  Programa: prioridade e situação da tarefa. *Spec: 3.8, 4.8.*
- [ ] **11. O opcional: `T?` e `null`** — o `null` só onde o tipo diz; `valor else padrão`, o
  `else` com bloco e `yield`; `if (T x = opcional)` e `while (T x = opcional)`; sem estreitamento
  (`x != null` não muda o tipo); a biblioteca devolve opcionais (`find`, `Dicts.get`, `Env.get`,
  `input`, `Convert.to_i32`); quando usar um opcional e quando um erro. Programa: buscar uma tarefa
  pelo nome. *Spec: 3.10, 6.2 (o que pode faltar).*

### Parte III — Organizando o programa

- [ ] **12. Classes** — campos sempre privados, `pub { }` para os métodos públicos, o
  construtor; os campos obrigatórios que o construtor atribui (e as regras que garantem que nenhum
  objeto existe pela metade); `self`; os valores iniciais e a ordem da criação; o objeto é
  compartilhado e a igualdade é por identidade; sem herança — e o que fazer no lugar. Programa: a
  classe `Agenda`. *Spec: 4.6, 5.1, 5.3.*
- [ ] **13. Interfaces** — o contrato que uma classe declara cumprir; listas de interface e
  polimorfismo; um valor de interface nunca volta a ser a classe; como uma interface substitui a
  função passada como argumento (estratégia, filtro, comparador); `Lists.sort_by` como a outra
  saída. Programa: filtros de tarefas. *Spec: 3.7, 4.9.*
- [ ] **14. Erros e exceções** — os erros de execução e os seus tipos; `error Nome;` para os seus;
  `throw`, `try`/`except`/`finally` (a ordem dos `except`, o `finally` que sempre roda, o erro no
  `finally` que substitui o anterior); relançar com `throw e`; os campos `kind`, `message`,
  `line`, `column`; o stack trace; erro ou opcional: a regra da biblioteca. Programa: validar o que
  o usuário digita. *Spec: 4.10, 5.6, 7.1, 7.3.*
- [ ] **15. `const` e imutabilidade** — `const` em local, parâmetro e global; o recipiente
  inteiro fechado, em qualquer profundidade; o `const` que para no objeto de classe (e por que
  `const Pessoa p` é erro); o `const` global como expressão constante calculada na compilação.
  *Spec: 3.9, 4.3.*
- [ ] **16. Genéricos: funções** — `fn maior[T](T a, T b) -> T`; a monomorfização (uma cópia
  por combinação de tipos, sem custo na execução); a conferência no uso (sem restrições
  declaradas: `T` precisa ter o que o corpo usa — e por que a Cinza escolheu isso em vez de
  `T: Forma`, com o preço: o erro aparece no uso, com o rastro); a dedução dos tipos e os tipos explícitos;
  `cast<T>` para literais genéricos; o rastro dos usos nos erros; o limite de 64 cópias
  encadeadas. *Spec: 8.1 a 8.5.*
- [ ] **17. Genéricos: structs, classes e interfaces** — `struct Par[A, B]`, `class Pilha[T]`,
  `interface Comparavel[T]`; os tipos sempre escritos no uso; o corpo do método conferido só
  quando ele é usado; uma classe genérica que cumpre uma interface genérica. Programa: uma
  coleção genérica de tarefas. *Spec: 8.6, 8.7.*
- [ ] **18. Módulos e projetos** — o projeto (`czm.mod`, `cinza init`); `module a.b;` no começo
  de cada arquivo importado; `import` pela raiz do projeto, o apelido e o `as`; `pub`; o que um
  módulo enxerga; os módulos nativos; como organizar as pastas. Programa: o `tarefas` dividido em
  módulos. *Spec: 4.11.*
- [ ] **19. Pacotes** — dependências git e locais no `czm.mod` (`requer`); `cinza add`, `remove` e
  `get`; o `czm.lock` e o cache; a escolha das versões (a maior pedida, sem misturar majors); como
  publicar um pacote seu (uma tag `vX.Y.Z`). *Spec: 4.11 (o czm.mod, pacotes).*

### Parte IV — A biblioteca na prática

- [ ] **20. Arquivos, ambiente e processos** — `Files` (ler e gravar inteiro, linhas, bytes,
  pastas, caminhos; toda falha é `IOError`); `Env` (só leitura); `Process` (sem shell: os
  argumentos como estão); `input`, `args` e `exit` num utilitário de linha de comando. Programa:
  o `tarefas` lendo e gravando um arquivo de texto. *Spec: 6.1, 6.2 Files, Env, Process.*
- [ ] **21. Números: `Math`, `Random` e `Convert`** — as funções seguem a largura do argumento;
  domínio e estouro como erros; arredondamentos (`round` se afasta do zero; `round_to` arredonda
  o número como ele é escrito); `is_close` e por que `0.1 + 0.2 != 0.3`; números aleatórios com
  semente; `Convert` para bases e para a conversão que pode falhar sem erro. *Spec: 6.2 Math,
  Random, Convert.*
- [ ] **22. Expressões regulares e JSON** — `Regex` (o motor linear no estilo do RE2: nenhum
  padrão trava o programa; a sintaxe, os grupos, as trocas com `$1`, o `split`); `Json` (o
  `Json.Value` e o `Json.Kind`, ler com `field` e `get`, montar e gravar, `to_text_pretty`).
  Programa: o `tarefas` gravando em JSON. *Spec: 6.2 Regex, Json.*
- [ ] **23. Datas e tempo** — o instante como `i64`; horário local e UTC; montar, desmontar,
  `format` e `parse`; contas com dias e dias de calendário; medir duração com `clock`. Programa:
  os prazos das tarefas. *Spec: 6.2 Time.*

### Parte V — Indo além

- [ ] **24. Os dois compiladores** — o caminho de um programa: léxico → sintaxe → semântico (o
  front-end comum) → bytecode e a máquina de registradores (CVM) ou → CIR → LLVM → objeto e lld
  (nativo); a CVM (`cinza`) e o nativo (`cinzac`): a mesma linguagem, a
  mesma saída e os mesmos erros; quando usar cada um; `-O2`; os executáveis sem dependências;
  diagnosticar um programa (o stack trace, rodar nos dois para comparar); ver
  o bytecode (`--bytecode`) e o IR (`--emit-llvm`); medir o desempenho do seu programa; a memória
  (contagem de referências e o coletor de ciclos, sem destrutores). *Spec: 5.9, 7.4; os DESENHO.md.*
- [ ] **25. Ferramentas e hábitos** — o `cinza fmt`; o servidor de linguagem e a extensão do VS
  Code; ler as mensagens de erro (os léxicos saem todos juntos, o sintático se recupera, o
  semântico para no primeiro; o rastro dos genéricos); testar o próprio programa (um padrão
  simples de testes com `throw` e o código de saída). *Spec: 7.2, 7.3.*
- [ ] **26. Projeto final: o `tarefas` completo** — o programa de linha de comando inteiro, com
  módulos, classes, interfaces, genéricos, opcionais, erros, JSON em arquivo e datas, comentado
  parte por parte; como estendê-lo. O código em `livro/exemplos/tarefas/`, rodado pela suíte.

### Apêndices

- [ ] **A. Gramática** — a EBNF da Cinza (a do capítulo 2 da spec, com notas de leitura).
- [ ] **B. Precedência e associatividade** — a tabela dos operadores, com `not`/`and`/`or` e o
  `else`.
- [ ] **C. Palavras reservadas** — a lista, os sinônimos (`and`, `or`, `not`, `str`) e as
  embutidas que não são reservadas.
- [ ] **D. Referência rápida da biblioteca** — cada módulo numa página: as funções com a
  assinatura e uma linha, apontando para a spec.
- [ ] **E. Mensagens de erro** — as mensagens mais comuns de cada fase (léxico, sintaxe, import,
  semântico, execução), o que significam e como resolver (o item do Q7 "as mensagens revistas
  como referência").
- [ ] **F. Vindo de outra linguagem** — Python, Java, C#, C, C++, JavaScript e Go: o que muda,
  em tabelas (nulos, números, coleções, classes, erros, genéricos, módulos), sem tentar fazer da
  Cinza uma cópia de nenhuma delas.
- [ ] **G. Glossário** — os termos da linguagem e do livro.

## Fora do livro (o resto do Q7)

- [ ] **O PDF e o HTML do livro** (`make livro`, pelo pandoc): os capítulos e os apêndices na
  ordem do sumário, os blocos `saida` e `erro` com rótulo, os comentários HTML de fora; o PDF na
  release.
- [ ] **Guia de instalação e uso** (`INSTALAR.md`, ou a seção do capítulo 1 extraída): os pacotes,
  o PATH, os dois comandos e as opções, compilar do código-fonte.
- [ ] **README** em português e em inglês, apontando para o livro, a spec e o guia.
- [x] **O suporte da suíte**: o `run_tests.py` lendo os exemplos de `livro/` e as soluções de
  `livro/solucoes/` (antes do primeiro capítulo, para que cada um já nasça testado). — 2026-11-25
- [x] **O livro no pacote da release** (`distribuicao/empacota.sh`, ao lado da spec; o `PLANO.md`
  fica de fora). — 2026-11-25

## Ordem de trabalho

1. O suporte da suíte e o `livro/README.md` (o sumário com os links).
2. Os capítulos na ordem, **um por etapa**: escrever; rodar os exemplos e as soluções nos dois
   backends; reler contra a spec; marcar a caixa aqui, com a data. Os defeitos ou as mensagens
   ruins achados no caminho entram no compilador com teste, como em qualquer correção.
3. Os apêndices (A a C saem da spec; D e E ao fim, quando as mensagens já foram vistas no livro
   todo; F e G por último).
4. O prefácio, o guia de instalação e os READMEs.
5. Uma releitura do livro inteiro, de ponta a ponta, atrás de incoerências entre os capítulos.

Commits por parte do livro, num ramo `q7-livro`; merge no `main` quando o autor pedir.

## Andamento

| Data | O que foi feito |
|------|-----------------|
| 2026-11-25 | Plano escrito; público decidido (quem já programa). |
| 2026-11-25 | O suporte da suíte (`livro/*.md`, `livro/solucoes/`, `livro/exemplos/`); o sumário (`livro/README.md`); o capítulo 1, com 10 exemplos e 4 soluções, nos dois backends. Achados no caminho, corrigidos no compilador com teste: o `;` que falta é relatado no fim da linha dele (era no começo da seguinte); as colunas contam caracteres, não bytes (decidido pelo autor; spec 7.3; o LSP converte); o README da extensão listava o `?` como inválido. |
| 2026-11-25 | Os exemplos no formato do livro (o código limpo, ```saida e ```erro à parte; decidido pelo autor). O capítulo 2, com 23 exemplos e as soluções. Achado no caminho, corrigido no léxico com teste: um literal com expoente (`6.022e23`, `1.5e-3`) diz que não há notação com expoente e sugere o `cast<f64>("...")` (antes falava do sufixo `f`/`ff`, e `1.5e-3` virava `1.5e` menos 3). |
| 2026-11-25 | Revisão externa do plano: entram a regra editorial do porquê, objetivos e pré-requisitos por capítulo, o mapa do sistema de tipos (cap. 2), o caminho da compilação e o diagnóstico (cap. 24), C++ e C# no apêndice F. |
