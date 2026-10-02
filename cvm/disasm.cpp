#include "bytecode.h"
#include "../operacoes.h"
#include <cstdio>
#include <sstream>

namespace cinza::cvm {

// ============================================================================
// DESMONTADOR (--bytecode): uma linha por instrução — posição, nome,
// operandos e, quando ajuda, um comentário.
// ============================================================================

const char* opName(Op op) {
    static const char* const nomes[] = {
#define CVM_NOME(n) #n,
        CVM_OPCODES(CVM_NOME)
#undef CVM_NOME
    };
    const auto i = static_cast<std::size_t>(op);
    return i < static_cast<std::size_t>(Op::COUNT) ? nomes[i] : "?";
}

static std::string pos4(std::size_t n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04zu", n);
    return buf;
}

static std::string constante(const Value& v) {
    if (v.kind() == Value::Kind::STRING) return "\"" + v.asString() + "\"";
    return v.toString();
}

std::string disassemble(const Image& img) {
    std::ostringstream out;
    for (std::size_t pi = 0; pi < img.protos.size(); ++pi) {
        const Proto& p = img.protos[pi];
        out << (p.hidden() ? "<inicialização>" : "fn " + p.name)
            << "  (" << p.num_params << (p.num_params == 1 ? " parâmetro, " : " parâmetros, ")
            << p.num_regs << (p.num_regs == 1 ? " registrador)" : " registradores)") << "\n";

        for (std::size_t i = 0; i < p.code.size(); ++i) {
            const Instr& in = p.code[i];
            std::string ops, nota;
            const std::string ra = "r" + std::to_string(in.a);
            const std::string rb = "r" + std::to_string(in.b);
            const std::string rc = "r" + std::to_string(in.c);
            auto alvo = [&] { return "-> " + pos4(static_cast<std::size_t>(
                                                static_cast<std::int64_t>(i) + 1 + in.bc())); };
            switch (in.op) {
                case Op::LOADK:     ops = ra + ", k" + std::to_string(in.b);
                                    nota = constante(p.consts[in.b]); break;
                case Op::LOADINT:   ops = ra + ", " + std::to_string(in.bc()); break;
                case Op::LOADBOOL:  ops = ra + (in.b ? ", true" : ", false"); break;
                case Op::LOADVOID: case Op::RET:
                                    ops = ra; break;
                case Op::RETVOID:   break;
                case Op::GETGLOBAL: case Op::SETGLOBAL:
                                    ops = ra + ", g" + std::to_string(in.b); break;
                case Op::MOVE: case Op::NEG_I: case Op::NEG_D: case Op::I2D: case Op::NOT:
                                    ops = ra + ", " + rb; break;
                case Op::JMP:       ops = alvo(); break;
                case Op::JMPIF: case Op::JMPIFNOT: case Op::FORNEXT: case Op::FORNEXT_D:
                                    ops = ra + ", " + alvo(); break;
                case Op::FORPREP:   ops = ra + ", " + rb;
                                    nota = "iterador em r" + std::to_string(in.c); break;
                case Op::CALL:
                    ops  = ra + ", " + (img.protos[in.b].hidden() ? std::string("<inicialização>") : img.protos[in.b].name) +
                           ", " + std::to_string(in.c);
                    break;
                case Op::NEWLIST: case Op::NEWDICT:
                    ops = ra + ", " + rb + ", " + std::to_string(in.c);
                    break;
                case Op::NEWOBJ:
                    ops = ra + ", " + displayName(img.classes[in.b].decl->class_name);
                    break;
                case Op::NEWSTRUCT:
                    ops = ra + ", " + displayName(img.structs[in.b]->name) + ", " + std::to_string(in.c);
                    break;
                case Op::GETFIELD:
                    ops = ra + ", " + rb + ", #" + std::to_string(in.c);
                    break;
                case Op::SETFIELD:
                    ops = ra + ", #" + std::to_string(in.b) + ", " + rc;
                    break;
                case Op::CALLIFACE: {
                    const InterfaceDecl* i = img.interfaces[in.b];
                    ops = ra + ", " + displayName(i->name) + "." + i->methods[in.c].name;
                    break;
                }
                case Op::TYPEOF: case Op::CAST:
                    ops  = ra + ", " + rb + ", k" + std::to_string(in.c);
                    nota = p.consts[in.c].toString();
                    break;
                case Op::KEEPLOCK: case Op::NEG:
                    ops = ra + ", " + rb;
                    break;
                case Op::NEWERROR:
                    ops = ra + ", " + displayName(p.consts[in.b].asString()) + ", " +
                          (in.c == NO_REG ? std::string("-") : rc);
                    break;
                case Op::ERRFIELD: {
                    static const char* const campos[] = {"kind", "message", "line", "column"};
                    ops = ra + ", " + rb + "." + campos[in.c & 3];
                    break;
                }
                case Op::THROW:
                    ops = ra;
                    break;
                case Op::GETFIRST: case Op::GETSECOND:
                    ops = ra + ", " + rb;
                    break;
                case Op::CALLBUILTIN:
                    ops = ra + ", " + std::string(builtinName(static_cast<Builtin>(in.b))) + ", " +
                          std::to_string(in.c);
                    break;
                case Op::CALLNATIVE:
                    ops  = ra + ", " + img.natives[in.b]->name + ", " + std::to_string(in.c);
                    break;
                default:            ops = ra + ", " + rb + ", " + rc; break;   // três registradores
            }
            std::string linha = "  " + pos4(i) + "  " + opName(in.op);
            linha.resize(std::max<std::size_t>(linha.size(), 19), ' ');
            linha += ops;
            if (!nota.empty()) {
                linha.resize(std::max<std::size_t>(linha.size(), 40), ' ');
                linha += "; " + nota;
            }
            out << linha << "\n";
        }
        if (!p.handlers.empty()) {
            out << "  tratadores (mais interno primeiro):\n";
            for (const Handler& h : p.handlers)
                out << "    [" << pos4(h.start) << ", " << pos4(h.end) << ")  "
                    << (h.kind.empty() ? std::string("qualquer") : displayName(h.kind))
                    << " -> " << pos4(h.target) << ", erro em r" << h.reg << "\n";
        }
        out << "\n";
    }
    return out.str();
}

} // namespace cinza::cvm
