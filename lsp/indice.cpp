#include "indice.h"
#include "../ast.h"
#include "../natives.h"
#include "../plataforma.h"
#include "../source_files.h"
#include "../types.h"
#include <algorithm>
#include <climits>
#include <filesystem>

namespace cinza::lsp {

namespace {

// os CompletionItemKind do LSP
constexpr int K_METODO = 2, K_FUNCAO = 3, K_CAMPO = 5, K_VARIAVEL = 6, K_CLASSE = 7, K_INTERFACE = 8,
              K_MODULO = 9, K_PALAVRA = 14, K_ENUM_VALOR = 20, K_CONSTANTE = 21, K_STRUCT = 22, K_ENUM = 13;

bool palavra(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

std::string mostra(TypeRef t) { return t ? displayName(t->str()) : std::string(); }

// O nome como está escrito no código: sem o prefixo do módulo (util::f) nem os tipos de uma cópia
// de genérico (maior[i32], Par[string, i32])
std::string escrito(const std::string& nome) {
    std::string s = nome.substr(nome.rfind(':') == std::string::npos ? 0 : nome.rfind(':') + 1);
    return s.substr(0, s.find('['));
}
std::string mostra(const TypePtr& t) { return t ? displayName(t->toString()) : std::string(); }

std::string assinatura(const std::string& nome, const std::vector<Parameter>& ps, const TypePtr& ret) {
    std::string s = "fn " + nome + "(";
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (i) s += ", ";
        s += mostra(ps[i].type) + " " + ps[i].name;
    }
    s += ")";
    const std::string r = mostra(ret);
    if (!r.empty() && r != "void") s += " -> " + r;
    return s;
}

std::string assinaturaNativa(const std::string& nome, const NativeFn& f) {
    std::string s = "fn " + nome + "(";
    for (std::size_t i = 0; i < f.params.size(); ++i) {
        if (i) s += ", ";
        s += mostra(f.params[i]);
        if (f.variadic && i + 1 == f.params.size()) s += "...";
    }
    s += ")";
    const std::string r = mostra(f.ret);
    if (!r.empty() && r != "void") s += " -> " + r;
    return s;
}

class Construtor {
public:
    Construtor(Indice& ix, const std::function<const std::vector<std::string>&(int)>& linhas)
        : ix_(ix), linhas_(linhas) {}

    void registra(Program& p) {
        auto guarda = [&](Stmt* s) {
            switch (s->node_kind) {
                case NodeKind::FunctionDecl: decls_[displayName(static_cast<FunctionDecl*>(s)->name)] = s; break;
                case NodeKind::ClassDecl: decls_[displayName(static_cast<ClassDecl*>(s)->class_name)] = s; break;
                case NodeKind::StructDecl: decls_[displayName(static_cast<StructDecl*>(s)->name)] = s; break;
                case NodeKind::EnumDecl: decls_[displayName(static_cast<EnumDecl*>(s)->name)] = s; break;
                case NodeKind::ErrorDecl: decls_[displayName(static_cast<ErrorDecl*>(s)->name)] = s; break;
                case NodeKind::InterfaceDecl: decls_[displayName(static_cast<InterfaceDecl*>(s)->name)] = s; break;
                case NodeKind::VarDecl: decls_[displayName(static_cast<VarDeclStmt*>(s)->name)] = s; break;
                default: break;
            }
        };
        for (auto& s : p.statements) guarda(s.get());
        for (auto& s : p.instancias) guarda(s.get());
    }

    void percorre(Program& p) {
        for (auto& s : p.statements) {
            if (s->token.line > 0) inicios_[canonicoDe(s->token.file_id)].push_back(s->token.line);
            stmt(s.get());
        }
        for (auto& s : p.instancias) stmt(s.get());
    }

    // as locais valem até o começo da declaração seguinte do nível de cima
    void fecha() {
        for (auto& [arq, v] : inicios_) std::sort(v.begin(), v.end());
        for (Local& l : ix_.locais) {
            const auto& v = inicios_[l.arquivo];
            auto it = std::upper_bound(v.begin(), v.end(), l.fim);
            l.fim = it == v.end() ? INT_MAX : *it - 1;
        }
    }

