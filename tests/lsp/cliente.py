"""Um cliente LSP mínimo para os testes do `cinza --lsp` (Q6), usado por tests/run_tests.py."""

import json
from urllib.parse import unquote
import subprocess
import threading
from pathlib import Path


class Cliente:
    def __init__(self, binario, pasta):
        self.p = subprocess.Popen([str(binario), "--lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, cwd=str(pasta))
        self.proximo = 1
        self.mensagens = []
        self.cond = threading.Condition()
        threading.Thread(target=self._le, daemon=True).start()

    def _le(self):
        f = self.p.stdout
        while True:
            tamanho = None
            while True:
                linha = f.readline()
                if not linha:
                    return
                linha = linha.strip()
                if not linha:
                    break
                if linha.lower().startswith(b"content-length:"):
                    tamanho = int(linha.split(b":")[1])
            corpo = f.read(tamanho)
            with self.cond:
                self.mensagens.append(json.loads(corpo.decode("utf-8")))
                self.cond.notify_all()

    def _envia(self, m):
        corpo = json.dumps(m, ensure_ascii=False).encode("utf-8")
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(corpo) + corpo)
        self.p.stdin.flush()

    def pede(self, metodo, params, espera=10):
        i = self.proximo
        self.proximo += 1
        self._envia({"jsonrpc": "2.0", "id": i, "method": metodo, "params": params})
        return self._espera(lambda m: m.get("id") == i, espera)

    def avisa(self, metodo, params):
        self._envia({"jsonrpc": "2.0", "method": metodo, "params": params})

    def _espera(self, cond, espera):
        with self.cond:
            ok = self.cond.wait_for(lambda: any(cond(m) for m in self.mensagens), timeout=espera)
            if not ok:
                return None
            for k, m in enumerate(self.mensagens):
                if cond(m):
                    return self.mensagens.pop(k)

    def diagnosticos(self, uri, espera=10):
        m = self._espera(lambda m: m.get("method") == "textDocument/publishDiagnostics"
                         and normaliza(m["params"]["uri"]) == normaliza(uri), espera)
        return None if m is None else m["params"]["diagnostics"]

    def encerra(self):
        self.pede("shutdown", None)
        self.avisa("exit", None)
        try:
            return self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
            return None


def uri_de(caminho):
    return Path(caminho).resolve().as_uri()


def normaliza(uri):
    # file:///C%3A/x e file:///c:/x são o mesmo arquivo (o VS Code manda a primeira forma)
    return unquote(uri).lower()
