#ifndef CINZA_CIR_H
#define CINZA_CIR_H

// ============================================================================
// CIR — a IR intermediária da Cinza (llvm/DESENHO.md, seção 4)
//
// Entre a AST analisada e o LLVM. A tradução AST → CIR (gera_cir.cpp) decide a
// ordem de avaliação, os temporários e a posse; CIR → LLVM (emissor.cpp) é
// mecânica.
//
//   - slots: as variáveis (parâmetros, locais), lidas e escritas por Carrega e
//     Grava; tudo o que atravessa blocos passa por um slot;
//   - temporários: escritos uma vez (no LLVM viram valores SSA); usados só em
//     blocos que a definição domina — a tradução de uma expressão garante isso
//     (o curto-circuito de and/or abre blocos que voltam ao mesmo ponto);
//   - globais: os const do nível superior, escritos pela função de inicialização.
//
// As operações que podem falhar (contas, conversões, chamadas) levam a posição
// (`pos`) do token que a CVM usa na mesma operação: erros e stack trace iguais.
// P4: números, bool, funções, if/while/for range, print, const globais.
// P6: list, dict e pair (objetos do runtime, com os elementos como CzValor).
// P7: structs (valor: guardar uma cópia emprestada a clona), objetos de classe e
// interface (ponteiro), enum e tipo (CzValor sem referência), métodos e type().
// P8: erros (llvm/DESENHO.md 5.3). Toda operação que pode falhar leva o bloco tratador
// (`erro`): com erro pendente depois dela, o fluxo vai para lá — o except de um try da
// função, ou a saída de erro, que solta os slots e volta ao chamador com o erro.
// P5: strings e a posse (llvm/DESENHO.md 4.3): um slot de tipo com referência é
// dono do valor e começa vazio; a tradução escreve cada retem/solta (o emissor não
// decide nada sobre posse).
// ============================================================================

#include "../lexer.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cinza::cir {

enum class Tipo : std::uint8_t {
    Void, Bool, I32, I64, F32, F64,
    Str,     // ponteiro para CzString
    Valor,   // CzValor embrulhado (16 bytes)
    Lista,   // ponteiro para CinzaList
    Dict,    // ponteiro para CinzaDict
    Par,     // ponteiro para CinzaPair
    Struct,  // ponteiro para StructValue (cada slot tem o seu: guardar clona)
    Obj,     // ponteiro para ClassInstance (objeto de classe ou valor de interface)
    Enum,    // CzValor do enum (descritor e índice), sem referência
    TipoV,   // CzValor de type() e dos literais de tipo, sem referência
    Desc,    // ponteiro para um descritor do runtime (struct, classe)
    Erro,    // ponteiro para ErrorValue (o erro como valor: except, Tipo("msg"))
};

inline bool ehInteiro(Tipo t)   { return t == Tipo::I32 || t == Tipo::I64; }
inline bool ehFlutuante(Tipo t) { return t == Tipo::F32 || t == Tipo::F64; }
// Tipo com referência (um ponteiro para objeto do runtime, com posse)
inline bool ehRef(Tipo t) {
    return t == Tipo::Str || t == Tipo::Lista || t == Tipo::Dict || t == Tipo::Par || t == Tipo::Struct ||
           t == Tipo::Obj || t == Tipo::Erro;
}
// Representado como um CzValor inteiro (embrulhar não muda nada)
inline bool ehValorDireto(Tipo t) { return t == Tipo::Valor || t == Tipo::Enum || t == Tipo::TipoV; }

// Um operando: constante ou temporário
struct Operando {
    enum class K : std::uint8_t {
        Temp, I32, I64, F32, F64, Bool,
        Literal,   // str: uma string do módulo (imortal)
        Nulo,      // str: o ponteiro nulo (slot vazio)
        Vazio,     // valor: o CzValor void (parte omitida de uma fatia)
        DescStruct, DescClasse,   // desc: o descritor `indice`
        Enum,      // enum: o valor `i` do enum `indice` (Modulo::enums)
        TipoLit,   // tipo: o tipo `indice` da tabela (Modulo::tipos)
    } k = K::Temp;
    std::uint32_t indice = 0;   // Temp: o temporário; Literal: a string do módulo
    std::int64_t  i      = 0;
    double        f      = 0;

