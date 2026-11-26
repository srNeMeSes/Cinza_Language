# Backend nativo da Cinza (LLVM) — desenho

Versão de revisão 2, 2026-11-18 (a revisão 1 recebeu uma revisão externa; o que mudou está na
etapa P0 do `REVISAO_V2.md`). Nada daqui está implementado: este documento fecha os contratos
antes do código (plano e etapas no `REVISAO_V2.md`, seção P). Onde houver uma escolha ainda
aberta, ela está marcada **[decidir]**.

## 0. Objetivo e princípios

- `cinzac prog.cinza -o prog.exe` gera um executável nativo que se comporta **exatamente** como
  `cinza prog.cinza` na CVM: mesma saída, mesmos erros (tipo, mensagem, posição, stack trace),
  mesmo código de saída. A especificação (`spec/`) é a referência; a CVM é a implementação que a
  confirma.
- O LLVM é um **backend**: gera e otimiza código. A semântica da linguagem mora no front-end
  (léxico, parser, semântico, genéricos) e no runtime, que são comuns aos dois backends. Trocar o
  LLVM no futuro não pode exigir mexer no front-end.
- A CVM continua até o `cinzac` passar a suíte inteira; então se decide o papel dela.
- Nada muda na linguagem. Onde a spec não dizia algo de que o backend depende, a regra é
  escrita a partir do que a CVM já faz (seção 5.4).

## 1. Arquitetura

```
léxico → parser → semântico (AST analisada, genéricos copiados)            ← front-end, sem LLVM
             ├── compilador da CVM (da AST) → bytecode → CVM            → cinza prog.cinza
             └── AST → CIR (posse, temporários, erros, limpeza)
                       → CIR → LLVM → .o → lld embutido (ligação, Q5)   → cinzac prog.cinza -o prog.exe
runtime (C++; contagem embutida + coleta de ciclos; interface C)          ← comum aos dois
```

- **Dois executáveis.** `cinza` (CVM) não depende do LLVM. `cinzac` liga a `libLLVM`. Os dois
  usam as mesmas bibliotecas estáticas do front-end e do runtime.
- **A CVM continua compilando da AST**, sem passar pela CIR: são duas implementações
  independentes da mesma semântica, e por isso uma confere a outra.
- **Ferramentas, versões fixas:** compilador do projeto GCC 15.2 (WinLibs, UCRT, `libstdc++`);
  LLVM **20.1.8** do MSYS2 CLANG64, usado só pela **API em C** (`llvm-c`), que não depende da
  ABI de C++ (o LLVM do CLANG64 é compilado com `libc++`, incompatível com a `libstdc++` do
  GCC). Os cabeçalhos vêm do pacote `mingw-w64-clang-x86_64-llvm` (instalado no P3, na mesma
  versão 20.1.8-2 da `llvm-libs`). O GCC os inclui com `-idirafter`, para os cabeçalhos do
  CLANG64 não passarem na frente dos dele; só `llvm/emissor.cpp` e `llvm/cinzac.cpp` os usam.
- **Cabeçalhos e biblioteca da mesma versão, nunca misturados.** O banco local do pacman lista o
  pacote de cabeçalhos em `20.1.8-2`, igual à `libLLVM-20.dll` instalada; o repositório do
  MSYS2 pode já estar numa versão mais nova (22.x). Instalar com `pacman -S` **sem** `-y` (sem
  atualizar o banco) traz os cabeçalhos da 20.1.8. Se o espelho não tiver mais esse arquivo, a
  alternativa é atualizar o conjunto inteiro (`llvm`, `llvm-libs`, `clang`, que o build com
  ASan também usa) para a mesma versão nova e fixar essa — decisão do autor na etapa P3.
- **Alvo:** `x86_64-w64-windows-gnu` (o do GCC do projeto). Os objetos gerados ligam com o
  runtime compilado pelo mesmo GCC.
- **Dependências:** o `cinzac` precisa da `libLLVM-20.dll` (139 MB) e das 7 DLLs de que ela
  depende (`libc++`, `libffi-8`, `libiconv-2`, `liblzma-5`, `libxml2-16`, `libzstd`, `zlib1`) e
  do `g++` para ligar. O `make cinzac` copia as oito para `llvm/bin/`, ao lado do `cinzac.exe` e
  da `libcinzart.a`: o Windows procura ali antes do `PATH`, onde o `C:\MinGW\bin` do GCC tem
  outras versões de `zlib1`, `libzstd` e `libxml2-16` (com elas o `cinzac` caía com
  `0xC0000139`). **Os programas gerados não dependem do LLVM**: ligam com o runtime, com
  `-static-libstdc++ -static-libgcc`.

## 2. Runtime (pré-requisito: etapa P1)

O runtime de hoje (`value.h`, `gc_object.h`, `gc.h`, `operacoes.cpp`, `stdlib.cpp`) é usado pela
CVM em C++. Para o código nativo ele muda de forma — e a CVM muda junto, conferida pelos testes,
antes de existir uma linha de LLVM.

### 2.1 Objeto com cabeçalho e contagem embutida

Hoje: `std::shared_ptr` (contagem **atômica**, fora do objeto, inacessível ao código gerado).
Depois: todo objeto do runtime começa com o cabeçalho

```c
typedef struct CzObjeto {
    uint32_t refs;        // contagem de referências (não atômica: a Cinza não tem threads)
    uint8_t  tipo;        // CZ_STRING, CZ_LISTA, CZ_DICT, CZ_PAR, CZ_OBJETO, CZ_STRUCT, CZ_ERRO
    uint8_t  marcas;      // CZ_IMORTAL (literais), CZ_RASTREADO, bits da coleta de ciclos
    uint16_t _reservado;
    const void* desc;     // descritor: classe, struct (nome e campos) ou nada
    struct CzObjeto* ant;   // no registro da coleta de ciclos (só contêineres rastreados);
    struct CzObjeto* prox;  //   depois de morto, `prox` encadeia a fila de destruição
} CzObjeto;               // 32 bytes
```

Os contêineres rastreados (lista, dict, par, objeto, struct) acrescentam, depois do cabeçalho, o
que a coleta de ciclos usa durante a varredura (`gc_refs`, `gc_marca`: `GcObject` em
`gc_object.h`); strings e erros ficam só com os 32 bytes. *(Implementado no P1.)*

- **Contagem não atômica**, decisão explícita: a Cinza não tem threads. Se um dia tiver, esta
  decisão é revista (contagem atômica ou objetos por thread).
- **Todo objeto nasce com `refs = 1`**: a posse inicial é de quem o criou (a função do runtime
  que o devolve com +1, seção 7.1).
- **`retain`** é `++refs` (exceto se `CZ_IMORTAL`); **`release`** é `--refs` e, chegando a zero,
  `cz_destroi(o)`. `release` de um ponteiro nulo não faz nada (seção 4.3). No código nativo os
  dois são inline; `cz_destroi` é chamada ao runtime. **Nenhum dos três lança exceção** nem
  aloca memória.
- **Verificações no build de depuração** (`make debug`, ASan): `release` com `refs == 0`
  (liberação dupla) e `retain` que estoura o contador param o programa com erro interno. No build
  normal não há teste (o contador de 32 bits não estoura: cada referência ocupa memória).

**Destruição iterativa (sem recursão).** Corrige um defeito de hoje: soltar uma cadeia de 400 mil
nós (`class No { list<No> f; }`) estoura a pilha do sistema na CVM (`0xC00000FD`), porque o
`shared_ptr` destrói em recursão. É o *trashcan* do CPython. O contrato:

1. O objeto chega a `refs == 0`.
2. Se é rastreado (`CZ_RASTREADO`), sai do registro da coleta de ciclos (`ant`/`prox`).
3. Entra na **fila de destruição**, que é intrusiva: o próprio campo `prox` encadeia a fila (o
   objeto já saiu do registro, então o campo está livre). Nenhuma alocação durante a destruição.
4. Se a fila já está sendo esvaziada (um `cz_destroi` mais acima na pilha), para aqui.
5. Senão, um laço esvazia a fila: tira um objeto, solta as referências dos campos dele (um filho
   que chega a zero entra na mesma fila, pelos passos 1 a 4) e devolve a memória do objeto.
