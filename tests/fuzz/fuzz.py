#!/usr/bin/env python3
"""Fuzzing da Cinza (REVISAO_V2.md, Q2): mutações dos programas da suíte contra os dois backends.

Cada mutante (de 1 a 3 mutações num programa de tests/ ou exemplos/: apagar, duplicar, trocar ou
inserir símbolos e linhas, enxertar linhas de outro programa, cortar o arquivo, aninhamento
fundo, bytes inválidos) roda:
  - no cinza (a CVM: front-end, compilação e execução), com CINZA_VAZAMENTOS=1;
  - no cinzac --emit-cir (o front-end e a tradução para a CIR);
  - numa amostra (--diferencial), compilado pelo cinzac e executado: a saída, os erros e o
    código de saída têm de ser os da CVM.

Defeito é: queda do processo (código de saída do Windows/sinal), "Erro interno", travamento do
front-end (o --emit-cir passou do tempo), vazamento (código 70) e diferença entre os backends.
Erros de compilação e de execução comuns são o esperado. Cada defeito novo (por assinatura) é
gravado em --saida, com o programa minimizado.

Uso: python tests/fuzz/fuzz.py [--tempo 300] [--semente N] [--trabalhos 6] [--diferencial 0.1]
"""

import argparse
import hashlib
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
EXE = ".exe" if os.name == "nt" else ""
CINZA = RAIZ / f"cinza{EXE}"
CINZAC = RAIZ / "llvm" / "bin" / f"cinzac{EXE}"

TOKEN = re.compile(r'"(?:[^"\\\n]|\\.)*"?|[A-Za-z_][A-Za-z0-9_]*|\d+(?:\.\d*)?(?:ff|f)?|->|==|!=|<=|>=|&&|\|\||'
                   r'\+=|-=|\*=|/=|%=|\s+|.', re.S)
POOL = ("fn class struct enum interface error pub const new self return if else while for in break "
        "continue try except finally throw import as cast and or not true false i32 i64 f32 f64 "
        "string str bool void var list dict pair print printf format input range type exit main "
        "Error ValueError IndexError KeyError Math Strings Lists").split() + \
       ["(", ")", "{", "}", "[", "]", "<", ">", ";", ",", ".", ":", "=", "+", "-", "*", "/", "%", "!",
        "==", "!=", "<=", ">=", "&&", "||", "->", "+=", "::", "\"", "\"x\"", "\"{x}\"", "\"{\"", "\"}\"",
        "0", "1", "-1", "2147483647", "2147483648", "9223372036854775807", "9223372036854775808",
        "99999999999999999999", "1.5", "1.5f", "1.5ff", "0.0ff", "1e10", "ação", "x", "y", "T", "_",
        "list<i32>", "dict<string, i32>", "pair<i32, i32>", "[i32]", "Caixa[i32]", "new P()", "@", "\\"]
# mutações que costumam manter o programa válido (o diferencial precisa de programas que rodam)
BORDAS = ["0", "1", "-1", "2", "7", "-7", "100", "255", "256", "65535", "2147483646", "2147483647",
          "-2147483647", "4294967296", "9223372036854775806", "9223372036854775807"]
BORDAS_F = ["0.0", "-0.0", "0.5", "1.0", "-1.5", "3.4028234", "1000000.0", "0.1", "123456789.125"]
FAMILIAS = [["+", "-", "*", "/", "%"], ["<", "<=", ">", ">=", "==", "!="], ["&&", "||"],
            ["+=", "-=", "*=", "/=", "%="]]
NAO_DETERMINISTICO = re.compile(r"\b(Random|Time|Files|input)\b")
CODIGO_INTERNO = re.compile(r"Erro interno|Erro fatal|Assertion|terminate called|std::")


def tokens(texto):
    return TOKEN.findall(texto)


