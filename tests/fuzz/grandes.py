#!/usr/bin/env python3
"""Programas grandes (REVISAO_V2.md, Q2): o tempo, a memória e o resultado dos dois backends.

Famílias de programas em tamanhos crescentes — muitas funções, muitas classes e structs, uma main
de 100 mil linhas, muitas locais, aninhamento até o limite do parser (e além dele), cadeias de
operadores, literais enormes, identificadores compridos. Para cada programa: o cinza (a CVM:
front-end, compilação e execução) e o cinzac em -O0 e -O2 (a compilação e o executável), com o
tempo, o pico de memória e o código de saída. A saída nativa tem de ser a da CVM.

Defeito é: queda, erro interno, tempo esgotado, saída diferente entre os backends, e um tempo
que cresce bem mais que o tamanho (quadrático) de um tamanho para o seguinte. Programas além de
um limite documentado (2000 níveis de aninhamento, 65535 registradores por função na CVM) têm de
terminar com a mensagem desse limite, não cair.

Uso: python tests/fuzz/grandes.py [--familia nome]... [--tempo-limite 300] [--sem-o2]
"""

import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import psutil

RAIZ = Path(__file__).resolve().parents[2]
EXE = ".exe" if os.name == "nt" else ""
CINZA = RAIZ / f"cinza{EXE}"
CINZAC = RAIZ / "llvm" / "bin" / f"cinzac{EXE}"


# ── as famílias: (nome, tamanhos, gerador(n) → (fonte, saída esperada ou None)) ──
def funcoes(n):
    fs = [f"fn fun{i}(i32 x) -> i32 {{\n  return (x + {i}) % 1000;\n}}\n" for i in range(n)]   # f32 é um tipo
    corpo = "".join(f"  t = fun{i}(t);\n" for i in range(n))
    return "".join(fs) + f"fn main() {{\n  i32 t = 0;\n{corpo}  print(t);\n}}\n", None


def cadeia_de_chamadas(n):   # f_i chama f_{i-1}: profundidade de chamada pequena (só n < 2000)
    fs = ["fn g0(i32 x) -> i32 {\n  return x;\n}\n"]
    fs += [f"fn g{i}(i32 x) -> i32 {{\n  return g{i - 1}(x + 1);\n}}\n" for i in range(1, n)]
    return "".join(fs) + f"fn main() {{\n  print(g{n - 1}(0));\n}}\n", f"{n - 1}"


def linhas(n):   # uma main de n linhas com uma variável só
    corpo = "".join(f"  t = (t + {i % 97}) % 100003;\n" for i in range(n))
    return f"fn main() {{\n  i64 t = 0;\n{corpo}  print(t);\n}}\n", None


def locais(n):   # n variáveis locais numa função (a CVM tem 65535 registradores)
    corpo = "".join(f"  i32 v{i} = {i % 100};\n" for i in range(n))
    return f"fn main() {{\n{corpo}  print(v0 + v{n - 1});\n}}\n", None


def parenteses(n):
    return f"fn main() {{\n  i32 x = {'(' * n}1{')' * n};\n  print(x);\n}}\n", "1"


def blocos(n):
    return "fn main() {\n" + "{\n" * n + "  print(1);\n" + "}\n" * n + "}\n", "1"


def ifs(n):
    return "fn main() {\n  i32 x = 1;\n" + "if (x > 0) {\n" * n + "  print(x);\n" + "}\n" * n + "}\n", "1"


def soma_encadeada(n):   # a + a + a ...: a árvore cresce com n
    return f"fn main() {{\n  i64 a = 1;\n  i64 t = {' + '.join(['a'] * n)};\n  print(t);\n}}\n", f"{n}"


def negacoes(n):
    return f"fn main() {{\n  i32 x = {'-' * n}1;\n  print(x);\n}}\n", "-1" if n % 2 else "1"


def chamadas_aninhadas(n):
    return "fn id(i32 x) -> i32 {\n  return x;\n}\n" + \
           f"fn main() {{\n  print({'id(' * n}7{')' * n});\n}}\n", "7"


def listas_aninhadas(n):
    return f"fn main() {{\n  print({'[' * n}1{']' * n});\n}}\n", "[" * n + "1" + "]" * n


def classes(n):
    cs = [f"class C{i} {{\n  i32 v = {i};\n  pub {{\n    fn valor() -> i32 {{ return v; }}\n  }}\n}}\n" for i in range(n)]
    corpo = "".join(f"  t += new C{i}().valor() % 10;\n" for i in range(n))
    return "".join(cs) + f"fn main() {{\n  i64 t = 0;\n{corpo}  print(t);\n}}\n", None