    static Operando temp(std::uint32_t t)    { Operando o; o.k = K::Temp; o.indice = t; return o; }
    static Operando literal(std::uint32_t l) { Operando o; o.k = K::Literal; o.indice = l; return o; }
    static Operando i32(std::int64_t v)      { Operando o; o.k = K::I32; o.i = v; return o; }
    static Operando i64(std::int64_t v)      { Operando o; o.k = K::I64; o.i = v; return o; }
    static Operando boolean(bool v)          { Operando o; o.k = K::Bool; o.i = v; return o; }
    static Operando f32(double v)            { Operando o; o.k = K::F32; o.f = v; return o; }
    static Operando f64(double v)            { Operando o; o.k = K::F64; o.f = v; return o; }
    static Operando nulo()                   { Operando o; o.k = K::Nulo; return o; }
    static Operando vazio()                  { Operando o; o.k = K::Vazio; return o; }
    static Operando descStruct(std::uint32_t d) { Operando o; o.k = K::DescStruct; o.indice = d; return o; }
    static Operando descClasse(std::uint32_t d) { Operando o; o.k = K::DescClasse; o.indice = d; return o; }
    static Operando enumerado(std::uint32_t e, std::int64_t v) { Operando o; o.k = K::Enum; o.indice = e; o.i = v; return o; }
    static Operando tipoLit(std::uint32_t t) { Operando o; o.k = K::TipoLit; o.indice = t; return o; }
};

enum class Op : std::uint8_t {
    Carrega,       // dst = carrega slot[aux]
    Grava,         // slot[aux] = a
    CarregaG,      // dst = carrega global[aux]
    GravaG,        // global[aux] = a
    Arit,          // dst = a op b — aux: o TokenType (+ - * / %); confere estouro e zero
    Compara,       // dst: bool = a pred b — aux: o TokenType (== != < <= > >=)
    Neg,           // dst = -a (inteiro: confere o menor valor)
    Nao,           // dst: bool = !a
    Converte,      // dst = a convertido para o tipo de dst (confere a faixa ao estreitar)
    Embrulha,      // dst: valor = embrulha a — sem posse
    Desembrulha,   // dst = desembrulha a (o tipo de dst)
    ChamaNativa,   // dst: valor = call.nativa N(args...) — aux: N; o resultado tem posse
    ConverteValor, // dst: valor = a convertido no runtime para o kind de `tipo` (nativas genéricas)
    Chama,         // [dst =] call F(args...) — aux: F
    ChamaIface,    // [dst =] call.iface T(obj, args...) — aux: a tabela; o método pela classe do obj
    CampoDireto,   // dst = campo aux de a (obj ou struct) — só campo sem referência (P10: em linha)
    GravaCampoDireto, // campo aux de a = b — só valor sem referência (o antigo também não tem)
    Retem,         // retem a (str): uma referência a mais
    Solta,         // solta a — str: solta a referência (nulo: nada); valor: a que ele segura
    ChamaRt,       // [dst =] call.rt R(args...) — aux: o Rt (tabela abaixo)
    ConferePasso,  // confere passo (i64) != 0 — senão ValueError do range (pos: a chamada)
    PassoRange,    // dst: i64 = próximo contador do for range — args: i, passo, fim (i64);
                   // tipo: a largura do iterador; passou da largura: fim (o último)
    Br,            // br alvo
    BrC,           // brc a, alvo, senao
    Ret,           // ret [a]
    RetErro,       // volta ao chamador com o erro pendente (o valor de retorno não importa)
    Inalcancavel,  // fim de um caminho que o semântico garante que não chega
};

