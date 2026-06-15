#ifndef CINZA_ENVIRONMENT_H
#define CINZA_ENVIRONMENT_H

#include "value.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <stdexcept>

namespace cinza {

// ============================================================================
// ENVIRONMENT
//
// Armazena os valores das variáveis durante a execução.
// Análogo ao SymbolTable do semântico, mas guarda Values reais
// em vez de metadados de tipo.
//
// Estrutura: pilha de escopos (Scope = unordered_map<string, Value>).
//
//   pushScope()   → abre novo nível (entrada de bloco, função, for)
//   popScope()    → fecha o nível atual (saída do bloco)
//   define()      → declara variável no escopo atual
//   assign()      → atualiza variável existente (percorre a pilha)
//   get()         → lê variável (percorre a pilha)
//   exists()      → verifica existência em qualquer nível
//
// Invariantes:
//   - Sempre há pelo menos o escopo global (nível 0).
//   - O ambiente não gerencia estado de "binding" (BOUND/UNBOUND).
//     Toda variável inserida via define() já carrega um Value concreto.
//     A ausência de inicializador em list<T>/dict<K,V> é resolvida pelo
//     executor antes de chamar define() — aqui só chegam valores válidos.
// ============================================================================

class Environment {
public:
    using Scope = std::unordered_map<std::string, Value>;

private:
    std::vector<Scope> scopes;

public:
    Environment() {
        scopes.emplace_back(); // escopo global
    }

    // ── Gerenciamento de escopo ───────────────────────────────────────────

    void pushScope() {
        scopes.emplace_back();
    }

    void popScope() {
        if (scopes.size() > 1) {
            scopes.pop_back();
        }
    }

    int depth() const { return static_cast<int>(scopes.size()); }

    // ── Declaração ────────────────────────────────────────────────────────
    // Define uma nova variável no escopo atual.
    // Sobrescreve se já existir (compatível com re-entradas de função).
    void define(const std::string& name, Value val) {
        scopes.back()[name] = std::move(val);
    }

    // ── Leitura ───────────────────────────────────────────────────────────
    // Percorre do escopo mais interno para o global.
    // Lança std::runtime_error se não encontrar (nunca deve ocorrer após
    // análise semântica bem-sucedida).
    Value get(const std::string& name) const {
        for (auto it = scopes.crbegin(); it != scopes.crend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        throw std::runtime_error(
            "Variável '" + name + "' não encontrada em runtime "
            "(isso não deveria acontecer após análise semântica)");
    }

    // ── Escrita ───────────────────────────────────────────────────────────
    // Atualiza a variável no escopo onde ela foi declarada.
    // Retorna true se encontrou e atualizou; false caso contrário.
    bool assign(const std::string& name, Value val) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                found->second = std::move(val);
                return true;
            }
        }
        return false;
    }

    // ── Consulta ──────────────────────────────────────────────────────────
    bool exists(const std::string& name) const {
        for (auto it = scopes.crbegin(); it != scopes.crend(); ++it) {
            if (it->count(name)) return true;
        }
        return false;
    }

    bool existsInCurrentScope(const std::string& name) const {
        return scopes.back().count(name) > 0;
    }

    // ── Acesso direto ao escopo atual (para sincronização de campos) ──────
    // Usado pelo executor para copiar campos de instância de/para o ambiente
    // quando entra/sai de um método.
    Scope& currentScope() { return scopes.back(); }
    const Scope& currentScope() const { return scopes.back(); }
};

} // namespace cinza

#endif // CINZA_ENVIRONMENT_H
