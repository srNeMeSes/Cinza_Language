#!/usr/bin/env python3
"""Runner de testes da linguagem Cinza.

Uso: python tests/run_tests.py [--sem-vazamentos] [--cinzac [--O2] [--asan]] [binario] [filtro]

Os programas rodam na CVM. Com --cinzac (P3, llvm/DESENHO.md seção 10), cada teste é
compilado pelo cinzac (padrão: llvm/bin/cinzac.exe) e o executável roda com os mesmos
args/stdin/env; um erro de compilação conta como a saída do teste, e o que o cinzac
ainda não compila (código 75) é contado à parte, nem como acerto nem como falha; --O2
compila cada teste com otimização (o padrão é -O0, mais rápido de compilar); --asan, com o
código gerado instrumentado pelo AddressSanitizer (o cinzac de depuração, make cinzac-debug). Todo
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
CINZAC_ASAN = False # Q2: --asan (com --cinzac)
PASTA_NATIVA = None # onde ficam os executáveis gerados (temporária)
NAO_SUPORTADO = 75  # código de saída do cinzac para "ainda não suportado"
contador_nativo = 0
executados_nativos = 0   # testes que rodaram como executável nativo (não só a compilação)


def ler_ambiente(caminho):
    """Ambiente do processo: o atual mais as variáveis de `// env: NOME=valor`.
    (tests/czm.mod faz de tests/ a raiz do projeto: `import modulos.geom;` vale em
    qualquer subpasta.) Com o modo sem vazamento (o padrão), o
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
            opcoes = (["-O2"] if CINZAC_O2 else []) + (["--asan"] if CINZAC_ASAN else [])
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
    # um programa que termina bem não escreve na saída de erro (pegava o "libunwind: pc not in
    # table" que o executável nativo do Windows escrevia a cada erro capturado)
    if codigo_esperado == 0 and stderr.strip():
        return False, "saída de erro num programa que terminou bem", detalhes
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


def confere_versao(binario):
    """Q5: a versão é uma só — a de versao.h, a da spec (spec/README.md) e a que o compilador
    mostra em --version."""
    m = re.search(r'#define CINZA_VERSAO "([^"]+)"', (RAIZ / "versao.h").read_text(encoding="utf-8"))
    versao = m.group(1) if m else "?"
    spec = (RAIZ / "spec" / "README.md").read_text(encoding="utf-8")
    problemas = []
    if f"Versão {versao} ·" not in spec:
        problemas.append(f"a spec (spec/README.md) não diz 'Versão {versao}'")
    if binario.exists():
        saida = subprocess.run([str(binario), "--version"], capture_output=True).stdout.decode("utf-8", "replace")
        if not re.match(rf"cinzac? {re.escape(versao)} ", saida):
            problemas.append(f"'{binario.name} --version' mostrou {saida.strip()!r}, não a versão {versao}")
    for p in problemas:
        print(f"FAIL  versão: {p}")
    return not problemas


def testes_do_formatador(binario, programas):
    """Q6: o cinza fmt. Cada tests/formatador/X.entrada.cinza dá exatamente X.saida.cinza (e a saída
    não muda se formatada de novo), ou recusa com o trecho de X.erro. E em todos os programas da
    suíte o formatador é idempotente e nunca recusa um programa por mudar os tokens."""
    def fmt(entrada):
        r = subprocess.run([str(binario), "fmt", "-"], input=entrada, capture_output=True, timeout=TIMEOUT_S)
        return r.returncode, r.stdout, r.stderr.decode("utf-8", "replace")

    resultados = []
    pasta = DIR_TESTES / "formatador"
    for entrada in sorted(pasta.glob("*.entrada.cinza")):
        base = entrada.name[: -len(".entrada.cinza")]
        nome = f"formatador/{base}"
        rc, saida, err = fmt(entrada.read_bytes())
        esperada = pasta / f"{base}.saida.cinza"
        erro = pasta / f"{base}.erro"
        if erro.exists():
            trecho = erro.read_text(encoding="utf-8").strip()
            ok = rc != 0 and trecho in err
            resultados.append((nome, ok, f"esperada a recusa com {trecho!r}; rc={rc}, {err.strip()!r}"))
        elif esperada.exists():
            if rc != 0:
                resultados.append((nome, False, f"recusou: {err.strip()}"))
            elif saida != esperada.read_bytes():
                resultados.append((nome, False, "a saída difere de " + esperada.name))
            else:
                rc2, saida2, _ = fmt(saida)
                resultados.append((nome, rc2 == 0 and saida2 == saida, "formatar de novo muda a saída"))
        else:
            resultados.append((nome, False, f"falta {base}.saida.cinza ou {base}.erro"))

    problemas = []
    for p in programas:
        if not isinstance(p, Path) or p.suffix != ".cinza":
            continue
        rc, saida, err = fmt(p.read_bytes())
        if rc != 0:
            if "mudaria os tokens" in err:
                problemas.append(f"{p.relative_to(DIR_TESTES).as_posix()}: {err.strip()}")
            continue   # erro léxico ou de sintaxe: recusa certa
        rc2, saida2, _ = fmt(saida)
        if rc2 != 0 or saida2 != saida:
            problemas.append(f"{p.relative_to(DIR_TESTES).as_posix()}: formatar de novo muda o resultado")
    resultados.append(("formatador/a suíte (idempotente, os mesmos tokens)", not problemas, "; ".join(problemas[:3])))
    return resultados


