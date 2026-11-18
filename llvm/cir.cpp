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

}  // namespace

std::string texto(const Modulo& m) {
    std::ostringstream out;
    for (std::size_t i = 0; i < m.nativas.size(); ++i)
        out << "nativa N" << i << " = " << (m.nativas[i].modulo.empty() ? "" : m.nativas[i].modulo + ".")
            << m.nativas[i].nome << "\n";
    if (!m.nativas.empty()) out << "\n";
    for (const Funcao& f : m.funcoes) {
        out << "fn " << f.nome << "() -> " << nomeTipo(f.ret) << "\n";
        for (std::size_t t = 0; t < f.temps.size(); ++t)
            out << "  slot t" << t << ": " << nomeTipo(f.temps[t]) << "\n";
        for (const Bloco& b : f.blocos) {
            out << b.nome << ":\n";
            for (const Instr& in : b.instr) {
                out << "  ";
                switch (in.op) {
                    case Op::Embrulha:
                        out << "t" << in.dst << " = embrulha " << operando(m, in.args[0]);
                        break;
                    case Op::ChamaNativa:
                        out << "t" << in.dst << " = call.nativa N" << in.nativa << "(";
                        for (std::size_t a = 0; a < in.args.size(); ++a)
                            out << (a ? ", " : "") << operando(m, in.args[a]);
                        out << ")";
                        break;
                    case Op::Solta:
                        out << "solta " << operando(m, in.args[0]);
                        break;
                    case Op::Ret:
                        out << "ret";
                        break;
                }
                out << "\n";
            }
        }
        out << "\n";
    }
    return out.str();
}

}  // namespace cinza::cir
