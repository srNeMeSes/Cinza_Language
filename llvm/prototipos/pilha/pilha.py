"""P2 (llvm/DESENHO.md 6.3): confere a garantia da pilha física com o protótipo.

  1. quadros.py: o orçamento da fórmula cobre os quadros reais do LLVM;
  2. quanto um print do runtime (GCC) gasta no topo, e quanto custa cada nível de lista
     aninhada no toString (o risco à parte do desenho);
  3. 2000 níveis do quadro extremo, em -O0 e -O2, com a reserva de pilha calculada pela
     fórmula e a rede de segurança: termina normalmente;
  4. a mesma recursão com a reserva fixa de 64 MB: com a rede, para com erro interno
     claro; sem ela (o controle), estoura com 0xC00000FD.

O código "gerado" é C compilado pelo clang 20.1.8 do MSYS2 CLANG64 (o mesmo LLVM do
cinzac); o runtime é C++ compilado pelo g++ do projeto; a ligação é pelo g++, como o
cinzac vai fazer. Os executáveis ficam numa pasta temporária, fora do projeto.

Uso (na raiz do projeto): python llvm/prototipos/pilha/pilha.py
"""
import pathlib, subprocess, sys, tempfile

AQUI = pathlib.Path(__file__).resolve().parent
RAIZ = AQUI.parent.parent.parent
CLANG = r"C:\msys64\clang64\bin\clang.exe"
GXX = "g++"

ORCAMENTO_MAX = 484096        # caso D do quadros.py: (80000 + 160000) × 2 + 4096
NIVEIS = 2000                 # spec 5.8
GUARDA = 64 * 1024            # páginas de guarda do Windows no fim da pilha, com folga
RUNTIME_NA_MARGEM = 64 * 1024 # o runtime no topo, com folga (o print medido gasta ~4 KB)
FOLGA_RUNTIME = 8 << 20       # a reserva: 2000 × orçamento + margem + 8 MB


def roda(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", **kw)
    return r.returncode, (r.stdout + r.stderr).strip()


def compila(pasta):
    cod, saida = roda([GXX, "-O3", "-std=c++20", "-c", str(AQUI / "runtime_sim.cpp"),
                       "-o", str(pasta / "runtime_sim.o")])
    if cod: sys.exit("runtime_sim.cpp: " + saida)
    for opt in ("-O0", "-O2"):
        cod, saida = roda([CLANG, opt, "-c", str(AQUI / "recursao.c"), "-o", str(pasta / f"recursao{opt}.o")])
        if cod: sys.exit("recursao.c: " + saida)


def liga(pasta, opt, reserva):
    exe = pasta / f"recursao{opt}_{reserva // 1024}k.exe"
    cod, saida = roda([GXX, str(pasta / f"recursao{opt}.o"), str(pasta / "runtime_sim.o"), "-o", str(exe),
                       "-static-libstdc++", "-static-libgcc", f"-Wl,--stack,{reserva}"])
    if cod: sys.exit("ligação: " + saida)
    return exe


def executa(exe, *args):
    try:
        return roda([str(exe), *args], cwd=exe.parent, timeout=120)
    except subprocess.TimeoutExpired:
        return None, "tempo esgotado"


def main():
    print("== 1. orçamento x quadros reais do LLVM")
    cod, saida = roda([sys.executable, str(AQUI / "quadros.py")])
    print(saida)
    falhas = 0 if cod == 0 else 1

    with tempfile.TemporaryDirectory(prefix="cinza_pilha_") as d:
        pasta = pathlib.Path(d)
        compila(pasta)

        print("\n== 2. pilha usada pelo runtime (GCC -O3, value.h de verdade)")
        cod, saida = executa(liga(pasta, "-O0", 64 << 20), "medir")
        print(saida)
        print_bytes = int(next(l for l in saida.splitlines() if l.startswith("print:")).split()[1])

        if print_bytes > RUNTIME_NA_MARGEM:
            print(f"ERRADO: o print gastou {print_bytes} bytes, mais que os {RUNTIME_NA_MARGEM} da margem")
            falhas += 1
        margem = ORCAMENTO_MAX + RUNTIME_NA_MARGEM + GUARDA
        reserva = max(64 << 20, NIVEIS * ORCAMENTO_MAX + margem + FOLGA_RUNTIME)
        print(f"\nmargem da rede = orçamento máximo + 64 KB do runtime + 64 KB de guarda = {margem} bytes")
        print(f"reserva da fórmula = max(64 MB, {NIVEIS} × {ORCAMENTO_MAX} + margem + 8 MB) = {reserva // 1024} KB")

        print("\n== 3. 2000 níveis do quadro extremo com a reserva da fórmula e a rede")
        for opt in ("-O0", "-O2"):
            cod, saida = executa(liga(pasta, opt, reserva), "fundo", str(NIVEIS), str(margem))
            certo = cod == 0 and "ok: 2000" in saida
            falhas += 0 if certo else 1
            print(f"{opt}: código {cod} — {saida.splitlines()[-1]}  [{'certo' if certo else 'ERRADO'}]")

        print("\n== 4. a mesma recursão com a reserva fixa de 64 MB")
        exe = liga(pasta, "-O0", 64 << 20)
        cod, saida = executa(exe, "fundo", str(NIVEIS), str(margem))
        certo = cod == 3 and "pilha do sistema esgotada" in saida
        falhas += 0 if certo else 1
        print(f"com a rede: código {cod} — {saida.splitlines()[-1]}  [{'certo' if certo else 'ERRADO'}]")
        cod, saida = executa(exe, "fundo", str(NIVEIS), "0")
        estouro = cod is not None and (cod & 0xFFFFFFFF) == 0xC00000FD
        falhas += 0 if estouro else 1
        print(f"sem a rede (controle): código {cod if cod is None else hex(cod & 0xFFFFFFFF)}"
              f"  [{'estourou, como esperado' if estouro else 'ERRADO'}]")

    print("\n" + ("tudo conferido" if falhas == 0 else f"{falhas} conferência(s) falharam"))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
