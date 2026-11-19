#!/usr/bin/env python3
"""Runner de testes da linguagem Cinza.

Uso: python tests/run_tests.py [--sem-vazamentos] [--cinzac [--O2]] [binario] [filtro]

Os programas rodam na CVM. Com --cinzac (P3, llvm/DESENHO.md seção 10), cada teste é
compilado pelo cinzac (padrão: llvm/bin/cinzac.exe) e o executável roda com os mesmos
args/stdin/env; um erro de compilação conta como a saída do teste, e o que o cinzac
ainda não compila (código 75) é contado à parte, nem como acerto nem como falha; --O2
compila cada teste com otimização (o padrão é -O0, mais rápido de compilar). Todo
teste roda com
CINZA_VAZAMENTOS=1: se sobrar um objeto do runtime ao sair, o programa termina com
código 70 e o teste falha (--sem-vazamentos desliga).

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
tests/spec_exemplos/ e conferido com as mesmas regras. Um capítulo com a linha `<!-- exemplos-pendentes: motivo -->` é escrito antes do
código: seus exemplos são o alvo de uma etapa em andamento e ficam de fora (a suíte
avisa quantos). Blocos sem essas linhas
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
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
DIR_TESTES = RAIZ / "tests"
DIR_SPEC = RAIZ / "spec"
DIR_EXEMPLOS = DIR_TESTES / "spec_exemplos"   # gerado a cada execução (fora do git)
RE_BLOCO = re.compile(r"^```cinza[ \t]*\r?\n(.*?)^```", re.M | re.S)
RE_PENDENTES = re.compile(r"<!--\s*exemplos-pendentes:\s*(.*?)\s*-->")
# Folgado para o build com sanitizers (-O0 + ASan é dezenas de vezes mais lento:
# o exemplo da rede neural passava dos 10 s); um laço infinito continua sendo pego
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


VAZAMENTOS = True   # P1: todo teste confere também que nenhum objeto do runtime sobra
CINZAC = False      # P3: --cinzac
CINZAC_O2 = False   # P9: --O2 (com --cinzac)
PASTA_NATIVA = None # onde ficam os executáveis gerados (temporária)
NAO_SUPORTADO = 75  # código de saída do cinzac para "ainda não suportado"
contador_nativo = 0
executados_nativos = 0   # testes que rodaram como executável nativo (não só a compilação)


def ler_ambiente(caminho):
    """Ambiente do processo: o atual mais as variáveis de `// env: NOME=valor`.
    (tests/czm.mod faz de tests/ a raiz do projeto: `import modulos.geom;` vale em
    qualquer subpasta, sem CINZA_PATH.) Com o modo sem vazamento (o padrão), o
    programa sai com código 70 se sobrar algum objeto do runtime ao terminar."""
    env = dict(os.environ)
    if VAZAMENTOS:
        env["CINZA_VAZAMENTOS"] = "1"
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


def eh_teste(caminho):
    """Numa pasta com czm.mod próprio abaixo de tests/ (um projeto de exemplo, como
    projetos/crud_contatos), só os arquivos com expectativas são testes; o resto é
    o código do projeto, importado por eles. Fora disso, todo .cinza é teste (e
    um sem expectativa falha, para não passar despercebido)."""
    pasta = caminho.parent
    while pasta != DIR_TESTES and DIR_TESTES in pasta.parents:
        if (pasta / "czm.mod").is_file():
            esperado, erros, _ = ler_expectativas(caminho)
            return bool(esperado or erros or ler_codigo_saida(caminho))
        pasta = pasta.parent
    return True


def descrever_codigo(codigo):
    """Traduz códigos de saída de crash em algo legível."""
    if codigo < 0:
        return f"{codigo} (morto pelo sinal {-codigo})"
    if codigo > 0xFFFF:  # NTSTATUS do Windows, ex.: 0xC00000FD = stack overflow
        return f"{codigo} (0x{codigo:08X}, crash do processo)"
    return str(codigo)


def rodar(binario, caminho):
    """Executa um teste. Retorna (passou, motivo, detalhes); passou é None quando o
    cinzac ainda não compila o programa (modo --cinzac)."""
    global contador_nativo, executados_nativos
    esperado, erros, proibidos = ler_expectativas(caminho)
    codigo_esperado = ler_codigo_saida(caminho)
    if not esperado and not erros and not codigo_esperado:
        return False, "nenhum '// expect' encontrado no arquivo", ""

    comando = [str(binario), str(caminho.relative_to(RAIZ))] + ler_argumentos(caminho)
    proc = None
    if CINZAC:
        contador_nativo += 1
        exe = PASTA_NATIVA / f"t{contador_nativo}.exe"
        try:
            opcoes = ["-O2"] if CINZAC_O2 else []
            comp = subprocess.run([str(binario), str(caminho.relative_to(RAIZ)), "-o", str(exe)] + opcoes,
                                  cwd=RAIZ, capture_output=True, timeout=TIMEOUT_S * 2,
                                  env=ler_ambiente(caminho))
        except subprocess.TimeoutExpired:
            return False, f"tempo esgotado na compilação ({TIMEOUT_S * 2} s)", ""
        if comp.returncode == NAO_SUPORTADO:
            msg = comp.stderr.decode("utf-8", errors="replace").strip().splitlines()
            motivo = msg[-1].split("ainda não suportado: ", 1)[-1] if msg else "?"
            return None, motivo, ""
        if comp.returncode != 0:
            proc = comp   # erro de compilação: é a saída do teste
        else:
            comando = [str(exe)] + ler_argumentos(caminho)
            executados_nativos += 1

    try:
        if proc is None:
            proc = subprocess.run(comando, cwd=RAIZ, capture_output=True, timeout=TIMEOUT_S,
                                  env=ler_ambiente(caminho), input=ler_entrada(caminho))
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

    if proc.returncode == 70 and "[vazamentos]" in stderr:
        linha = next(l for l in stderr.splitlines() if "[vazamentos]" in l)
        return False, "vazamento de memória: " + linha.replace("[vazamentos] ", ""), detalhes

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
        pendente = RE_PENDENTES.search(texto)
        if pendente:
            n = len(RE_BLOCO.findall(texto))
            print(f"PEND  spec/{md.name}: {n} exemplos fora da suíte ({pendente.group(1)})")
            continue
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


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    global VAZAMENTOS, CINZAC, CINZAC_O2, PASTA_NATIVA
    if "--sem-vazamentos" in sys.argv:
        VAZAMENTOS = False
        sys.argv.remove("--sem-vazamentos")
    if "--cinzac" in sys.argv:
        CINZAC = True
        sys.argv.remove("--cinzac")
    if "--O2" in sys.argv:
        CINZAC_O2 = True
        sys.argv.remove("--O2")

    if len(sys.argv) > 1:
        binario = Path(sys.argv[1])
        if not binario.is_absolute():
            binario = RAIZ / binario
    elif CINZAC:
        binario = RAIZ / "llvm" / "bin" / "cinzac.exe"
        if not binario.exists():
            binario = RAIZ / "llvm" / "bin" / "cinzac"
    else:
        binario = RAIZ / "cinza.exe"
        if not binario.exists():
            binario = RAIZ / "cinza"
    filtro = sys.argv[2] if len(sys.argv) > 2 else ""

    if not binario.exists():
        print(f"Binário não encontrado: {binario} (rode 'make' antes)")
        return 2

    # Testes em subpastas por assunto (tests/genericos/gen_modulo.cinza); ficam de fora os
    # módulos de apoio, os exemplos extraídos da spec e pastas locais (tests_adm)
    fora = {"modulos", "spec_exemplos", "tests_adm"}
    testes = [(p.relative_to(DIR_TESTES).as_posix(), p)
              for p in sorted(DIR_TESTES.rglob("*.cinza"))
              if not fora.intersection(p.relative_to(DIR_TESTES).parts[:-1])
              and filtro in p.relative_to(DIR_TESTES).as_posix() and eh_teste(p)]
    testes += exemplos_da_spec(filtro)
    if not testes:
        print(f"Nenhum teste encontrado em {DIR_TESTES} com o filtro '{filtro}'")
        return 2

    falhas = 0
    nao_suportados = {}   # motivo → quantos (modo --cinzac)
    with tempfile.TemporaryDirectory(prefix="cinza_nativo_") as pasta:
        PASTA_NATIVA = Path(pasta)
        for nome, caminho in testes:
            passou, motivo, detalhes = rodar(binario, caminho)
            if passou is None:
                nao_suportados[motivo] = nao_suportados.get(motivo, 0) + 1
                print(f"NSUP  {nome}: {motivo}")
            elif passou:
                print(f"PASS  {nome}")
            else:
                falhas += 1
                print(f"FAIL  {nome}: {motivo}")
                if detalhes:
                    print(detalhes)

    nsup = sum(nao_suportados.values())
    if CINZAC and nsup:
        print("\nAinda não suportados pelo cinzac (os motivos mais comuns):")
        for motivo, n in sorted(nao_suportados.items(), key=lambda kv: -kv[1])[:10]:
            print(f"  {n:4}  {motivo}")
    resumo = f"{len(testes) - falhas - nsup} passaram, {falhas} falharam"
    if CINZAC:
        resumo += (f", {nsup} ainda não suportados ({executados_nativos} rodaram como executável "
                   f"nativo; os demais que passaram são erros de compilação, do front-end comum)")
    print(f"\n{resumo}, {len(testes)} no total")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
