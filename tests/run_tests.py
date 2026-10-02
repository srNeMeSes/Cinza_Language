#!/usr/bin/env python3
"""Runner de testes da linguagem Cinza.

Uso: python tests/run_tests.py [binario] [filtro]

Cada tests/*.cinza declara a saída esperada em comentários:
    // expect: <linha exata do stdout>
    // expect-error: <trecho da mensagem de erro>
    // expect-not: <trecho que NÃO pode aparecer em stdout+stderr>
    // args: a b c   (argumentos passados depois do arquivo → main(list<string> args))
    // env: NOME=valor (variável de ambiente; caminhos relativos à raiz do projeto)
    // stdin: linha   (uma linha da entrada padrão; várias diretivas = várias linhas)

Sem expect-error: o programa deve sair com código 0 e o stdout deve ser
exatamente a sequência de linhas `expect`.
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
TIMEOUT_S = 10

RE_EXPECT = re.compile(r"//\s*expect:\s?(.*)$")
RE_EXPECT_ERROR = re.compile(r"//\s*expect-error:\s?(.*)$")
RE_EXPECT_NOT = re.compile(r"//\s*expect-not:\s?(.*)$")
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
    if not esperado and not erros:
        return False, "nenhum '// expect' encontrado no arquivo", ""

    try:
        proc = subprocess.run(
            [str(binario), str(caminho.relative_to(RAIZ))] + ler_argumentos(caminho),
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

    if proc.returncode != 0:
        return False, f"código de saída {descrever_codigo(proc.returncode)}", detalhes
    if linhas != esperado:
        return False, "stdout difere do esperado", detalhes
    return True, "", ""


def main():
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

    testes = sorted(p for p in DIR_TESTES.glob("*.cinza") if filtro in p.name)
    if not testes:
        print(f"Nenhum teste encontrado em {DIR_TESTES} com o filtro '{filtro}'")
        return 2

    falhas = 0
    for caminho in testes:
        passou, motivo, detalhes = rodar(binario, caminho)
        if passou:
            print(f"PASS  {caminho.name}")
        else:
            falhas += 1
            print(f"FAIL  {caminho.name}: {motivo}")
            if detalhes:
                print(detalhes)

    print(f"\n{len(testes) - falhas} passaram, {falhas} falharam, {len(testes)} no total")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
