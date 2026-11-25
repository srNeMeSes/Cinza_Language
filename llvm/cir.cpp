#include "cir.h"
#include <sstream>

namespace cinza::cir {

std::uint32_t Modulo::literal(const std::string& s) {
    // pelo índice: procurar na lista era quadrático (50 mil literais: 20 s no -O0)
    auto [it, novo] = indice_literais.try_emplace(s, static_cast<std::uint32_t>(literais.size()));
    if (novo) literais.push_back(s);
    return it->second;
}

std::uint32_t Modulo::nativa(const std::string& modulo, const std::string& nome) {
    for (std::size_t i = 0; i < nativas.size(); ++i)
        if (nativas[i].modulo == modulo && nativas[i].nome == nome) return static_cast<std::uint32_t>(i);
    nativas.push_back({modulo, nome});
    return static_cast<std::uint32_t>(nativas.size() - 1);
}

const InfoRt& infoRt(Rt r) {
    using T = Tipo;
    static const InfoRt tabela[] = {
        {"cz_concat",            T::Str,   {T::Valor, T::Valor}, false},
        {"cz_texto",             T::Str,   {T::Valor}, false},
        {"cz_str_igual",         T::Bool,  {T::Str, T::Str}, false},
        {"cz_str_compara",       T::I32,   {T::Str, T::Str}, false},
        {"cz_str_bytes",         T::I64,   {T::Str}, false},
        {"cz_str_caractere",     T::Str,   {T::Str, T::I64}, false},
        {"cz_str_tam_caractere", T::I64,   {T::Str, T::I64}, false},
        {"cz_formata",           T::Str,   {T::Valor, T::I32, T::I32}, false},
        {"cz_de_texto",          T::Valor, {T::Str, T::I32}, true},
        {"cz_indice",            T::Valor, {T::Valor, T::Valor}, true},
        {"cz_fatia",             T::Valor, {T::Valor, T::Valor, T::Valor, T::Valor}, true},
        {"cz_metodo0",           T::Valor, {T::I32, T::Valor}, true},
        {"cz_metodo1",           T::Valor, {T::I32, T::Valor, T::Valor}, true},
        {"cz_metodo2",           T::Valor, {T::I32, T::Valor, T::Valor, T::Valor}, true},
        {"cz_lista_nova",        T::Lista, {}, false, true},
        {"cz_dict_novo",         T::Dict,  {}, false, true},
        {"cz_par_novo",          T::Par,   {T::Valor, T::Valor}, false},
        {"cz_par_campo",         T::Valor, {T::Valor, T::I32}, false},
        {"cz_igual",             T::Bool,  {T::Valor, T::Valor}, false},
        {"cz_elementos",         T::Lista, {T::Valor}, true},
        {"cz_lista_tam",         T::I64,   {T::Lista}, false},
        {"cz_lista_elem",        T::Valor, {T::Lista, T::I64}, false},
        {"cz_indice_lugar",      T::Valor, {T::Valor, T::Valor}, true},
        {"cz_grava_indice",      T::Void,  {T::Valor, T::Valor, T::Valor}, true},
        {"cz_struct_novo",       T::Struct, {T::Desc}, false, true},
        {"cz_obj_novo",          T::Obj,   {T::Desc}, false},
        {"cz_campo",             T::Valor, {T::Valor, T::I32}, false},
        {"cz_grava_campo",       T::Void,  {T::Valor, T::I32, T::Valor}, false},
        {"cz_struct_copia",      T::Struct, {T::Struct}, false},
        {"cz_classe_de",         T::I32,   {T::Obj}, false},
        {"cz_tipo_de",           T::Valor, {T::Valor, T::TipoV}, false},
        {"cz_erro_novo",         T::Erro,  {T::Str, T::Str, T::I32, T::I32, T::I32}, false},
        {"cz_erro_campo",        T::Valor, {T::Erro, T::I32}, false},
        {"cz_erro_casa",         T::Bool,  {T::Str}, false},
        {"cz_pega_erro",         T::Erro,  {}, false},
        {"cz_lanca",             T::Void,  {T::Erro}, true},
        {"cz_relanca",           T::Void,  {T::Erro}, true},
        {"cz_valor_copia",       T::Valor, {T::Valor}, false},
        {"cz_lista_add",         T::Void,  {T::Lista, T::Valor}, false},
    };
    return tabela[static_cast<std::size_t>(r)];
}

bool podeFalhar(const Instr& in) {
    switch (in.op) {
        case Op::Arit: case Op::Neg: case Op::Converte: case Op::ChamaNativa: case Op::ConverteValor:
        case Op::Chama: case Op::ChamaIface: case Op::ConferePasso:
            return true;
        case Op::ChamaRt:
            return infoRt(static_cast<Rt>(in.aux)).falha;
        default:
            return false;
    }
}

const char* nomeTipo(Tipo t) {
    switch (t) {
        case Tipo::Void:  return "void";
        case Tipo::Bool:  return "bool";
        case Tipo::I32:   return "i32";
        case Tipo::I64:   return "i64";
        case Tipo::F32:   return "f32";
        case Tipo::F64:   return "f64";
        case Tipo::Str:   return "str";
        case Tipo::Valor: return "valor";
        case Tipo::Opc:   return "opc";
        case Tipo::Lista: return "lista";
        case Tipo::Dict:  return "dict";
        case Tipo::Par:   return "par";
        case Tipo::Struct: return "struct";
        case Tipo::Obj:   return "obj";
        case Tipo::Enum:  return "enum";
        case Tipo::TipoV: return "tipo";
        case Tipo::Desc:  return "desc";
        case Tipo::Erro:  return "erro";
    }
    return "?";
}

namespace {

// Uma string como literal da CIR: entre aspas, com os escapes da Cinza
std::string citado(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\t': r += "\\t";  break;
            case '\r': r += "\\r";  break;
            default:   r += c;
        }
    }
    return r + "\"";
}