    void membros(Program& p) {
        auto add = [&](const std::string& tipo, Item it) { ix_.tipos[tipo].push_back(std::move(it)); };
        auto tudo = [&](std::vector<StmtPtr>& v) {
            for (auto& s : v) {
                if (s->node_kind == NodeKind::ClassDecl) {
                    auto* c = static_cast<ClassDecl*>(s.get());
                    const std::string n = displayName(c->class_name);
                    for (auto& m : c->pub_methods) {
                        auto* f = static_cast<FunctionDecl*>(m.get());
                        add(n, {f->name, K_METODO, assinatura(f->name, f->parameters, f->return_type)});
                    }
                } else if (s->node_kind == NodeKind::StructDecl) {
                    auto* st = static_cast<StructDecl*>(s.get());
                    for (auto& f : st->fields) add(displayName(st->name), {f.name, K_CAMPO, mostra(f.type) + " " + f.name});
                } else if (s->node_kind == NodeKind::InterfaceDecl) {
                    auto* i = static_cast<InterfaceDecl*>(s.get());
                    for (auto& m : i->methods)
                        add(displayName(i->name), {m.name, K_METODO, assinatura(m.name, m.parameters, m.return_type)});
                } else if (s->node_kind == NodeKind::EnumDecl) {
                    auto* e = static_cast<EnumDecl*>(s.get());
                    for (auto& v : e->members) add(displayName(e->name), {v, K_ENUM_VALOR, displayName(e->name) + "." + v});
                }
            }
        };
        tudo(p.statements);
        tudo(p.instancias);
    }

private:
    Indice&                                                   ix_;
    const std::function<const std::vector<std::string>&(int)>& linhas_;
    std::map<std::string, Stmt*>                              decls_;
    std::map<std::string, std::vector<int>>                   inicios_;
    std::map<int, std::string>                                canonicos_;

    struct Decl {
        std::string   nome;
        std::uint32_t slot;
        Token         tok;
        std::string   info, tipo;
    };
    std::vector<std::vector<Decl>> escopos_;
    const ClassDecl*               classe_ = nullptr;

    const std::string& canonicoDe(int file_id) {
        auto it = canonicos_.find(file_id);
        if (it != canonicos_.end()) return it->second;
        return canonicos_[file_id] = canonico(fileName(file_id));
    }

    // A coluna (em bytes, a partir de 1) do nome na linha do token, a partir dele
    int coluna(const Token& t, const std::string& nome) {
        const auto& ls = linhas_(t.file_id);
        if (t.line < 1 || t.line > static_cast<int>(ls.size()) || nome.empty()) return t.column;
        const std::string& l = ls[static_cast<std::size_t>(t.line - 1)];
        std::size_t p = l.find(nome, t.column > 0 ? static_cast<std::size_t>(t.column - 1) : 0);
        while (p != std::string::npos) {
            const bool antes = p == 0 || !palavra(static_cast<unsigned char>(l[p - 1]));
            const bool depois = p + nome.size() >= l.size() || !palavra(static_cast<unsigned char>(l[p + nome.size()]));
            if (antes && depois) return static_cast<int>(p) + 1;
            p = l.find(nome, p + 1);
        }
        return t.column;
    }

    // Uma ocorrência do nome na linha do token; `def` é o token da declaração (nulo: sem definição)
    void ocorre(const Token& t, const std::string& nome, std::string info, std::string tipo, const Token* def,
                const std::string& nome_def) {
        if (t.line <= 0 || nome.empty()) return;
        Ocorrencia o;
        o.arquivo = canonicoDe(t.file_id);
        o.linha = t.line;
        o.coluna = coluna(t, nome);
        o.tamanho = static_cast<int>(nome.size());
        o.nome = nome;
        o.info = std::move(info);
        o.tipo = std::move(tipo);
        if (def && def->line > 0) {
            o.def_arquivo = canonicoDe(def->file_id);
            o.def_linha = def->line;
            o.def_coluna = coluna(*def, nome_def);
            o.def_tamanho = static_cast<int>(nome_def.size());
        }
        ix_.ocorrencias.push_back(std::move(o));
    }

    void declara(const std::string& nome, std::uint32_t slot, const Token& tok, const std::string& tipo, int kind) {
        Decl d{nome, slot, tok, tipo + " " + nome, tipo};
        ocorre(tok, nome, d.info, tipo, &tok, nome);
        if (!escopos_.empty()) escopos_.back().push_back(d);
        Local l;
        l.arquivo = canonicoDe(tok.file_id);
        l.linha = tok.line;
        l.coluna = coluna(tok, nome);
        l.fim = tok.line;
        l.item = {nome, kind, d.info};
        ix_.locais.push_back(std::move(l));
    }

