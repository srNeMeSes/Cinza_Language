#include "cir.h"
#include <sstream>

namespace cinza::cir {

std::uint32_t Modulo::literal(const std::string& s) {
    for (std::size_t i = 0; i < literais.size(); ++i)
        if (literais[i] == s) return static_cast<std::uint32_t>(i);
    literais.push_back(s);
    return static_cast<std::uint32_t>(literais.size() - 1);
}

std::uint32_t Modulo::nativa(const std::string& modulo, const std::string& nome) {
    for (std::size_t i = 0; i < nativas.size(); ++i)
        if (nativas[i].modulo == modulo && nativas[i].nome == nome) return static_cast<std::uint32_t>(i);
    nativas.push_back({modulo, nome});
    return static_cast<std::uint32_t>(nativas.size() - 1);
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
                    case Op::Solta:    out << "solta "; args(); break;
                    case Op::ConferePasso: out << "confere.passo "; args(); break;
                    case Op::PassoRange: dst(); out << "passo.range " << nomeTipo(in.tipo) << " "; args(); break;
                    case Op::Br:       out << "br " << f.blocos[in.alvo].nome; break;
                    case Op::BrC:      out << "brc "; args(); out << ", " << f.blocos[in.alvo].nome << ", "
                                           << f.blocos[in.senao].nome; break;
                    case Op::Ret:      out << "ret"; if (!in.args.empty()) { out << " "; args(); } break;
                    case Op::Inalcancavel: out << "inalcançável"; break;
                }
                if (in.pos.line) out << "   ; " << in.pos.line << ":" << in.pos.column;
                out << "\n";
            }
        }
        out << "\n";
    }
    return out.str();
}

}  // namespace cinza::cir