6. A profundidade da pilha do sistema fica constante, qualquer que seja o tamanho da cadeia.

Durante o esvaziamento nenhuma coleta de ciclos começa (a coleta só roda ao criar contêineres, e
destruir não cria nada); a própria coleta, ao esvaziar os contêineres do lixo, alimenta a mesma
fila. O teste da cadeia longa é **obrigatório** na migração (não um teste avulso): ele roda na
suíte normal e no modo sem vazamento.
- **Coleta de ciclos** (`gc.h`) sem mudança de algoritmo: *trial deletion* sobre os contêineres
  rastreados (lista, dict, par, objeto, struct), com `refs` no lugar de `use_count`. Não precisa
  enumerar raízes na pilha: uma referência de uma variável local é contagem que sobra. Strings e
  erros **não** são rastreados (não contêm contêineres, não formam ciclo).
- **Q11 (2026-11-24): gerações e tipos acíclicos.** Os contêineres nascem jovens; a coleta comum
  percorre só os jovens (uma referência de um velho para um jovem conta como de fora) e promove
  os que sobram; a completa, as duas gerações, roda quando os velhos crescem metade desde a
  última. Os objetos de classes e structs cujo tipo não alcança a si mesmo pelos campos (o
  semântico decide; o descritor leva `rastreia`) nem entram no registro: nunca estão num ciclo.
- **Q11: alocação.** Os objetos do runtime, os vetores de campos e os nós dos dicts vêm de um
  alocador de objetos pequenos (`aloca::` em `gc_object.h`: classes de 16 bytes até 256, listas
  livres, sem trava); no build de depuração, direto do heap (para o ASan).
- **Quando a coleta roda:** nas funções do runtime que criam contêineres (o equivalente ao
  `NEWOBJ` de hoje), quando nasceram contêineres suficientes. É seguro porque todo ponteiro vivo no
  código gerado é **dono** de uma referência (seção 3): nenhum ponteiro emprestado atravessa uma
  chamada ao runtime sem um dono que o conte.

### 2.2 Strings

Hoje: `std::string` **por valor** dentro do `Value` — atribuir, passar ou pôr numa lista copia o
texto inteiro. Depois: objeto imutável com contagem.

**Implementado no P1:** `CzString` é o cabeçalho mais o texto, imutável, num `std::string`
interno (`const std::string texto`). O código do runtime continua lendo `asString()` como
`const std::string&`, sem mudar os ~135 lugares que o usam. O código nativo **nunca** lê o layout
da string: só pelas funções do runtime (`cz_str_dados(s)`, `cz_str_bytes(s)`), então a forma
interna pode mudar sem afetar ninguém. Quando a medição pedir, a otimização é guardar os bytes
na mesma alocação, logo depois do cabeçalho (`sizeof(CzString) + bytes + 1`, UTF-8 terminado em
`'\0'`, acessados por `reinterpret_cast<char*>(s + 1)` e não por um membro de array flexível,
que não é C++ padrão), e guardar o número de caracteres (o `s.size()` da linguagem).

Literais do programa nativo são `CzString` constantes no executável, com `CZ_IMORTAL` (sem
alocação, sem contagem). Concatenação, fatia e conversões criam strings novas.

### 2.3 O `Value` vira um valor de 16 bytes, compatível com C

```c
typedef struct CzValor {
    uint8_t  tipo;        // VOID, I32, I64, F32, F64, BOOL, ENUM, TIPO, ou um ponteiro de objeto
    uint8_t  _r[3];
    uint32_t extra;       // ENUM: o índice do valor
    union { int32_t i32; int64_t i64; float f32; double f64; uint8_t b;
            CzObjeto* obj; const void* ptr; } u;     // ptr: TypeInfo* (TIPO), CzEnumDesc* (ENUM)
} CzValor;
```

**Três tipos, papéis distintos** — mesmo layout não torna dois tipos C++ intercambiáveis: tratar
um vetor de `CzValor` como um vetor de `Value` (`reinterpret_cast`) é comportamento indefinido,
porque ali não existem objetos `Value`.

- **`CzValor`**: a estrutura C acima, sem construtor nem destrutor. É o que atravessa a interface
  C (seção 7) e o que as coleções guardam. Quem a segura cuida da posse (seção 3).
- **`Value`**: a classe C++ da CVM e do código do runtime. Contém um `CzValor` como único membro
  (`v`); o construtor de cópia faz `retain`, o destrutor faz `release`, e a cópia de struct
  continua clonando. Os acessores conferem o tipo (erro interno, nunca comportamento
  indefinido) e devolvem os objetos como ponteiro sem posse (`CinzaList*`...). *(Implementado
  no P1.)*
- **Chamada de uma função da biblioteca pelo código nativo** (`cz_nativa(id, args, n)`, P3): o
  runtime monta um pequeno vetor de `Value` a partir dos `CzValor` recebidos (uma cópia com
  `retain` por argumento) e chama a `NativeFn` de hoje, sem mudar as ~200 funções da
  biblioteca e sem `reinterpret_cast`. Uma vista sem posse (`ValorRef`, com os mesmos
  acessores de leitura) fica como otimização, se a medição mostrar que a cópia pesa.

O layout é conferido na compilação do runtime e por um teste do `cinzac` (seção 8.4):

```c
static_assert(sizeof(CzValor) == 16 && alignof(CzValor) == 8);
static_assert(offsetof(CzValor, tipo) == 0 && offsetof(CzValor, extra) == 4 &&
              offsetof(CzValor, u) == 8);
static_assert(sizeof(CzObjeto) == 32 && offsetof(CzObjeto, refs) == 0);
```

No código nativo o `CzValor` aparece só onde um valor precisa ser embrulhado: elementos de
coleções, argumentos das funções nativas, `print`.

### 2.4 Descritores sem AST

Antes, `ClassInstance`, `StructValue` e os valores de enum apontavam para a declaração na AST
(`ClassDecl*`, `StructDecl*`, `EnumDecl*`), e cada objeto guardava uma cópia do nome da classe.
**Implementado no P1:** o runtime usa descritores próprios, guardados no campo `desc` do
cabeçalho; a CVM os monta a partir da AST (a imagem é a dona) e o `cinzac` os emitirá como
constantes. O `value.h` não inclui mais a AST.

- `CzClasseDesc`: nome completo (para `<Nome object>`, mensagens e `type()`), índice na tabela
  de classes do backend (métodos, tabelas de interface) e número de campos;
- `CzStructDesc`: nome e nomes dos campos (para o `print` `Ponto{x: 1}` e `s.campo`);
- `CzEnumDesc`: nome e nomes dos valores (`Cor.Verde`);
- o tipo de `type()` continua sendo o `TypeInfo` de `types.h`: ele não depende da AST (é uma
  estrutura pura, única por tipo, comparada por ponteiro), então serve de descritor de tipo
  para os dois backends — o `cinzac` vai criá-los pelo mesmo `TypeContext`;
- `CzFuncao`: nome no stack trace e posição da declaração (seção 6) — entra com o backend
  nativo (P8); a CVM continua com os protótipos dela.

### 2.5 Erros

`ErrorValue` vira objeto com contagem (`CzErro`; **implementado no P1**, não rastreado pela
coleta): tipo (o nome: `"ValueError"`, `"MeuErro"`), mensagem, posição (arquivo, linha, coluna) e
stack trace (lista de linhas, montada quando o erro nasce). O sinal de `exit(código)` continua sendo um erro de tipo especial
(`EXIT_KIND`), que nenhum `except` captura (seção 5).

### 2.6 Testes de memória

- **Contador de objetos vivos** no runtime (já existe para os contêineres:
  `gc_registro.vivos`; passa a contar todos os objetos). Com a variável de ambiente
  `CINZA_VAZAMENTOS=1`, ao sair o runtime solta os `const` globais, roda uma última coleta e, se
  sobrar algum objeto vivo, escreve quantos e de que tipo no stderr e sai com código 70.
  **Implementado no P1, e ligado por padrão no `run_tests.py`** (a conferência não muda nada
  quando não há vazamento): todo teste da suíte é também teste de vazamento, sem rodar a suíte
  duas vezes; `--sem-vazamentos` desliga.