    const Decl* local(const std::string& nome, std::uint32_t slot) const {
        for (auto e = escopos_.rbegin(); e != escopos_.rend(); ++e)
            for (auto d = e->rbegin(); d != e->rend(); ++d)
                if (d->nome == nome && d->slot == slot) return &*d;
        for (auto e = escopos_.rbegin(); e != escopos_.rend(); ++e)   // o slot pode ter sido reaproveitado
            for (auto d = e->rbegin(); d != e->rend(); ++d)
                if (d->nome == nome) return &*d;
        return nullptr;
    }

    Stmt* decl(const std::string& nome) {
        auto it = decls_.find(displayName(nome));
        return it == decls_.end() ? nullptr : it->second;
    }

    // A declaração de um tipo (o token e o texto do "passar o mouse")
    bool infoTipo(Stmt* s, const Token*& tok, std::string& nome, std::string& info) {
        if (!s) return false;
        switch (s->node_kind) {
            case NodeKind::ClassDecl: nome = displayName(static_cast<ClassDecl*>(s)->class_name); info = "class " + nome; break;
            case NodeKind::StructDecl: nome = displayName(static_cast<StructDecl*>(s)->name); info = "struct " + nome; break;
            case NodeKind::ErrorDecl: nome = displayName(static_cast<ErrorDecl*>(s)->name); info = "error " + nome; break;
            case NodeKind::InterfaceDecl: nome = displayName(static_cast<InterfaceDecl*>(s)->name); info = "interface " + nome; break;
            case NodeKind::EnumDecl: {
                auto* e = static_cast<EnumDecl*>(s);
                nome = displayName(e->name);
                info = "enum " + nome + " { ";
                for (std::size_t i = 0; i < e->members.size(); ++i) info += (i ? ", " : "") + e->members[i];
                info += " }";
                break;
            }
            default: return false;
        }
        tok = &s->token;
        return true;
    }

    void funcao(FunctionDecl* f, const std::string& dono) {
        const std::string nome_visto = dono.empty() ? displayName(f->name) : dono + "." + f->name;
        // uma cópia de genérico (maior[i32]) tem os tokens da original: a declaração já foi indexada
        if (f->name.find('[') == std::string::npos)
            ocorre(f->token, escrito(f->name), assinatura(nome_visto, f->parameters, f->return_type), "", &f->token,
                   escrito(f->name));
        if (!f->type_params.empty() || !f->body) return;   // a original de um genérico: o corpo só nas cópias
        escopos_.emplace_back();
        for (std::size_t i = 0; i < f->parameters.size(); ++i) {
            const Parameter& p = f->parameters[i];
            declara(p.name, static_cast<std::uint32_t>(i), p.token, mostra(p.type), K_VARIAVEL);
        }
        corpo(f->body.get());
        escopos_.pop_back();
    }

    // o corpo de uma função: o bloco sem abrir outro escopo (os parâmetros estão no de fora)
    void corpo(Stmt* s) {
        if (s && s->node_kind == NodeKind::Block)
            for (auto& x : static_cast<BlockStmt*>(s)->statements) stmt(x.get());
        else
            stmt(s);
    }

