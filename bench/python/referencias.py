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


# os sete de antes do Q11
class NoCiclo:
    __slots__ = ("vizinhos",)
    def __init__(self): self.vizinhos = []

def ciclos():
    for _ in range(300000):
        a, b = NoCiclo(), NoCiclo()
        a.vizinhos.append(b)
        b.vizinhos.append(a)
    print("ok")


def fib_(n):
    return n if n < 2 else fib_(n - 1) + fib_(n - 2)

def fib():
    print(fib_(27))


def laco():
    soma = 0
    i = 0
    while i < 2000000:
        soma += i * i % 7
        i += 1
    print(soma)


class Ponto:
    __slots__ = ("x", "y")
    def __init__(self, a, b): self.x, self.y = a, b
    def get_x(self): return self.x
    def get_y(self): return self.y
    def move(self, d): self.x = self.x + d

def metodos():
    p = Ponto(1, 2)
    t = 0
    for _ in range(3000000):
        t += p.get_x() + p.get_y()
        p.move(1)
    print(t)


class Contador:
    __slots__ = ("total", "passos")
    def __init__(self): self.total = self.passos = 0
    def soma(self, v):
        self.total += v
        self.passos += 1
    def valor(self): return self.total + self.passos

def objetos():
    c = Contador()
    for i in range(500000):
        c.soma(i % 10)
    print(c.valor())


def strings():
    def junta(a, b): return a + b
    def tam(s): return len(s)
    t = 0
    base = "abc"
    for _ in range(3000000):
        t += tam(base) + tam(junta(base, "x"))
    print(t)


def structs():
    # o struct da Cinza é valor: aqui, tuplas (imutáveis, a cópia não é observável)
    def soma(a, b): return (a[0] + b[0], a[1] + b[1])
    def comprimento2(a): return a[0] * a[0] + a[1] * a[1]
    acc = (0.0, 0.0)
    passo = (1.0, 0.5)
    t = 0.0
    for _ in range(1000000):
        acc = soma(acc, passo)
        t += comprimento2(passo)
    print(acc, t)


TODOS = [listas, dicts, arvores, corpos, texto, structs_lista, ciclos, fib, laco, metodos, objetos, strings, structs]

if __name__ == "__main__":
    sys.setrecursionlimit(10000)
    escolhidos = [f for f in TODOS if len(sys.argv) < 2 or f.__name__ == sys.argv[1]]
    for f in escolhidos:
        t = time.perf_counter()
        f()
        print(f"  {f.__name__}: {(time.perf_counter() - t) * 1000:.0f} ms", file=sys.stderr)