def testes_de_pacotes(binario):
    """Q10: as dependências locais do czm.mod (requer ./caminho [as nome]): a transitiva, o 'as',
    e os erros do czm.mod e dos nomes de pacote, cada caso numa pasta temporária."""
    r = []

    def projeto(base, arquivos):
        for caminho, texto in arquivos.items():
            p = base / caminho
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(texto, encoding="utf-8")

    def roda(base, *args):
        p = subprocess.run([str(binario), *args], cwd=base, capture_output=True, timeout=TIMEOUT_S)
        return p.returncode, p.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), p.stderr.decode("utf-8", "replace")

    LIB_A = {"libs/a/czm.mod": "projeto alfa\nrequer ../b\n",
             "libs/a/x.cinza": "module x;\nimport beta.y;\npub fn f() -> string { return \"x+\" + y.g(); }\n",
             "libs/b/czm.mod": "projeto beta\n",
             "libs/b/y.cinza": "module y;\npub fn g() -> string { return \"y\"; }\n"}
    casos = [
        ("transitiva", {**LIB_A, "czm.mod": "projeto app\nrequer ./libs/a\n",
                        "main.cinza": "import alfa.x;\nfn main() { print(x.f()); }\n"}, 0, "x+y\n"),
        ("as", {**LIB_A, "czm.mod": "requer ./libs/a as outro\n",
                "main.cinza": "import outro.x;\nfn main() { print(x.f()); }\n"}, 0, "x+y\n"),
        ("sem nome", {"czm.mod": "requer ./libs/c\n", "libs/c/czm.mod": "// sem projeto\n",
                      "main.cinza": "fn main() { }\n"}, 1, "não declara o nome no czm.mod dela"),
        ("nomes repetidos", {"czm.mod": "requer ./libs/c\nrequer ./libs/d\n", "libs/c/czm.mod": "projeto util\n",
                             "libs/d/czm.mod": "projeto util\n", "main.cinza": "fn main() { }\n"},
         1, "dois pacotes com o nome 'util' (linhas 1 e 2)"),
        ("nome igual a pasta", {"czm.mod": "requer ./libs/c\n", "libs/c/czm.mod": "projeto libs\n",
                                "main.cinza": "fn main() { }\n"}, 1, "é também uma pasta ou um arquivo do projeto"),
        ("nome de nativo", {"czm.mod": "requer ./libs/c as Math\n", "libs/c/czm.mod": "projeto c\n",
                            "main.cinza": "fn main() { }\n"}, 1, "tem o nome de um módulo da biblioteca padrão"),
        ("declaração desconhecida", {"czm.mod": "projeto app\nversao 1\n", "main.cinza": "fn main() { }\n"},
         1, "czm.mod:2:1: ImportError: declaração desconhecida no czm.mod: 'versao'"),
        ("cinza nova demais", {"czm.mod": "cinza 9.0\n", "main.cinza": "fn main() { }\n"}, 1, "o projeto pede a Cinza 9.0"),
        ("dependência que não existe", {"czm.mod": "requer ../nada\n", "main.cinza": "fn main() { }\n"},
         1, "a dependência '../nada' não é um projeto"),
        ("módulo que falta", {**LIB_A, "czm.mod": "requer ./libs/a\n", "main.cinza": "import alfa.zz;\nfn main() { }\n"},
         1, "o pacote 'alfa' não tem o módulo 'zz'"),
        ("git sem versão", {"czm.mod": "requer github.com/ana/json\n", "main.cinza": "fn main() { }\n"},
         1, "precisa da versão (uma tag)"),
    ]
    for nome, arquivos, rc_esperado, esperado in casos:
        with tempfile.TemporaryDirectory(prefix="cinza_pacote_") as pasta:
            base = Path(pasta)
            projeto(base, arquivos)
            rc, out, err = roda(base, "main.cinza")
            ok = rc == rc_esperado and (esperado in out if rc_esperado == 0 else esperado in err)
            r.append((f"pacotes/{nome}", ok, f"código {rc}, saída {out!r}, stderr {err.strip()!r}"))
    return r


