# 7. Erros e diagnósticos

## 7.1 Erros de runtime

Todo erro de runtime tem um **tipo**. `Error` é a raiz: `except (Error e)` captura qualquer erro.
Os tipos embutidos:

| Tipo                 | Quando a linguagem o lança |
|----------------------|----------------------------|
| `Error`              | raiz; erros sem categoria própria |
| `ValueError`         | valor inválido para a operação: `range` com passo 0, `Convert` com texto inválido, `Strings.split`/`replace` com separador vazio, `Math.sqrt`/`log`/`pow` fora do domínio, `Random.int(a, b)` com `a > b` |
| `IndexError`         | índice de lista negativo ou além do fim, `remove` inválido, `Strings.substr` e `Lists.slice` fora dos limites, `Random.choice` de lista vazia |
| `KeyError`           | leitura ou atualização (`d[k] = v`) de chave inexistente, `remove` de chave inexistente |
| `ZeroDivisionError`  | `/` ou `%` por zero (`int` ou `decimal`) |
| `OverflowError`      | resultado de `int` fora da faixa; resultado de `decimal` infinito; `Math.abs` do menor `int`; `floor`/`ceil`/`round` fora de `int`; `Math.pow` infinito; `Lists.sum` que estoura |
| `IOError`            | fim da entrada em `input`; falha ao abrir, ler ou gravar em `Files` |
| `StackOverflowError` | mais de 2000 chamadas aninhadas |
| `TypeError`          | não é lançado pela linguagem (toda conferência de tipo é na compilação); existe para uso com `throw` |

Novos tipos de erro são declarados com `error Nome;` (seção 4.10) e também são filhos de `Error`.

```cinza
error SaldoInsuficiente;
fn saca(int saldo, int valor) -> int {
  if (valor > saldo) { throw SaldoInsuficiente("faltam " + (valor - saldo)); }
  return saldo - valor;
}
fn main() {
  try {
    print(saca(100, 30));
    print(saca(100, 130));
  } except (SaldoInsuficiente e) {
    print(e.kind, e.message);
  }
}
// expect: 70
// expect: SaldoInsuficiente faltam 30
```

```cinza
fn r(int n) -> int { return r(n + 1); }
fn main() {
  try { print(r(0)); } except (StackOverflowError e) { print("capturado"); }
}
// expect: capturado
```

## 7.2 Erros de compilação

| Tipo            | Fase | Relato |
|-----------------|------|--------|
| `LexicalError`  | léxico: caractere inválido, string sem fim, escape desconhecido, literal fora da faixa, identificador com acento | todos os do arquivo, juntos |
| `SyntaxError`   | gramática | o analisador se recupera e relata os erros que encontrar |
| `ImportError`   | módulos: arquivo não encontrado, importação circular, nome reservado | o primeiro |
| `SemanticError` | tipos, nomes, regras das declarações | o primeiro |

Se houver erro de compilação, nada do programa é executado.

Mais de **2000 níveis** de aninhamento — parênteses, operadores unários, operadores de uma mesma
cadeia (`a + b + c ...`) ou instruções umas dentro das outras — é `SyntaxError`
("Código aninhado demais"), relatado uma só vez; a análise para ali.

```cinza
fn main() {
  int a = ;
  print(1;
}
// expect-error: SyntaxError: Esperado expressão
// expect-error: SyntaxError: Esperado ')' após argumentos
```

## 7.3 Formato das mensagens

Todo erro mostrado ao usuário — de compilação ou de runtime não capturado — segue o formato:

```
arquivo:linha:coluna: Tipo: mensagem
```

Sem posição (por exemplo, um arquivo que não abre): `arquivo: Tipo: mensagem`. O arquivo aparece
como foi informado na linha de comando, com `/` como separador; num erro dentro de um módulo,
aparece o caminho do módulo. Linhas e colunas começam em 1.

Um erro de runtime não capturado vem seguido do **stack trace**, da chamada mais interna para
fora, uma por linha:

```
prog.cinza:2:36: IndexError: índice 5 fora dos limites (tamanho: 1)
    em f (prog.cinza:2)
    em main (prog.cinza:5)
```

Pilhas muito fundas mostram as 10 chamadas mais internas, uma linha `... (N chamadas omitidas)` e
a mais externa. Métodos aparecem como `Classe.metodo`, construtores como `new Classe`.

```cinza
fn f(list<int> l) -> int { return l[5]; }
fn main() {
  list<int> l = [1];
  print(f(l));
}
// expect-error: IndexError: índice 5 fora dos limites (tamanho: 1)
// expect-error:     em f (
// expect-error:     em main (
```

## 7.4 Linha de comando e código de saída

```
cinza [opções] arquivo.cinza [argumentos para main...]
```

| Opção            | Efeito |
|------------------|--------|
| `--tokens`, `-t` | mostra os tokens do arquivo principal |
| `--ast`, `-a`    | mostra a árvore sintática do arquivo principal |
| `--help`, `-h`   | mostra a ajuda |
| `--bytecode`     | mostra o bytecode da máquina virtual antes de executar |
| `--interp`       | executa no interpretador de referência em vez da máquina virtual (os resultados são os mesmos) |

As opções vêm **antes** do arquivo; tudo o que vem depois dele vai para `main(list<string> args)`,
mesmo que comece com `-`.

O código de saída é **0** quando a `main` termina normalmente, **1** em qualquer erro de
compilação ou erro de runtime não capturado, e o código pedido quando o programa chama
`exit(codigo)` (capítulo 6).