std::string operando(const Modulo& m, const Operando& o) {
    switch (o.k) {
        case Operando::K::Temp:    return "t" + std::to_string(o.indice);
        case Operando::K::I32:     return "i32 " + std::to_string(o.i);
        case Operando::K::I64:     return "i64 " + std::to_string(o.i);
        case Operando::K::F32:     { std::ostringstream s; s << "f32 " << o.f; return s.str(); }
        case Operando::K::F64:     { std::ostringstream s; s << "f64 " << o.f; return s.str(); }
        case Operando::K::Bool:    return o.i ? "bool true" : "bool false";
        case Operando::K::Literal: return "str " + citado(m.literais[o.indice]);
        case Operando::K::Nulo:    return "str nulo";
        case Operando::K::Vazio:   return "valor vazio";
        case Operando::K::Ausente: return "null";
        case Operando::K::DescStruct: return "desc " + m.structs[o.indice].nome;
        case Operando::K::DescClasse: return "desc " + m.classes[o.indice].nome;
        case Operando::K::Enum:    return m.enums[o.indice].nome + "." + m.enums[o.indice].valores[static_cast<std::size_t>(o.i)];
        case Operando::K::TipoLit: return "tipo T" + std::to_string(o.indice);
    }
    return "?";
}

const char* simbolo(std::uint32_t op) {
    switch (static_cast<TokenType>(op)) {
        case TokenType::OP_PLUS:          return "add";
        case TokenType::OP_MINUS:         return "sub";
        case TokenType::OP_MULTIPLY:      return "mul";
        case TokenType::OP_DIVIDE:        return "div";
        case TokenType::OP_MODULO:        return "rem";
        case TokenType::OP_EQUAL:         return "eq";
        case TokenType::OP_NOT_EQUAL:     return "ne";
        case TokenType::OP_LESS:          return "lt";
        case TokenType::OP_LESS_EQUAL:    return "le";
        case TokenType::OP_GREATER:       return "gt";
        case TokenType::OP_GREATER_EQUAL: return "ge";
        default:                          return "?";
    }
}

}  // namespace