- O contador só vê objetos do runtime; o **ASan/UBSan** continua cobrindo o resto (uso depois de
  liberar, estouro de buffer, memória do C++). Os dois juntos.
- Testes específicos em `tests/gc/`: ciclos de cada tipo (lista ↔ objeto, dict → objeto → lista,
  struct dentro de lista dentro de objeto), cadeias longas (o defeito de 2.1), atribuição a si
  mesmo (`a = a`), objeto criado num construtor que lança erro, temporários de expressões que
  falham no meio.

## 3. Posse (ownership)

Regras que o compilador (CIR) aplica; o programa não as vê (spec 5.9: não há destrutores, o
momento da liberação não é observável). Convenção **+0/+1**, como no Swift.

| Situação | Regra |
|---|---|
| variável local | é dona de uma referência ao valor que guarda |
| temporário | é dono de uma referência; vive até o ponto de liberação marcado na CIR (seção 4.3) |
| parâmetro | chega **emprestado** (+0): o chamador continua dono durante a chamada |
| parâmetro reatribuído no corpo | na entrada, vira uma local dona (`retain`); a regra de cima não basta, porque não se solta o que não é seu |
| retorno | sai **com posse** (+1): quem recebe passa a ser dono |
| `return x` de uma local | move a posse (sem `retain`/`release`): a local morre ali |
| `return p` de um parâmetro, campo ou elemento | `retain` antes de retornar |
| atribuição `a = v` | `retain(v)` **antes** de `release(antigo)` (vale `a = a`) |
| ler campo, elemento, valor de dict | o resultado vira um temporário dono (`retain`): o contêiner pode mudar antes do uso (`l[0] = f(l[0])`) |
| guardar em campo, lista, dict | o contêiner passa a ser dono (`retain`); o valor antigo da posição é solto |
| copiar struct | `retain` de cada campo que é referência; destruir struct solta esses campos |
| interface | dona da referência ao objeto (a tabela de métodos é estática) |
| `CzValor` embrulhado | dono da referência do que embrulha |
| erro pendente | o estado de erro (seção 5) é dono; o `except` passa a posse para a variável dele |
| `const` global | dono até o fim do programa |

Tudo isso é inserido **mecanicamente** na CIR; tirar pares `retain`/`release` desnecessários é
otimização posterior (etapa P10), que não muda o resultado.

## 4. CIR — a IR intermediária da Cinza

### 4.1 O que é

Uma representação tipada e simples, entre a AST analisada e o LLVM. A tradução AST → CIR é onde
moram as decisões da Cinza (ordem de avaliação, temporários, posse, propagação de erros, `finally`);
CIR → LLVM é mecânica. Não é SSA: as variáveis são posições (*slots*) lidas e escritas, como o
`clang` faz com `alloca`; o `mem2reg` do LLVM as transforma em registradores. A CIR não tem
otimizações próprias no início: o LLVM otimiza.

### 4.2 Estrutura

- **Módulo:** o programa inteiro (todos os módulos da Cinza num só, seção 8.3): funções, `const`
  globais, descritores, literais.
- **Função:** parâmetros e locais tipados, blocos básicos; cada bloco termina num salto, num
  desvio condicional, num `ret` ou num `unreachable`.
- **Tipos:** `bool`, `i32`, `i64`, `f32`, `f64`, `enum E`, `str`, `lista`, `dict`, `par`,
  `obj C`, `erro`, `tipo`, `struct S` (valor), `iface I` (par objeto + tabela), `valor`
  (`CzValor` embrulhado).
- **Instruções:**

| Grupo | Instruções |
|---|---|
| dados | `const`, `borrow s`, `copy s`, `move s`, `store.own s, v`, `store.copy s, v` (seção 4.3), `campo.get/set` (objeto, por índice), `struct.get/set`, `embrulha`/`desembrulha` (`CzValor`) |
| números | `add/sub/mul.ov` (com estouro → erro), `div/rem.ck` (zero e `menor / -1` → erro), `fadd…fdiv.ck` (zero e não finito → erro), `neg.ov`, `icmp`, `fcmp`, `sext`, `fpext`, `conv.ck` (`cast<T>` com teste de faixa) |
| chamadas | `call f` (função da Cinza), `call.iface` (pela tabela), `call.rt` (runtime), `call.nativa` (biblioteca); cada uma marca se **pode falhar** |
| posse | `retain`, `release` |
| erros | `lanca e`, `erro?` (desvio para o tratador se há erro pendente), `pega` (tira o erro do estado e o entrega a uma local), `tipo_do_erro` |
| pilha | `entra f` / `sai` (seção 6), `pos p` (a posição atual, antes de algo que pode falhar) |
| fluxo | `br`, `brc`, `ret`, `unreachable` |

### 4.3 Leitura, escrita, temporários e limpeza

**Leitura e escrita de slots.** Para tipos sem referência (números, `bool`, `enum`) as cinco
operações abaixo são leituras e escritas simples. Para os tipos com referência, cada uma diz o que
acontece com a posse — nenhuma decisão fica implícita no gerador LLVM:

| Operação | Resultado | Contagem |
|---|---|---|
| `borrow s` | o valor, **emprestado**: válido enquanto `s` não for escrito, movido ou solto | nada |
| `copy s` | o valor com **posse própria** | `retain` |
| `move s` | o valor com a posse que era de `s`; `s` fica **vazio** | nada |
| `store.own s, v` | `s` passa a ser dono de `v`, que já tinha posse; o valor antigo de `s` é solto **depois** | `release` do antigo |
| `store.copy s, v` | igual, com `v` emprestado | `retain(v)` antes, `release` do antigo depois |

`string b = a;` é `store.copy b, (borrow a)`; o argumento de uma chamada é `borrow` (o
parâmetro chega emprestado, seção 3); `return r` de uma local é `move r`.

**Slots vazios.** Todo slot de tipo com referência começa **vazio** (ponteiro nulo; para
`struct` e interface, todos os campos de referência nulos) na entrada da função, e `release` de
um slot vazio não faz nada. Depois de `move` ou de `release`, o slot volta a ser vazio. Assim um
caminho de erro pode soltar todos os slots do escopo que deixa, mesmo os que ainda não foram
escritos ou já foram movidos, sem liberação dupla.

**Temporários.**

- Todo valor intermediário que é dono de referência ocupa um **slot temporário** explícito na
  CIR, com a criação e o `release` visíveis no fluxo. Nenhum `release` fica implícito.
- **Regra geral:** o temporário é solto no **fim da instrução** em que nasceu (a instrução da
  Cinza: declaração, atribuição, expressão, `return`, condição de `if`/`while`, cabeçalho do
  `for`). Em `lista[i] = cria_lista();` o resultado de `cria_lista()` é um temporário; a
  gravação na lista faz `retain` (a lista vira dona) e o temporário é solto depois, no fim da
  instrução — nunca antes do último uso.
- **Escopos de limpeza:** a tradução mantém uma pilha de escopos (bloco, laço, `try`, a própria
  instrução), cada um com os slots donos que ele abriu. Toda saída — fim do bloco, `return`,
  `break`, `continue`, erro — passa pelos `release` dos escopos que deixa, do mais interno para
  fora. Por isso o `return` antecipado e o erro no meio de uma expressão soltam o que devem.
- Soltar antes do fim da instrução (no último uso) é otimização posterior; a semântica não muda.

### 4.4 Exemplo

```cinza
fn rotulo(string nome, i32 n) -> string {
  string r = nome + ": " + cast<string>(n);
  if (n > 10) { return r; }
  return nome;
}
```

```
fn rotulo(nome: str (emprestado), n: i32) -> str
  slot r: str            ; dono; começa vazio
  slot t1: str, t2: str  ; temporários da instrução 1; começam vazios
b0:
  entra rotulo
  v1 = call.rt cz_str_concat(borrow nome, ": ")   ; +1, não falha
  store.own t1, v1
  v2 = call.rt cz_str_de_i32(n)                   ; +1, não falha
  store.own t2, v2
  v3 = call.rt cz_str_concat(borrow t1, borrow t2)
  store.own r, v3
  release t1
  release t2                                      ; fim da instrução 1
  c  = icmp.gt i32 n, 10
  brc c, b1, b2
b1:
  v4 = move r                                     ; r fica vazio
  sai
  ret v4
b2:
  v5 = copy nome                                  ; parâmetro emprestado → posse (retain)
  release r                                       ; saída do escopo da função
  sai
  ret v5
```

