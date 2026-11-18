"""P2 (llvm/DESENHO.md 6.3): o orçamento de quadro que a CIR vai calcular cobre o quadro
que o LLVM gera de verdade?

Gera funções C que imitam o que o `cinzac` vai emitir para funções da Cinza (locais
escalares, referências, structs por valor, structs passados por valor a chamadas,
temporários de expressões longas), compila com o clang 20.1.8 do MSYS2 CLANG64 — o mesmo
LLVM que o `cinzac` vai usar — em -O0 e -O2 com -fstack-usage, e compara o quadro real com
o orçamento da fórmula do desenho:

    orçamento = (soma dos slots + cópias de struct passadas por valor) × 2 + 4 KB

Uso (na raiz do projeto): python llvm/prototipos/pilha/quadros.py
"""
import pathlib, subprocess, sys, tempfile

CLANG = r"C:\msys64\clang64\bin\clang.exe"


def struct_c(nome, campos):
    linhas = [f"typedef struct {{ long long c[{campos}]; }} {nome};"]
    return "\n".join(linhas)


# Cada caso: (nome, código C da função, slots em bytes, cópias por valor em bytes)
def casos():
    c = []
    # A: função comum — 6 locais i64, laço e chamadas
    corpo = """long long a_simples(long long n) {
  long long s = 0, i = 0, q = 0, r = 0, t = 0, u = 0;
  for (i = 0; i < n; i++) { q = i * i % 7; s += q; r = ext_i64(s); t += r; u = t - s; }
  return s + u;
}"""
    c.append(("A comum (6 i64)", corpo, 6 * 8, 0))

    # B: 12 referências (string/list/objeto) e temporários de 8 chamadas encadeadas
    corpo = """void* b_referencias(void* x) {
  void *a = x, *b = x, *c = x, *d = x, *e = x, *f = x, *g = x, *h = x, *i = x, *j = x, *k = x, *l = x;
  void* t = ext_ptr(ext_ptr(ext_ptr(ext_ptr(ext_ptr(ext_ptr(ext_ptr(ext_ptr(a))))))));
  ext_ptr(b); ext_ptr(c); ext_ptr(d); ext_ptr(e); ext_ptr(f); ext_ptr(g); ext_ptr(h);
  ext_ptr(i); ext_ptr(j); ext_ptr(k); ext_ptr(l);
  return t;
}"""
    c.append(("B 12 refs + 8 temporários", corpo, (12 + 8 + 1) * 8, 0))

    # C: struct de 20 campos (160 B), 4 locais, 6 chamadas que o recebem por valor
    corpo = """long long c_struct_medio(long long n) {
  S20 a = {{n}}, b = {{n + 1}}, c = {{n + 2}}, d = {{n + 3}};
  return ext_s20(a) + ext_s20(b) + ext_s20(c) + ext_s20(d) + ext_s20(a) + ext_s20(b);
}"""
    c.append(("C 4 structs de 160 B, 6 por valor", corpo, 4 * 160, 6 * 160))

    # D: o extremo — struct de 1000 campos (8 KB), 10 locais, 20 chamadas por valor
    locs = ", ".join(f"v{i} = {{{{n + {i}}}}}" for i in range(10))
    chamadas = " + ".join(f"ext_s1000(v{i % 10})" for i in range(20))
    corpo = f"""long long d_extremo(long long n) {{
  S1000 {locs};
  return {chamadas};
}}"""
    c.append(("D 10 structs de 8 KB, 20 por valor", corpo, 10 * 8000, 20 * 8000))

    # E: expressão longa — 200 chamadas, cada resultado um temporário
    soma = " + ".join(f"ext_i64(n + {i})" for i in range(200))
    corpo = f"""long long e_expressao(long long n) {{
  return {soma};
}}"""
    c.append(("E 200 temporários i64", corpo, 200 * 8, 0))

    # F: struct pequeno (CzValor-like, 16 B) em 50 locais
    locs = ", ".join(f"w{i} = {{{{n + {i}}}}}" for i in range(50))
    usos = " + ".join(f"ext_s2(w{i})" for i in range(50))
    corpo = f"""long long f_muitos_pequenos(long long n) {{
  S2 {locs};
  return {usos};
}}"""
    c.append(("F 50 structs de 16 B por valor", corpo, 50 * 16, 50 * 16))
    return c


CABECALHO = "\n".join([
    struct_c("S2", 2), struct_c("S20", 20), struct_c("S1000", 1000),
    "long long ext_i64(long long);",
    "void* ext_ptr(void*);",
    "long long ext_s2(S2);",
    "long long ext_s20(S20);",
    "long long ext_s1000(S1000);",
])


def mede(codigo, opt):
    with tempfile.TemporaryDirectory() as d:
        src = pathlib.Path(d) / "f.c"
        src.write_text(CABECALHO + "\n" + codigo + "\n", encoding="utf-8")
        r = subprocess.run([CLANG, opt, "-fstack-usage", "-c", str(src), "-o", str(pathlib.Path(d) / "f.o")],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(r.stderr)
        su = (pathlib.Path(d) / "f.su").read_text().split()
        return int(su[-2])


def main():
    print(f"{'caso':38} {'-O0':>8} {'-O2':>8} {'orçamento':>10}  folga mínima")
    ok = True
    for nome, codigo, slots, copias in casos():
        o0, o2 = mede(codigo, "-O0"), mede(codigo, "-O2")
        orc = (slots + copias) * 2 + 4096
        folga = orc / max(o0, o2)
        ok = ok and orc >= max(o0, o2)
        print(f"{nome:38} {o0:8} {o2:8} {orc:10}  {folga:5.2f}x")
    print("orçamento cobre todos os quadros" if ok else "ORÇAMENTO INSUFICIENTE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
