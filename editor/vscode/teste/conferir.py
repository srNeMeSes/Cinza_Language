#!/usr/bin/env python3
"""Teste da gramática da extensão do VS Code (editor/vscode).

Tokeniza os .cinza de tests/ e exemplos/ com o motor de cores do próprio VS Code (varre.js,
rodado pelo Node que vem dentro dele) e compara os trechos marcados como inválidos com
invalidos.esperado: cada um é um erro léxico de propósito num teste (identificador com
acento, número sem a precisão, escape desconhecido...). Uma diferença é um falso positivo
novo (algo válido ficou vermelho) ou uma marca perdida.

Uso: python editor/vscode/teste/conferir.py [--atualizar]
     (--atualizar grava a saída atual como a esperada, depois de conferida à mão)
O VS Code é procurado em VSCODE (o Code.exe), senão no lugar padrão da instalação.
"""

import os
import subprocess
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent
RAIZ = AQUI.parents[2]
ESPERADO = AQUI / "invalidos.esperado"


def acha_vscode():
    candidatos = []
    if os.environ.get("VSCODE"):
        candidatos.append(Path(os.environ["VSCODE"]))
    local = os.environ.get("LOCALAPPDATA")
    if local:
        candidatos.append(Path(local) / "Programs" / "Microsoft VS Code" / "Code.exe")
    candidatos.append(Path(r"C:\Program Files\Microsoft VS Code\Code.exe"))
    for exe in candidatos:
        if not exe.is_file():
            continue
        # resources/app fica ao lado do Code.exe, ou numa subpasta com o hash da versão
        for base in [exe.parent] + sorted(p for p in exe.parent.iterdir() if p.is_dir()):
            app = base / "resources" / "app"
            if (app / "node_modules.asar").is_file():
                return exe, app
    return None, None


def main():
    exe, app = acha_vscode()
    if not exe:
        print("VS Code não encontrado (defina VSCODE com o caminho do Code.exe): teste não rodou")
        return 1
    env = dict(os.environ, ELECTRON_RUN_AS_NODE="1")
    proc = subprocess.run([str(exe), str(AQUI / "varre.js"), str(app), str(RAIZ)],
                          capture_output=True, env=env, timeout=600)
    if proc.returncode != 0:
        print(proc.stderr.decode("utf-8", "replace"))
        return 1
    obtido = [l for l in proc.stdout.decode("utf-8").splitlines() if l]
    if "--atualizar" in sys.argv:
        ESPERADO.write_text("\n".join(obtido) + "\n", encoding="utf-8", newline="\n")
        print(f"{ESPERADO.relative_to(RAIZ)} atualizado: {len(obtido)} trechos")
        return 0
    esperado = [l for l in ESPERADO.read_text(encoding="utf-8").splitlines() if l]
    novos = [l for l in obtido if l not in esperado]
    perdidos = [l for l in esperado if l not in obtido]
    for l in novos:
        print("NOVO (falso positivo?)  " + l.replace("\t", "  "))
    for l in perdidos:
        print("PERDIDO                 " + l.replace("\t", "  "))
    print(proc.stderr.decode("utf-8", "replace").strip())
    if novos or perdidos:
        print("a gramática mudou o que marca como inválido")
        return 1
    print("gramática: ok (os trechos inválidos são os esperados)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