O `--emit-cir` do `cinzac` mostra a CIR neste formato.

**Implementado no P4** (`llvm/cir.h`, `llvm/gera_cir.cpp`): a parte da CIR sem referências —
slots (`carrega`/`grava`), globais dos `const`, contas (`add`…`rem`, com o operador como na
CVM), `cmp`, `neg`, `not`, `conv`, `embrulha`/`desembrulha`, `call.nativa` (com
`conv.valor` para as nativas genéricas), `call`, `br`/`brc`/`ret`/`inalcançável` e duas
operações do `for range` (`confere.passo`, `passo.range`). A CIR está mais perto do LLVM do que o
plano acima: um temporário é escrito uma vez e vira um valor SSA, e pode ser usado em blocos que a
definição domina — o curto-circuito de `and`/`or` abre blocos no meio de uma expressão
(`f(a, b and c)`) e o valor de `a` atravessa sem passar por um slot. Cada operação que pode falhar
leva a posição do token que a CVM usa nela, e é isso que dá as mesmas mensagens e o mesmo stack
trace. `entra`/`sai`/`pos` não são instruções: o emissor os gera (seção 6.2).

**Implementado no P5** (strings e posse), com as regras desta seção e três escolhas simples, que
o P10 pode afinar sem mudar a semântica:

- a CIR não tem `borrow`/`copy`/`move`/`store.own` como instruções: a tradução escreve a
  sequência (`carrega` emprestado; `retem`; o `store.own` como `carrega` do antigo, `grava`,
  `solta` do antigo), e o emissor só traduz — `grava` é uma gravação simples;
- **parâmetros**: a função retém cada um no próprio slot na entrada e o solta na saída, então
  reatribuir um parâmetro é igual a reatribuir uma local (o P10 pode tirar o par quando o
  parâmetro não é reatribuído);
- **escopos**: toda saída da função (cada `return` e o fim do corpo) vai para um bloco `saida`
  único, que solta todos os slots donos — os de blocos internos também, por isso uma variável
  de um laço guarda a última string até a função sair (o valor de antes é solto a cada nova
  gravação). As saídas de escopo que não são da função (`break`/`continue`, e o `except` de
  um `try`, P8) não perdem referência com essa regra: o que fica nos slots é solto na próxima
  gravação ou na saída da função;
- **temporários**: um valor com posse que só é lido (o resultado de `a + b` passado ao
  `print`) vai para um slot temporário da instrução, solto e esvaziado no fim dela; o
  curto-circuito de `and`/`or` deixa o slot vazio quando o lado direito não roda. Um valor com
  posse que vai ser guardado (declaração, atribuição, `return`, `const`) passa direto, sem
  `retem`; um emprestado ganha `retem`.

As funções de string do runtime (`call.rt`, uma tabela na CIR: `cz_concat`, `cz_texto`,
`cz_str_igual`, `cz_str_compara`, `cz_indice`, `cz_fatia`, `cz_formata`, `cz_de_texto`,
`cz_metodo0..2`, as do `for` em string) recebem e devolvem os valores da CVM e chamam as mesmas
regras (`operacoes.cpp`). Os `const` com referência são soltos por uma função oculta depois da
`main`, antes da conferência de vazamentos.

**Implementado no P6** (list, dict, pair): os três são ponteiros para os objetos do runtime de
hoje (`CinzaList`, `CinzaDict`, `CinzaPair`, com os elementos como `CzValor`), com a mesma posse
das strings. Literais, `first`/`second`, os métodos embutidos (o `callBuiltin` da CVM), a
leitura por índice (`indexGet`), a gravação com o caminho da spec 5.2 (`indexPlace`: cada
nível intermediário é um list/dict, e a mudança vale no objeto compartilhado), o `==` que desce
nos elementos e o `for` sobre a cópia dos elementos (`forElements`, percorrida por índice) são
chamadas ao runtime. Um `call.rt` pode ser **variádico**: os argumentos depois dos fixos vão
num vetor de `CzValor` (literais de lista e dict). Struct dentro do caminho de uma gravação fica
para o P7. **Ciclos**: com list, dict e pair não se forma ciclo na Cinza (um tipo não contém a si
mesmo sem class ou struct), então a coleta só tem trabalho no P7; ela já é compatível com o
código nativo, porque só lê as contagens e os filhos dos contêineres — as referências dos
slots nativos entram na contagem como as dos registradores da CVM.

**Implementado no P7** (structs, classes, interfaces, enums, `type()`), ainda com o layout do
runtime de hoje — os campos tipados da seção 8.1 ficam para o P10:

- **struct** é um ponteiro para o `StructValue` do runtime, com a regra de valor da spec 5.1
  feita pela posse: **cada slot tem o seu struct**. Guardar um struct emprestado (variável,
  `return`, parâmetro na entrada da função) o clona (`cz_struct_copia`), em vez do `retem`; um
  struct com posse (recém-criado, lido de uma lista ou de um campo — leituras que já copiam)
  passa direto. Gravar um campo de um struct local muda o do slot, no lugar; um caminho com
  níveis de struct (`l[0].p.x = 1`, `obj.p.x += 1`) lê cada nível como cópia, grava e devolve ao
  nível de cima, como o `assignPlace` da CVM. As funções do runtime que mudam um objeto
  (`cz_grava_campo`) não passam pelo `Value::empresta`, que clonaria o struct alvo;
- **objeto de classe e valor de interface** são o mesmo ponteiro para o `ClassInstance`, com
  `retem`/`solta`. Um método é uma função com o self como primeiro parâmetro; o construtor
  ("new Classe" no trace) e o inicializador dos campos (oculto, como na CVM) também. A chamada
  por interface lê o índice da classe do objeto (`cz_classe_de`) e chama pela tabela do método
  (uma constante: a função de cada classe, ou nulo);
- **enum e tipo** são o `CzValor` inteiro (sem referência): o descritor do enum ou o `TypeInfo`
  e o índice. Os `TypeInfo` de `type()` e dos literais de tipo são refeitos na partida pelo
  `TypeContext` (o tipo, o nome e os parâmetros, numa tabela do módulo), então `type(x) == T`
  compara os mesmos ponteiros que na CVM;
- os **descritores** (`CzStructDesc`, `CzClasseDesc`, `CzEnumDesc`) são criados na partida e
  vivem até o fim do processo; `cz_struct_novo` e `cz_obj_novo` são os pontos da coleta de
  ciclos, como o `NEWSTRUCT`/`NEWOBJ`;
- **função oculta e o stack trace**: uma chamada feita de dentro do inicializador de campos
  grava a posição no quadro de quem fez o `new` — o `call_line` que a CVM guarda no quadro
  seguinte ao oculto; o trace sai igual (teste `tests/nativo/p7_erro_init`).

**Implementado no P9**: o programa inteiro num módulo LLVM só (seção 8.3), com todos os
módulos da Cinza na ordem topológica — a mesma da inicialização dos `const` na CVM. Os nomes de
topo dos módulos importados já chegam completos do semântico (`util.texto::soma`), o que dá o
símbolo `cz:util.texto::soma` sem regra nova. `main(list<string> args)` recebe do `main` do C a
lista com os argumentos depois do executável (`cz_args`). **Literais grandes**: um `call.rt`
com vetor de 8 ou mais elementos põe os constantes num global já pronto e grava só os outros,
logo antes da chamada (nenhum código da Cinza roda entre as gravações e a chamada, que copia os
elementos); com uma instrução por elemento, uma lista de 67 mil números levava 9 s para
compilar em -O0 e mais de 60 s em -O2 — agora, cerca de 1 s nos dois.

**P10 (otimização, sem mudar o layout do runtime)**:

- **parâmetros emprestados**: um parâmetro que o corpo não reatribui nem usa como base de uma
  gravação (`p = v`, `p.x = v`, `p[i] = v`) fica emprestado — sem `retem`/`solta` nem o clone de
  um struct na entrada, e sem soltar na saída. Quem chamou segura o valor até a volta, e nada na
  função alcança os slots dele (sem closures). O self também;