class Fuzzer:
    def __init__(self, args):
        self.args = args
        self.rng = random.Random(args.semente)
        self.lock = threading.Lock()
        self.sementes = []
        for pasta in ("tests", "exemplos"):
            for p in sorted((RAIZ / pasta).rglob("*.cinza")):
                if any(x in p.parts for x in ("tests_adm", "spec_exemplos")):
                    continue
                texto = p.read_text(encoding="utf-8", errors="replace")
                # sem as linhas de expectativa: só o programa
                texto = "\n".join(l for l in texto.splitlines() if not l.lstrip().startswith("//"))
                self.sementes.append((p.relative_to(RAIZ).as_posix(), texto))
        self.linhas = [l for _, t in self.sementes for l in t.splitlines() if l.strip()]
        # a pasta de trabalho é um projeto: czm.mod e os módulos dos testes, para os imports
        self.trabalho = Path(tempfile.mkdtemp(prefix="cinza_fuzz_"))
        (self.trabalho / "czm.mod").write_text("", encoding="utf-8")
        shutil.copytree(RAIZ / "tests" / "modulos", self.trabalho / "modulos")
        self.saida = Path(args.saida)
        self.saida.mkdir(parents=True, exist_ok=True)
        self.vistos = set()
        self.contagem = {"mutantes": 0, "cinza": 0, "cinzac": 0, "diferencial": 0, "defeitos": 0}

    # ── mutações ─────────────────────────────────────────────────────────
    def muta(self, texto, rng):
        for _ in range(rng.randint(1, 3)):
            toks = tokens(texto)
            if not toks:
                toks = [""]
            op = rng.randrange(16)
            i = rng.randrange(len(toks))
            if op >= 12:   # as que mantêm o programa válido: números e operadores
                if op in (12, 13):
                    nums = [k for k, t in enumerate(toks) if re.fullmatch(r"\d+(\.\d*)?(ff|f)?", t)]
                    if nums:
                        k = rng.choice(nums)
                        m = re.fullmatch(r"\d+(\.\d*)?(ff|f)?", toks[k])
                        if m.group(1) is None:
                            toks[k] = rng.choice(BORDAS).lstrip("-")
                        else:
                            toks[k] = rng.choice(BORDAS_F).lstrip("-") + (m.group(2) or "")
                else:
                    ops = [k for k, t in enumerate(toks) if any(t in f for f in FAMILIAS)]
                    if ops:
                        k = rng.choice(ops)
                        familia = next(f for f in FAMILIAS if toks[k] in f)
                        toks[k] = rng.choice(familia)
                texto = "".join(toks)
                continue
            if op == 0:
                del toks[i]
            elif op == 1:
                toks.insert(i, toks[i])
            elif op == 2:
                toks[i] = rng.choice(POOL)
            elif op == 3:
                toks[i] = rng.choice(toks)
            elif op == 4:
                j = rng.randrange(len(toks))
                toks[i], toks[j] = toks[j], toks[i]
            elif op == 5:
                toks.insert(i, " " + rng.choice(POOL) + " ")
            elif op in (6, 7, 8):
                ls = texto.splitlines()
                if not ls:
                    ls = [""]
                k = rng.randrange(len(ls))
                if op == 6:
                    del ls[k]
                elif op == 7:
                    ls.insert(k, rng.choice(self.linhas))   # enxerto de outro programa
                else:
                    ls.insert(k, ls[k])
                texto = "\n".join(ls)
                continue
            elif op == 9:
                texto = texto[: rng.randrange(len(texto) + 1)]
                continue
            elif op == 10:   # aninhamento fundo
                abre, fecha = rng.choice([("(", ")"), ("[", "]"), ("{", "}"), ("-", ""), ("!", ""),
                                          ("(1 + ", ")"), ("list<", ">"), ("f(", ")")])
                n = rng.choice([50, 500, 5000, 50000])
                toks.insert(i, abre * n + (fecha * n if rng.random() < 0.7 else ""))
            else:   # um byte qualquer (UTF-8 inválido entra na escrita)
                toks.insert(i, chr(rng.choice([0, 1, 9, 13, 0x7F, 0xA0, 0xE9, 0x2028, 0xFEFF, 0x1F600])))
            texto = "".join(toks)
        return texto

    # ── execução ─────────────────────────────────────────────────────────
    def roda(self, cmd, tempo, ambiente=None):
        env = dict(os.environ, CINZA_VAZAMENTOS="1")
        if ambiente:
            env.update(ambiente)
        try:
            p = subprocess.run(cmd, cwd=self.trabalho, capture_output=True, timeout=tempo,
                               stdin=subprocess.DEVNULL, env=env)
            return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            return None, "", ""

    @staticmethod
    def queda(rc):
        return rc is not None and (rc < 0 or rc > 255)

    def defeito(self, alvo, rc, out, err, frontend):
        """A assinatura do defeito, ou None se a saída é a de um programa (válido ou não)"""
        if rc is None:
            return f"{alvo}: travou" if frontend else None
        texto = out + err
        if self.queda(rc):
            return f"{alvo}: caiu com {rc:#x}"
        if CODIGO_INTERNO.search(texto):
            linha = next((l for l in texto.splitlines() if CODIGO_INTERNO.search(l)), "")
            return f"{alvo}: " + re.sub(r"\d+", "N", linha.strip())[:120]
        if rc == 70 and "[vazamentos]" in err:
            return f"{alvo}: vazamento " + re.sub(r"\d+", "N", err.strip().splitlines()[-1])[:80]
        return None

    def testa(self, texto, n, diferencial):
        """Os defeitos do mutante: [(assinatura, alvo)]; `diferencial`: também compila e compara"""
        arq = self.trabalho / f"m{n}.cinza"
        arq.write_bytes(texto.encode("utf-8", "surrogatepass"))
        achados = []
        rc1, out1, err1 = self.roda([str(CINZA), arq.name], self.args.tempo_cvm)
        d = self.defeito("cinza", rc1, out1, err1, frontend=False)
        if d:
            achados.append((d, "cinza"))
        rc2, out2, err2 = self.roda([str(CINZAC), "--emit-cir", arq.name], self.args.tempo_frontend)
        d = self.defeito("cinzac --emit-cir", rc2, out2, err2, frontend=True)
        if d:
            achados.append((d, "cir"))
        with self.lock:
            self.contagem["cinza"] += 1
            self.contagem["cinzac"] += 1
        # diferencial: só programas que a CVM terminou, determinísticos, que o cinzac compila
        if (rc1 is not None and not self.queda(rc1) and rc2 == 0 and not NAO_DETERMINISTICO.search(texto)
                and diferencial):
            exe = self.trabalho / f"m{n}{EXE}"
            rc3, _, err3 = self.roda([str(CINZAC), arq.name, "-o", exe.name], 60)
            d = self.defeito("cinzac", rc3, "", err3, frontend=True)
            if d:
                achados.append((d, "cinzac"))
            elif rc3 == 0:
                rc4, out4, err4 = self.roda([str(exe)], self.args.tempo_cvm * 3)
                with self.lock:
                    self.contagem["diferencial"] += 1
                d = self.defeito("nativo", rc4, out4, err4, frontend=False)
                if d:
                    achados.append((d, "nativo"))
                elif rc4 is not None and (rc4, out4, err4) != (rc1, out1, err1):
                    achados.append(("diferença entre a CVM e o nativo", "diferencial"))
            for f in (exe,):
                try:
                    f.unlink()
                except OSError:
                    pass
        return achados

    def assina(self, texto, n, alvo):
        """A assinatura do defeito deste alvo no texto (para a minimização)"""
        return {a: d for d, a in self.testa(texto, n, alvo in ("cinzac", "nativo", "diferencial"))}.get(alvo)

    def minimiza(self, texto, n, alvo, assinatura, limite):
        """Delta debugging por linhas e depois por símbolos, mantendo a mesma assinatura"""
        fim = time.time() + limite
        for unidade in ("linhas", "tokens"):
            partes = texto.splitlines(keepends=True) if unidade == "linhas" else tokens(texto)
            tam = max(1, len(partes) // 2)
            while tam >= 1 and time.time() < fim:
                i, mudou = 0, False
                while i < len(partes) and time.time() < fim:
                    tentativa = partes[:i] + partes[i + tam:]
                    if tentativa and self.assina("".join(tentativa), n, alvo) == assinatura:
                        partes, mudou = tentativa, True
                    else:
                        i += tam
                if not mudou:
                    tam //= 2
            texto = "".join(partes)
        return texto

    def registra(self, assinatura, alvo, texto, origem, n):
        with self.lock:
            if assinatura in self.vistos:
                return
            self.vistos.add(assinatura)
            self.contagem["defeitos"] += 1
        minimo = self.minimiza(texto, n, alvo, assinatura, self.args.tempo_minimizar)
        h = hashlib.sha1(assinatura.encode()).hexdigest()[:8]
        base = self.saida / f"defeito_{h}"
        base.with_suffix(".cinza").write_bytes(minimo.encode("utf-8", "surrogatepass"))
        base.with_suffix(".original.cinza").write_bytes(texto.encode("utf-8", "surrogatepass"))
        base.with_suffix(".txt").write_text(f"{assinatura}\nsemente: {origem}\n", encoding="utf-8")
        print(f"DEFEITO  {assinatura}\n         {base.with_suffix('.cinza')} (de {origem})", flush=True)

    def trabalho_de(self, n):
        rng = random.Random(f"{self.args.semente}:{n}")
        origem, semente = rng.choice(self.sementes)
        texto = self.muta(semente, rng)
        for assinatura, alvo in self.testa(texto, n, rng.random() < self.args.diferencial):
            self.registra(assinatura, alvo, texto, origem, n)
        for f in (self.trabalho / f"m{n}.cinza",):
            try:
                f.unlink()
            except OSError:
                pass
        with self.lock:
            self.contagem["mutantes"] += 1

    def roda_tudo(self):
        print(f"{len(self.sementes)} sementes; {self.args.trabalhos} trabalhos; {self.args.tempo} s; "
              f"semente {self.args.semente}; achados em {self.saida}", flush=True)
        fim = time.time() + self.args.tempo
        n = 0
        with ThreadPoolExecutor(self.args.trabalhos) as ex:
            pendentes = set()
            while time.time() < fim:
                while len(pendentes) < self.args.trabalhos * 2:
                    pendentes.add(ex.submit(self.trabalho_de, n))
                    n += 1
                pronto = next(iter(pendentes))
                pronto.result()
                pendentes.discard(pronto)
            for f in pendentes:
                f.result()
        shutil.rmtree(self.trabalho, ignore_errors=True)
        c = self.contagem
        print(f"{c['mutantes']} mutantes ({c['diferencial']} no diferencial); {c['defeitos']} defeitos distintos",
              flush=True)
        return 1 if c["defeitos"] else 0


def main():
    ap = argparse.ArgumentParser(description="Fuzzing da Cinza (os dois backends)")
    ap.add_argument("--tempo", type=float, default=300, help="segundos de fuzzing")
    ap.add_argument("--semente", type=int, default=int(time.time()))
    ap.add_argument("--trabalhos", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--diferencial", type=float, default=0.1, help="fração compilada e comparada")
    ap.add_argument("--tempo-cvm", type=float, default=4)
    ap.add_argument("--tempo-frontend", type=float, default=15)
    ap.add_argument("--tempo-minimizar", type=float, default=120)
    ap.add_argument("--saida", default=str(Path(tempfile.gettempdir()) / "cinza_fuzz_achados"))
    args = ap.parse_args()
    if not CINZA.exists() or not CINZAC.exists():
        print("faltam o cinza e o cinzac: make e make cinzac")
        return 2
    return Fuzzer(args).roda_tudo()


if __name__ == "__main__":
    sys.exit(main())
