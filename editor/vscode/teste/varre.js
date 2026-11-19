// Teste da gramática da extensão: tokeniza os .cinza do repositório com o mesmo motor de
// cores do VS Code (vscode-textmate + Oniguruma, os que vêm dentro dele) e lista, em ordem,
// cada trecho marcado como inválido ("arquivo<TAB>linha:coluna<TAB>escopo<TAB>texto").
// Roda pelo Node do próprio VS Code (conferir.py cuida disso):
//   ELECTRON_RUN_AS_NODE=1 Code.exe varre.js <pasta app do VS Code> <raiz do repositório>
const fs = require('fs');
const path = require('path');
const [app, raiz] = process.argv.slice(2);
const tm = require(path.join(app, 'node_modules.asar', 'vscode-textmate'));
const onig = require(path.join(app, 'node_modules.asar', 'vscode-oniguruma'));

// os programas: tests/ (sem os gerados) e exemplos/
const FORA = new Set(['spec_exemplos', 'tests_adm']);
function lista(pasta, saida) {
  for (const nome of fs.readdirSync(pasta).sort()) {
    const p = path.join(pasta, nome);
    if (fs.statSync(p).isDirectory()) { if (!FORA.has(nome)) lista(p, saida); }
    else if (nome.endsWith('.cinza')) saida.push(p);
  }
  return saida;
}

(async () => {
  const wasm = fs.readFileSync(path.join(app, 'node_modules.asar.unpacked', 'vscode-oniguruma', 'release', 'onig.wasm'));
  await onig.loadWASM(wasm.buffer);
  const gramatica = path.join(__dirname, '..', 'syntaxes', 'cinza.tmLanguage.json');
  const registry = new tm.Registry({
    onigLib: Promise.resolve({ createOnigScanner: (s) => new onig.OnigScanner(s), createOnigString: (s) => new onig.OnigString(s) }),
    loadGrammar: async () => tm.parseRawGrammar(fs.readFileSync(gramatica, 'utf8'), gramatica),
  });
  const grammar = await registry.loadGrammar('source.cinza');
  const arquivos = [];
  for (const d of ['tests', 'exemplos']) lista(path.join(raiz, d), arquivos);
  const saida = [];
  for (const arq of arquivos) {
    const rel = path.relative(raiz, arq).split(path.sep).join('/');
    let estado = tm.INITIAL;
    fs.readFileSync(arq, 'utf8').replace(/^﻿/, '').split(/\r?\n/).forEach((linha, i) => {
      const r = grammar.tokenizeLine(linha, estado);
      for (const t of r.tokens) {
        const inv = t.scopes.filter((s) => s.startsWith('invalid'));
        if (inv.length) saida.push(`${rel}\t${i + 1}:${t.startIndex + 1}\t${inv.join(',')}\t${linha.slice(t.startIndex, t.endIndex)}`);
      }
      estado = r.ruleStack;
    });
  }
  process.stdout.write(saida.join('\n') + '\n');
  process.stderr.write(`${arquivos.length} arquivos, ${saida.length} trechos inválidos\n`);
})().catch((e) => { process.stderr.write(String(e && e.stack || e) + '\n'); process.exit(2); });