    void stmt(Stmt* s) {
        if (!s) return;
        switch (s->node_kind) {
            case NodeKind::FunctionDecl: funcao(static_cast<FunctionDecl*>(s), ""); break;
            case NodeKind::ClassDecl: {
                auto* c = static_cast<ClassDecl*>(s);
                const std::string n = displayName(c->class_name);
                ocorre(c->token, n, "class " + n, "", &c->token, n);
                const ClassDecl* antes = classe_;
                classe_ = c;
                for (auto& f : c->fields) {
                    ocorre(f.token, f.name, mostra(f.type) + " " + f.name, mostra(f.type), &f.token, f.name);
                    expr(f.initializer.get());
                }
                if (!c->type_params.empty()) break;   // Q1a: a genérica — o índice vem das cópias
                if (c->constructor && c->constructor->corpo_analisado) {
                    escopos_.emplace_back();
                    for (std::size_t i = 0; i < c->constructor->parameters.size(); ++i) {
                        const Parameter& p = c->constructor->parameters[i];
                        declara(p.name, static_cast<std::uint32_t>(i), p.token, mostra(p.type), K_VARIAVEL);
                    }
                    corpo(c->constructor->body.get());
                    escopos_.pop_back();
                }
                for (auto* lista : {&c->priv_methods, &c->pub_methods})
                    for (auto& m : *lista)
                        if (static_cast<FunctionDecl*>(m.get())->corpo_analisado) funcao(static_cast<FunctionDecl*>(m.get()), n);
                classe_ = antes;
                break;
            }
            case NodeKind::StructDecl: {
                auto* st = static_cast<StructDecl*>(s);
                const std::string n = displayName(st->name);
                ocorre(st->token, n, "struct " + n, "", &st->token, n);
                for (auto& f : st->fields) {
                    ocorre(f.token, f.name, mostra(f.type) + " " + f.name, mostra(f.type), &f.token, f.name);
                    expr(f.initializer.get());
                }
                break;
            }
            case NodeKind::EnumDecl: {
                auto* e = static_cast<EnumDecl*>(s);
                const Token* t = nullptr;
                std::string n, info;
                infoTipo(s, t, n, info);
                ocorre(e->token, n, info, "", &e->token, n);
                for (std::size_t i = 0; i < e->member_tokens.size(); ++i)
                    ocorre(e->member_tokens[i], e->members[i], n + "." + e->members[i], n, &e->member_tokens[i], e->members[i]);
                break;
            }
            case NodeKind::ErrorDecl: case NodeKind::InterfaceDecl: {
                const Token* t = nullptr;
                std::string n, info;
                if (infoTipo(s, t, n, info)) ocorre(s->token, n, info, "", &s->token, n);
                break;
            }
            case NodeKind::VarDecl: {
                auto* v = static_cast<VarDeclStmt*>(s);
                expr(v->initializer.get());
                std::string tipo = mostra(v->type);
                if ((tipo.empty() || tipo == "var") && v->initializer) tipo = mostra(v->initializer->resolved_type);
                if (escopos_.empty()) {
                    ocorre(v->token, v->name, "const " + tipo + " " + v->name, tipo, &v->token, v->name);
                } else {
                    declara(v->name, v->res.slot, v->token, tipo, v->is_const ? K_CONSTANTE : K_VARIAVEL);
                }
                break;
            }
            case NodeKind::Assign: {
                auto* a = static_cast<AssignStmt*>(s);
                expr(a->target.get());
                expr(a->value.get());
                break;
            }
            case NodeKind::ExprStmt: expr(static_cast<ExprStmt*>(s)->expression.get()); break;
            case NodeKind::Block: {
                escopos_.emplace_back();
                for (auto& x : static_cast<BlockStmt*>(s)->statements) stmt(x.get());
                escopos_.pop_back();
                break;
            }
            case NodeKind::If: {
                auto* i = static_cast<IfStmt*>(s);
                expr(i->condition.get());
                if (i->decl_type) {   // Q1b: if (T nome = opcional): nome só no primeiro bloco
                    escopos_.emplace_back();
                    declara(i->decl_name, i->decl_slot, i->decl_token, mostra(i->decl_resolved), K_VARIAVEL);
                    stmt(i->then_branch.get());
                    escopos_.pop_back();
                } else {
                    stmt(i->then_branch.get());
                }
                stmt(i->else_branch.get());
                break;
            }
            case NodeKind::While: {
                auto* w = static_cast<WhileStmt*>(s);
                expr(w->condition.get());
                if (w->decl_type) {
                    escopos_.emplace_back();
                    declara(w->decl_name, w->decl_slot, w->decl_token, mostra(w->decl_resolved), K_VARIAVEL);
                    stmt(w->body.get());
                    escopos_.pop_back();
                } else {
                    stmt(w->body.get());
                }
                break;
            }
            case NodeKind::For: {
                auto* f = static_cast<ForStmt*>(s);
                expr(f->iterable.get());
                escopos_.emplace_back();
                Token t = f->token;
                std::string tipo = mostra(f->type_iterator);
                declara(f->iterator_name, f->iter_slot, t, tipo, K_VARIAVEL);
                stmt(f->body.get());
                escopos_.pop_back();
                break;
            }
            case NodeKind::Try: {
                auto* t = static_cast<TryStmt*>(s);
                stmt(t->body.get());
                for (auto& h : t->handlers) {
                    escopos_.emplace_back();
                    declara(h.var_name, h.var_slot, h.token, displayName(h.type_name), K_VARIAVEL);
                    stmt(h.body.get());
                    escopos_.pop_back();
                }
                stmt(t->finally_block.get());
                break;
            }
            case NodeKind::Throw: expr(static_cast<ThrowStmt*>(s)->value.get()); break;
            case NodeKind::Return: expr(static_cast<ReturnStmt*>(s)->value.get()); break;
            case NodeKind::Yield: expr(static_cast<YieldStmt*>(s)->value.get()); break;   // Q1b
            default: break;
        }
    }

