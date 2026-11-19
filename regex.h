#ifndef CINZA_REGEX_H
#define CINZA_REGEX_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================================
// EXPRESSÕES REGULARES (Q3) — o motor do módulo Regex, no estilo do RE2: o
// padrão vira um programa e a busca roda numa máquina de Pike, em tempo linear
// no tamanho do texto vezes o do programa, para qualquer padrão (sem a explosão
// exponencial dos motores com retrocesso; por isso não há \1 nem lookaround).
// Trabalha em code points: "." casa "ç" inteiro, e as posições contam caracteres.
// A escolha entre alternativas e repetições é a do Perl (a primeira que casar,
// a repetição gulosa ou preguiçosa), e a busca acha a ocorrência mais à esquerda.
//
// Sintaxe (spec/06-biblioteca.md, Regex): literais; . (menos \n); classes [a-z],
// [^...], \d \w \s e as negadas \D \W \S (letras e espaço como em Strings);
// grupos (...), (?:...), (?<nome>...); alternativa |; repetições * + ? {n}
// {n,} {n,m}, preguiçosas com ? depois; âncoras ^ $ (o texto inteiro); \b \B;
// escapes \n \t \r \f \v e de pontuação; (?i) no começo: sem diferenciar
// maiúsculas de minúsculas.
// ============================================================================

namespace cinza::regex {

// Padrão inválido: o motivo e a posição (em caracteres) no padrão
struct ErroDePadrao : std::runtime_error {
    std::size_t posicao;
    ErroDePadrao(const std::string& motivo, std::size_t pos) : std::runtime_error(motivo), posicao(pos) {}
};

class Regex {
public:
    // Compila; lança ErroDePadrao
    explicit Regex(const std::u32string& padrao);

    // Os grupos de captura (sem contar o 0, a ocorrência inteira) e o nome de cada um
    // (nomes[i] é o do grupo i + 1; "" sem nome)
    std::size_t grupos() const { return nomes_.size(); }
    const std::vector<std::string>& nomes() const { return nomes_; }

    // Procura a ocorrência mais à esquerda a partir de `inicio` (âncoras e \b olham o
    // texto inteiro). `caps` recebe 2 posições por grupo, do 0 em diante (-1 se o grupo
    // não participou). `inteiro`: só uma que começa em `inicio` e vai até o fim do texto.
    bool procura(const std::u32string& s, std::size_t inicio, std::vector<int>& caps, bool inteiro = false) const;

    enum class Op : std::uint8_t { Char, Qualquer, Classe, Divide, Pula, Grava, Casou, Inicio, Fim, Borda, NaoBorda };
    struct Inst {
        Op       op;
        char32_t c = 0;   // Char (já em minúscula com (?i))
        int      x = 0;   // Divide/Pula: destino (Divide: o preferido); Grava: a posição; Classe: qual
        int      y = 0;   // Divide: o outro destino
    };
    struct Classe {
        std::vector<std::pair<char32_t, char32_t>> faixas;
        bool negada = false;
    };

private:
    std::vector<Inst>        prog_;
    std::vector<Classe>      classes_;
    std::vector<std::string> nomes_;
    bool                     sem_caixa_ = false;   // (?i)

    bool casa(const Inst& in, char32_t c) const;
    bool naClasse(const Classe& k, char32_t c) const;
    friend class Compilador;
};

} // namespace cinza::regex

#endif // CINZA_REGEX_H