- **`return` de uma local dona move** o valor (o slot fica vazio), sem `retem` e sem clonar um
  struct — menos com um `finally` pendente, que ainda pode ler a local;
- **contagem em linha no -O2**: `retem` e `solta` leem as marcas e a contagem do cabeçalho
  (deslocamentos 5 e 0, conferidos pelo `--confere-layout`) e só chamam o runtime para destruir
  (`cz_destroi`). No -O0 continuam chamadas ao runtime, para a `libcinzart` de depuração
  (`CINZA_CONFERE_REFS`) conferir cada operação. Uma função com mais de 256 `solta` (os
  temporários de uma expressão funda, todos vivos até o fim da instrução) fica com as chamadas:
  os 5 blocos de cada `solta` em linha faziam o alocador de registradores do LLVM crescer mais
  que linear (uma lista de 1000 níveis levava 114 s);
- **funções enormes sem otimização** (Q2): acima de 3000 instruções da CIR (o `main` de um
  programa gerado, uma tabela escrita à mão) a função vai com `optnone` e `noinline`, mesmo no
  -O2 — o alocador e o *machine sinking* do LLVM crescem mais que linear com milhares de blocos
  (o `main` de 1000 linhas com objetos levava 11 s; agora 2,4 s). Como a geração de código do -O2
  ainda é quadrática nelas, depois dos passes o `emiteObjeto` divide o módulo: as enormes vão para
  um segundo objeto (`prog.exe.grandes.o`), gerado em -O0, com as declarações do resto, e os nomes
  locais ficam externos para os dois se enxergarem (um `main` de 100 mil linhas: tempo esgotado →
  15 s). Com `--asan` o módulo fica inteiro. Ela roda igual, só sem otimizar;
- **a regra das 2000 chamadas em quem chama**, como na CVM (o prólogo só empilha o quadro);
  numa chamada por interface, pelo descritor da função escolhida (uma tabela paralela à dos
  métodos). Com isso uma análise do módulo acha as funções que não podem voltar com erro (sem
  operação que falha nem chamada a função da Cinza: getters, setters sem conta), e a conferência
  do erro depois de chamá-las sai;
- **campos sem referência em linha** (seção 8.1): `campo`/`grava.campo` leem e gravam o
  `CzValor` do campo direto no `std::vector<Value>` do objeto ou struct. O que isso supõe — o
  deslocamento do vetor (48 nos dois, em todo sistema), o `Value` do tamanho do `CzValor`, o vetor começando pelo
  ponteiro dos elementos — é conferido pelo `--confere-layout` (o `cinzac` e a `libcinzart` são
  compilados pelo mesmo GCC).

## 5. Erros

### 5.1 Um mecanismo só

`IndexError`, `OverflowError`, um `throw MeuErro("...")` e o sinal de `exit` usam o mesmo
caminho. Sem desenrolar a pilha (*unwinding*): nenhuma tabela de exceções, nenhuma
*personality function*; o comportamento é o mesmo em Windows e Linux.

### 5.2 O estado de execução

```c
typedef struct CzExec {
    CzErro*  erro;        // erro pendente, ou NULL
    uint32_t prof;        // profundidade de chamadas (seção 6)
    CzQuadro quadros[CZ_MAX_PROF + 1];
} CzExec;
extern CzExec cz_exec;    // um só (sem threads); acessado só pelo cabeçalho do runtime
```

O estado **não** é opaco, e isso é uma decisão: os campos `erro`, `prof` e `quadros` fazem parte
do contrato entre o runtime e o código gerado (como o `errno` do C), com os deslocamentos
conferidos por `static_assert` e testados pelo `cinzac` (seção 8.4). Motivo: o teste de erro
acontece depois de quase toda chamada, e uma chamada a mais para ler o estado
(`cz_tem_erro()`) custaria uma chamada por chamada — sem LTO entre o programa e a `libcinzart.a`,
o LLVM não a eliminaria.

O que fica de fato encapsulado:

- o código gerado **lê** `erro`, mas só **escreve** nele pelas funções do runtime
  (`cz_lanca`, `cz_pega_erro`, `cz_devolve_erro`), que cuidam da posse do erro;
- `prof` e `quadros` são escritos pelo código gerado só nas operações `entra`/`pos`/`sai`
  (seção 6), sempre do mesmo jeito;
- o resto do estado do runtime (fila de destruição, registro da coleta, contadores) não aparece na
  interface;
- o código C++ do runtime usa funções (`czExec()`), não o símbolo direto, para que passar a um
  estado por thread mude um lugar só.

Com threads, `cz_exec` passa a ser por thread e o código gerado o lê pelo endereço que a função
recebe ou por armazenamento local de thread — uma mudança da convenção, registrada aqui para não
ser esquecida. A alternativa, devolver `{valor, erro}` em toda função, fica descartada: complica
a convenção de chamada sem ganho para um programa de uma thread.

### 5.3 Protocolo

- **Invariante:** ao entrar numa função e ao sair dela normalmente, não há erro pendente.
- **Lançar** (`throw`, ou um erro do runtime): cria o `CzErro` (posição atual e stack trace
  naquele instante), põe em `cz_exec.erro` (o estado vira dono) e desvia para o tratador do
  ponto atual.
- **Depois de toda chamada que pode falhar** (função da Cinza, runtime que pode falhar,
  biblioteca nativa): `if (cz_exec.erro) goto tratador`. Funções `void` também. Chamadas que não
  podem falhar (marcadas na interface do runtime) não testam. Otimização posterior: não testar
  depois de funções da Cinza que comprovadamente não falham.
- **Tratador** de um ponto: solta os temporários e os escopos até o `try` mais interno da
  função; sem `try`, solta tudo, `sai` e retorna (o chamador vê o erro pendente).
- **`except (Tipo e)`:** compara o tipo do erro pendente com os `except`, na ordem; o primeiro
  que casa faz `pega`: tira o erro do estado (volta a não haver erro pendente) e entrega a posse
  à variável `e`. `except (Error e)` casa com tudo, menos o sinal de `exit`. Nenhum casou: o
  erro continua pendente e o tratamento sobe.
- **`finally`:** a entrada no `finally` guarda o motivo pendente numa local da função — fluxo
  normal, `return` (com o valor), `break`, `continue` ou erro (o `CzErro` sai do estado e passa a
  ser da local). O `finally` roda sem erro pendente. No fim dele, o motivo continua: o erro volta
  ao estado, o `return` acontece, o laço segue. Um bloco `finally` só, com o motivo numa local
  (sem duplicar o código para cada saída).
- **`exit(código)`:** um erro do tipo `EXIT_KIND` com o código; nenhum `except` o captura, mas
  ele roda os `finally` pendentes; no topo, o programa sai com o código.