    void expr(Expr* e) {
        if (!e) return;
        switch (e->node_kind) {
            case NodeKind::Identifier: {
                auto* id = static_cast<IdentifierExpr*>(e);
                const std::string tipo = mostra(e->resolved_type);
                switch (id->res.kind) {
                    case Resolution::Kind::Local: {
                        const Decl* d = local(id->name, id->res.slot);
                        ocorre(id->token, id->name, d ? d->info : tipo + " " + id->name, tipo, d ? &d->tok : nullptr, id->name);
                        break;
                    }
                    case Resolution::Kind::Field: {
                        const ClassDecl::Field* f = classe_ && id->res.slot < classe_->fields.size() ? &classe_->fields[id->res.slot] : nullptr;
                        ocorre(id->token, id->name, tipo + " " + id->name, tipo, f ? &f->token : nullptr, id->name);
                        break;
                    }
                    case Resolution::Kind::Global: {
                        Stmt* g = decl(id->name);
                        ocorre(id->token, id->name, "const " + tipo + " " + id->name, tipo, g ? &g->token : nullptr, id->name);
                        break;
                    }
                    case Resolution::Kind::Type: {
                        const Token* t = nullptr;
                        std::string n, info;
                        if (infoTipo(decl(id->type_value ? id->type_value->str() : id->name), t, n, info))
                            ocorre(id->token, id->name, info, "", t, n);
                        break;
                    }
                    case Resolution::Kind::Self:
                        ocorre(id->token, id->name, tipo + " self", tipo, nullptr, "");
                        break;
                    default: break;
                }
                break;
            }
            case NodeKind::Call: {
                auto* c = static_cast<CallExpr*>(e);
                std::string visto = c->function_name;
                if (auto p = visto.find("::"); p != std::string::npos) visto.replace(p, 2, ".");
                visto = displayName(visto);
                const std::string so_nome = visto.substr(visto.rfind('.') == std::string::npos ? 0 : visto.rfind('.') + 1);
                if (c->target) {
                    const FunctionDecl* f = c->target;
                    ocorre(c->token, escrito(so_nome), assinatura(visto, f->parameters, f->return_type),
                           mostra(e->resolved_type), &f->token, escrito(f->name));
                } else if (c->native) {
                    ocorre(c->token, escrito(so_nome), assinaturaNativa(visto, *c->native), mostra(e->resolved_type), nullptr, "");
                }
                for (auto& a : c->arguments) expr(a.get());
                for (auto& p : c->interp) expr(p.expr.get());
                break;
            }
            case NodeKind::MemberAccess: {
                auto* m = static_cast<MemberAccessExpr*>(e);
                expr(m->object.get());
                if (m->enum_decl) {
                    const auto& toks = m->enum_decl->member_tokens;
                    const Token* t = m->field_index >= 0 && m->field_index < static_cast<int>(toks.size()) ? &toks[static_cast<std::size_t>(m->field_index)] : nullptr;
                    ocorre(m->token, m->member_name, displayName(m->enum_decl->name) + "." + m->member_name,
                           mostra(e->resolved_type), t, m->member_name);
                    break;
                }
                const Token* def = nullptr;
                if (m->object && m->object->resolved_type && m->field_index >= 0) {
                    Stmt* d = decl(m->object->resolved_type->str());
                    const std::vector<ClassDecl::Field>* campos = nullptr;
                    if (d && d->node_kind == NodeKind::ClassDecl) campos = &static_cast<ClassDecl*>(d)->fields;
                    if (d && d->node_kind == NodeKind::StructDecl) campos = &static_cast<StructDecl*>(d)->fields;
                    if (campos && m->field_index < static_cast<int>(campos->size()))
                        def = &(*campos)[static_cast<std::size_t>(m->field_index)].token;
                }
                ocorre(m->token, m->member_name, mostra(e->resolved_type) + " " + m->member_name, mostra(e->resolved_type),
                       def, m->member_name);
                break;
            }
            case NodeKind::MethodCall: {
                auto* m = static_cast<MethodCallExpr*>(e);
                expr(m->object.get());
                const std::string dono = m->object ? mostra(m->object->resolved_type) : "";
                if (m->target) {
                    ocorre(m->token, m->method_name, assinatura(dono + "." + m->method_name, m->target->parameters, m->target->return_type),
                           mostra(e->resolved_type), &m->target->token, m->method_name);
                } else if (m->iface && m->iface_method >= 0 && m->iface_method < static_cast<int>(m->iface->methods.size())) {
                    const auto& mt = m->iface->methods[static_cast<std::size_t>(m->iface_method)];
                    ocorre(m->token, m->method_name, assinatura(dono + "." + m->method_name, mt.parameters, mt.return_type),
                           mostra(e->resolved_type), &mt.token, m->method_name);
                } else {
                    ocorre(m->token, m->method_name, "fn " + dono + "." + m->method_name + "(...) -> " + mostra(e->resolved_type),
                           mostra(e->resolved_type), nullptr, "");
                }
                for (auto& a : m->arguments) expr(a.get());
                break;
            }
            case NodeKind::New: {
                auto* n = static_cast<NewExpr*>(e);
                const Token* t = nullptr;
                std::string nome, info;
                if (infoTipo(decl(e->resolved_type ? e->resolved_type->str() : n->class_name), t, nome, info)) {
                    const std::string escrito = displayName(n->class_name).substr(0, displayName(n->class_name).find('['));
                    ocorre(n->token, escrito, info, nome, t, nome.substr(0, nome.find('[')));
                }
                for (auto& a : n->arguments) expr(a.get());
                break;
            }
            case NodeKind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e);
                expr(b->left.get());
                expr(b->right.get());
                break;
            }
            case NodeKind::Unary: expr(static_cast<UnaryExpr*>(e)->operand.get()); break;
            case NodeKind::ListLiteral:
                for (auto& x : static_cast<ListLiteralExpr*>(e)->elements) expr(x.get());
                break;
            case NodeKind::DictLiteral:
                for (auto& [k, v] : static_cast<DictLiteralExpr*>(e)->pairs) { expr(k.get()); expr(v.get()); }
                break;
            case NodeKind::PairLiteral: {
                auto* p = static_cast<PairLiteralExpr*>(e);
                expr(p->first.get());
                expr(p->second.get());
                break;
            }
            case NodeKind::IndexAccess: {
                auto* i = static_cast<IndexAccessExpr*>(e);
                expr(i->object.get());
                expr(i->index.get());
                break;
            }
            case NodeKind::Slice: {
                auto* s = static_cast<SliceExpr*>(e);
                expr(s->object.get());
                expr(s->start.get());
                expr(s->end.get());
                expr(s->step.get());
                break;
            }
            case NodeKind::Cast: expr(static_cast<CastExpr*>(e)->operand.get()); break;
            case NodeKind::Else: {   // Q1b
                auto* el = static_cast<ElseExpr*>(e);
                expr(el->value.get());
                expr(el->alternative.get());
                if (el->block) stmt(el->block.get());
                break;
            }
            default: break;
        }
    }
};

