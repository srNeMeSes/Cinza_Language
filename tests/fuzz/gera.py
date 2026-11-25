#!/usr/bin/env python3
"""Gerador de programas Cinza válidos para o teste diferencial (REVISAO_V2.md, Q2).

Cada programa é gerado do zero (no estilo do Csmith): bem tipado, determinístico e com fim
garantido — laços com limite fixo, recursão com profundidade limitada, chamadas só para funções
definidas antes. Usa números das quatro larguras perto das bordas, strings, listas, dicts,
structs (com a cópia por valor), classes, uma interface, um enum, funções genéricas e
try/except/finally; contas que estouram, divisões por zero e índices fora da faixa acontecem de
propósito, quase sempre dentro de um try. Q2 (2026-11-25): também o opcional T? (null, else,
if com declaração, o T? devolvido por método), uma classe genérica, listas de structs (o campo
lido e gravado pelo índice, o for), as funções do Math que o cinzac faz em linha (com argumentos
fora do domínio: a guarda e a mensagem de erro), o dict com remoções (a ordem de inserção) e uma
classe com um campo opcional do próprio tipo (um ciclo para a coleta).

Cada programa roda na CVM (cinza) e é compilado pelo cinzac em -O0 e em -O2; a saída, os erros e
o código de saída têm de ser os mesmos nos três (e nada pode vazar: CINZA_VAZAMENTOS=1). Um
programa que não compila ou não termina é defeito do gerador (contado à parte, com amostras).

Uso: python tests/fuzz/gera.py [--tempo 300] [--semente N] [--trabalhos 6]
     python tests/fuzz/gera.py --mostra N     (só imprime o programa da semente N)
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

INT_BORDAS = {"i32": [0, 1, 2, 3, 7, 10, 100, 255, 65535, 2147483647, 2147483646, 1073741824, 46341],
              "i64": [0, 1, 2, 5, 1000, 4294967296, 9223372036854775807, 3037000500, 4611686018427387904]}
FLT = ["0.0", "0.5", "1.0", "1.5", "2.25", "3.0", "10.0", "0.1", "100.5", "1000000.0", "123.375"]
PALAVRAS = ["a", "b", "ab", "cinza", "ação", "x1", "", "Olá", "zz", "10", "-3", "abc"]
NUMS = ("i32", "i64", "f32", "f64")


class Escopo:
    def __init__(self, pai=None):
        self.pai, self.vars = pai, {}
        self.protegidos = set(pai.protegidos) if pai else set()

    def todos(self):
        d = dict(self.pai.todos()) if self.pai else {}
        d.update(self.vars)
        return d


class Programa:
    def __init__(self, rng):
        self.r = rng
        self.n = 0
        self.funcoes = []      # (nome, [tipos dos parâmetros], retorno)
        self.structs = {}      # nome → [(campo, tipo)]
        self.classes = {}      # nome → info
        self.enum = None       # (nome, [valores])
        self.consts = {}       # nome → tipo
        self.profundidade_laco = 0
        self.grandes = {}      # valor i64 grande → a const G que o guarda
        self.genericas = {}    # Q2: "Caixa[i32]" → o T ("i32")

    PREFIXOS = {"v": "v", "i": "it", "f": "fun", "c": "cont", "x": "el", "p": "par", "q": "arg",
                "e": "err", "cp": "copia"}   # nunca um nome de tipo (i32, f64...)

    def novo(self, p="v"):
        self.n += 1
        return f"{self.PREFIXOS.get(p, p)}{self.n}"

    def chance(self, p):
        return self.r.random() < p

    # ── tipos ────────────────────────────────────────────────────────────
    def tipo_simples(self):
        return self.r.choice(["i32", "i32", "i64", "f32", "f64", "bool", "string", "string"])

    def tipo_qualquer(self):
        t = [self.tipo_simples()] * 6 + ["list<i32>", "list<string>", "dict<string, i32>"]
        t += list(self.structs) + list(self.classes) + ([self.enum[0]] if self.enum else [])
        t += ["i32?", "string?"] + [f"list<{s}>" for s in self.structs] + list(self.genericas)
        return self.r.choice(t)

    # ── literais ─────────────────────────────────────────────────────────
    def lit(self, t):
        r = self.r
        if t in ("i32", "i64"):
            v = r.choice(INT_BORDAS[t]) if self.chance(0.4) else r.randint(0, 50)
            # um literal solto é i32: os grandes de i64 vêm das const G (declaradas como i64)
            s = self.grandes.get(v, str(v)) if t == "i64" else str(v)
            return f"(-{s})" if v and self.chance(0.2) else s
        if t in ("f32", "f64"):
            s = r.choice(FLT) + ("f" if t == "f32" else "ff")
            return f"(-{s})" if self.chance(0.2) else s
        if t == "bool":
            return r.choice(["true", "false"])
        if t == "string":
            return '"' + r.choice(PALAVRAS) + '"'
        raise ValueError(t)

    # ── expressões ───────────────────────────────────────────────────────
    def vars_de(self, esc, t):
        return [n for n, tt in esc.todos().items() if tt == t]

    def expr(self, t, esc, d=0):
        r = self.r
        fundo = d >= 3
        vs = self.vars_de(esc, t) + [c for c, tt in self.consts.items() if tt == t]
        if t in NUMS:
            op = r.randrange(9 if not fundo else 2)
            if op == 0 or not vs:
                return self.lit(t)
            if op == 1:
                return r.choice(vs)
            if op in (2, 3, 4):
                ops = ["+", "-", "*", "/"] + (["%"] if t in ("i32", "i64") else [])
                return f"({self.expr(t, esc, d + 1)} {r.choice(ops)} {self.expr(t, esc, d + 1)})"
            if op == 5:
                return f"(-{self.expr(t, esc, d + 1)})"
            if op == 6:   # conversão de outro número (pode estourar)
                o = r.choice([x for x in NUMS if x != t])
                return f"cast<{t}>({self.expr(o, esc, d + 1)})"
            if op == 7:   # tamanho, campo, chamada
                return self.expr_derivada(t, esc, d)
            if t == "f64" and self.chance(0.6):   # Q2: o Math em linha do cinzac (com a guarda)
                f = r.choice(["sqrt", "log", "log10", "exp", "sin", "cos", "atan", "tanh", "cosh"])
                if self.chance(0.3):
                    f2 = r.choice(["pow", "atan2", "hypot"])
                    return f"Math.{f2}({self.expr('f64', esc, d + 1)}, {self.expr('f64', esc, d + 1)})"
                return f"Math.{f}({self.expr('f64', esc, d + 1)})"
            if t == "i64" and self.chance(0.4):
                f = r.choice(["floor", "ceil", "round", "trunc"])
                return f"Math.{f}({self.expr('f64', esc, d + 1)})"
            if t in ("i32", "i64") and self.chance(0.5):
                return f"Math.abs({self.expr(t, esc, d + 1)})"
            fm = r.choice(["max", "min"])
            return f"Math.{fm}({self.expr(t, esc, d + 1)}, {self.expr(t, esc, d + 1)})"
        if t == "bool":
            op = r.randrange(7 if not fundo else 2)
            if op == 0 or (op == 1 and not vs):
                return self.lit(t)
            if op == 1:
                return r.choice(vs)
            if op in (2, 3):
                o = r.choice(NUMS + ("string",))
                cmp = r.choice(["<", "<=", ">", ">=", "==", "!="])
                return f"({self.expr(o, esc, d + 1)} {cmp} {self.expr(o, esc, d + 1)})"
            if op == 4:
                return f"(!{self.expr('bool', esc, d + 1)})"
            if op == 5:
                return f"({self.expr('bool', esc, d + 1)} {r.choice(['and', 'or', '&&', '||'])} {self.expr('bool', esc, d + 1)})"
            if self.enum and self.vars_de(esc, self.enum[0]):
                return f"({r.choice(self.vars_de(esc, self.enum[0]))} == {self.enum[0]}.{r.choice(self.enum[1])})"
            return self.lit(t)
        if t == "string":
            op = r.randrange(8 if not fundo else 2)
            if op == 0 or (op == 1 and not vs):
                return self.lit(t)
            if op == 1:
                return r.choice(vs)
            if op == 2:
                o = r.choice(NUMS + ("bool", "string"))
                return f"({self.expr('string', esc, d + 1)} + {self.expr(o, esc, d + 1)})"
            if op == 3:
                return f"cast<string>({self.expr(r.choice(NUMS), esc, d + 1)})"
            if op == 4 and vs:   # índice e fatia (IndexError de propósito)
                s = r.choice(vs)
                if self.chance(0.5):
                    return f"{s}[{r.randint(0, 4)}]"
                return f"{s}[{r.randint(0, 2)}:{r.randint(0, 5)}]"
            if op == 5:
                return f"Strings.upper({self.expr('string', esc, d + 1)})"
            if op == 6:
                return f'format("<{{{self.expr_simples(esc)}}}>")'
            return self.expr_derivada(t, esc, d)
        if t.endswith("?"):   # Q2: o opcional — null, um valor, ou outro opcional
            base = t[:-1]
            vs2 = self.vars_de(esc, t)
            op = r.randrange(4)
            if op == 0:
                return "null"
            if op == 1 and vs2:
                return r.choice(vs2)
            if op == 2 and base == "i32" and self.vars_de(esc, "dict<string, i32>"):
                return f'Dicts.get({r.choice(self.vars_de(esc, "dict<string, i32>"))}, "{r.choice(["a", "b", "z"])}")'
            return self.expr(base, esc, d + 1)
        if t in self.genericas:
            vs2 = self.vars_de(esc, t)
            if vs2 and self.chance(0.7):
                return r.choice(vs2)
            return f"new {t}()"
        if t.startswith("list<"):
            el = t[5:-1]
            vs2 = self.vars_de(esc, t)
            if vs2 and self.chance(0.6):
                return r.choice(vs2)
            return "[" + ", ".join(self.expr(el, esc, 3) for _ in range(r.randint(1, 4))) + "]"
        if t.startswith("dict<"):
            vs2 = self.vars_de(esc, t)
            if vs2 and self.chance(0.6):
                return r.choice(vs2)
            ks = r.sample(["a", "b", "c", "d", "ação"], r.randint(1, 3))
            return "{" + ", ".join('{"%s", %s}' % (k, self.expr("i32", esc, 3)) for k in ks) + "}"
        if t in self.structs:
            vs2 = self.vars_de(esc, t)
            if vs2 and self.chance(0.6):
                return r.choice(vs2)
            campos = self.structs[t]
            if self.chance(0.5):
                return f"new {t}(" + ", ".join(self.expr(ct, esc, 3) for _, ct in campos) + ")"
            escolhidos = r.sample(campos, r.randint(0, len(campos)))
            return f"new {t}(" + ", ".join(f"{c}: {self.expr(ct, esc, 3)}" for c, ct in escolhidos) + ")"
        if t in self.classes:
            vs2 = self.vars_de(esc, t)
            if vs2 and self.chance(0.7):
                return r.choice(vs2)
            return self.novo_objeto(t, esc)
        if self.enum and t == self.enum[0]:
            vs2 = self.vars_de(esc, t)
            if vs2 and self.chance(0.5):
                return r.choice(vs2)
            return f"{t}.{r.choice(self.enum[1])}"
        raise ValueError(t)

    def expr_simples(self, esc):
        vs = [n for n, t in esc.todos().items() if t in NUMS + ("string", "bool")]
        return self.r.choice(vs) if vs else "1"

    def novo_objeto(self, t, esc):
        c = self.classes[t]
        return f"new {t}(" + ", ".join(self.expr(pt, esc, 3) for pt in c["ctor"]) + ")"

    def expr_derivada(self, t, esc, d):
        """Um valor de tipo t tirado de outra coisa: tamanho, campo, método, função"""
        r = self.r
        opcoes = []
        todos = esc.todos()
        if t == "i32":
            for n, tt in todos.items():
                if tt in ("string", "list<i32>", "list<string>", "dict<string, i32>"):
                    opcoes.append(f"{n}.size()")
                if tt == "list<i32>":
                    opcoes.append(f"{n}[{r.randint(0, 4)}]")
                if tt == "dict<string, i32>":
                    opcoes.append(f'{n}["{r.choice(["a", "b", "c", "z"])}"]')
        for n, tt in todos.items():
            if tt in self.structs:
                for c, ct in self.structs[tt]:
                    if ct == t:
                        opcoes.append(f"{n}.{c}")
            if tt in self.classes:
                for m, (ps, rt) in self.classes[tt]["metodos"].items():
                    if rt == t:
                        opcoes.append(f"{n}.{m}(" + ", ".join(self.expr(p, esc, 3) for p in ps) + ")")
        for nome, ps, rt in self.funcoes:
            if rt == t and d < 2:
                if nome == "rec":   # profundidade pequena (a recursão termina)
                    opcoes.append(f"rec({r.randint(0, 12)}, {self.expr('i64', esc, d + 2)})")
                    continue
                opcoes.append(f"{nome}(" + ", ".join(self.expr(p, esc, d + 2) for p in ps) + ")")
        if t in ("i32", "f64", "string") and d < 2 and self.chance(0.3):
            opcoes.append(f"maior[{t}]({self.expr(t, esc, d + 2)}, {self.expr(t, esc, d + 2)})")
        for n, tt in todos.items():   # Q2
            if tt == t + "?":
                opcoes.append(f"({n} else {self.expr(t, esc, 3)})")
            if tt.startswith("list<") and tt[5:-1] in self.structs:
                for c, ct in self.structs[tt[5:-1]]:
                    if ct == t:
                        opcoes.append(f"{n}[{r.randint(0, 3)}].{c}")
            if tt in self.genericas:
                if t == "i32":
                    opcoes.append(f"{n}.tamanho()")
                if t == self.genericas[tt]:
                    opcoes.append(f"({n}.ultimo() else {self.expr(t, esc, 3)})")
        if t == "i32" and self.chance(0.2):
            opcoes.append(f"cria_lista({self.r.randint(0, 5)}).conta()")
        return r.choice(opcoes) if opcoes else self.lit(t)

    # ── instruções ───────────────────────────────────────────────────────
    def bloco(self, esc, ind, n, ret=None, laco=False):
        linhas = []
        local = Escopo(esc)
        for _ in range(n):
            linhas += self.instr(local, ind, ret, laco)
        return linhas

    def instr(self, esc, ind, ret, laco):
        r = self.r
        p = "  " * ind
        k = r.randrange(19)
        todos = esc.todos()
        if k >= 16:   # Q2: as construções novas (uma em cada ~6 instruções)
            return self.instr_nova(esc, ind, ret, laco)
        if k <= 2:   # declaração
            t = self.tipo_qualquer()
            nome = self.novo()
            if t.startswith(("list", "dict")) and self.chance(0.3):
                linha = f"{p}{t} {nome};"
            else:
                # var com um inteiro pequeno infere i32: nada de var num i64
                primitivo = t in ("i32", "f32", "f64", "bool", "string")
                linha = f"{p}{'var' if primitivo and self.chance(0.15) else t} {nome} = {self.expr(t, esc)};"
            esc.vars[nome] = t
            return [linha]
        if k == 3:   # atribuição
            alvos = [n for n, t in todos.items() if n not in esc.protegidos and t not in self.classes]
            if not alvos:
                return [f"{p}print({self.expr('string', esc)});"]
            n = r.choice(alvos)
            t = todos[n]
            if t in NUMS + ("string",) and self.chance(0.5):
                opc = ["+="] if t == "string" else ["+=", "-=", "*=", "/="] + (["%="] if t in ("i32", "i64") else [])
                o = r.choice(NUMS) if t == "string" else t
                return [f"{p}{n} {r.choice(opc)} {self.expr(o, esc)};"]
            return [f"{p}{n} = {self.expr(t, esc)};"]
        if k in (4, 5):   # print
            itens = [self.expr(self.tipo_qualquer(), esc) for _ in range(r.randint(1, 3))]
            return [f"{p}print({', '.join(itens)});"]
        if k == 6:   # coleções e campos
            opc = []
            for n, t in todos.items():
                if t == "list<i32>":
                    opc += [f"{n}.add({self.expr('i32', esc)});", f"{n}[{r.randint(0, 3)}] = {self.expr('i32', esc)};",
                            f"{n}[{r.randint(0, 3)}] += {self.expr('i32', esc)};"]
                if t == "list<string>":
                    opc.append(f"{n}.add({self.expr('string', esc)});")
                if t == "dict<string, i32>":
                    k2 = r.choice(["a", "b", "c", "z"])
                    opc += [f'{n}.add({{"{k2}", {self.expr("i32", esc)}}});', f'{n}["{k2}"] = {self.expr("i32", esc)};']
                if t in self.structs and n not in esc.protegidos:
                    for c, ct in self.structs[t]:
                        opc.append(f"{n}.{c} = {self.expr(ct, esc)};")
                if t in self.classes:
                    for m, (ps, rt) in self.classes[t]["metodos"].items():
                        if rt == "void":
                            opc.append(f"{n}.{m}(" + ", ".join(self.expr(pt, esc) for pt in ps) + ");")
            if not opc:
                return [f"{p}print({self.expr('i32', esc)});"]
            return [p + r.choice(opc)]
        if k == 7 and ind < 5:   # if
            linhas = [f"{p}if ({self.expr('bool', esc)}) {{"] + self.bloco(esc, ind + 1, r.randint(1, 3), ret, laco)
            if self.chance(0.4):
                linhas += [f"{p}}} else {{"] + self.bloco(esc, ind + 1, r.randint(1, 2), ret, laco)
            return linhas + [f"{p}}}"]
        if k == 8 and ind < 5 and self.profundidade_laco < 2:   # for range
            i = self.novo("i")
            a, b = r.randint(-3, 3), r.randint(0, 6)
            passo = r.choice(["", ", 1", ", 2", ", -1", ", -2"])
            if "-" in passo:
                a, b = b, a
            self.profundidade_laco += 1
            interno = Escopo(esc)
            interno.vars[i] = "i32"
            interno.protegidos.add(i)
            corpo = self.bloco(interno, ind + 1, r.randint(1, 3), ret, True)
            self.profundidade_laco -= 1
            return [f"{p}for (i32 {i} in range({a}, {b}{passo})) {{"] + corpo + [f"{p}}}"]
        if k == 9 and ind < 5 and self.profundidade_laco < 2:   # for numa coleção
            cols = [n for n, t in todos.items() if t in ("list<i32>", "list<string>", "string", "dict<string, i32>")]
            if not cols:
                return [f"{p}print({self.expr('bool', esc)});"]
            c = r.choice(cols)
            tc = todos[c]
            te = {"list<i32>": "i32", "list<string>": "string", "string": "string",
                  "dict<string, i32>": "pair<string, i32>"}[tc]
            x = self.novo("x")
            self.profundidade_laco += 1
            interno = Escopo(esc)
            interno.vars[x] = te if te != "pair<string, i32>" else "par"
            interno.protegidos.add(x)
            corpo = self.bloco(interno, ind + 1, r.randint(1, 3), ret, True)
            if te == "pair<string, i32>":
                corpo = [f"{p}  print({x}.first, {x}.second);"] + corpo
            self.profundidade_laco -= 1
            return [f"{p}for ({te} {x} in {c}) {{"] + corpo + [f"{p}}}"]
        if k == 10 and ind < 5 and self.profundidade_laco < 2:   # while com contador
            c = self.novo("c")
            self.profundidade_laco += 1
            interno = Escopo(esc)
            interno.vars[c] = "i32"
            interno.protegidos.add(c)
            corpo = [f"{p}  {c} += 1;"] + self.bloco(interno, ind + 1, r.randint(1, 3), ret, True)
            self.profundidade_laco -= 1
            return [f"{p}i32 {c} = 0;", f"{p}while ({c} < {r.randint(1, 5)}) {{"] + corpo + [f"{p}}}"]
        if k == 11 and ind < 5:   # try
            e = self.novo("e")
            linhas = [f"{p}try {{"] + self.bloco(esc, ind + 1, r.randint(1, 4), ret, laco)
            tipos = r.sample(["ValueError", "IndexError", "KeyError", "ZeroDivisionError", "OverflowError"],
                             r.randint(0, 2)) + (["Error"] if self.chance(0.8) else [])
            for tp in tipos:
                linhas += [f"{p}}} except ({tp} {e}) {{", f'{p}  print("pegou", {e}.kind, {e}.message, {e}.line);']
                e = self.novo("e")
            if not tipos or self.chance(0.3):
                linhas += [f"{p}}} finally {{"] + [f'{p}  print("finally");']
            return linhas + [f"{p}}}"]
        if k == 12:   # saídas, sempre dentro de um if (código depois de uma saída é erro)
            if laco and self.chance(0.5):
                return [f"{p}if ({self.expr('bool', esc)}) {{", f"{p}  {r.choice(['break', 'continue'])};", f"{p}}}"]
            if ret is not None and self.chance(0.5):
                v = "" if ret == "void" else " " + self.expr(ret, esc)
                return [f"{p}if ({self.expr('bool', esc)}) {{", f"{p}  return{v};", f"{p}}}"]
            if self.chance(0.4):
                tp = r.choice(["ValueError", "Falha", "IndexError"])
                return [f"{p}if ({self.expr('bool', esc)}) {{", f'{p}  throw {tp}("lançado " + {self.expr("string", esc)});', f"{p}}}"]
            return [f"{p}print({self.expr('f64', esc)});"]
        if k == 13:   # cópia de struct: mudar a cópia não muda o original
            ss = [n for n, t in todos.items() if t in self.structs]
            if not ss:
                return [f"{p}print({self.expr('string', esc)});"]
            s = r.choice(ss)
            cp = self.novo("cp")
            c, ct = r.choice(self.structs[todos[s]])
            esc.vars[cp] = todos[s]
            return [f"{p}{todos[s]} {cp} = {s};", f"{p}{cp}.{c} = {self.expr(ct, esc)};", f"{p}print({s}, {cp});"]
        if k == 14:   # chamada de função como instrução
            fs = [f for f in self.funcoes]
            if not fs:
                return [f"{p}print({self.expr('i64', esc)});"]
            nome, ps, rt = r.choice(fs)
            return [f"{p}print({nome}(" + ", ".join(self.expr(pt, esc, 2) for pt in ps) + "));"
                    if rt != "void" else f"{p}{nome}(" + ", ".join(self.expr(pt, esc, 2) for pt in ps) + ");"]
        return [f'{p}printf("{{{self.expr_simples(esc)}}} {{{self.expr_simples(esc)}:6}}");']

    def instr_nova(self, esc, ind, ret, laco):
        """Q2: o opcional, a classe genérica, listas de structs, o dict com remoções"""
        r = self.r
        p = "  " * ind
        todos = esc.todos()
        opc = []
        for n, t in todos.items():
            if t.endswith("?") and ind < 5:
                base = t[:-1]
                x = self.novo("x")
                interno = Escopo(esc)
                interno.vars[x] = base
                interno.protegidos.add(x)
                corpo = self.bloco(interno, ind + 1, r.randint(1, 2), ret, laco)
                opc.append([f"{p}if ({base} {x} = {n}) {{", f"{p}  print(\"tem\", {x});"] + corpo +
                           [f"{p}}} else {{", f"{p}  print(\"null\", {n} == null);", f"{p}}}"])
                if n not in esc.protegidos:
                    opc.append([f"{p}{n} = {self.expr(t, esc)};"])
            if t in self.genericas:
                el = self.genericas[t]
                opc.append([f"{p}{n}.poe({self.expr(el, esc)});", f"{p}print({n}.tamanho(), {n}.ultimo());"])
            if t.startswith("list<") and t[5:-1] in self.structs:
                s = t[5:-1]
                c, ct = r.choice(self.structs[s])
                opc.append([f"{p}{n}.add({self.expr(s, esc)});"])
                grava = [f"{p}{n}[{r.randint(0, 3)}].{c} = {self.expr(ct, esc)};", f"{p}print({n});"]
                opc += [grava, grava]   # o caminho do lugar (lista, depois campo): peso dobrado
                if ind < 5 and self.profundidade_laco < 2:
                    x = self.novo("x")
                    opc.append([f"{p}for ({s} {x} in {n}) {{", f"{p}  {x}.{c} = {self.expr(ct, esc)};",
                                f"{p}  print({x}.{c});", f"{p}}}", f"{p}print({n});"])
            if t == "dict<string, i32>":
                k1 = r.choice(["a", "b", "c", "z"])
                opc.append([f'{p}if ({n}.has("{k1}")) {{', f'{p}  {n}.remove("{k1}");', f"{p}}}",
                            f'{p}{n}.add({{"{k1}", {self.expr("i32", esc)}}});', f"{p}print({n}, {n}.keys());"])
        if not opc:
            return [f"{p}print({self.expr(self.tipo_qualquer(), esc)});"]
        return r.choice(opc)

    # ── declarações ──────────────────────────────────────────────────────
    def gera(self):
        r = self.r
        out = ["import Math;", "import Strings;", "import Dicts;", "", "error Falha;", ""]
        self.enum = ("Cor", ["Vermelho", "Verde", "Azul"])
        out += ["enum Cor { Vermelho, Verde, Azul }", ""]
        for v in INT_BORDAS["i64"]:   # os i64 que não cabem num literal solto (i32)
            if v > 2147483647:
                nome = f"G{len(self.grandes) + 1}"
                self.grandes[v] = nome
                out.append(f"const i64 {nome} = {v};")
        for _ in range(r.randint(1, 2)):   # const
            t = r.choice(["i32", "i64", "f64", "string"])
            nome = f"K{len(self.consts) + 1}"
            out.append(f"const {t} {nome} = {self.lit(t)};")
            self.consts[nome] = t
        out.append("")
        for k in range(r.randint(1, 2)):   # structs
            nome = f"S{k + 1}"
            campos = [(f"c{j}", self.tipo_simples()) for j in range(r.randint(1, 3))]
            out += [f"struct {nome} {{"] + [f"  {t} {c} = {self.lit(t)};" for c, t in campos] + ["}", ""]
            self.structs[nome] = campos
        out += ["interface Medida {", "  fn medida() -> i64;", "}", ""]
        for k in range(r.randint(1, 2)):   # classes
            out += self.classe(f"C{k + 1}")
        out += ["fn maior[T](T a, T b) -> T {", "  if (a > b) {", "    return a;", "  }", "  return b;", "}", ""]
        # Q2: uma classe genérica (o T? devolvido) e uma classe com um campo opcional do próprio
        # tipo (os nós em ciclo: a coleta de ciclos precisa liberá-los)
        out += ["class Caixa[T] {", "  list<T> itens;", "  pub {", "    fn poe(T x) { itens.add(x); }",
                "    fn tamanho() -> i32 { return itens.size(); }",
                "    fn ultimo() -> T? {", "      if (itens.size() == 0) { return null; }",
                "      return itens[itens.size() - 1];", "    }", "  }", "}", "",
                "class No {", "  i32 v = 0;", "  No? prox;", "  pub {", "    No(i32 x) { v = x; }",
                "    fn liga(No n) { prox = n; }",
                "    fn conta() -> i32 {", "      i32 n = 1;", "      No? a = prox;",
                "      while (No b = a) {", "        if (b.v == v) { return n; }", "        n += 1;",
                "        a = b.prox;", "        if (n > 50) { return n; }", "      }", "      return n;", "    }",
                "  }", "}", "",
                "fn cria_lista(i32 n) -> No {", "  No primeiro = new No(0);", "  No atual = primeiro;",
                "  for (i32 i in range(1, n + 1)) {", "    No novo = new No(i);", "    atual.liga(novo);",
                "    atual = novo;", "  }", "  atual.liga(primeiro);", "  return primeiro;", "}", ""]
        for g in ("i32", "string"):
            self.genericas[f"Caixa[{g}]"] = g
        # recursão com profundidade limitada
        out += ["fn rec(i32 n, i64 acc) -> i64 {", "  if (n <= 0) {", "    return acc;", "  }",
                f"  return rec(n - 1, acc * {r.randint(1, 9)} + cast<i64>(n));", "}", ""]
        self.funcoes.append(("rec", ["i32", "i64"], "i64"))
        for _ in range(r.randint(2, 4)):
            out += self.funcao()
        esc = Escopo()
        # a main em trechos; a maioria num try que imprime o erro e segue (senão o primeiro
        # estouro encerra o programa), alguns soltos (o relato de erro não pego também conta)
        corpo = []
        for _ in range(r.randint(3, 7)):
            if self.chance(0.7):
                e = self.novo("e")
                corpo += ["  try {"] + self.bloco(esc, 2, r.randint(2, 5)) +                          [f"  }} except (Error {e}) {{", f'    print("erro", {e}.kind, {e}.message, {e}.line);', "  }"]
            else:
                corpo += self.bloco(esc, 1, r.randint(1, 3))
        # usa a interface: uma lista com objetos das classes
        objs = [self.novo_objeto(c, esc) for c in self.classes]
        corpo += [f"  list<Medida> ms = [{', '.join(objs)}];", "  i64 total = 0;",
                  "  for (Medida m in ms) {", "    total += m.medida();", "  }", "  print(\"total\", total);"]
        out += ["fn main() {"] + corpo + ["}"]
        return "\n".join(out) + "\n"

    def funcao(self):
        r = self.r
        nome = self.novo("f")
        ps = [(self.novo("p"), self.tipo_qualquer()) for _ in range(r.randint(0, 3))]
        rt = r.choice(["void", "i32", "i64", "f64", "string", "bool"])
        esc = Escopo()
        for pn, pt in ps:
            esc.vars[pn] = pt
        corpo = self.bloco(esc, 1, r.randint(2, 6), ret=rt)
        if rt != "void":
            corpo.append(f"  return {self.expr(rt, esc)};")
        self.funcoes.append((nome, [pt for _, pt in ps], rt))
        return [f"fn {nome}(" + ", ".join(f"{pt} {pn}" for pn, pt in ps) + f") -> {rt} {{"] + corpo + ["}", ""]

    def classe(self, nome):
        r = self.r
        campos = [(f"a{j}", r.choice(["i32", "i64", "f64", "string", "bool"])) for j in range(r.randint(1, 3))]
        ctor = [campos[0][1]]
        info = {"ctor": ctor, "metodos": {}}
        esc = Escopo()
        for c, t in campos:
            esc.vars[c] = t
        esc.vars["hist"] = "list<i32>"
        out = [f"class {nome} : Medida {{"] + [f"  {t} {c} = {self.lit(t)};" for c, t in campos] + ["  list<i32> hist;", "  pub {"]
        out += [f"    {nome}({ctor[0]} v) {{", f"      {campos[0][0]} = v;", "      hist.add(1);", "    }"]
        # a medida da interface
        num = [c for c, t in campos if t in NUMS]
        m = f"cast<i64>({num[0]})" if num and campos[0][1] != "f64" else "cast<i64>(hist.size())"
        out += ["    fn medida() -> i64 {", f"      return {m} + cast<i64>(hist.size());", "    }"]
        info["metodos"]["medida"] = ([], "i64")
        for k in range(r.randint(1, 3)):
            mn = f"m{k}"
            rt = r.choice(["void", "i32", "i64", "string", "bool"])
            ps = [(self.novo("q"), r.choice(["i32", "string"])) for _ in range(r.randint(0, 2))]
            interno = Escopo(esc)
            for pn, pt in ps:
                interno.vars[pn] = pt
            corpo = self.bloco(interno, 3, r.randint(1, 3), ret=rt)
            if rt != "void":
                corpo.append(f"      return {self.expr(rt, interno)};")
            out += [f"    fn {mn}(" + ", ".join(f"{pt} {pn}" for pn, pt in ps) + f") -> {rt} {{"] + corpo + ["    }"]
            info["metodos"][mn] = ([pt for _, pt in ps], rt)
        out += ["  }", "}", ""]
        self.classes[nome] = info
        return out


# ── o teste diferencial ──────────────────────────────────────────────────
class Diferencial:
    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.trabalho = Path(tempfile.mkdtemp(prefix="cinza_gera_"))
        self.saida = Path(args.saida)
        self.saida.mkdir(parents=True, exist_ok=True)
        self.c = {"programas": 0, "invalidos": 0, "nao_terminam": 0, "comparados": 0, "defeitos": 0,
                  "com_erro": 0}
        self.vistos = set()
        self.invalidos = {}

    def roda(self, cmd, tempo):
        env = dict(os.environ, CINZA_VAZAMENTOS="1")
        try:
            p = subprocess.run(cmd, cwd=self.trabalho, capture_output=True, timeout=tempo,
                               stdin=subprocess.DEVNULL, env=env)
            return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")
        except subprocess.TimeoutExpired:
            return None, "", ""

    def testa(self, fonte, n):
        """[(assinatura, detalhe)] dos defeitos; 'invalido'/'nao_termina' para o gerador"""
        arq = self.trabalho / f"g{n}.cinza"
        arq.write_text(fonte, encoding="utf-8")
        rc, out, err = self.roda([str(CINZA), arq.name], self.args.tempo_programa)
        if rc is None:
            return "nao_termina", []
        if re.search(r"^\S+:\d+:\d+: (SemanticError|SyntaxError|LexicalError)", err, re.M):
            return "invalido", [err.strip().splitlines()[0]]
        defeitos = []
        if rc < 0 or rc > 255 or "Erro interno" in out + err:
            defeitos.append(("cinza caiu", f"{rc}\n{err[-2000:]}"))
        if rc == 70:
            defeitos.append(("cinza vazou", err[-500:]))
        for opt in ("-O0", "-O2"):
            exe = self.trabalho / f"g{n}{opt}{EXE}"
            rcc, _, errc = self.roda([str(CINZAC), opt, arq.name, "-o", exe.name], 120)
            if rcc != 0:
                defeitos.append((f"cinzac {opt} não compilou", f"{rcc}\n{errc[-2000:]}"))
                continue
            rn, outn, errn = self.roda([str(exe)], self.args.tempo_programa * 3)
            try:
                exe.unlink()
            except OSError:
                pass
            if (rn, outn, errn) != (rc, out, err):
                defeitos.append((f"nativo {opt} difere da CVM",
                                 f"cvm: rc={rc}\n{out[-1500:]}\n{err[-800:]}\n--- nativo: rc={rn}\n{outn[-1500:]}\n{errn[-800:]}"))
        return ("com_erro" if rc != 0 else "ok"), defeitos

    def um(self, n):
        semente = f"{self.args.semente}:{n}"
        fonte = Programa(random.Random(semente)).gera()
        estado, defeitos = self.testa(fonte, n)
        try:
            (self.trabalho / f"g{n}.cinza").unlink()
        except OSError:
            pass
        with self.lock:
            self.c["programas"] += 1
            if estado == "invalido":
                self.c["invalidos"] += 1
                msg = re.sub(r"^\S+:\d+:\d+: ", "", defeitos[0])
                chave = re.sub(r"'[^']*'", "'_'", msg)[:90]
                if chave not in self.invalidos:
                    self.invalidos[chave] = semente
                return
            if estado == "nao_termina":
                self.c["nao_terminam"] += 1
                return
            self.c["comparados"] += 1
            if estado == "com_erro":
                self.c["com_erro"] += 1
            novos = [(a, d) for a, d in defeitos if a not in self.vistos]
            for a, _ in novos:
                self.vistos.add(a)
            self.c["defeitos"] += len(novos)
        for a, d in novos:
            h = hashlib.sha1((a + semente).encode()).hexdigest()[:8]
            base = self.saida / f"defeito_{h}"
            base.with_suffix(".cinza").write_text(fonte, encoding="utf-8")
            base.with_suffix(".txt").write_text(f"{a}\nsemente: {semente}\n\n{d}\n", encoding="utf-8")
            print(f"DEFEITO  {a}  (semente {semente})\n         {base.with_suffix('.cinza')}", flush=True)

    def roda_tudo(self):
        print(f"{self.args.trabalhos} trabalhos; {self.args.tempo} s; semente {self.args.semente}; "
              f"achados em {self.saida}", flush=True)
        fim = time.time() + self.args.tempo
        n = 0
        with ThreadPoolExecutor(self.args.trabalhos) as ex:
            pend = []
            while time.time() < fim:
                while len(pend) < self.args.trabalhos * 2:
                    pend.append(ex.submit(self.um, n))
                    n += 1
                pend.pop(0).result()
            for f in pend:
                f.result()
        shutil.rmtree(self.trabalho, ignore_errors=True)
        c = self.c
        print(f"{c['programas']} programas: {c['comparados']} comparados nos três ({c['com_erro']} terminam com "
              f"erro), {c['invalidos']} inválidos e {c['nao_terminam']} sem terminar (defeitos do gerador); "
              f"{c['defeitos']} defeitos", flush=True)
        if self.invalidos:
            print("programas inválidos (o gerador precisa de ajuste):")
            for msg, s in sorted(self.invalidos.items())[:30]:
                print(f"  {msg}   [semente {s}]")
        return 1 if c["defeitos"] else 0


def main():
    ap = argparse.ArgumentParser(description="Gerador de programas Cinza válidos (teste diferencial)")
    ap.add_argument("--tempo", type=float, default=300)
    ap.add_argument("--semente", type=int, default=int(time.time()))
    ap.add_argument("--trabalhos", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap.add_argument("--tempo-programa", type=float, default=10)
    ap.add_argument("--saida", default=str(Path(tempfile.gettempdir()) / "cinza_gera_achados"))
    ap.add_argument("--mostra", help="imprime o programa da semente 'S:N' e sai")
    args = ap.parse_args()
    if args.mostra:
        sys.stdout.reconfigure(encoding="utf-8")
        print(Programa(random.Random(args.mostra)).gera())
        return 0
    if not CINZA.exists() or not CINZAC.exists():
        print("faltam o cinza e o cinzac: make e make cinzac")
        return 2
    return Diferencial(args).roda_tudo()


if __name__ == "__main__":
    sys.exit(main())
