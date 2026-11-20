// A extensão da Cinza: o destaque de sintaxe (syntaxes/cinza.tmLanguage.json) e, desde a 0.8, o
// servidor de linguagem do próprio cinza (`cinza --lsp`, REVISAO_V2.md Q6): os erros do compilador
// enquanto se digita e a formatação do documento (o `cinza fmt`). O executável vem da configuração
// `cinza.caminho` (padrão: `cinza`, procurado no PATH).
const vscode = require("vscode");

// Na pasta de desenvolvimento (editor/vscode, sem `npm install`) a biblioteca do cliente pode não
// estar: a extensão segue só com o destaque de sintaxe. No .vsix ela vai dentro.
let LanguageClient;
try {
    ({ LanguageClient } = require("vscode-languageclient/node"));
} catch (e) {
    LanguageClient = undefined;
}

let cliente;

function inicia() {
    if (!LanguageClient) {
        vscode.window.showInformationMessage(
            "Cinza: o servidor de linguagem está desligado (falta o vscode-languageclient: rode " +
                "`npm install` em editor/vscode, ou instale o .vsix); o destaque de sintaxe continua."
        );
        return Promise.resolve();
    }
    const exe = vscode.workspace.getConfiguration("cinza").get("caminho") || "cinza";
    const servidor = { command: exe, args: ["--lsp"] };
    cliente = new LanguageClient(
        "cinza",
        "Cinza",
        { run: servidor, debug: servidor },
        { documentSelector: [{ scheme: "file", language: "cinza" }] }
    );
    return cliente.start().catch((e) => {
        cliente = undefined;
        vscode.window.showWarningMessage(
            `Cinza: não foi possível iniciar '${exe} --lsp' (${e.message}). ` +
                "Ajuste 'cinza.caminho' nas configurações; o destaque de sintaxe continua."
        );
    });
}

function activate(context) {
    context.subscriptions.push(
        vscode.workspace.onDidChangeConfiguration(async (e) => {
            if (!e.affectsConfiguration("cinza.caminho")) return;
            if (cliente) await cliente.stop();
            await inicia();
        })
    );
    return inicia();
}

function deactivate() {
    return cliente ? cliente.stop() : undefined;
}

module.exports = { activate, deactivate };
