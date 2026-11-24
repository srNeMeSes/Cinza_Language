# Referência do bench/ (Q11): os mesmos programas em Python, para comparar a ordem de grandeza.
# Uso: python bench/python/referencias.py [nome]   (sem nome: todos, com o tempo de cada um)
import math
import sys
import time


def listas():
    l = []
    for i in range(2000000):
        l.append((i * 7919) % 100003)
    soma = 0
    for i in range(len(l)):
        soma += l[i]
    for x in l:
        soma += x
    for i in range(len(l)):
        l[i] = l[i] + 1
    l.sort()
    print(soma, l[0], l[-1])


def dicts():
    conta = {}
    for i in range(1000000):
        k = (i * 31) % 5000
        if k in conta:
            conta[k] = conta[k] + 1
        else:
            conta[k] = 1
    nomes = {}
    for i in range(300000):
        k = "n" + str(i % 2000)
        if k in nomes:
            nomes[k] = nomes[k] + 1
        else:
            nomes[k] = 1
    print(len(conta), conta[0], len(nomes), nomes["n7"])


class No:
    __slots__ = ("esq", "dir")

    def __init__(self):
        self.esq = None
        self.dir = None

    def conta(self):
        if self.esq is not None and self.dir is not None:
            return 1 + self.esq.conta() + self.dir.conta()
        return 1


def cria(prof):
    n = No()
    if prof > 0:
        n.esq = cria(prof - 1)
        n.dir = cria(prof - 1)
    return n


def arvores():
    total = 0
    for _ in range(20):
        total += cria(15).conta()
    longa = cria(18)
    print(total, longa.conta())


class Corpo:
    __slots__ = ("x", "y", "vx", "vy", "m")

    def __init__(self, x, y, m):
        self.x, self.y, self.vx, self.vy, self.m = x, y, 0.0, 0.0, m

    def interage(self, b, dt):
        dx = self.x - b.x
        dy = self.y - b.y
        d2 = dx * dx + dy * dy + 0.01
        f = dt / (d2 * math.sqrt(d2))
        self.vx -= dx * b.m * f
        self.vy -= dy * b.m * f
        b.vx += dx * self.m * f
        b.vy += dy * self.m * f


def corpos():
    cs = [Corpo(float(i), float(i % 3), 1.0 + i * 0.1) for i in range(8)]
    for _ in range(100000):
        n = len(cs)
        for i in range(n):
            a = cs[i]
            for j in range(i + 1, n):
                a.interage(cs[j], 0.001)
        for c in cs:
            c.x += 0.001 * c.vx
            c.y += 0.001 * c.vy
    e = sum(c.m * (c.vx * c.vx + c.vy * c.vy) for c in cs)
    print(f"{e:.6f}")


def texto():
    partes = ["item" + str(i) for i in range(300000)]
    tudo = ",".join(partes)
    de_volta = tudo.split(",")
    tam = 0
    com_7 = 0
    for s in de_volta:
        tam += len(s)
        if "7" in s:
            com_7 += 1
    print(len(tudo), len(de_volta), tam, com_7)


def structs_lista():
    ps = [(float(i), float(i % 100)) for i in range(500000)]
    sx = sy = 0.0
    for _ in range(4):
        for x, y in ps:
            sx += x
            sy += y
        for i in range(len(ps)):
            ps[i] = (ps[i][0] + 1.0, ps[i][1])
    print(sx, sy)


TODOS = [listas, dicts, arvores, corpos, texto, structs_lista]

if __name__ == "__main__":
    sys.setrecursionlimit(10000)
    escolhidos = [f for f in TODOS if len(sys.argv) < 2 or f.__name__ == sys.argv[1]]
    for f in escolhidos:
        t = time.perf_counter()
        f()
        print(f"  {f.__name__}: {(time.perf_counter() - t) * 1000:.0f} ms", file=sys.stderr)
