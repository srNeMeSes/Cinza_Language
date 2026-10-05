# Rede neural (MLP) em Cinza

Uma rede neural completa escrita só em Cinza, sem nada de fora. Ela aprende o preço de casas a partir
de um CSV, grava os pesos num arquivo binário próprio (`.czr`) e depois usa esse arquivo para prever,
sem precisar treinar de novo.

## Como usar

Rode de dentro desta pasta (os caminhos relativos partem da pasta atual):

```
cinza gera_casas.cinza                         # cria casas.csv (800 casas sintéticas)
cinza treinar.cinza                            # treina e grava modelo.czr
cinza prever.cinza modelo.czr 3 2 120 1        # 3 quartos, 2 banheiros, 120 m², com piscina
cinza prever.cinza modelo.czr                  # pergunta cada valor
cinza prever.cinza modelo.czr --csv casas.csv  # prevê cada linha de um CSV
```

`treinar.cinza [dados.csv] [modelo.czr] [épocas]` aceita outro CSV, outro nome de arquivo e outro
número de épocas (padrão 300, cerca de 12 s na CVM).

## O CSV

```
qtd_quartos,qtd_banheiros,m²,tem_piscina,valor
5,4,176,1,768300
```

- A primeira linha traz os cabeçalhos. Os nomes vão para o `.czr` e aparecem na previsão.
- A **última coluna é o alvo**, e as demais são as entradas. Nada no código está preso a 5 colunas:
  outro CSV numérico treina do mesmo jeito.
- Separador `,` com ponto decimal, ou `;` com vírgula decimal (como o Excel em português grava). O
  separador é detectado pelo cabeçalho. Aspas em volta dos campos e o BOM do Excel são aceitos.
- Linha com número de colunas errado ou valor que não é número é recusada, com a linha e a coluna.

## A rede

```
4 entradas → 16 (ReLU) → 8 (ReLU) → 1 (linear)
```

- **Padronização** (z-score) das entradas e do alvo, com média e desvio calculados só no treino.
- **Inicialização** de He nas camadas ReLU e de Xavier na saída. A ReLU é "leaky" (0,01 abaixo de
  zero), para nenhum neurônio morrer de vez.
- **Treino** por retropropagação com erro quadrático, em mini-lotes de 32 embaralhados a cada
  época, e otimizador **Adam** (taxa 0,003).
- **Avaliação** com 80% das linhas para treino e 20% guardadas para teste. Mostra o erro médio
  absoluto, o RMSE e o R² nos dois conjuntos.

Os preços do `casas.csv` saem de uma regra com ±6% de ruído aleatório, então nenhum modelo erra menos
que esse ruído (≈ R$ 30 mil). A rede chega a ele: R² ≈ 0,96 nas casas de teste, que ela nunca viu.

| Arquivo            | O que faz |
|--------------------|-----------|
| `mlp.cinza`        | `Camada`, `Rede`, `Modelo` (treino, previsão, salvar) e `carregar()` |
| `czr.cinza`        | o formato `.czr`: `Escritor` e `Leitor` |
| `dados.cinza`      | leitura do CSV, divisão treino/teste, formatação em reais |
| `treinar.cinza`    | programa de treino |
| `prever.cinza`     | programa de previsão |
| `gera_casas.cinza` | gera o CSV de exemplo |

## O formato `.czr`

**Cada byte carrega 7 bits** (vai de 0 a 127), então o arquivo é ASCII puro. O formato nasceu antes de
`Files.write_bytes` existir, quando um programa em Cinza só gravava texto UTF-8 (de 128 em diante, um
caractere vira dois bytes); hoje o `Escritor` e o `Leitor` usam `Files.write_bytes`/`read_bytes`, mas o
formato continua de 7 bits para os `.czr` já gravados seguirem válidos:

| Campo     | Bytes | Conteúdo |
|-----------|-------|----------|
| cabeçalho | 4     | `C` `Z` `R` e a versão (1) |
| `int`     | 5     | 35 bits sem sinal, o grupo mais significativo primeiro |
| `decimal` | 10    | 2 bytes com o sinal e o expoente IEEE 754, depois 8 bytes com os 52 bits da mantissa |
| texto     | 5 + 3n | o número de caracteres, depois 21 bits (o código Unicode) por caractere |
| rodapé    | 5     | Adler-32 de todos os bytes anteriores |

O conteúdo vem nesta ordem: os nomes das entradas e do alvo; a arquitetura (entradas, saídas e
ativação de cada camada); as épocas treinadas; as médias e desvios da padronização; os pesos e vieses.

O decimal é decomposto só com multiplicações e divisões por 2, que são exatas. Por isso o número
lido é **bit a bit** o número gravado, e o modelo relido prevê exatamente o mesmo que o treinado. O
`treinar.cinza` confere isso no fim. Ao ler, o `Leitor` confere a assinatura, a versão, a soma de
verificação e a coerência das camadas antes de usar qualquer peso. Um arquivo corrompido ou de outro
formato é recusado com uma mensagem clara.

O teste `tests/exemplo_rede_neural.cinza` cobre a ida e volta exata dos decimais, o treino, salvar e
reler, e a detecção de corrupção.