def structs_largos(n):   # um struct de n campos
    campos = "".join(f"  i32 c{i} = {i};\n" for i in range(n))
    return f"struct Largo {{\n{campos}}}\nfn main() {{\n  Largo l = new Largo();\n  l.c{n - 1} += 1;\n" \
           f"  print(l.c0, l.c{n - 1});\n}}\n", f"0 {n}"


def enum_grande(n):
    vals = ", ".join(f"V{i}" for i in range(n))
    return f"enum E {{ {vals} }}\nfn main() {{\n  E e = E.V{n - 1};\n  print(e, e == E.V{n - 1});\n}}\n", \
           f"E.V{n - 1} true"


def texto_literal(n):   # um literal de n caracteres
    return f'fn main() {{\n  string s = "{"a" * n}";\n  print(s.size());\n}}\n', f"{n}"


def lista_literal(n):
    return f"fn main() {{\n  list<i32> l = [{', '.join(str(i % 1000) for i in range(n))}];\n" \
           f"  print(l.size(), l[{n - 1}]);\n}}\n", f"{n} {(n - 1) % 1000}"


def dict_literal(n):
    pares = ", ".join(f'{{"k{i}", {i}}}' for i in range(n))
    return f"fn main() {{\n  dict<string, i32> d = {{{pares}}};\n  print(d.size(), d[\"k{n - 1}\"]);\n}}\n", \
           f"{n} {n - 1}"


def literais_distintos(n):   # muitas strings literais diferentes
    corpo = "".join(f'  t += "lit{i}".size();\n' for i in range(n))
    return f"fn main() {{\n  i64 t = 0;\n{corpo}  print(t);\n}}\n", None


def identificador_longo(n):
    nome = "x" * n
    return f"fn main() {{\n  i32 {nome} = 3;\n  print({nome});\n}}\n", "3"


def parametros(n):
    ps = ", ".join(f"i32 p{i}" for i in range(n))
    args = ", ".join(str(i % 10) for i in range(n))
    return f"fn f({ps}) -> i32 {{\n  return p0 + p{n - 1};\n}}\nfn main() {{\n  print(f({args}));\n}}\n", \
           f"{(n - 1) % 10}"


FAMILIAS = [
    ("funcoes", [1000, 5000, 20000], funcoes),
    ("cadeia_de_chamadas", [100, 1000, 1990], cadeia_de_chamadas),
    ("linhas", [10000, 50000, 100000], linhas),
    ("locais", [1000, 30000, 65000, 70000], locais),
    ("parenteses", [100, 1000, 1990, 2001, 100000], parenteses),
    ("blocos", [100, 1000, 1990, 2001], blocos),
    ("ifs", [100, 1000, 1990, 2001], ifs),
    ("soma_encadeada", [100, 1000, 1990, 2001, 100000], soma_encadeada),
    ("negacoes", [100, 1000, 1990, 2001, 100000], negacoes),
    ("chamadas_aninhadas", [100, 1000, 1990, 2001], chamadas_aninhadas),
    ("listas_aninhadas", [100, 1000, 1990, 2001], listas_aninhadas),
    ("classes", [100, 1000, 5000], classes),
    ("structs_largos", [100, 1000, 10000], structs_largos),
    ("enum_grande", [100, 10000, 100000], enum_grande),
    ("texto_literal", [10000, 1000000, 10000000], texto_literal),
    ("lista_literal", [10000, 100000, 1000000], lista_literal),
    ("dict_literal", [1000, 10000, 100000], dict_literal),
    ("literais_distintos", [1000, 10000, 50000], literais_distintos),
    ("identificador_longo", [100, 10000, 1000000], identificador_longo),
    ("parametros", [10, 100, 1000, 10000], parametros),
]


def mede(cmd, cwd, limite):
    """(código, stdout, stderr, segundos, pico de memória em MB) — código None: tempo esgotado"""
    inicio = time.perf_counter()
    p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL)
    pico = 0
    try:
        proc = psutil.Process(p.pid)
    except psutil.Error:
        proc = None
    saida = [b"", b""]
    import threading
    def le(i, f):
        saida[i] = f.read()
    ts = [threading.Thread(target=le, args=(0, p.stdout)), threading.Thread(target=le, args=(1, p.stderr))]
    for t in ts:
        t.start()
    while p.poll() is None:
        if time.perf_counter() - inicio > limite:
            p.kill()
            p.wait()
            for t in ts:
                t.join()
            return None, "", "", limite, pico / 2 ** 20
        try:
            if proc:
                pico = max(pico, proc.memory_info().peak_wset if hasattr(proc.memory_info(), "peak_wset")
                           else proc.memory_info().rss)
        except psutil.Error:
            pass
        time.sleep(0.01)
    for t in ts:
        t.join()
    return (p.returncode, saida[0].decode("utf-8", "replace"), saida[1].decode("utf-8", "replace"),
            time.perf_counter() - inicio, pico / 2 ** 20)