- **Saídas do `finally`, por precedência.** `return`, `break` e `continue` dentro de um
  `finally` são **erro de compilação** (spec 5.6; o semântico já recusa: "'return' não é
  permitido dentro de 'finally'"). Então o `finally` só termina de três jeitos:

  | O `finally` termina... | Fluxo pendente (normal, `return`, `break`, `continue`, erro ou `exit`) |
  |---|---|
  | normalmente | **continua**: o erro volta a subir, o `return` acontece, o laço segue, o `exit` continua |
  | lançando um erro | **é substituído** pelo erro novo; o pendente é solto (um `return`, com o valor; um erro; um `exit`) |
  | chamando `exit` | **é substituído** pelo `exit` (é um erro especial: mesma regra da linha de cima) |

  É exatamente o que a CVM faz hoje (conferido: `try { throw ... } finally { exit(4); }` sai com
  4; `try { exit(3); } finally { throw KeyError(...); }` termina com o `KeyError`). A spec 5.6
  passa a dizer isso explicitamente, sem mudança de comportamento.
- **No topo:** o `main` gerado chama a `main` da Cinza; com erro pendente, imprime o relato (o
  mesmo formato da CVM: posição, tipo, mensagem, stack trace) e sai com código 1.
- **Implementado no P8** (no P4–P7 todo erro era fatal, por um `cz_morre` que não soltava
  nada). Cada instrução da CIR que pode falhar leva o **bloco tratador** do ponto em que está
  (`Instr::erro`); o emissor desvia para ele depois do caminho de erro de uma conta, de uma
  chamada ao runtime que deixou erro pendente e de **toda chamada de função da Cinza** (a
  função volta com o erro pendente; o valor de retorno não importa). Fora de um `try`, o
  tratador é a **saída de erro** da função: solta todos os slots donos (os temporários da
  instrução interrompida, as locais, os parâmetros, um retorno pendente de um `finally`), faz o
  `sai` e volta (`ret.erro`). Dentro de um `try`, a **captura**: solta os temporários (os das
  outras instruções estão vazios), compara o erro com os `except` na ordem (`cz_erro_casa`) e o
  primeiro que casa tira o erro do estado para a variável (`cz_pega_erro`). Um `finally` guarda o
  motivo pendente num slot (0 normal, 1 `return` com o valor noutro slot, 2 `break`, 3
  `continue`, 4 erro — o erro tirado do estado num slot) e o despacha no fim, no contexto de
  fora; o erro volta com `cz_relanca`, o mesmo objeto (mesma posição e trace). O
  `StackOverflowError` sai do prólogo da função chamada, que volta antes de entrar. Um erro na
  inicialização dos `const` pula a `main`. A conferência de vazamentos vale de novo também nos
  programas que terminam com erro, como na CVM.
- **Fronteira com o C++: nenhuma exceção do C++ escapa de uma função `cz_*`**, qualquer que
  seja. Cada uma termina num bloco que trata, na ordem:
  - `RuntimeError` → vira o erro pendente (o mesmo erro da linguagem que a CVM lançaria);
  - `std::bad_alloc` → **erro fatal**, como na CVM hoje: escreve "Erro fatal: memória
    insuficiente para continuar o programa" (texto fixo, sem alocar) e encerra com código 1. A
    linguagem não tem erro capturável de memória;
  - qualquer outra (`std::exception` ou `...`) → **erro interno** fatal, como o
    `reportInternalError` da CVM: escreve a mensagem e encerra com código 1.

  Converter um `RuntimeError` em `CzErro` aloca; se essa alocação falhar, vale o caso do
  `bad_alloc`. Por isso todo o código gerado é `nounwind`, e `retain`, `release` e `cz_destroi`
  não lançam nada.

## 6. Profundidade de chamadas, pilha e stack trace

### 6.1 O `StackOverflowError` é uma regra da linguagem, não da máquina

A spec 5.8 define: **acima de 2000 chamadas aninhadas** (função, método ou construtor), a chamada
lança `StackOverflowError`. Não é o estouro da pilha do sistema: é uma contagem, portável e
igual nos dois backends. O código nativo conta como a CVM.

### 6.2 A sombra da pilha

`cz_exec.quadros` guarda, por chamada ativa, o descritor da função e a posição atual nela:

```c
typedef struct CzQuadro { const CzFuncao* f; const CzPos* pos; } CzQuadro;
```

- **`entra f`:** se `prof == 2000`, lança `StackOverflowError` com a posição da declaração de
  `f` (como a CVM); senão `quadros[prof++] = {f, posição da declaração}`.
- **`pos p`:** antes de cada operação que pode falhar, grava a posição atual (um ponteiro para
  uma tabela constante `{arquivo, linha, coluna}`) no quadro do topo.
- **`sai`:** `prof--`, em toda saída (normal ou por erro).
- **Stack trace:** ao nascer, o erro percorre os quadros do topo para baixo e monta as linhas
  ("em f (arquivo:linha)"), no mesmo formato e com a mesma abreviação da CVM (spec 7.3).
- As funções ocultas da CVM (inicializadores de campo) também não contam aqui.

Custo por chamada: um teste, duas gravações e um decremento — o preço de a regra das 2000
chamadas e o stack trace serem exatamente os da CVM.

**Implementado no P4** (antes do planejado: era do P8, mas sem ela nem a recursão nem o stack
trace dos erros de conta ficariam iguais aos da CVM). Diferenças do plano, para o caminho rápido
ficar sem gravação:

- `entra` grava o quadro com `pos` nulo, e o quadro do topo nunca é lido: a linha dele no trace é
  a do erro;
- `pos` só é gravado **antes de cada chamada de função da Cinza** (é a linha da chamada em
  andamento, que o trace usa nos quadros de fora). As operações que podem falhar passam a posição
  como argumento à função do caminho de erro (`cz_falha_binaria(…, &pos)`, `cz_nativa(…, &pos)`),
  que completa o erro como o `VM::fail`: sem posição, a da operação; o trace, da sombra;
- a função de inicialização dos `const` não entra na sombra (é oculta, como na CVM): um erro nela
  sai sem trace.

O teste `prof >= 2000` fica no prólogo da função chamada, com a posição da declaração dela no
erro, como a CVM.

### 6.3 A pilha física

Um programa válido, com menos de 2000 chamadas aninhadas, **não pode** terminar com
`0xC00000FD` porque os quadros dele são grandes. Uma pilha fixa de 64 MB não garante isso: são
32 KB por nível, sem contar o runtime, e um quadro com muitos structs por valor pode passar disso.
A garantia vem de três partes:

1. **Orçamento por função, calculado pelo `cinzac`** (o LLVM não expõe o tamanho do quadro
   para COFF: a seção `.stack_sizes` só existe em ELF, e a `llvm-c` não a liga). A CIR conhece cada slot e o tipo dele,
   então dá um limite superior do quadro de cada função: a soma dos slots, mais as cópias de
   struct passadas por valor, vezes 2 (folga para o que o LLVM acrescenta: registradores salvos,
   alinhamento, argumentos de saída), mais 4 KB fixos.
2. **Pilha dimensionada para o programa.** O executável é ligado com reserva de pilha
   `max(64 MB, 2000 × maior orçamento + margem + 8 MB)`: os 2000 quadros, a margem da rede de
   segurança (item 3), que um programa válido nunca pode alcançar, e 8 MB para o runtime em C++
   no topo (`print`, `format`, a biblioteca). Sem a margem na conta, um programa com um quadro
   maior que 8 MB bateria na rede mesmo válido. No Windows de 64 bits a reserva é só espaço de
   endereços: as páginas são usadas sob demanda, então uma reserva grande não custa memória.
3. **Rede de segurança na entrada.** Junto do teste das 2000 chamadas, `entra` compara o
   endereço atual da pilha com o limite calculado na partida (`GetCurrentThreadStackLimits`) mais
   uma margem. Se faltar espaço — o que só acontece se o orçamento do item 1 estiver errado —, o
   programa para com um **erro interno** claro ("pilha do sistema esgotada"), nunca com o
   `0xC00000FD` silencioso. Não é um `StackOverflowError`: a regra da linguagem continua sendo só
   a das 2000 chamadas, igual nos dois backends.

**Conferido no P2** com o protótipo `llvm/prototipos/pilha/` (`python
llvm/prototipos/pilha/pilha.py`): o "código gerado" é C compilado pelo clang 20.1.8 do CLANG64 (o
mesmo LLVM do `cinzac`), o runtime é C++ com o `value.h` de verdade compilado pelo GCC do
projeto, e a ligação é pelo `g++`, como no `cinzac`.

- **O orçamento cobre os quadros reais** do LLVM (`-fstack-usage`) em seis casos, do comum ao
  extremo (dez structs de 8 KB e vinte chamadas que os recebem por valor), em -O0 e -O2; a folga
  mínima é de 2× (o extremo em -O0, onde o quadro é quase a soma exata dos slots e das cópias).
- **O runtime no topo gasta pouco:** um `print` típico (lista de 600 valores e um dict em
  texto) usa ~4 KB de pilha. A **margem da rede de segurança** é então: maior orçamento do
  programa + 64 KB (o runtime, com folga) + 64 KB (as páginas de guarda do Windows).
- **2000 níveis do quadro extremo** terminam normalmente em -O0 e -O2 com a reserva da fórmula
  (≈ 930 MB de espaço de endereços, sem custo de memória real).
- **Com a reserva fixa de 64 MB**, a mesma recursão para com "Erro interno: pilha do sistema
  esgotada" quando tem a rede, e estoura com `0xC00000FD` sem ela (o controle).
- O endereço do quadro vem de `__builtin_frame_address(0)` (no LLVM, `llvm.frameaddress`) e o fim
  da pilha, de `GetCurrentThreadStackLimits` na partida.
- **Sem janela de relatório de erro:** o executável gerado chama `SetErrorMode` na partida (o
  `cinza.exe` também passou a chamar): se algo derrubar o processo, ele termina na hora, em vez
  de ficar preso esperando o relatório do Windows e segurando o executável.

O P2 encontrou também um risco que já existia na CVM: as funções **recursivas do runtime em
C++** sobre dados aninhados sem limite. O `toString` gastava 848 bytes de pilha por nível de
aninhamento, e um programa Cinza válido — `struct No { list<No> f; }` encadeado 200 mil vezes e
impresso — derrubava o `cinza.exe` com `0xC00000FD`. **Corrigido no P1b**: a conversão em texto
e a igualdade (`==`) usam uma pilha explícita, no heap, como a destruição da seção 2.1 (o `<` não
desce em contêineres, que não são ordenáveis). Assim o runtime no topo da pilha gasta uma
quantidade limitada, qualquer que seja a profundidade dos dados — o que a margem da rede de
segurança precisa.

## 7. Interface C do runtime (`libcinzart`)

### 7.1 Convenções

- Funções `extern "C"`, prefixo `cz_`, declaradas num cabeçalho único (`cinzart.h`). O runtime
  continua em C++ por dentro.
- **Argumentos emprestados; objetos retornados com posse (+1)** — sem exceção, para nenhuma
  função precisar de documentação própria sobre posse.
- Cada função é marcada **`CZ_FALHA`** (pode deixar erro pendente: o chamador testa) ou não.
- **Nenhuma exceção do C++ escapa** de uma função `cz_*` (seção 5.3); as funções são
  declaradas `noexcept` no lado C++.
- Nenhuma estrutura de C++ aparece na interface: só os tipos C das seções 2 e 5.

**Implementado no P3** (`llvm/cinzart.{h,cpp}`): `cz_inicio`, `cz_fim`, `cz_literais` (todas as
strings literais do módulo de uma vez, de uma tabela constante `{bytes, tamanho}` para o global
`@cz.literais`; imortais; o `cz_fim` solta os literais depois da última coleta e antes de conferir
vazamentos — no Q2 trocou uma chamada `cz_str_literal` por literal, que num programa com 50 mil
literais virava 50 mil chamadas na partida, e num vetor de literal grande uma string literal vai
como a marca constante `CZ_TIPO_LITERAL` com o índice dela), `cz_nativa_resolve` (pelo nome do módulo e da função, uma vez na partida),
`cz_nativa` e `cz_solta_valor`; `cz_exec` só com o campo `erro`. **No P4**: `CzPos`, `CzFuncao`,
`CzQuadro` e o `CzExec` completo (`erro`, `prof`, `quadros`), conferidos pelo `--confere-layout`;
`cz_arquivos` (os nomes na ordem dos `file_id` do front-end); o caminho de erro dos números
(`cz_falha_binaria`, `cz_falha_neg`, `cz_falha_conv`, `cz_falha_passo`), que refaz a operação pelo
`applyBinaryOp`/`negateOp`/`convertValue` da CVM para a mensagem ser a mesma; `cz_estouro`;
`cz_conv_valor` (nativas genéricas); `cz_nativa` com a posição; e `cz_morre` (que o P8 tirou:
`cz_erro_novo`, `cz_erro_campo`, `cz_erro_casa`, `cz_pega_erro`, `cz_lanca`, `cz_relanca`). A tabela abaixo é o
plano das etapas seguintes.

### 7.2 Grupos

| Grupo | Exemplos |
|---|---|
| ciclo de vida | `cz_inicio(argc, argv)` (argumentos em UTF-8), `cz_fim()` → código de saída, `cz_destroi(o)` |
| strings | `cz_str_concat`, `cz_str_de_i32/i64/f32/f64/bool`, `cz_str_indice` (`CZ_FALHA`), `cz_str_fatia` (`CZ_FALHA`), `cz_str_igual`, `cz_str_compara` |
| conversões | `cz_conv_*` (`cast<T>(string)`, `CZ_FALHA`) — as mesmas regras de `convertValue` |
| listas | `cz_lista_nova(n)`, `cz_lista_add`, `cz_lista_get` (`CZ_FALHA`: `IndexError`), `cz_lista_set` (`CZ_FALHA`), `cz_lista_tam`, `cz_lista_remove` (`CZ_FALHA`)… |
| dict, par | `cz_dict_novo`, `cz_dict_get/set/add/has/remove` (com a mesma regra do `KeyError`), `cz_par_novo` |
| objetos e structs | `cz_obj_novo(desc)`, `cz_struct_embrulha(desc, campos)`, `cz_struct_desembrulha` |
| erros | `cz_erro_novo(tipo, msg)`, `cz_lanca(erro)`, `cz_estouro(f)`, os erros dos números (`cz_falha_estouro`…) |
| saída e entrada | `cz_print(valores, n)`, `cz_format(...)`, `cz_input` (`CZ_FALHA`) |
| biblioteca | `cz_nativa(id, args, n)` → as `NativeFn` de hoje, com os argumentos copiados para `Value` (seção 2.3) |
| tipos | `cz_tipo_de(valor)`, os `TypeInfo` do programa (seção 2.4) |

As regras de `print`, `format`, conversões e da biblioteca continuam escritas **uma vez**, no
runtime, para os dois backends.

## 8. Do programa ao código nativo

### 8.1 Tipos

| Cinza | Código nativo |
|---|---|
| `i32`, `i64` | `i32`, `i64` |
| `f32`, `f64` | `float`, `double` |
| `bool` | `i1` nas contas, `i8` na memória |
| `enum` | `i32` (o índice do valor); embrulhado, `CzValor` com o id do enum. Um `enum` com dados, se um dia existir, terá outro layout |
| `string`, `list`, `dict`, `pair`, erro | ponteiro para o objeto do runtime |
| objeto de classe | ponteiro para o objeto: cabeçalho + campos como `CzValor`, acesso por índice fixo (sem procurar) |
| `struct` | struct LLVM **por valor** com os campos nos tipos nativos; embrulhado (numa lista), um `CzStruct` do runtime, copiado na entrada e na saída (como a cópia da spec 5.1) |
| interface | par `{objeto, tabela de métodos}`, como o `dyn Trait` do Rust; ao sair de um `CzValor` (elemento de lista), a tabela é procurada no descritor da classe |
| `type()` | ponteiro para `TypeInfo` (seção 2.4) |

**Q11 (2026-11-25)**: os campos ficam **dentro do objeto**, logo depois do cabeçalho (o
`GcObject`, 40 bytes) e de um contador, na mesma alocação: o campo i está em
`objeto + 48 + 16·i`, sem ponteiro a carregar (`--confere-layout` confere).

**Decidido no P10 (2026-11-19)**: os campos continuam no layout da CVM (um `CzValor` por campo,
no vetor do objeto), com um **caminho rápido**: o código gerado lê e grava em linha os campos
sem referência (número, `bool`, enum, tipo), direto no vetor; os campos com referência seguem
pelo runtime. A representação nativa (campos tipados num struct LLVM, `list<i32>` como vetor
de `int32_t`) fica de fora por enquanto — ela mexeria no runtime inteiro (print, `==`, a coleta,
as nativas).

**Q11 (2026-11-24): em linha, com guardas.** Sem mudar a representação: o código gerado faz o
caminho comum e, se uma guarda falha, chama o runtime de sempre (que lança o erro). Os campos
com referência: o `CzValor` copiado com posse (`CopiaValor`) e a gravação que retém o novo antes
de soltar o antigo. As listas: `TamLista`, `ElemLista` (guarda `0 <= i < tamanho`; ponteiros
retidos), `GravaElemLista` (só sem referência), `TomaElemLista` (o `for` tira o struct da cópia
dele). `lista[i].campo` sem clonar o struct. As funções numéricas do `Math` com argumento f64
(`Matematica`, `Finito`): intrínsecas exatas ou a libm, com as guardas de domínio e de resultado.

### 8.2 Números

A tabela da seção 12 do `cvm/DESENHO.md`: intrínsecas `llvm.sadd/ssub/smul.with.overflow` (sem
`nsw`/`nuw`: estouro é erro definido, não comportamento indefinido), testes de divisor zero e de
`menor / -1`, teste de resultado não finito no ponto flutuante, sem *fast-math*, conversões com
teste de faixa.

### 8.3 Unidade de compilação e nomes

- **Um módulo LLVM só** com o programa inteiro (o LLVM vê tudo; a otimização entre funções e
  módulos da Cinza sai de graça). Compilação separada por módulo fica para depois.
- **Nomes de símbolos** determinísticos e reproduzíveis entre compilações — **não** uma ABI
  estável (nada garante que um `.o` antigo ligue com um novo):
  - `cz:<caminho do módulo>::<nome>` — `cz:util.texto::soma`; o principal não tem caminho:
    `cz:::main`;
  - método: `cz:<módulo>::<Classe>.<método>`, construtor `cz:<módulo>::<Classe>.<new>`;
  - cópias genéricas com os tipos canônicos: `cz:::maior[i32]`,
    `cz:::Par[string, util.texto::Ponto]`.
  - Sem ambiguidade: identificadores não têm `.`, `:` nem `[`. O `main` do C é gerado à parte e
    chama `cz:::main`.

### 8.4 LLVM

Tudo pela `llvm-c`, isolada numa camada própria (`llvm/emissor.*`): é o único lugar que inclui
`llvm-c/`. A sequência, na ordem:

1. **Inicializar o alvo X86**: `LLVMInitializeX86TargetInfo`, `LLVMInitializeX86Target`,
   `LLVMInitializeX86TargetMC`, `LLVMInitializeX86AsmPrinter`.
2. **Criar a `TargetMachine`** para o triple `x86_64-w64-windows-gnu` (o do GCC do projeto),
   CPU genérica `x86-64`, modelo de relocação e de código padrão.
3. **Configurar o módulo antes de gerar qualquer função**: `LLVMSetTarget` com o triple e
   `LLVMSetModuleDataLayout` com o layout da `TargetMachine`
   (`LLVMCreateTargetDataLayout`). Sem isso o LLVM não sabe tamanhos, alinhamentos e
   deslocamentos do alvo, e as estruturas do código gerado podiam não casar com as do runtime
   compilado pelo GCC.
4. **Gerar o IR** a partir da CIR (`LLVMBuild*`).
5. **Verificar**: `LLVMVerifyModule` sempre antes de otimizar; no build de depuração do
   `cinzac`, também depois. IR inválido é erro interno do `cinzac`, com a mensagem do LLVM.
6. **Otimizar** com `LLVMRunPasses`: `default<O0>` (padrão) ou `default<O2>` (`-O2`). A
   função devolve um `LLVMErrorRef`: o `cinzac` testa, relata a mensagem
   (`LLVMGetErrorMessage`) e a libera (`LLVMDisposeErrorMessage`).
7. **Emitir o objeto** com `LLVMTargetMachineEmitToFile`; a mensagem de erro dela também é
   relatada e liberada (`LLVMDisposeMessage`).

Outros pontos:

- Todo o código gerado é `nounwind` (seção 5.3).
- **Layout conferido**: um teste do `cinzac` gera, para cada estrutura da interface (`CzValor`,
  `CzObjeto`, `CzString`, `CzExec`, `CzQuadro`), o tamanho e os deslocamentos pelo
  `DataLayout` e os compara com os `static_assert` do runtime.
- A informação de depuração da `llvm-c` (`DebugInfo.h`) é marcada como experimental na 20.1.8:
  quando entrar (seção 11), fica isolada na mesma camada.
- **Ligação** pelo driver `g++` (o mesmo que compilou o runtime, por causa da `libstdc++`):
  `g++ prog.o libcinzart.a -o prog.exe -static-libstdc++ -static-libgcc -Wl,--stack,<N>`, com o
  `N` da seção 6.3.

## 9. O comando `cinzac`

```
cinzac prog.cinza -o prog.exe          compila e liga (sem -o: o nome do programa; .exe no Windows)
cinzac -O2 prog.cinza -o prog.exe      com otimização
cinzac --emit-cir prog.cinza           mostra a CIR
cinzac --emit-llvm prog.cinza -o p.ll  grava o IR do LLVM (texto, depois dos passes)
cinzac --manter-objeto ...             não apaga o .o depois de ligar
cinzac --confere-layout                confere CzValor, CzObjeto e CzExec (LLVM x runtime)
cinzac --version                       a versão da linguagem, do cinzac e do LLVM, e o alvo
```

Erros de compilação saem iguais aos do `cinza` (o front-end é o mesmo). O `cinza prog.cinza`
(CVM) não muda. Códigos de saída: 0 certo; 1 erro do programa ou da ligação; **75** recurso
que o `cinzac` ainda não compila (`arquivo:linha:coluna: [cinzac] ainda não suportado: ...`),
que diminui a cada etapa até a P9. O `cinzac` fica em `llvm/bin/` (`make cinzac`).

**A ligação (Q5).** O `cinzac` liga sozinho: o `lld` vai dentro dele como biblioteca
(`llvm/ligador.cpp`; o driver MinGW no Windows, o ELF no Linux), e a pasta `runtime/` ao lado dele
tem a `libcinzart.a` e tudo o que o programa liga — os arquivos de partida e as bibliotecas do C++
(libc++, libc++abi, libunwind, compiler-rt; no Windows as do mingw-w64, no Linux a musl). A linha
do `lld` é a do `clang -static` de cada sistema, com `-L` só para `runtime/`: nada vem da máquina.
No Linux o programa sai estático (roda em qualquer x86-64); no Windows, ele importa só as DLLs do
próprio sistema (o UCRT). `CINZA_GXX` liga por um compilador externo, como antes (o build de ASan,
`CINZA_GXX=clang++`), e `CINZA_LIGACAO` acrescenta opções nos dois modos.

Por isso o `cinzac` usa a API em C++ do LLVM (o `lld` não tem uma em C) e é compilado com o mesmo
C++ das bibliotecas estáticas do LLVM: no Windows, o clang do CLANG64 com a libc++; no Linux de
distribuição (`make ESTATICO=1`, no Alpine), o clang com a libstdc++ do LLVM do Alpine, enquanto o
runtime usa a libc++. O que os dois lados compartilham por layout (o `CzValor`, o cabeçalho dos
objetos, o vetor de campos no deslocamento 48) é conferido por `static_assert` nos dois e pelo
`--confere-layout`. Do LLVM entra só o alvo x86 (`llvm/alvos_vazios.cpp`: o driver do `lld` pede a
inicialização de todos, por causa do LTO, e o `cinzac` nunca lhe passa bitcode) e, no Windows, sem o
libxml2 (`llvm/xml_vazio.cpp`: o manifesto do Windows, que o driver MinGW nunca junta): o `cinzac`
fica com ~45 MB no Windows e ~72 MB no Linux, sem DLLs.

## 10. Testes

- `run_tests.py --cinzac` (P3): para cada teste, compila com o `cinzac` e roda o executável com
  os mesmos `// args:`, `// stdin:`, `// env:`; confere `// expect`, `// expect-error`,
  `// expect-exit` como hoje. Um erro de compilação conta como saída do teste (mesma mensagem
  nos dois). O código 75 conta à parte ("ainda não suportado"), com um resumo dos motivos mais
  comuns — o mapa do que falta —, e o resumo diz quantos rodaram como executável nativo (os
  demais que passam são erros de compilação, do front-end comum). `make test-llvm` roda o
  `--confere-layout` e a suíte.
- O modo sem vazamento (seção 2.6) vale para os dois backends: ligado por padrão no
  `run_tests.py`.
- ASan/UBSan no runtime e no código gerado (o `cinzac` aceita `--sanitize` para ligar com eles).
- A meta da etapa P9: a suíte inteira passa nos dois backends, sem vazamento.

## 11. Fora deste desenho (depois)

Informação de depuração (DWARF/CodeView), compilação separada por módulo, ABI estável, threads,
campos e coleções com tipos nativos, eliminação de `retain`/`release`, outros alvos (ARM64,
macOS).
