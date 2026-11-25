#!/usr/bin/env python3
"""Teste diferencial do motor de regex da Cinza (regex.cpp, Q3) contra o `re` do Python.

Padrões aleatórios de uma gramática pequena (literais, ., classes, \\d \\w, grupos, |, * + ?
{n,m} e os preguiçosos, ^ $ \\b) e textos aleatórios. Para o que os dois motores definem igual
— a ocorrência mais à esquerda, com a preferência do Perl entre alternativas e repetições —
compara matches (texto inteiro), find (o índice) e groups (a ocorrência e os grupos). Ficam fora
os padrões em que as regras diferem de propósito: uma repetição de algo que casa o vazio (o Python
e o RE2 divergem nos grupos) e as várias ocorrências (vazias depois da anterior: a Cinza segue o
RE2). Defeito é qualquer resposta diferente, gravada com o padrão e o texto.

Uso: python tests/fuzz/regex_dif.py [--casos 3000] [--semente 1]
"""

import argparse
import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
CINZA = RAIZ / ("cinza.exe" if os.name == "nt" else "cinza")
LETRAS = "abç"


def atomo(r, prof):
    k = r.randrange(10)
    if prof > 2 or k < 4:
        return r.choice(LETRAS)
    if k == 4:
        return "."
    if k == 5:
        return r.choice(["[ab]", "[^a]", "[a-c]", "\\d", "\\w", "[ç0-9]"])
    if k == 6:
        return "(" + alternativa(r, prof + 1) + ")"
    if k == 7:
        return "(?:" + alternativa(r, prof + 1) + ")"
    if k == 8:
        return r.choice(["^", "$", "\\b"])
    return r.choice(LETRAS) + r.choice(LETRAS)


def pode_vazio(p):
    """O trecho pode casar o vazio em algum lugar? Então não é repetido aqui: as âncoras (^ $ \\b)
    não consomem nada, e sem elas o trecho casar "" quer dizer que alguma iteração pode ser vazia."""
    sem_ancoras = p.replace("\\b", "").replace("^", "").replace("$", "")
    try:
        return sem_ancoras == "" or re.fullmatch(sem_ancoras, "") is not None
    except re.error:
        return True


def repeticao(r, prof):
    a = atomo(r, prof)
    if r.randrange(3) == 0 and not pode_vazio(a):
        q = r.choice(["*", "+", "?", "{2}", "{1,3}", "{0,2}", "{2,}"])
        if r.randrange(3) == 0:
            q += "?"
        a += q
    return a


def sequencia(r, prof):
    return "".join(repeticao(r, prof) for _ in range(r.randint(1, 4)))


def alternativa(r, prof):
    partes = [sequencia(r, prof)]
    while r.randrange(4) == 0:
        partes.append(sequencia(r, prof))
    return "|".join(partes)


def literal_cinza(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def resposta_python(p, t):
    # Q1b: find devolve i32? (null sem ocorrência) e groups list<string?> (null no grupo que não
    # participou; "" no que casou o vazio)
    m = re.search(p, t)
    grupos = [] if not m else [m.group(0)] + list(m.groups())
    return (str(re.fullmatch(p, t) is not None).lower(), str(m.start()) if m else "null",
            "[" + ", ".join('"' + g + '"' if g is not None else "null" for g in grupos) + "]")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--casos", type=int, default=3000)
    ap.add_argument("--semente", type=int, default=1)
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    r = random.Random(args.semente)
    casos = []
    while len(casos) < args.casos:
        p = alternativa(r, 0)
        try:
            re.compile(p)
        except re.error:
            continue
        t = "".join(r.choice(LETRAS + "1 ") for _ in range(r.randint(0, 8)))
        casos.append((p, t))
    linhas = ["import Regex as re;", "fn main() {"]
    for p, t in casos:
        lp, lt = literal_cinza(p), literal_cinza(t)
        linhas.append(f"  print(re.matches({lt}, {lp}), re.find({lt}, {lp}), re.groups({lt}, {lp}));")
    linhas.append("}")
    pasta = Path(tempfile.mkdtemp(prefix="cinza_regex_"))
    arq = pasta / "dif.cinza"
    arq.write_text("\n".join(linhas) + "\n", encoding="utf-8")
    proc = subprocess.run([str(CINZA), str(arq)], capture_output=True)
    if proc.returncode != 0:
        print("o cinza falhou:", proc.stderr.decode("utf-8", "replace")[:500])
        return 1
    saidas = proc.stdout.decode("utf-8").replace("\r\n", "\n").rstrip("\n").split("\n")
    defeitos = []
    for (p, t), s in zip(casos, saidas):
        esperado = " ".join(resposta_python(p, t))
        if s != esperado:
            defeitos.append(f"padrão {p!r} texto {t!r}: Cinza {s!r}, Python {esperado!r}")
    print(f"{len(casos)} casos, {len(defeitos)} diferenças")
    for d in defeitos[:20]:
        print("DEFEITO ", d)
    return 1 if defeitos else 0


if __name__ == "__main__":
    sys.exit(main())