def testes_de_pacotes_git(binario):
    """Q10: os pacotes git — cinza add/get/remove com repositórios git temporários (file://), o
    czm.lock (a versão, o commit e o hash), a seleção das versões (a maior pedida na mesma major;
    majors diferentes são erro) e as conferências da compilação (o lock, o cache e o hash)."""
    import shutil
    r = []
    try:
        subprocess.run(["git", "--version"], capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return [("pacotes-git/git instalado", False, "os testes de pacotes git precisam do git")]

    def git(pasta, *args):
        subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "core.autocrlf=false", *args],
                       cwd=pasta, capture_output=True, check=True)

    def escreve(base, arquivos):
        for caminho, texto in arquivos.items():
            p = base / caminho
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(texto.encode("utf-8"))

    def repo(base, nome, versoes):
        """um repositório com um commit e uma tag por versão: {tag: {arquivo: texto}}"""
        pasta = base / "repos" / nome
        pasta.mkdir(parents=True)
        git(pasta, "init", "-q")
        for tag, arquivos in versoes.items():
            escreve(pasta, arquivos)
            git(pasta, "add", "-A")
            git(pasta, "commit", "-qm", tag)
            git(pasta, "tag", tag)
        return "file://" + pasta.as_posix()

    def roda(base, *args):
        amb = dict(os.environ, CINZA_PACOTES=str(base / "cache"))
        p = subprocess.run([str(binario), *args], cwd=base / "app", capture_output=True, timeout=TIMEOUT_S * 2, env=amb)
        return p.returncode, p.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), p.stderr.decode("utf-8", "replace")

    def json_v(v, extra=""):
        return {"czm.mod": "projeto json\ncinza 0.6\n" + extra,
                "json.cinza": "module json;\npub fn v() -> string { return \"" + v + "\"; }\n"}

    APP = {"app/czm.mod": "projeto app\n", "app/main.cinza": "import json;\nfn main() { print(json.v()); }\n"}

    def caso(nome, passo):
        with tempfile.TemporaryDirectory(prefix="cz_") as pasta:
            base = Path(pasta)
            try:
                ok, detalhe = passo(base)
            except subprocess.CalledProcessError as e:
                ok, detalhe = False, f"git falhou: {e.cmd} {e.stderr!r}"
            r.append((f"pacotes-git/{nome}", ok, detalhe))

    def add_e_roda(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0"), "v1.1.0": json_v("1.1")})
        escreve(base, APP)
        rc1, _, e1 = roda(base, "add", url, "v1.0.0")
        lock = (base / "app" / "czm.lock").read_text(encoding="utf-8") if (base / "app" / "czm.lock").exists() else ""
        rc2, o2, e2 = roda(base, "main.cinza")
        mod = (base / "app" / "czm.mod").read_text(encoding="utf-8")
        ok = (rc1 == 0 and rc2 == 0 and o2 == "1.0\n" and f"requer {url} v1.0.0" in mod and
              re.search(r"pacote \S+ v1\.0\.0 [0-9a-f]{40} sha256-[0-9a-f]{64}", lock) is not None)
        return ok, f"add {rc1} {e1!r}; roda {rc2} {o2!r} {e2!r}; lock {lock!r}"
    caso("add com versão grava o czm.mod e o czm.lock", add_e_roda)

    def add_ultima(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0"), "v1.10.0": json_v("1.10"), "v1.9.0": json_v("1.9")})
        escreve(base, APP)
        rc1, _, e1 = roda(base, "add", url)
        rc2, o2, e2 = roda(base, "main.cinza")
        return rc1 == 0 and o2 == "1.10\n", f"add {rc1} {e1!r}; roda {rc2} {o2!r} {e2!r}"
    caso("add sem versão pega a maior tag", add_ultima)

    def mvs(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0"), "v1.2.0": json_v("1.2"), "v1.3.0": json_v("1.3")})
        url_b = repo(base, "beta", {"v0.1.0": {"czm.mod": f"projeto beta\nrequer {url} v1.2.0\n",
                                               "beta.cinza": "module beta;\nimport json;\npub fn b() -> string { return json.v(); }\n"}})
        escreve(base, {"app/czm.mod": f"projeto app\nrequer {url} v1.0.0\nrequer {url_b} v0.1.0\n",
                       "app/main.cinza": "import json;\nimport beta;\nfn main() { print(json.v() + \" \" + beta.b()); }\n"})
        rc1, _, e1 = roda(base, "get")
        rc2, o2, e2 = roda(base, "main.cinza")
        return rc1 == 0 and o2 == "1.2 1.2\n", f"get {rc1} {e1!r}; roda {rc2} {o2!r} {e2!r}"
    caso("a maior versão pedida vale para todos", mvs)

    def major(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0"), "v2.0.0": json_v("2.0")})
        url_b = repo(base, "beta", {"v0.1.0": {"czm.mod": f"projeto beta\nrequer {url} v2.0.0\n",
                                               "beta.cinza": "module beta;\n"}})
        escreve(base, {"app/czm.mod": f"projeto app\nrequer {url} v1.0.0\nrequer {url_b} v0.1.0\n",
                       "app/main.cinza": "fn main() { }\n"})
        rc, _, e = roda(base, "get")
        return rc == 1 and "conflito de versões" in e and "pede v1.0.0" in e and "pede v2.0.0" in e, f"{rc} {e!r}"
    caso("majors diferentes são erro", major)

    def sem_get(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0")})
        escreve(base, {"app/czm.mod": f"requer {url} v1.0.0\n", "app/main.cinza": "fn main() { }\n"})
        rc, _, e = roda(base, "main.cinza")
        return rc == 1 and "não está no czm.lock: rode 'cinza get'" in e, f"{rc} {e!r}"
    caso("sem o cinza get, a compilação pede", sem_get)

    def sem_cache(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0")})
        escreve(base, APP)
        roda(base, "add", url)
        shutil.rmtree(base / "cache")
        rc, _, e = roda(base, "main.cinza")
        rc2, _, _ = roda(base, "get")
        rc3, o3, _ = roda(base, "main.cinza")
        return rc == 1 and "não está no cache" in e and rc2 == 0 and o3 == "1.0\n", f"{rc} {e!r}; get {rc2}; {o3!r}"
    caso("sem o cache, o cinza get baixa de novo", sem_cache)

    def alterado(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0")})
        escreve(base, APP)
        roda(base, "add", url)
        [arq] = (base / "cache").glob("json-*@v1.0.0/json.cinza")
        arq.write_text("module json;\npub fn v() -> string { return \"mexido\"; }\n", encoding="utf-8")
        rc, _, e = roda(base, "main.cinza")
        rc2, _, e2 = roda(base, "get")
        rc3, o3, _ = roda(base, "main.cinza")
        return (rc == 1 and "não confere com o hash do czm.lock" in e and rc2 == 0 and o3 == "1.0\n",
                f"{rc} {e!r}; get {rc2} {e2!r}; {o3!r}")
    caso("o cache alterado é recusado, e o get o refaz", alterado)

    def tag_movida(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0")})
        escreve(base, APP)
        roda(base, "add", url)
        pasta = base / "repos" / "json"
        escreve(pasta, json_v("outro"))
        git(pasta, "commit", "-qam", "outro")
        git(pasta, "tag", "-f", "v1.0.0")
        shutil.rmtree(base / "cache")
        rc, _, e = roda(base, "get")
        return rc == 1 and "mudou no repositório (a tag foi movida)" in e, f"{rc} {e!r}"
    caso("a tag movida no repositório é recusada", tag_movida)

    def desatualizado(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0"), "v1.1.0": json_v("1.1")})
        escreve(base, APP)
        roda(base, "add", url, "v1.0.0")
        escreve(base, {"app/czm.mod": f"projeto app\nrequer {url} v1.1.0\n"})
        rc, _, e = roda(base, "main.cinza")
        return rc == 1 and "o czm.lock está desatualizado; rode 'cinza get'" in e, f"{rc} {e!r}"
    caso("o czm.lock desatualizado é recusado", desatualizado)

    def remove(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0")})
        escreve(base, APP)
        roda(base, "add", url, "as", "js")
        rc, _, e = roda(base, "remove", "js")
        mod = (base / "app" / "czm.mod").read_text(encoding="utf-8")
        return (rc == 0 and "requer" not in mod and not (base / "app" / "czm.lock").exists(),
                f"{rc} {e!r}; czm.mod {mod!r}")
    caso("remove tira a linha e o czm.lock", remove)

    def git_com_local(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0", "requer ../outro\n")})
        escreve(base, {"app/czm.mod": f"requer {url} v1.0.0\n", "app/main.cinza": "fn main() { }\n"})
        rc, _, e = roda(base, "get")
        return rc == 1 and "um pacote git não pode ter" in e, f"{rc} {e!r}"
    caso("um pacote git com dependência local é erro", git_com_local)

    def tag_inexistente(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0")})
        escreve(base, APP)
        rc, _, e = roda(base, "add", url, "v9.0.0")
        mod = (base / "app" / "czm.mod").read_text(encoding="utf-8")
        return rc == 1 and "não foi possível baixar" in e and mod == "projeto app\n", f"{rc} {e!r}; {mod!r}"
    caso("a tag que não existe é erro, e o czm.mod fica como estava", tag_inexistente)

    def comentario_e_url(base):
        url = repo(base, "json", {"v1.0.0": json_v("1.0")})
        escreve(base, {"app/czm.mod": f"projeto app // o app\nrequer {url} v1.0.0 // o json\n",
                       "app/main.cinza": "import json;\nfn main() { print(json.v()); }\n"})
        rc1, _, e1 = roda(base, "get")
        rc2, o2, e2 = roda(base, "main.cinza")
        return rc1 == 0 and o2 == "1.0\n", f"get {rc1} {e1!r}; {rc2} {o2!r} {e2!r}"
    caso("o // de uma URL não é comentário", comentario_e_url)

    def add_local_de_subpasta(base):
        escreve(base, {"app/czm.mod": "projeto app\n", "app/src/main.cinza": "import util.u;\nfn main() { print(u.f()); }\n",
                       "libs/util/czm.mod": "projeto util\n",
                       "libs/util/u.cinza": "module u;\npub fn f() -> string { return \"u\"; }\n"})
        amb = dict(os.environ, CINZA_PACOTES=str(base / "cache"))
        p = subprocess.run([str(binario), "add", "../../libs/util"], cwd=base / "app" / "src", capture_output=True,
                           timeout=TIMEOUT_S, env=amb)
        mod = (base / "app" / "czm.mod").read_text(encoding="utf-8")
        rc2, o2, e2 = roda(base, "src/main.cinza")
        return (p.returncode == 0 and "requer ../libs/util\n" in mod and o2 == "u\n",
                f"add {p.returncode} {p.stderr!r}; czm.mod {mod!r}; roda {rc2} {o2!r} {e2!r}")
    caso("add local de uma subpasta grava o caminho a partir da raiz", add_local_de_subpasta)
    return r


def testes_do_projeto(binario):
    """Q1f: sem czm.mod, um import de arquivo é erro (só os nativos valem); 'cinza init' cria o
    czm.mod e o mesmo programa passa a rodar; um segundo 'init' recusa."""
    r = []
    with tempfile.TemporaryDirectory(prefix="cinza_projeto_") as pasta:
        pasta = Path(pasta)
        (pasta / "util.cinza").write_text("module util;\npub fn dobro(i32 x) -> i32 { return x * 2; }\n", encoding="utf-8")
        (pasta / "main.cinza").write_text("import util;\nimport Math;\nfn main() { print(util.dobro(21), Math.abs(-1)); }\n",
                                          encoding="utf-8")
        (pasta / "so.cinza").write_text("import Math;\nfn main() { print(Math.abs(-2)); }\n", encoding="utf-8")
        def roda(*args):
            p = subprocess.run([str(binario), *args], cwd=pasta, capture_output=True, timeout=TIMEOUT_S)
            return p.returncode, p.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), \
                   p.stderr.decode("utf-8", "replace")
        rc, out, err = roda("main.cinza")
        r.append(("projeto/import sem czm.mod é erro", rc == 1 and "não está num projeto" in err and "cinza init" in err,
                  f"código {rc}, stderr {err.strip()!r}"))
        rc, out, err = roda("so.cinza")
        r.append(("projeto/só nativos, sem czm.mod", rc == 0 and out == "2\n", f"código {rc}, saída {out!r} {err.strip()!r}"))
        rc, out, err = roda("init")
        mod = (pasta / "czm.mod").read_text(encoding="utf-8") if (pasta / "czm.mod").is_file() else ""
        # Q10: com o nome do projeto (o da pasta) e a versão da linguagem
        r.append(("projeto/cinza init cria o czm.mod", rc == 0 and f"projeto {pasta.name}" in mod and "cinza 0.6" in mod,
                  f"código {rc}, {out!r} {err!r}, czm.mod {mod!r}"))
        rc, out, err = roda("main.cinza")
        r.append(("projeto/com czm.mod, o import vale", rc == 0 and out == "42 1\n", f"código {rc}, saída {out!r} {err.strip()!r}"))
        rc, out, err = roda("init")
        r.append(("projeto/init de novo recusa", rc == 1 and "já existe" in err, f"código {rc}, stderr {err.strip()!r}"))
    return r


def testes_do_lsp(binario):
    """Q6: o servidor de linguagem (cinza --lsp), por um cliente mínimo (tests/lsp/cliente.py): os
    erros do front-end como diagnósticos (com a coluna em UTF-16, depois de acentos), limpos quando
    o texto é corrigido, as análises repetidas no mesmo processo, um erro num módulo importado no
    arquivo dele, a formatação e o encerramento."""
    sys.path.insert(0, str(DIR_TESTES / "lsp"))
    from cliente import Cliente, uri_de
    r = []
    with tempfile.TemporaryDirectory(prefix="cinza_lsp_") as pasta:
        pasta = Path(pasta)
        (pasta / "czm.mod").write_text("", encoding="utf-8")   # Q1f: os imports exigem o projeto
        principal = pasta / "prog.cinza"
        uri = uri_de(principal)
        c = Cliente(binario, pasta)
        try:
            ini = c.pede("initialize", {"processId": None, "rootUri": uri_de(pasta), "capabilities": {}})
            caps = (ini or {}).get("result", {}).get("capabilities", {})
            r.append(("lsp/initialize", caps.get("documentFormattingProvider") is True and caps.get("textDocumentSync") == 1,
                      f"resposta: {ini}"))
            c.avisa("initialized", {})

            texto = 'fn main() {\n  print("ação", nao_existe);\n}\n'
            c.avisa("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "cinza", "version": 1, "text": texto}})
            ds = c.diagnosticos(uri)
            col = texto.split("\n")[1].index("nao_existe")
            ok = (ds is not None and len(ds) == 1 and "SemanticError" in ds[0]["message"] and "nao_existe" in ds[0]["message"]
                  and ds[0]["range"]["start"] == {"line": 1, "character": col}
                  and ds[0]["range"]["end"] == {"line": 1, "character": col + len("nao_existe")})
            r.append(("lsp/erro semântico (coluna em UTF-16, depois de acentos)", ok, f"diagnósticos: {ds}"))

            def muda(t, versao):
                c.avisa("textDocument/didChange", {"textDocument": {"uri": uri, "version": versao}, "contentChanges": [{"text": t}]})
                return c.diagnosticos(uri)

            ds = muda('fn main() {\n  print(1\n}\n', 2)
            r.append(("lsp/erro de sintaxe", bool(ds) and all("SyntaxError" in d["message"] for d in ds), f"diagnósticos: {ds}"))
            ds = muda('fn main() {\n  print(1);\n}\n', 3)
            r.append(("lsp/corrigido: sem erros", ds == [], f"diagnósticos: {ds}"))
            def quantos(ds):
                return -1 if ds is None else len(ds)
            repetidas = []
            for k in range(5):
                repetidas.append(quantos(muda('fn main() { i32 x = "a"; }\n', 10 + 2 * k)))
                repetidas.append(quantos(muda('fn main() { i32 x = 1; }\n', 11 + 2 * k)))
            r.append(("lsp/análises repetidas no mesmo processo", repetidas == [1, 0] * 5, f"contagens: {repetidas}"))

            muda("fn main(){print(1);}\n", 30)
            f = c.pede("textDocument/formatting", {"textDocument": {"uri": uri}, "options": {"tabSize": 2, "insertSpaces": True}})
            edicoes = (f or {}).get("result") or []
            r.append(("lsp/formatação (o cinza fmt)", len(edicoes) == 1 and edicoes[0]["newText"] == "fn main() { print(1); }\n",
                      f"resposta: {f}"))

            (pasta / "util.cinza").write_text("module util; pub fn f() -> i32 {\n  return \"texto\";\n}\n", encoding="utf-8")
            muda("import util;\nfn main() {\n  print(util.f());\n}\n", 31)
            du = c.diagnosticos(uri_de(pasta / "util.cinza"))
            r.append(("lsp/erro num módulo importado, no arquivo dele", bool(du) and du[0]["range"]["start"]["line"] == 1,
                      f"diagnósticos: {du}"))
            r += testes_do_indice(c, pasta)
        finally:
            rc = c.encerra()
        r.append(("lsp/shutdown e exit", rc == 0, f"código de saída {rc}"))
    return r


PROGRAMA_LSP = """import util;
import Strings as st;

struct Ponto {
  i32 x = 0;
  i32 y = 0;
}

enum Cor { Vermelho, Verde }

class Conta {
  i64 saldo = 0;
  pub {
    fn deposita(i64 v) { saldo += v; }
    fn total() -> i64 { return saldo; }
  }
}

fn maior[T](T a, T b) -> T {
  if (a > b) { return a; }
  return b;
}

fn main() {
  Conta c = new Conta();
  c.deposita(10);
  Ponto p = new Ponto(1, 2);
  i32 soma = p.x + p.y;
  string nome = st.upper("ação");
  Cor cor = Cor.Verde;
  for (i32 i in range(0, 3)) { soma += maior(i, util.dobro(i)); }
  try { print(c.total(), soma, nome, cor); } except (ValueError e) { print(e.message); }
}
"""


def testes_do_indice(c, pasta):
    """O tipo ao passar o mouse, ir para a definição e completar nomes, sobre um programa com classe,
    struct, enum, genérico, módulo da Cinza, módulo nativo, laço e except."""
    from cliente import uri_de, normaliza
    (pasta / "util.cinza").write_text("module util; pub fn dobro(i32 x) -> i32 {\n  return x * 2;\n}\n", encoding="utf-8")
    uri = uri_de(pasta / "indice.cinza")
    c.avisa("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "cinza", "version": 1, "text": PROGRAMA_LSP}})
    ds = c.diagnosticos(uri)
    r = [("lsp/índice: o programa sem erros", ds == [], f"diagnósticos: {ds}")]
    linhas = PROGRAMA_LSP.split("\n")

    def pos(alvo, n=0, delta=1):
        k = 0
        for i, l in enumerate(linhas):
            j = l.find(alvo)
            while j >= 0:
                if k == n:
                    return {"line": i, "character": j + delta}
                k += 1
                j = l.find(alvo, j + 1)
        raise ValueError(alvo)

    def hover(alvo, n=0):
        res = (c.pede("textDocument/hover", {"textDocument": {"uri": uri}, "position": pos(alvo, n)}) or {}).get("result")
        return res and res["contents"]["value"].replace("```cinza\n", "").replace("\n```", "")

    def definicao(alvo, n=0):
        res = (c.pede("textDocument/definition", {"textDocument": {"uri": uri}, "position": pos(alvo, n)}) or {}).get("result")
        return res and (normaliza(res["uri"]).rsplit("/", 1)[-1], res["range"]["start"]["line"], res["range"]["start"]["character"])

    casos = [
        ("soma", 1, "i32 soma", ("indice.cinza", 27, 6)),
        ("deposita", 1, "fn Conta.deposita(i64 v)", ("indice.cinza", 13, 7)),
        ("saldo", 1, "i64 saldo", ("indice.cinza", 11, 6)),
        ("Verde", 1, "Cor.Verde", ("indice.cinza", 8, 21)),
        ("maior", 1, "fn maior(i32 a, i32 b) -> i32", ("indice.cinza", 18, 3)),
        ("dobro", 0, "fn util.dobro(i32 x) -> i32", ("util.cinza", 0, 20)),
        ("upper", 0, "fn Strings.upper(string) -> string", None),
        ("e.message", 0, "ValueError e", ("indice.cinza", 31, 64)),
        ("Conta()", 0, "class Conta", ("indice.cinza", 10, 6)),
    ]
    for alvo, n, info, defin in casos:
        h, d = hover(alvo, n), definicao(alvo, n)
        r.append((f"lsp/índice: {alvo}", h == info and d == defin, f"hover {h!r} (esperado {info!r}), definição {d} (esperada {defin})"))

    def completa(texto, linha, coluna):
        c.avisa("textDocument/didChange", {"textDocument": {"uri": uri}, "contentChanges": [{"text": texto}]})
        c.diagnosticos(uri)
        res = (c.pede("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": linha, "character": coluna}}) or {}).get("result")
        return [i["label"] for i in res or []]

    alvo = linhas.index("  c.deposita(10);")
    com_ponto = "\n".join(linhas[:alvo + 1] + ["  c.", "  st."] + linhas[alvo + 1:])
    membros = completa(com_ponto, alvo + 1, 4)
    r.append(("lsp/completar: os métodos públicos depois de c.", membros == ["deposita", "total"], f"itens: {membros}"))
    modulo = completa(com_ponto, alvo + 2, 5)
    r.append(("lsp/completar: o módulo nativo depois de st.", "upper" in modulo and "split" in modulo, f"itens: {modulo[:8]}"))
    livres = completa(PROGRAMA_LSP, linhas.index("  i32 soma = p.x + p.y;"), 2)
    esperados = {"c", "p", "Ponto", "Conta", "maior", "main", "print", "st", "util", "fn"}
    r.append(("lsp/completar: as locais anteriores, o nível de cima e os embutidos", esperados <= set(livres) and "nome" not in livres,
              f"itens: {livres[:20]}"))
    return r


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    global VAZAMENTOS, CINZAC, CINZAC_O2, CINZAC_ASAN, PASTA_NATIVA
    if "--sem-vazamentos" in sys.argv:
        VAZAMENTOS = False
        sys.argv.remove("--sem-vazamentos")
    if "--cinzac" in sys.argv:
        CINZAC = True
        sys.argv.remove("--cinzac")
    if "--O2" in sys.argv:
        CINZAC_O2 = True
        sys.argv.remove("--O2")
    if "--asan" in sys.argv:
        CINZAC_ASAN = True
        sys.argv.remove("--asan")

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
    if not confere_versao(binario):
        sys.exit(1)

    if not binario.exists():
        print(f"Binário não encontrado: {binario} (rode 'make' antes)")
        return 2

    # Testes em subpastas por assunto (tests/genericos/gen_modulo.cinza); ficam de fora os
    # módulos de apoio, os exemplos extraídos da spec e pastas locais (tests_adm)
    fora = {"modulos", "spec_exemplos", "tests_adm", "formatador", "lsp"}
    testes = [(p.relative_to(DIR_TESTES).as_posix(), p)
              for p in sorted(DIR_TESTES.rglob("*.cinza"))
              if not fora.intersection(p.relative_to(DIR_TESTES).parts[:-1])
              and filtro in p.relative_to(DIR_TESTES).as_posix() and eh_teste(p)]
    testes += exemplos_da_spec(filtro)
    so_formatador = not CINZAC and filtro and (filtro in "formatador/" or filtro in "lsp/")
    if not testes and not so_formatador:
        print(f"Nenhum teste encontrado em {DIR_TESTES} com o filtro '{filtro}'")
        return 2

    falhas = 0
    extras = 0            # os testes do formatador (não são programas a rodar)
    nao_suportados = {}   # motivo → quantos (modo --cinzac)
    extras_lista = []
    if not CINZAC and "formatador" in (filtro or "formatador"):
        extras_lista += testes_do_formatador(binario, [c for _, c in testes])
    if not CINZAC and "lsp" in (filtro or "lsp"):
        extras_lista += testes_do_lsp(binario)
    if not CINZAC and "projeto" in (filtro or "projeto"):
        extras_lista += testes_do_projeto(binario)
    if not CINZAC and "pacote" in (filtro or "pacote"):
        extras_lista += testes_de_pacotes(binario)
        extras_lista += testes_de_pacotes_git(binario)
    if extras_lista:
        for nome, passou, motivo in extras_lista:
            extras += 1
            if passou:
                print(f"PASS  {nome}")
            else:
                falhas += 1
                print(f"FAIL  {nome}: {motivo}")
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
    total = len(testes) + extras
    resumo = f"{total - falhas - nsup} passaram, {falhas} falharam"
    if CINZAC:
        resumo += (f", {nsup} ainda não suportados ({executados_nativos} rodaram como executável "
                   f"nativo; os demais que passaram são erros de compilação, do front-end comum)")
    print(f"\n{resumo}, {total} no total")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