const char* const PALAVRAS[] = {"fn", "return", "if", "else", "while", "for", "in", "struct", "enum", "interface",
                                "class", "pub", "new", "const", "break", "continue", "try", "except", "finally",
                                "throw", "error", "import", "as", "self", "cast", "true", "false", "null", "yield", "module", "and", "or",
                                "not", "i32", "i64", "f32", "f64", "string", "bool", "void", "list", "dict", "pair"};

} // namespace

std::string canonico(const std::string& caminho) {
    std::error_code ec;
    const auto p = std::filesystem::weakly_canonical(caminhoUtf8(caminho), ec);
    std::string s = textoCaminho(ec ? std::filesystem::absolute(caminhoUtf8(caminho), ec) : p);
#ifdef _WIN32
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));   // sem diferença de caixa
#endif
    return s;
}

Indice constroi(std::vector<Module>& modulos, const std::function<const std::vector<std::string>&(int)>& linhas) {
    Indice ix;
    Construtor c(ix, linhas);
    for (auto& m : modulos)
        if (m.program && !m.native) c.registra(*m.program);
    for (auto& m : modulos)
        if (m.program && !m.native) {
            c.percorre(*m.program);
            c.membros(*m.program);
        }
    c.fecha();

    if (modulos.empty()) return ix;
    Module& principal = modulos.back();
    ix.principal = canonico(principal.display);
    // o nível de cima do principal, os apelidos dos imports e os embutidos
    if (principal.program)
        for (auto& s : principal.program->statements) {
            switch (s->node_kind) {
                case NodeKind::FunctionDecl: {
                    auto* f = static_cast<FunctionDecl*>(s.get());
                    ix.globais.push_back({displayName(f->name), K_FUNCAO, assinatura(displayName(f->name), f->parameters, f->return_type)});
                    break;
                }
                case NodeKind::ClassDecl: ix.globais.push_back({displayName(static_cast<ClassDecl*>(s.get())->class_name), K_CLASSE, "class"}); break;
                case NodeKind::StructDecl: ix.globais.push_back({displayName(static_cast<StructDecl*>(s.get())->name), K_STRUCT, "struct"}); break;
                case NodeKind::EnumDecl: ix.globais.push_back({displayName(static_cast<EnumDecl*>(s.get())->name), K_ENUM, "enum"}); break;
                case NodeKind::InterfaceDecl: ix.globais.push_back({displayName(static_cast<InterfaceDecl*>(s.get())->name), K_INTERFACE, "interface"}); break;
                case NodeKind::ErrorDecl: ix.globais.push_back({displayName(static_cast<ErrorDecl*>(s.get())->name), K_CLASSE, "error"}); break;
                case NodeKind::VarDecl: {
                    auto* v = static_cast<VarDeclStmt*>(s.get());
                    ix.globais.push_back({displayName(v->name), K_CONSTANTE, "const " + displayName(v->name)});
                    break;
                }
                default: break;
            }
        }
    for (const char* n : {"print", "printf", "format", "input", "range", "exit", "type"})
        ix.globais.push_back({n, K_FUNCAO, "embutida"});
    for (const char* p : PALAVRAS) ix.globais.push_back({p, K_PALAVRA, ""});
    for (const auto& [apelido, idx] : principal.imports) {
        ix.globais.push_back({apelido, K_MODULO, "import"});
        Module& m = modulos[idx];
        std::vector<Item>& itens = ix.modulos[apelido];
        if (m.native) {
            for (const NativeFn& f : m.native->fns) itens.push_back({f.name, K_FUNCAO, assinaturaNativa(m.native->name + "." + f.name, f)});
            for (const NativeConst& k : m.native->consts) itens.push_back({k.name, K_CONSTANTE, mostra(k.type) + " " + k.name});
        } else if (m.program) {
            for (auto& s : m.program->statements) {
                if (!s->is_pub) continue;
                switch (s->node_kind) {
                    case NodeKind::FunctionDecl: {
                        auto* f = static_cast<FunctionDecl*>(s.get());
                        const std::string n = f->name.substr(f->name.rfind(':') == std::string::npos ? 0 : f->name.rfind(':') + 1);
                        itens.push_back({n, K_FUNCAO, assinatura(apelido + "." + n, f->parameters, f->return_type)});
                        break;
                    }
                    case NodeKind::ClassDecl: itens.push_back({displayName(static_cast<ClassDecl*>(s.get())->class_name), K_CLASSE, "class"}); break;
                    case NodeKind::StructDecl: itens.push_back({displayName(static_cast<StructDecl*>(s.get())->name), K_STRUCT, "struct"}); break;
                    case NodeKind::EnumDecl: itens.push_back({displayName(static_cast<EnumDecl*>(s.get())->name), K_ENUM, "enum"}); break;
                    case NodeKind::InterfaceDecl: itens.push_back({displayName(static_cast<InterfaceDecl*>(s.get())->name), K_INTERFACE, "interface"}); break;
                    case NodeKind::ErrorDecl: itens.push_back({displayName(static_cast<ErrorDecl*>(s.get())->name), K_CLASSE, "error"}); break;
                    case NodeKind::VarDecl: itens.push_back({displayName(static_cast<VarDeclStmt*>(s.get())->name), K_CONSTANTE, "const"}); break;
                    default: break;
                }
            }
        }
    }
    return ix;
}

} // namespace cinza::lsp