std::string texto(const Modulo& m) {
    std::ostringstream out;
    for (std::size_t i = 0; i < m.nativas.size(); ++i)
        out << "nativa N" << i << " = " << (m.nativas[i].modulo.empty() ? "" : m.nativas[i].modulo + ".")
            << m.nativas[i].nome << "\n";
    for (std::size_t i = 0; i < m.globais.size(); ++i)
        out << "global G" << i << ": " << nomeTipo(m.globais[i]) << "\n";
    if (!m.nativas.empty() || !m.globais.empty()) out << "\n";

    for (std::size_t fi = 0; fi < m.funcoes.size(); ++fi) {
        const Funcao& f = m.funcoes[fi];
        out << "fn F" << fi << " " << f.nome << "(";
        for (std::size_t p = 0; p < f.params.size(); ++p) out << (p ? ", " : "") << nomeTipo(f.params[p]);
        out << ") -> " << nomeTipo(f.ret) << (f.oculta ? "   ; oculta" : "") << "\n";
        for (std::size_t s = 0; s < f.slots.size(); ++s)
            out << "  slot s" << s << ": " << nomeTipo(f.slots[s]) << (s < f.params.size() ? "   ; parâmetro" : "")
                << "\n";
        for (const Bloco& b : f.blocos) {
            out << b.nome << ":\n";
            for (const Instr& in : b.instr) {
                out << "  ";
                auto dst = [&] { out << "t" << in.dst << ": " << nomeTipo(f.temps[in.dst]) << " = "; };
                auto args = [&] {
                    for (std::size_t a = 0; a < in.args.size(); ++a) out << (a ? ", " : "") << operando(m, in.args[a]);
                };
                switch (in.op) {
                    case Op::Carrega:  dst(); out << "carrega s" << in.aux; break;
                    case Op::Grava:    out << "grava s" << in.aux << ", "; args(); break;
                    case Op::CarregaG: dst(); out << "carrega G" << in.aux; break;
                    case Op::GravaG:   out << "grava G" << in.aux << ", "; args(); break;
                    case Op::Arit:     dst(); out << simbolo(in.aux) << " "; args(); break;
                    case Op::Compara:  dst(); out << "cmp." << simbolo(in.aux) << " "; args(); break;
                    case Op::Neg:      dst(); out << "neg "; args(); break;
                    case Op::Nao:      dst(); out << "not "; args(); break;
                    case Op::Converte: dst(); out << "conv "; args(); break;
                    case Op::Embrulha: dst(); out << "embrulha "; args(); break;
                    case Op::Desembrulha: dst(); out << "desembrulha "; args(); break;
                    case Op::ChamaNativa: dst(); out << "call.nativa N" << in.aux << "("; args(); out << ")"; break;
                    case Op::ConverteValor: dst(); out << "conv.valor " << nomeTipo(in.tipo) << " "; args(); break;
                    case Op::Chama:
                        if (m.funcoes[in.aux].ret != Tipo::Void) dst();
                        out << "call F" << in.aux << "("; args(); out << ")";
                        break;
                    case Op::CampoDireto: dst(); out << "campo " << in.aux << " "; args(); break;
                    case Op::GravaCampoDireto: out << "grava.campo " << in.aux << " "; args(); break;
                    case Op::ChamaIface:
                        if (m.tabelas[in.aux].ret != Tipo::Void) dst();
                        out << "call.iface " << m.tabelas[in.aux].nome << "("; args(); out << ")";
                        break;
                    case Op::Retem:    out << "retem "; args(); break;
                    case Op::Solta:    out << "solta "; args(); break;
                    case Op::ChamaRt:
                        if (infoRt(static_cast<Rt>(in.aux)).ret != Tipo::Void) dst();
                        out << "call.rt " << infoRt(static_cast<Rt>(in.aux)).nome << "("; args(); out << ")";
                        break;
                    case Op::ConferePasso: out << "confere.passo "; args(); break;
                    case Op::PassoRange: dst(); out << "passo.range " << nomeTipo(in.tipo) << " "; args(); break;
                    case Op::Br:       out << "br " << f.blocos[in.alvo].nome; break;
                    case Op::BrC:      out << "brc "; args(); out << ", " << f.blocos[in.alvo].nome << ", "
                                           << f.blocos[in.senao].nome; break;
                    case Op::Ret:      out << "ret"; if (!in.args.empty()) { out << " "; args(); } break;
                    case Op::RetErro:  out << "ret.erro"; break;
                    case Op::Inalcancavel: out << "inalcançável"; break;
                    case Op::EhNulo:   dst(); out << "eh.null "; args(); break;
                    case Op::Matematica: dst(); out << "mat " << in.aux << " "; args(); break;
                    case Op::Finito:   dst(); out << "finito "; args(); break;
                    case Op::TamLista: dst(); out << "tam.lista "; args(); break;
                    case Op::ElemLista: dst(); out << "elem.lista "; args(); break;
                    case Op::GravaElemLista: out << "grava.elem.lista "; args(); break;
                    case Op::CopiaValor: dst(); out << "copia.valor "; args(); break;
                    case Op::TomaElemLista: dst(); out << "toma.elem.lista "; args(); break;
                }
                if (podeFalhar(in)) out << "   ! " << f.blocos[in.erro].nome;
                if (in.pos.line) out << "   ; " << in.pos.line << ":" << in.pos.column;
                out << "\n";
            }
        }
        out << "\n";
    }
    return out.str();
}

}  // namespace cinza::cir
