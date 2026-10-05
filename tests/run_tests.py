#!/usr/bin/env python3
"""Runner de testes da linguagem Cinza.

Uso: python tests/run_tests.py [binario] [filtro] [--interp | --diff]

Por padrão os programas rodam na CVM (o modo padrão do cinza).
--interp: executa no interpretador de referência.
--diff: teste diferencial — executa cada programa no interpretador e na CVM e
        compara código de saída, stdout e stderr byte a byte (desenho da CVM,
        seção 11). Qualquer diferença é defeito da CVM.

Cada tests/*.cinza declara a saída esperada em comentários:
    // expect: <linha exata do stdout>
    // expect-error: <trecho da mensagem de erro>
    // expect-not: <trecho que NÃO pode aparecer em stdout+stderr>
    // expect-exit: N (o programa encerra com exit(N): código de saída exato N)
    // args: a b c   (argumentos passados depois do arquivo → main(list<string> args))
    // env: NOME=valor (variável de ambiente; caminhos relativos à raiz do projeto)
    // stdin: linha   (uma linha da entrada padrão; várias diretivas = várias linhas)

Também roda os exemplos executáveis da especificação (spec/*.md): todo bloco
```cinza que tenha `// expect:` ou `// expect-error:` é extraído para
tests/spec_exemplos/ e conferido com as mesmas regras. Blocos sem essas linhas
são só ilustrativos.

Sem expect-error: o programa deve sair com código 0 (ou o de `expect-exit`) e o
stdout deve ser exatamente a sequência de linhas `expect`.
Com expect-error: o programa deve sair com código 1 e o trecho deve
aparecer em stdout+stderr; linhas `expect`, se houver, devem casar com o
início do stdout.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
DIR_TESTES = RAIZ / "tests"
DIR_SPEC = RAIZ / "spec"
DIR_EXEMPLOS = DIR_TESTES / "spec_exemplos"   # gerado a cada execução (fora do git)
RE_BLOCO = re.compile(r"^```cinza[ \t]*\r?\n(.*?)^```", re.M | re.S)
# Folgado para o build com sanitizers (-O0 + ASan deixa o interpretador dezenas
# de vezes mais lento: o exemplo da rede neural passava dos 10 s); um laço
# infinito continua sendo pego
TIMEOUT_S = 30

RE_EXPECT = re.compile(r"//\s*expect:\s?(.*)$")
RE_EXPECT_ERROR = re.compile(r"//\s*expect-error:\s?(.*)$")
RE_EXPECT_NOT = re.compile(r"//\s*expect-not:\s?(.*)$")
RE_EXPECT_EXIT = re.compile(r"//\s*expect-exit:\s?(\d+)\s*$")
RE_ARGS = re.compile(r"//\s*args:\s?(.*)$")
RE_STDIN = re.compile(r"//\s*stdin:\s?(.*)$")
RE_ENV = re.compile(r"//\s*env:\s?(\w+)=(.*)$")


def ler_argumentos(caminho):
    """Argumentos de `// args:` (separados por espaço), passados depois do arquivo."""
    for linha in caminho.read_text(encoding="utf-8-sig").splitlines():
        m = RE_ARGS.search(linha)
        if m:
            return m.group(1).split()
    return []


def ler_entrada(caminho):
    """Entrada padrão do processo: as linhas de `// stdin:`, cada uma com fim de linha."""
    linhas = []
    for linha in caminho.read_text(encoding="utf-8-sig").splitlines():
        m = RE_STDIN.search(linha)
        if m:
            linhas.append(m.group(1).rstrip() + "\n")
    return "".join(linhas).encode("utf-8")


def ler_ambiente(caminho):
    """Ambiente do processo: o atual mais as variáveis de `// env: NOME=valor`."""
    env = dict(os.environ)
    for linha in caminho.read_text(encoding="utf-8-sig").splitlines():
        m = RE_ENV.search(linha)
        if m:
            env[m.group(1)] = m.group(2).rstrip()
    return env


def ler_expectativas(caminho):
    """Retorna (linhas esperadas no stdout, trechos de erro esperados, trechos proibidos)."""
    esperado, erros, proibidos = [], [], []
    for linha in caminho.read_text(encoding="utf-8-sig").splitlines():
        m = RE_EXPECT_ERROR.search(linha)
        if m:
            erros.append(m.group(1).rstrip())
            continue
        m = RE_EXPECT_NOT.search(linha)
        if m:
            proibidos.append(m.group(1).rstrip())
            continue
        m = RE_EXPECT.search(linha)
        if m:
            esperado.append(m.group(1).rstrip())
    return esperado, erros, proibidos


def ler_codigo_saida(caminho):
    """Código de `// expect-exit: N`, ou 0 se não houver."""
    for linha in caminho.read_text(encoding="utf-8-sig").splitlines():
        m = RE_EXPECT_EXIT.search(linha)
        if m:
            return int(m.group(1))
    return 0


def descrever_codigo(codigo):
    """Traduz códigos de saída de crash em algo legível."""
    if codigo < 0:
        return f"{codigo} (morto pelo sinal {-codigo})"
    if codigo > 0xFFFF:  # NTSTATUS do Windows, ex.: 0xC00000FD = stack overflow
        return f"{codigo} (0x{codigo:08X}, crash do processo)"
    return str(codigo)


def rodar(binario, caminho):
    """Executa um teste. Retorna (passou, motivo, detalhes)."""
    esperado, erros, proibidos = ler_expectativas(caminho)
    codigo_esperado = ler_codigo_saida(caminho)
    if not esperado and not erros and not codigo_esperado:
        return False, "nenhum '// expect' encontrado no arquivo", ""

    try:
        proc = subprocess.run(
            [str(binario)] + OPCOES + [str(caminho.relative_to(RAIZ))] + ler_argumentos(caminho),
            cwd=RAIZ, capture_output=True, timeout=TIMEOUT_S, env=ler_ambiente(caminho),
            input=ler_entrada(caminho),
        )
    except subprocess.TimeoutExpired:
        return False, f"tempo esgotado ({TIMEOUT_S} s)", ""

    stdout = proc.stdout.decode("utf-8", errors="replace").replace("\r\n", "\n")
    stderr = proc.stderr.decode("utf-8", errors="replace").replace("\r\n", "\n")
    linhas = stdout.splitlines()
    detalhes = (
        f"    código de saída: {descrever_codigo(proc.returncode)}\n"
        f"    esperado stdout: {esperado}\n"
        f"    obtido   stdout: {linhas}\n"
        f"    esperado erro:   {erros}\n"
        f"    obtido   stderr: {stderr.strip()!r}"
    )

    presentes = [t for t in proibidos if t in stdout + stderr]
    if presentes:
        return False, f"saída contém trecho proibido {presentes}", detalhes

    if erros:
        if proc.returncode == 0:
            return False, "esperava erro, mas o programa terminou com sucesso", detalhes
        # Fase 2.5: erro de compilação ou de runtime sai com código 1 (crash não conta)
        if proc.returncode != 1:
            return False, f"código de saída {descrever_codigo(proc.returncode)}, esperado 1", detalhes
        saida = stdout + stderr
        faltando = [e for e in erros if e not in saida]
        if faltando:
            return False, f"mensagem de erro não contém {faltando}", detalhes
        if linhas[:len(esperado)] != esperado:
            return False, "stdout antes do erro difere do esperado", detalhes
        return True, "", ""

    if proc.returncode != codigo_esperado:
        return False, (f"código de saída {descrever_codigo(proc.returncode)}, esperado "
                       f"{codigo_esperado}"), detalhes
    if linhas != esperado:
        return False, "stdout difere do esperado", detalhes
    return True, "", ""


def exemplos_da_spec(filtro):
    """Extrai os exemplos executáveis de spec/*.md. Devolve [(nome, caminho)],
    com nome no formato "spec/arquivo.md:linha"."""
    if not DIR_SPEC.is_dir():
        return []
    DIR_EXEMPLOS.mkdir(exist_ok=True)
    for velho in DIR_EXEMPLOS.glob("*.cinza"):
        velho.unlink()
    exemplos = []
    for md in sorted(DIR_SPEC.glob("*.md")):
        texto = md.read_text(encoding="utf-8")
        for m in RE_BLOCO.finditer(texto):
            codigo = m.group(1)
            if not (RE_EXPECT.search(codigo) or RE_EXPECT_ERROR.search(codigo) or
                    any(RE_EXPECT.search(l) or RE_EXPECT_ERROR.search(l) or RE_EXPECT_EXIT.search(l)
                        for l in codigo.splitlines())):
                continue
            linha = texto.count("\n", 0, m.start()) + 1
            nome = f"spec/{md.name}:{linha}"
            if filtro and filtro not in nome:
                continue
            caminho = DIR_EXEMPLOS / f"{md.stem}_{linha}.cinza"
            caminho.write_text(codigo, encoding="utf-8")
            exemplos.append((nome, caminho))
    return exemplos


OPCOES = []   # opções do interpretador antes do arquivo (ex.: --cvm)


def executar(binario, caminho, opcoes):
    """Executa um programa; devolve (código, stdout, stderr) em bytes."""
    try:
        proc = subprocess.run(
            [str(binario)] + opcoes + [str(caminho.relative_to(RAIZ))] + ler_argumentos(caminho),
            cwd=RAIZ, capture_output=True, timeout=TIMEOUT_S, env=ler_ambiente(caminho),
            input=ler_entrada(caminho),
        )
        return proc.returncode, proc.stdout, proc.stderr
    except subprocess.TimeoutExpired:
        return "tempo esgotado", b"", b""


def diferencial(binario, testes):
    """Compara os dois modos em cada programa. Devolve o número de diferenças."""
    diferencas = 0
    for nome, caminho in testes:
        a = executar(binario, caminho, ["--interp"])
        b = executar(binario, caminho, ["--cvm"])
        if a == b:
            print(f"IGUAL  {nome}")
            continue
        diferencas += 1
        print(f"DIFERENTE  {nome}")
        for rotulo, x, y in (("código", a[0], b[0]), ("stdout", a[1], b[1]), ("stderr", a[2], b[2])):
            if x != y:
                print(f"    {rotulo} interpretador: {x!r}")
                print(f"    {rotulo} cvm:           {y!r}")
    print(f"\n{len(testes) - diferencas} iguais, {diferencas} diferentes, {len(testes)} no total")
    return diferencas


def main():
    modo_diff = "--diff" in sys.argv
    if modo_diff:
        sys.argv.remove("--diff")
    for opcao in ("--cvm", "--interp"):
        if opcao in sys.argv:
            sys.argv.remove(opcao)
            OPCOES.append(opcao)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    if len(sys.argv) > 1:
        binario = Path(sys.argv[1])
        if not binario.is_absolute():
            binario = RAIZ / binario
    else:
        binario = RAIZ / "cinza.exe"
        if not binario.exists():
            binario = RAIZ / "cinza"
    filtro = sys.argv[2] if len(sys.argv) > 2 else ""

    if not binario.exists():
        print(f"Binário não encontrado: {binario} (rode 'make' antes)")
        return 2

    testes = [(p.name, p) for p in sorted(DIR_TESTES.glob("*.cinza")) if filtro in p.name]
    testes += exemplos_da_spec(filtro)
    if not testes:
        print(f"Nenhum teste encontrado em {DIR_TESTES} com o filtro '{filtro}'")
        return 2

    if modo_diff:
        return 1 if diferencial(binario, testes) else 0

    falhas = 0
    for nome, caminho in testes:
        passou, motivo, detalhes = rodar(binario, caminho)
        if passou:
            print(f"PASS  {nome}")
        else:
            falhas += 1
            print(f"FAIL  {nome}: {motivo}")
            if detalhes:
                print(detalhes)

    print(f"\n{len(testes) - falhas} passaram, {falhas} falharam, {len(testes)} no total")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
