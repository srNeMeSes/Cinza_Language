#ifndef CINZA_FORMATADOR_H
#define CINZA_FORMATADOR_H

#include <string>

namespace cinza {

// ============================================================================
// FORMATADOR (Q6) — `cinza fmt`: um estilo só, sem opções (como o gofmt).
//
// As quebras de linha são as do autor; o formatador refaz o recuo (2 espaços
// por nível de chave), os espaços entre os tokens (em volta dos operadores
// binários, depois da vírgula, nenhum dentro de parênteses e colchetes...), as
// linhas em branco (no máximo uma seguida) e o fim do arquivo (uma quebra).
// Uma linha que continua uma expressão aberta, dentro de ( ou [, guarda o
// alinhamento que o autor deu em relação à linha onde ela começou. Os
// comentários ficam como estão (o texto e o espaço antes de um comentário no
// fim da linha). O fim de linha do arquivo (\n ou \r\n) é mantido.
//
// O resultado tem exatamente os mesmos tokens que o original — o formatador
// confere isso pelo léxico da linguagem e recusa o arquivo se não tiver (nunca
// muda o que o programa faz).
// ============================================================================

// O texto formatado; false (com o motivo em `erro`) se o arquivo não puder ser
// formatado (erro léxico, ou uma string que não termina na linha)
bool formata(const std::string& fonte, std::string& saida, std::string& erro);

} // namespace cinza

#endif // CINZA_FORMATADOR_H