// As funções do runtime chamadas por ChamaRt (llvm/cinzart.h). Na chamada em C, um
// parâmetro `valor` vai por ponteiro; um resultado `valor` volta por um ponteiro de
// saída; as que podem falhar recebem a posição no fim e o chamador confere o erro.
// Resultados `str` e `valor` vêm com posse.
enum class Rt : std::uint8_t {
    Concat,          // str (valor a, valor b): a + b com string (o outro lado em texto)
    Texto,           // str (valor v): cast<string>(v)
    StrIgual,        // bool (str, str)
    StrCompara,      // i32 (str, str): < 0, 0, > 0
    StrBytes,        // i64 (str): o tamanho em bytes
    StrCaractere,    // str (str, i64 pos): o caractere UTF-8 que começa no byte pos
    StrTamCaractere, // i64 (str, i64 pos): quantos bytes ele tem
    Formata,         // str (valor, i32 largura, i32 casas): um {x:formato} do printf
    DeTexto,         // valor (str, i32 kind): cast<T>(string) — falha
    Indice,          // valor (valor obj, valor idx): obj[idx] — falha
    Fatia,           // valor (valor s, valor ini, valor fim, valor passo) — falha
    Metodo0,         // valor (i32 builtin, valor obj) — falha
    Metodo1,         // valor (i32 builtin, valor obj, valor a) — falha
    Metodo2,         // valor (i32 builtin, valor obj, valor a, valor b) — falha
    ListaNova,       // lista (valor...): os elementos, na ordem
    DictNovo,        // dict (valor...): chave, valor, chave, valor... (a última vence)
    ParNovo,         // par (valor a, valor b)
    ParCampo,        // valor (valor par, i32 qual): first (0) ou second (1)
    Igual,           // bool (valor a, valor b): o == da CVM (desce nos contêineres)
    Elementos,       // lista (valor col): a cópia dos elementos de um for (spec 5.5) — falha
    ListaTam,        // i64 (lista)
    ListaElem,       // valor (lista, i64 i): uma cópia do elemento
    IndiceLugar,     // valor (valor obj, valor k): o lugar obj[k] para gravar (B5) — falha
    GravaIndice,     // void (valor obj, valor k, valor v): obj[k] = v — falha
    StructNovo,      // struct (desc, valor...): os campos na ordem
    ObjNovo,         // obj (desc): os campos vazios (o inicializador e o construtor vêm depois)
    Campo,           // valor (valor obj, i32 i): uma cópia do campo (struct ou objeto)
    GravaCampo,      // void (valor obj, i32 i, valor v): no próprio objeto (sem clonar o struct)
    StructCopia,     // struct (struct): a cópia rasa (a regra da cópia de struct da spec 5.1)
    ClasseDe,        // i32 (obj): o índice da classe do objeto
    TipoDe,          // valor (valor x, tipo estático): type(x) (runtimeType)
    ErroNovo,        // erro (str tipo, str mensagem ou nulo, i32 arquivo, i32 linha, i32 coluna): Tipo("msg")
    ErroCampo,       // valor (erro, i32 qual): e.kind (0), e.message (1), e.line (2), e.column (3)
    ErroCasa,        // bool (str tipo): o erro pendente casa com except (Tipo e)? (Error: todos, menos o exit)
    PegaErro,        // erro (): tira o erro pendente do estado, com posse
    Lanca,           // void (erro): throw — o erro pendente passa a ser uma cópia dele — falha
    Relanca,         // void (erro com posse): devolve ao estado um erro tirado dele (o fim do finally) — falha
};
struct InfoRt {
    const char*       nome;     // o símbolo em C
    Tipo              ret;
    std::vector<Tipo> params;
    bool              falha;    // CZ_FALHA
    // depois dos fixos, os argumentos (valor) vão num vetor: (const CzValor*, uint64 n)
    bool              variadico = false;
};
const InfoRt& infoRt(Rt r);

