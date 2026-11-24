#pragma once
#include <string>
#include <vector>

namespace cinza::pacotes {

// ============================================================================
// PACOTES (Q10) — o czm.mod, o czm.lock, o cache e os comandos add/remove/get
//
// Um pacote é um projeto (uma pasta com czm.mod) num repositório git, pedido por uma tag semver
// (requer github.com/ana/json v1.2.0), ou uma pasta no disco (requer ../minha_lib). O
// `cinza get` escolhe uma versão de cada pacote git — a maior pedida na mesma major (seleção
// mínima, como no Go; majors diferentes são erro) —, baixa as que faltam pelo git instalado para
// o cache do usuário e grava o czm.lock (a versão, o commit e o hash do conteúdo de cada uma). A
// compilação só lê o czm.lock e o cache: nunca acessa a rede.
// ============================================================================

// Uma linha 'requer' do czm.mod
struct Requer {
    std::string origem;   // ../caminho (local) ou a URL do repositório (git)
    std::string versao;   // git: a tag (v1.2.0); local: vazio
    std::string nome;     // o 'as nome' (vazio: o nome declarado no czm.mod do pacote)
    bool        local = true;
    int         linha = 0;
};

// O czm.mod lido
struct Manifesto {
    std::string         nome;     // projeto nome (vazio: não declarado)
    std::vector<Requer> requer;
};

// Lê o texto de um czm.mod; false com a mensagem e a linha do erro
bool leManifesto(const std::string& texto, Manifesto& m, std::string& erro, int& linha);

// Uma linha do czm.lock: o pacote git escolhido
struct Travado {
    std::string origem, versao, commit, hash;
};

// Lê o czm.lock da raiz do projeto (sem o arquivo: vazio e true)
bool leLock(const std::string& raiz, std::vector<Travado>& out, std::string& erro, int& linha);

// A pasta do cache (CINZA_PACOTES, ou ~/.cinza/pacotes) e a de um pacote nele
std::string pastaDoCache();
std::string pastaNoCache(const std::string& origem, const std::string& versao);

// O hash do conteúdo de uma pasta (todos os arquivos, em ordem, menos .git): "sha256-<hex>"
std::string hashDaPasta(const std::string& pasta);

// "v1.2.0" → {1, 2, 0}; false se não for uma tag semver
bool versao(const std::string& tag, int v[3]);

// Os comandos (args a partir do nome do comando); devolvem o código de saída
int comandoGet(const std::vector<std::string>& args);
int comandoAdd(const std::vector<std::string>& args);
int comandoRemove(const std::vector<std::string>& args);

} // namespace cinza::pacotes