def resumo(rc, err):
    if rc is None:
        return "TEMPO"
    if rc < 0 or rc > 255:
        return f"CAIU {rc:#x}"
    if rc == 0:
        return "ok"
    linha = next((l for l in err.splitlines() if l.strip()), "")
    for marca in ("SyntaxError", "SemanticError", "LexicalError", "CVMError", "Erro interno", "Erro fatal"):
        if marca in linha:
            return marca + ": " + linha.split(marca, 1)[1].strip(": ")[:70]
    return f"rc={rc} " + linha[:70]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--familia", action="append")
    ap.add_argument("--tempo-limite", type=float, default=300)
    ap.add_argument("--sem-o2", action="store_true")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    pasta = Path(tempfile.mkdtemp(prefix="cinza_grandes_"))
    defeitos = []
    for nome, tamanhos, gerador in FAMILIAS:
        if args.familia and nome not in args.familia:
            continue
        anterior = {}   # ferramenta → (tamanho, segundos) do tamanho anterior

        def cresce(quem, n, t):
            # tamanho xk e tempo além de 2k vezes (com folga para o custo fixo, abaixo de 2 s)
            if quem in anterior and t > 2.0:
                n0, t0 = anterior[quem]
                if t / t0 > 2 * (n / n0):
                    defeitos.append(f"{nome}: o {quem} passou de {t0:.2f} s ({n0}) a {t:.2f} s ({n})")
            anterior[quem] = (n, t)

        for n in tamanhos:
            fonte, esperado = gerador(n)
            arq = pasta / f"{nome}_{n}.cinza"
            arq.write_text(fonte, encoding="utf-8")
            kb = len(fonte.encode()) / 1024
            rc, out, err, t, mem = mede([str(CINZA), arq.name], pasta, args.tempo_limite)
            cresce("cinza", n, t)
            linha = f"{nome:20} {n:>8} ({kb:8.0f} KB)  cinza {t:6.2f} s {mem:7.0f} MB  {resumo(rc, err):28}"
            estado_cvm = resumo(rc, err)
            if esperado is not None and rc == 0 and out.strip() != esperado:
                defeitos.append(f"{nome} {n}: a CVM imprimiu {out.strip()[:60]!r}, esperado {esperado[:60]!r}")
            if estado_cvm.startswith(("CAIU", "TEMPO", "Erro interno", "Erro fatal")):
                defeitos.append(f"{nome} {n}: cinza {estado_cvm}")
            for opt in (["-O0"] if args.sem_o2 else ["-O0", "-O2"]):
                exe = pasta / f"{nome}_{n}{opt}{EXE}"
                rcc, _, errc, tc, memc = mede([str(CINZAC), opt, arq.name, "-o", exe.name], pasta, args.tempo_limite)
                estado = resumo(rcc, errc)
                cresce(f"cinzac {opt}", n, tc)
                linha += f"  cinzac {opt} {tc:6.2f} s {memc:6.0f} MB"
                if rcc == 0:
                    rn, outn, errn, tn, _ = mede([str(exe)], pasta, args.tempo_limite)
                    linha += f" (roda {tn:5.2f} s)"
                    # um limite documentado da CVM (65535 registradores) que o nativo não tem
                    if not estado_cvm.startswith("CVMError") and (rn, outn, errn) != (rc, out, err):
                        defeitos.append(f"{nome} {n} {opt}: o nativo difere da CVM "
                                        f"(rc {rn} x {rc}: {outn.strip()[:40]!r} x {out.strip()[:40]!r})")
                    try:
                        exe.unlink()
                    except OSError:
                        pass
                else:
                    linha += f" [{estado}]"
                    if estado.startswith(("CAIU", "TEMPO", "Erro interno", "Erro fatal")):
                        defeitos.append(f"{nome} {n}: cinzac {opt} {estado}")
                    # o cinzac tem de recusar com o mesmo erro do front-end que a CVM
                    elif estado.split(":")[0] != estado_cvm.split(":")[0] and not estado_cvm.startswith("CVMError"):
                        defeitos.append(f"{nome} {n}: cinzac {opt} [{estado}] e a CVM [{estado_cvm}]")
            print(linha, flush=True)
            arq.unlink()
    print("\n" + ("\n".join("DEFEITO  " + d for d in defeitos) if defeitos else "nenhum defeito"))
    return 1 if defeitos else 0


if __name__ == "__main__":
    sys.exit(main())