struct Instr {
    Op                    op;
    std::uint32_t         dst = 0;     // o temporário escrito
    std::vector<Operando> args;
    std::uint32_t         aux = 0;     // slot, global, operador, nativa ou função
    std::uint32_t         alvo = 0, senao = 0;   // Br/BrC: os blocos
    Tipo                  tipo = Tipo::Void;     // ConverteValor: o tipo pedido
    Token                 pos;         // a posição do programa (erros, stack trace)
    std::uint32_t         erro = 0;    // o bloco tratador, se a operação pode falhar
};

struct Bloco {
    std::string        nome;
    std::vector<Instr> instr;
};

struct Funcao {
    std::string        nome;            // símbolo (llvm/DESENHO.md 8.3): cz:::main
    std::string        nome_trace;      // no stack trace: "f", "Classe.metodo"
    Token              decl;            // a declaração (StackOverflowError, stack trace)
    bool               oculta = false;  // a inicialização dos const: fora da pilha e do trace
    std::vector<Tipo>  params;          // os primeiros slots
    Tipo               ret = Tipo::Void;
    std::vector<Tipo>  slots;
    std::vector<Tipo>  temps;           // o tipo de cada temporário
    std::vector<Bloco> blocos;

    std::uint32_t novoTemp(Tipo t) { temps.push_back(t); return static_cast<std::uint32_t>(temps.size() - 1); }
    std::uint32_t novoSlot(Tipo t) { slots.push_back(t); return static_cast<std::uint32_t>(slots.size() - 1); }
    std::uint32_t novoBloco(const std::string& n) {
        blocos.push_back(Bloco{n + std::to_string(blocos.size()), {}});
        return static_cast<std::uint32_t>(blocos.size() - 1);
    }
};

// Uma função da biblioteca usada pelo programa, resolvida pelo nome na partida
struct Nativa {
    std::string modulo;   // "" = embutida (print, range...)
    std::string nome;
};

// Os descritores do runtime, criados na partida (CzStructDesc, CzClasseDesc, CzEnumDesc)
struct DescStruct {
    std::string              nome;
    std::vector<std::string> campos;
};
struct DescClasse {
    std::string   nome;
    std::uint32_t campos = 0;
};
struct DescEnum {
    std::string              nome;
    std::vector<std::string> valores;
};
// Um tipo para type() e os literais de tipo, refeito na partida pelo TypeContext: o
// TypeInfo::Kind, o nome e os parâmetros (índices de tipos anteriores na tabela)
struct DescTipo {
    std::uint8_t               kind = 0;
    std::string                nome;
    std::vector<std::uint32_t> params;
};
// Um método de interface: a função de cada classe (pelo índice da classe; -1 se não
// cumpre) e a assinatura comum (self primeiro)
struct TabelaIface {
    std::string       nome;     // "Forma.area"
    std::vector<Tipo> params;
    Tipo              ret = Tipo::Void;
    std::vector<int>  funcoes;
};

struct Modulo {
    std::vector<std::string> arquivos;   // os nomes dos arquivos, na ordem dos file_id
    std::vector<DescStruct>  structs;
    std::vector<DescClasse>  classes;    // o índice é o da classe no runtime
    std::vector<DescEnum>    enums;
    std::vector<DescTipo>    tipos;
    std::vector<TabelaIface> tabelas;
    std::vector<std::string> literais;   // as strings do programa (imortais)
    std::vector<Nativa>      nativas;
    std::vector<Tipo>        globais;    // os const do nível superior
    std::vector<Funcao>      funcoes;
    int                      init = -1;  // a função oculta que inicializa os const (-1: nenhuma)
    int                      main = -1;  // a main da Cinza
    int                      fim = -1;   // a função oculta que solta os const com referência

    std::uint32_t literal(const std::string& s);
    std::uint32_t nativa(const std::string& modulo, const std::string& nome);
};

// O texto da CIR (cinzac --emit-cir)
std::string texto(const Modulo& m);
// A instrução pode deixar erro pendente (e então segue para o bloco `erro`)
bool podeFalhar(const Instr& in);
const char* nomeTipo(Tipo t);

}  // namespace cinza::cir

#endif  // CINZA_CIR_H
