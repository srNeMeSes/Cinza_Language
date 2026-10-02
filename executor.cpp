#include "executor.h"
#include "semantic.h"   // op<...>: TypeChecker valida operações em runtime
#include "gc.h"         // coleta de ciclos
#include "operacoes.h"   // aritmética compartilhada com a CVM
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>

namespace cinza {

// ============================================================================
// FRAME GUARD (RAII) — A1 + B2
// Toda chamada de função, método ou construtor abre um frame novo, para que
// o corpo nunca enxergue os locais de quem chamou (escopo léxico). O frame tem
// os slots contados pelo semântico; o destrutor o fecha mesmo com exceção.
// ============================================================================
struct FrameGuard {
    Executor& ex;
    Value*    saved;
    FrameGuard(Executor& e, std::uint32_t num_slots) : ex(e), saved(e.locals) {
        ex.frames.emplace_back(num_slots);
        ex.locals = ex.frames.back().data();
    }
    // Frame já montado (argumentos nos primeiros slots, evalArgs)
    FrameGuard(Executor& e, std::vector<Value>&& slots) : ex(e), saved(e.locals) {
        ex.frames.push_back(std::move(slots));
        ex.locals = ex.frames.back().data();
    }
    ~FrameGuard() noexcept {
        ex.frames.pop_back();
        ex.locals = saved;
    }

    FrameGuard(const FrameGuard&)            = delete;
    FrameGuard& operator=(const FrameGuard&) = delete;
};

// CALL GUARD (RAII) — A6 + C4/B6
// Empilha a chamada em call_stack (usada no stack trace) e lança
// StackOverflowError ao passar do limite; o destrutor desempilha mesmo quando
// uma exceção atravessa a chamada.
struct CallGuard {
    Executor& ex;

    CallGuard(Executor& e, const std::string& nome, int call_line, const Token& tok) : ex(e) {
        // `nome` é o trace_name da declaração: vive na AST, então basta o ponteiro
        if (static_cast<int>(ex.call_stack.size()) >= ex.max_call_depth)
            ex.raise(RuntimeError("StackOverflowError",
                                  "profundidade máxima de chamadas (" +
                                  std::to_string(ex.max_call_depth) + ") excedida ao chamar '" +
                                  nome + "'", tok.line, tok.column, tok.file_id));
        ex.call_stack.push_back({&nome, call_line, tok.file_id});
    }
    ~CallGuard() noexcept { ex.call_stack.pop_back(); }

    CallGuard(const CallGuard&)            = delete;
    CallGuard& operator=(const CallGuard&) = delete;
};

// v2.00 #11: InstanceGuard — RAII para current_instance (e current_self, B5)
// Garante restauração automática dos dois mesmo com exceções.
struct InstanceGuard {
    ClassInstance*& slot;
    Value&          self_slot;
    ClassInstance*  saved;
    Value           saved_self;

    // novo_self: Value do objeto (INSTANCE) ou VOID para "fora de método"
    // Custo das chamadas: o self anterior é movido (não copiado) para o guard
    InstanceGuard(ClassInstance*& s, Value& self, const Value& novo_self)
        : slot(s), self_slot(self), saved(s), saved_self(std::move(self)) {
        slot = (novo_self.kind() == Value::Kind::INSTANCE) ? novo_self.asInstance().get()
                                                            : nullptr;
        self_slot = novo_self;
    }
    ~InstanceGuard() noexcept { slot = saved; self_slot = std::move(saved_self); }

    InstanceGuard(const InstanceGuard&)            = delete;
    InstanceGuard& operator=(const InstanceGuard&) = delete;
};

// ============================================================================
// PONTO DE ENTRADA
// ============================================================================

// Fase 2.5: pré-registro → avaliação dos const globais → chamada da main
void Executor::execute(const std::vector<const Program*>& programs, const GlobalLayout& layout,
                       const std::vector<std::string>& args) {
    // C5: os nomes dos módulos importados já vêm com prefixo do semântico,
    // então todos dividem os mesmos registros
    for (const Program* program : programs)
        preRegisterGlobals(*program);

    // B2: slots dos const globais; os de módulos nativos (Math.pi) já têm valor
    globals.assign(layout.count, Value());
    for (const auto& [slot, c] : layout.natives)
        globals[slot] = c->value;

    // O nível superior só tem declarações: executar em ordem = avaliar os const
    for (const Program* program : programs)
        for (const auto& stmt : program->statements)
            executeStmt(stmt.get());

    auto it = function_registry.find("main");
    if (it == function_registry.end())
        raise(RuntimeError("Erro interno: 'main' não encontrada "
                           "(o semântico deveria ter rejeitado o programa)"));

    // main(list<string> args): argumentos após o nome do arquivo
    std::vector<Value> frame(it->second->num_slots);
    if (!it->second->parameters.empty()) {
        std::vector<Value> elems;
        elems.reserve(args.size());
        for (const auto& a : args) elems.emplace_back(a);
        frame[0] = makeList(std::move(elems));
    }
    executeFunction(it->second, std::move(frame), 0);

    // CINZA_GC_STATS=1: coleta final e resumo (usado pelos testes da coleta)
    if (const char* e = std::getenv("CINZA_GC_STATS"); e && *e) {
        gcCollect();
        std::cout << "[gc] liberados em ciclos: " << gc_stats.liberados
                  << ", contêineres vivos: " << gc_registro.vivos << "\n";
    }
}

// ============================================================================
// PRE-REGISTRO
// Registra funções e classes antes de executar qualquer statement,
// permitindo referências para frente (forward calls).
// ============================================================================

void Executor::preRegisterGlobals(const Program& program) {
    for (const auto& stmt : program.statements) {
        if (auto fn  = dynamic_cast<const FunctionDecl*>(stmt.get()))
            function_registry[fn->name] = fn;
        if (auto cls = dynamic_cast<const ClassDecl*>(stmt.get()))
            class_registry[cls->class_name] = cls;
        if (stmt->node_kind == NodeKind::StructDecl) {   // C3
            auto* st = static_cast<const StructDecl*>(stmt.get());
            struct_registry[st->name] = st;
        }
        if (stmt->node_kind == NodeKind::ErrorDecl)      // C4
            error_kinds.insert(static_cast<const ErrorDecl*>(stmt.get())->name);
    }
    for (const auto& k : builtinErrorKinds()) error_kinds.insert(k);
}

// ============================================================================
// UTILITARIO DE ERRO
// ============================================================================

[[noreturn]] void Executor::throwRuntimeError(const std::string& msg,
                                               const Token& tok) const {
    raise(RuntimeError(msg, tok.line, tok.column, tok.file_id));
}

// C4/B6: todo erro de runtime sai por aqui e leva o stack trace do momento
[[noreturn]] void Executor::raise(RuntimeError err) const {
    if (err.trace.empty()) err.trace = snapshotTrace(err.line);
    throw err;
}

// Pilha do erro, da chamada mais interna para fora: "em f (prog.cinza:3)". A linha
// de cada chamada é onde a execução estava nela: o erro, na mais interna, e a
// chamada seguinte nas de fora. Pilhas muito fundas mostram só as pontas.
std::vector<std::string> Executor::snapshotTrace(int error_line) const {
    std::vector<std::string> trace;
    int line = error_line;
    for (size_t i = call_stack.size(); i-- > 0;) {
        trace.push_back("em " + *call_stack[i].name + " (" +
                        formatPosition({call_stack[i].file_id, line, 0}) + ")");   // B6
        line = call_stack[i].call_line;
    }
    constexpr size_t mostrar = 10;
    if (trace.size() > mostrar + 1) {
        const size_t omitidas = trace.size() - mostrar - 1;
        std::vector<std::string> curto(trace.begin(), trace.begin() + mostrar);
        curto.push_back("... (" + std::to_string(omitidas) + " chamadas omitidas)");
        curto.push_back(trace.back());
        trace = std::move(curto);
    }
    return trace;
}

// ============================================================================
// DISPATCHER DE STATEMENTS
// ============================================================================

Flow Executor::executeStmt(const Stmt* stmt) {
    if (!stmt) return Flow::Normal;

    // B3: switch no NodeKind em vez de cadeia de dynamic_cast
    switch (stmt->node_kind) {
        case NodeKind::VarDecl:         executeVarDecl(static_cast<const VarDeclStmt*>(stmt));                 return Flow::Normal;
        case NodeKind::Assign:          executeAssign(static_cast<const AssignStmt*>(stmt));                   return Flow::Normal;  // B5
        case NodeKind::ExprStmt:        executeExprStmt(static_cast<const ExprStmt*>(stmt));                   return Flow::Normal;
        case NodeKind::Block:           return executeBlock (static_cast<const BlockStmt*>(stmt));
        case NodeKind::If:              return executeIf    (static_cast<const IfStmt*>(stmt));
        case NodeKind::While:           return executeWhile (static_cast<const WhileStmt*>(stmt));
        case NodeKind::For:             return executeFor   (static_cast<const ForStmt*>(stmt));
        case NodeKind::Return:          return executeReturn(static_cast<const ReturnStmt*>(stmt));
        case NodeKind::Break:           return Flow::Break;      // C2: o laço consome
        case NodeKind::Continue:        return Flow::Continue;
        case NodeKind::Try:             return executeTry(static_cast<const TryStmt*>(stmt));   // C4
        case NodeKind::Throw:           executeThrow(static_cast<const ThrowStmt*>(stmt));      // não retorna
                                        return Flow::Normal;
        case NodeKind::ErrorDecl:       return Flow::Normal;     // registrado em preRegisterGlobals
        case NodeKind::EnumDecl:        return Flow::Normal;     // só declara nomes
        case NodeKind::InterfaceDecl:   return Flow::Normal;     // só declara assinaturas
        case NodeKind::FunctionDecl:
        case NodeKind::ClassDecl:
        case NodeKind::StructDecl:      return Flow::Normal;   // registradas em preRegisterGlobals
        default:                        break;
    }
    raise(RuntimeError("Tipo de statement não reconhecido pelo executor"));
}

// ============================================================================
// STATEMENTS
// ============================================================================

// Bloco { stmt* }
// B4: para no primeiro statement que não termine em Flow::Normal e o devolve.
// ScopeGuard garante popScope() também quando um RuntimeError atravessa o bloco.
Flow Executor::executeBlock(const BlockStmt* block) {
    for (const auto& s : block->statements) {
        Flow flow = executeStmt(s.get());
        if (flow != Flow::Normal) return flow;
    }
    return Flow::Normal;
}

// Declaracao de variavel:  tipo nome [= expr];
void Executor::executeVarDecl(const VarDeclStmt* stmt) {
    Value val;

    if (stmt->initializer) {
        val = evalExpr(stmt->initializer.get());
    } else {
        // Sem inicializador: somente list<T> e dict<K,V> chegam aqui.
        // O parser garante que qualquer outro tipo foi rejeitado antes.
        // Blindagem defensiva: protege contra inconsistências na pipeline.
        if (stmt->type->kind == Type::Kind::LIST)
            val = makeList();
        else if (stmt->type->kind == Type::Kind::DICT)
            val = makeDict();
        else
            raise(RuntimeError(
                "Erro interno: variável '" + stmt->name +
                "' sem inicializador chegou ao executor para tipo inválido. "
                "O parser/semântico deveria ter rejeitado esta declaração."));
    }

    // B2: const do nível superior vai para os globais; o resto, para o frame
    if (stmt->res.kind == Resolution::Kind::Global) globals[stmt->res.slot] = std::move(val);
    else                                            locals[stmt->res.slot]  = std::move(val);
}

// B5: atribuição com lvalue genérico, em duas etapas:
//   1. avalia, da esquerda para a direita, o que o alvo precisa (base
//      temporária e índices) e depois o valor;
//   2. percorre até o lugar e escreve, sem executar código do usuário no meio:
//      assim nenhum ponteiro fica inválido se o valor alterar a coleção.
void Executor::executeAssign(const AssignStmt* stmt) {
    PlacePath path;
    collectPlace(stmt->target.get(), path);
    Value val = evalExpr(stmt->value.get());
    Value* place = walkPlace(path);

    if (stmt->op == TokenType::OP_ASSIGN) {
        if (stmt->keep_lock) val = keepLock(*place, std::move(val));   // op<...>
        *place = std::move(val);
        return;
    }

    // C1: a op= b — o alvo foi avaliado uma única vez (collectPlace); applyBinary
    // não executa código do usuário, então `place` continua válido até a escrita
    Token op_tok  = stmt->token;
    op_tok.lexeme = tokenTypeToOperatorString(stmt->op);
    Value result  = applyBinary(compoundBaseOp(stmt->op), *place, val, op_tok);
    if (stmt->keep_lock) result = keepLock(*place, std::move(result));   // op<...>
    *place = std::move(result);
}

void Executor::collectPlace(const Expr* target, PlacePath& path) {
    switch (target->node_kind) {
        case NodeKind::Identifier:
            path.base_var = static_cast<const IdentifierExpr*>(target);
            return;
        case NodeKind::MemberAccess: {
            auto* m = static_cast<const MemberAccessExpr*>(target);
            collectPlace(m->object.get(), path);
            path.steps.push_back({true, m->member_name, m->field_index, Value(), &m->token});
            return;
        }
        case NodeKind::IndexAccess: {
            auto* ix = static_cast<const IndexAccessExpr*>(target);
            collectPlace(ix->object.get(), path);
            Value key = evalExpr(ix->index.get());
            path.steps.push_back({false, "", -1, std::move(key), &ix->token});
            return;
        }
        default:
            // Base que não é variável (ex.: f().campo): vira valor temporário.
            // list/dict/objetos são compartilhados, então a escrita chega ao original.
            path.base_var  = nullptr;
            path.base_temp = evalExpr(target);
            return;
    }
}

Value* Executor::walkPlace(PlacePath& path) {
    Value* place = &path.base_temp;
    if (path.base_var) {
        place = slotOf(path.base_var);
        if (!place)
            throwRuntimeError("Variável '" + path.base_var->name + "' não encontrada para "
                              "atribuição (não deveria ocorrer após análise semântica)",
                              path.base_var->token);
    }

    for (const PlaceStep& step : path.steps) {
        const Token& tok = *step.token;

        if (step.is_member && place->kind() == Value::Kind::STRUCT) {
            // C3: o campo vive dentro do struct guardado no próprio lugar (a
            // variável/elemento tem o seu struct, porque copiar struct clona)
            StructValue& sv = *place->asStruct();
            int i = step.index;
            if (i < 0 || i >= static_cast<int>(sv.fields.size()))
                throwRuntimeError("Campo '" + step.member + "' não encontrado no struct", tok);
            place = &sv.fields[static_cast<size_t>(i)];
        } else if (step.is_member) {
            if (place->kind() != Value::Kind::INSTANCE)
                throwRuntimeError("Atribuição a campo '" + step.member + "' em valor que não é objeto",
                                  tok);
            auto& fields = place->asInstance()->fields;
            if (step.index < 0 || step.index >= static_cast<int>(fields.size()))
                throwRuntimeError("Campo '" + step.member + "' não encontrado em '" +
                                  place->asInstance()->class_name + "'", tok);
            place = &fields[static_cast<size_t>(step.index)];
        } else if (place->kind() == Value::Kind::LIST || place->kind() == Value::Kind::DICT) {
            try {
                place = indexPlace(*place, step.key);   // CVM: regra compartilhada
            } catch (RuntimeError& err) {
                raise(RuntimeError(err.kind, err.message, tok.line, tok.column, tok.file_id));
            }
        } else {
            throwRuntimeError("Operador '[]' em tipo inválido", tok);
        }
    }
    return place;
}

// Expressao statement
void Executor::executeExprStmt(const ExprStmt* stmt) {
    evalExpr(stmt->expression.get());
}

// if (cond) then [else else_branch]
Flow Executor::executeIf(const IfStmt* stmt) {
    Value cond = evalExpr(stmt->condition.get());
    if (cond.asBool())
        return executeStmt(stmt->then_branch.get());
    if (stmt->else_branch)
        return executeStmt(stmt->else_branch.get());
    return Flow::Normal;
}

// while (cond) body
// B4: Return sobe para a função; Break encerra o laço; Continue segue para a
// próxima iteração (Break/Continue só passam a existir na Fase 4)
Flow Executor::executeWhile(const WhileStmt* stmt) {
    while (evalExpr(stmt->condition.get()).asBool()) {
        Flow flow = executeStmt(stmt->body.get());
        if (flow == Flow::Return) return flow;
        if (flow == Flow::Break)  break;
    }
    return Flow::Normal;
}

// for (tipo iter in iteravel) body
// B2: o iterador ocupa um slot do frame; cada iteração escreve nele
Flow Executor::executeFor(const ForStmt* stmt) {
    Value iterable = evalExpr(stmt->iterable.get());

    // Copia os elementos para nao ser afetado por mutacoes dentro do loop.
    // C2: dict → pair<K,V> em ordem de chave; string → um caractere (code point
    // UTF-8) por vez, coerente com string.size() (A10)
    std::vector<Value> elements;
    switch (iterable.kind()) {
        case Value::Kind::LIST:
            elements = iterable.asList()->elements;
            break;
        case Value::Kind::DICT:
            elements.reserve(iterable.asDict()->entries.size());
            for (const auto& [k, v] : iterable.asDict()->entries)
                elements.push_back(makePair(k, v));
            break;
        case Value::Kind::STRING: {
            const std::string& s = iterable.asString();
            for (size_t i = 0; i < s.size();) {
                size_t len = 1;
                while (i + len < s.size() &&
                       (static_cast<unsigned char>(s[i + len]) & 0xC0) == 0x80)
                    ++len;   // bytes de continuação pertencem ao mesmo caractere
                elements.emplace_back(s.substr(i, len));
                i += len;
            }
            break;
        }
        default:
            throwRuntimeError("'for' esperava list, dict ou string como iterável", stmt->token);
    }

    // A2: for (decimal x in list<int>) — o iterador recebe o valor convertido
    const bool to_decimal = stmt->type_iterator->kind == Type::Kind::DECIMAL;

    for (const Value& elem : elements) {
        if (to_decimal && elem.kind() == Value::Kind::INT)
            locals[stmt->iter_slot] = Value(static_cast<double>(elem.asInt()));
        else
            locals[stmt->iter_slot] = elem;
        Flow flow = executeStmt(stmt->body.get());
        if (flow == Flow::Return) return flow;
        if (flow == Flow::Break)  break;
    }
    return Flow::Normal;
}

// C4: try { } except (Tipo e) { } ... finally { }
//   - um RuntimeError do bloco vai para o primeiro except do mesmo tipo (ou Error);
//   - o finally roda em todos os casos: fluxo normal, return/break no try,
//     erro sem except, e erro lançado de dentro do próprio except;
//   - o erro pendente (std::exception_ptr) é relançado depois do finally.
// Erros internos do C++ não são capturáveis pelo programa, mas o finally roda.
Flow Executor::executeTry(const TryStmt* stmt) {
    Flow flow = Flow::Normal;
    std::exception_ptr pendente;

    try {
        flow = executeStmt(stmt->body.get());
    } catch (const RuntimeError& err) {
        const ExceptClause* handler = nullptr;
        for (const auto& h : stmt->handlers)
            if (h.type_name == "Error" || h.type_name == err.kind) { handler = &h; break; }

        if (!handler) {
            pendente = std::current_exception();
        } else {
            try {
                auto ev     = std::make_shared<ErrorValue>();
                ev->kind    = err.kind;
                ev->message = err.message;
                ev->line    = err.line;
                ev->column  = err.column;
                ev->trace   = err.trace;
                ev->file_id = err.file_id;
                locals[handler->var_slot] = Value(ev);   // B2
                flow = executeStmt(handler->body.get());
            } catch (...) {
                pendente = std::current_exception();   // erro lançado dentro do except
            }
        }
    } catch (...) {
        pendente = std::current_exception();
    }

    if (stmt->finally_block) {
        // Um `return` pendente no try/except guardou o valor em return_value;
        // funções chamadas no finally o sobrescreveriam, então ele é preservado.
        Value retorno_pendente = std::move(return_value);
        executeStmt(stmt->finally_block.get());   // semântico: sem return/break/continue
        return_value = std::move(retorno_pendente);
    }

    if (pendente) std::rethrow_exception(pendente);
    return flow;
}

// C6: chama uma nativa. Um erro lançado por ela sem posição recebe a
// posição da chamada (e o stack trace, via raise).
Value Executor::callNative(const NativeFn& fn, const CallExpr* expr) {
    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (const auto& arg : expr->arguments)
        args.push_back(evalExpr(arg.get()));
    try {
        Value r = fn.impl(std::span<const Value>(args));
        // Fase 7: nativa genérica que devolve int onde o tipo é decimal
        // (Lists.sum de uma list<decimal> vazia)
        if (r.kind() == Value::Kind::INT && expr->resolved_type &&
            expr->resolved_type->is(TypeInfo::Kind::Decimal))
            r = Value(static_cast<double>(r.asInt()));
        return r;
    } catch (RuntimeError& err) {
        if (err.line == 0)
            raise(RuntimeError(err.kind, err.message, expr->token.line, expr->token.column,
                               expr->token.file_id));
        throw;
    }
}

// C4: throw erro;  — relançar (`throw e;`) preserva tipo, linha e trace de origem
void Executor::executeThrow(const ThrowStmt* stmt) {
    Value v = evalExpr(stmt->value.get());
    const ErrorValue& ev = *v.asError();
    RuntimeError err(ev.kind, ev.message, static_cast<int>(ev.line), static_cast<int>(ev.column),
                     ev.file_id);
    err.trace = ev.trace;   // vazio num erro novo: raise tira o trace de agora
    raise(std::move(err));
}

// return [expr];
// B4: guarda o valor em return_value e sinaliza Flow::Return, que sobe
// pelos blocos até executeBody (antes: throw ReturnSignal)
Flow Executor::executeReturn(const ReturnStmt* stmt) {
    Value ret_val;
    if (stmt->value)
        ret_val = evalExpr(stmt->value.get());
    return_value = std::move(ret_val);
    return Flow::Return;
}

// ============================================================================
// DISPATCHER DE EXPRESSOES
// ============================================================================

Value Executor::evalExpr(const Expr* expr) {
    if (!expr) return Value();

    // B3: switch no NodeKind em vez de cadeia de dynamic_cast
    switch (expr->node_kind) {
        case NodeKind::Literal:      return evalLiteral     (static_cast<const LiteralExpr*>(expr));
        case NodeKind::Identifier:   return evalIdentifier  (static_cast<const IdentifierExpr*>(expr));
        case NodeKind::Binary:       return evalBinary      (static_cast<const BinaryExpr*>(expr));
        case NodeKind::Unary:        return evalUnary       (static_cast<const UnaryExpr*>(expr));
        case NodeKind::Call:         return evalCall        (static_cast<const CallExpr*>(expr));
        case NodeKind::MethodCall:   return evalMethodCall  (static_cast<const MethodCallExpr*>(expr));
        case NodeKind::MemberAccess: return evalMemberAccess(static_cast<const MemberAccessExpr*>(expr));
        case NodeKind::IndexAccess:  return evalIndexAccess (static_cast<const IndexAccessExpr*>(expr));
        case NodeKind::New:          return evalNew         (static_cast<const NewExpr*>(expr));
        case NodeKind::ListLiteral:  return evalListLiteral (static_cast<const ListLiteralExpr*>(expr));
        case NodeKind::DictLiteral:  return evalDictLiteral (static_cast<const DictLiteralExpr*>(expr));
        case NodeKind::PairLiteral:  return evalPairLiteral (static_cast<const PairLiteralExpr*>(expr));
        case NodeKind::Cast:         return evalCast        (static_cast<const CastExpr*>(expr));
        case NodeKind::TypeLiteral:  return Value(static_cast<const TypeLiteralExpr*>(expr)->value);
        default:                     break;
    }
    raise(RuntimeError("Tipo de expressão não reconhecido pelo executor"));
}

// ============================================================================
// EXPRESSOES
// ============================================================================

Value Executor::evalLiteral(const LiteralExpr* expr) {
    if (std::holds_alternative<std::int64_t>(expr->value)) return Value(std::get<std::int64_t>(expr->value));
    if (std::holds_alternative<double>     (expr->value)) return Value(std::get<double>(expr->value));
    if (std::holds_alternative<std::string>(expr->value)) return Value(std::get<std::string>(expr->value));
    if (std::holds_alternative<bool>       (expr->value)) return Value(std::get<bool>(expr->value));
    return Value();
}

Value Executor::evalIdentifier(const IdentifierExpr* expr) {
    if (expr->res.kind == Resolution::Kind::Type) return Value(expr->type_value);   // op<...>
    if (Value* v = slotOf(expr))
        return *v;

    raise(RuntimeError("Variável '" + expr->name + "' não encontrada em runtime",
                       expr->token.line, expr->token.column));
}

Value Executor::evalBinary(const BinaryExpr* expr) {
    // Short-circuit para && e ||
    if (expr->op == TokenType::OP_AND) {
        if (!evalExpr(expr->left.get()).asBool()) return Value(false);
        return Value(evalExpr(expr->right.get()).asBool());
    }
    if (expr->op == TokenType::OP_OR) {
        if (evalExpr(expr->left.get()).asBool()) return Value(true);
        return Value(evalExpr(expr->right.get()).asBool());
    }

    Value left  = evalExpr(expr->left.get());
    Value right = evalExpr(expr->right.get());

    return applyBinary(expr->op, left, right, expr->token);
}

// C1: aritmética/comparação de dois valores já avaliados. Usada por
// evalBinary e pelos operadores compostos (+=, -=, ...), que leem o alvo
// uma única vez (B5) e aplicam a operação aqui.
Value Executor::applyBinary(TokenType op, const Value& left, const Value& right,
                            const Token& tok) {
    // CVM: a regra mora em applyBinaryOp (operacoes.cpp), compartilhada com a
    // VM; aqui só se completa a posição do erro
    try {
        return applyBinaryOp(op, left, right);
    } catch (RuntimeError& err) {
        if (err.line == 0)
            raise(RuntimeError(err.kind, err.message, tok.line, tok.column, tok.file_id));
        throw;
    }
}

Value Executor::evalUnary(const UnaryExpr* expr) {
    Value operand = evalExpr(expr->operand.get());


    switch (expr->op) {
        case TokenType::OP_MINUS:
            try {
                return negateOp(operand);   // CVM: regra compartilhada (operacoes.cpp)
            } catch (RuntimeError& err) {
                raise(RuntimeError(err.kind, err.message, expr->token.line, expr->token.column,
                                   expr->token.file_id));
            }
        case TokenType::OP_NOT:
            return Value(!operand.asBool());
        default:
            throwRuntimeError("Operador unário desconhecido", expr->token);
    }
}

Value Executor::evalCall(const CallExpr* expr) {
    // op<...>: type(x) — o tipo do valor (o atual, se x for um op)
    if (expr->type_of) {
        Value v = evalExpr(expr->arguments[0].get());
        return Value(runtimeType(v, expr->type_of));
    }
    // C6/Fase 7: nativa já resolvida pelo semântico (prelude ou módulo nativo)
    if (expr->native) return callNative(*expr->native, expr);

    // Alvo resolvido pelo semântico: método da própria classe (A10: tem
    // precedência sobre função global de mesmo nome) ou função do usuário
    if (const FunctionDecl* fn = expr->target) {
        std::vector<Value> frame = evalArgs(expr->arguments, fn->num_slots);
        if (expr->implicit_method) {
            Value self = current_self;   // cópia: a chamada troca current_self
            return executeMethod(self, fn, std::move(frame), expr->token.line);
        }
        return executeFunction(fn, std::move(frame), expr->token.line);
    }

    auto it = function_registry.find(expr->function_name);
    if (it == function_registry.end())
        if (const NativeFn* native = findPrelude(expr->function_name))
            return callNative(*native, expr);   // C6: prelude (usuário tem precedência)
    if (it == function_registry.end() && error_kinds.count(expr->function_name)) {
        // C4: Tipo("mensagem") cria um erro; linha e coluna são as da criação
        auto ev     = std::make_shared<ErrorValue>();
        ev->kind    = expr->function_name;
        ev->message = expr->arguments.empty() ? "" : evalExpr(expr->arguments[0].get()).asString();
        ev->line    = expr->token.line;
        ev->column  = expr->token.column;
        ev->file_id = expr->token.file_id;
        return Value(std::move(ev));
    }
    if (it == function_registry.end())
        throwRuntimeError("Função '" + expr->function_name + "' não encontrada",
                          expr->token);

    return executeFunction(it->second, evalArgs(expr->arguments, it->second->num_slots),
                           expr->token.line);
}

std::vector<Value> Executor::evalArgs(const std::vector<ExprPtr>& args, std::uint32_t num_slots) {
    std::vector<Value> frame(std::max<size_t>(num_slots, args.size()));
    for (size_t i = 0; i < args.size(); ++i)
        frame[i] = evalExpr(args[i].get());
    return frame;
}

Value Executor::evalMethodCall(const MethodCallExpr* expr) {
    Value obj = evalExpr(expr->object.get());

    // Chamada pela interface: o método vem da tabela da classe do objeto
    if (expr->iface && obj.kind() == Value::Kind::INSTANCE) {
        for (const auto& [iface, metodos] : obj.asInstance()->decl->itables)
            if (iface == expr->iface) {
                const FunctionDecl* m = metodos[static_cast<size_t>(expr->iface_method)];
                return executeMethod(obj, m, evalArgs(expr->arguments, m->num_slots),
                                     expr->token.line);
            }
        throwRuntimeError("Erro interno: a classe não cumpre a interface", expr->token);
    }

    // Método de classe resolvido pelo semântico (sem herança: o tipo estático
    // do objeto é a classe dele)
    if (expr->target && obj.kind() == Value::Kind::INSTANCE)
        return executeMethod(obj, expr->target,
                             evalArgs(expr->arguments, expr->target->num_slots),
                             expr->token.line);

    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (const auto& arg : expr->arguments)
        args.push_back(evalExpr(arg.get()));

    if (obj.kind() == Value::Kind::LIST || obj.kind() == Value::Kind::DICT ||
        obj.kind() == Value::Kind::STRING)
        return callCollectionMethod(obj, expr->method_name, args, expr->token);

    if (obj.kind() == Value::Kind::INSTANCE) {
        auto instance = obj.asInstance();
        const ClassDecl* cls = instance->decl;
        if (!cls) throwRuntimeError("Instância sem ClassDecl", expr->token);

        auto findMethod = [&](const std::vector<StmtPtr>& methods) -> const FunctionDecl* {
            for (const auto& m : methods)
                if (auto fn = dynamic_cast<const FunctionDecl*>(m.get()))
                    if (fn->name == expr->method_name) return fn;
            return nullptr;
        };

        const FunctionDecl* method = findMethod(cls->pub_methods);
        if (!method) method = findMethod(cls->priv_methods);
        if (!method)
            throwRuntimeError("Método '" + expr->method_name + "' não encontrado em '" +
                              instance->class_name + "'", expr->token);

        std::vector<Value> frame(std::max<size_t>(method->num_slots, args.size()));
        std::move(args.begin(), args.end(), frame.begin());
        return executeMethod(obj, method, std::move(frame), expr->token.line);
    }

    throwRuntimeError("Tipo não possui métodos", expr->token);
}

Value Executor::evalMemberAccess(const MemberAccessExpr* expr) {
    if (expr->enum_decl)   // Cor.Verde
        return Value(EnumValue{expr->enum_decl, static_cast<std::uint32_t>(expr->field_index)});
    Value obj = evalExpr(expr->object.get());

    if (obj.kind() == Value::Kind::PAIR) {
        const auto& p = *obj.asPair();
        if (expr->member_name == "first")  return p.first;
        if (expr->member_name == "second") return p.second;
        throwRuntimeError("'pair' não possui o campo '" + expr->member_name + "'",
                          expr->token);
    }

    if (obj.kind() == Value::Kind::INSTANCE) {
        const auto& fields = obj.asInstance()->fields;
        const int i = expr->field_index;   // B2
        if (i >= 0 && i < static_cast<int>(fields.size())) return fields[static_cast<size_t>(i)];
        throwRuntimeError("Campo '" + expr->member_name + "' não encontrado em '" +
                         obj.asInstance()->class_name + "'", expr->token);
    }

    if (obj.kind() == Value::Kind::ERROR) {    // C4
        const ErrorValue& ev = *obj.asError();
        const std::string& m = expr->member_name;
        if (m == "kind")    return Value(displayName(ev.kind));   // C5: sem o prefixo do módulo
        if (m == "message") return Value(ev.message);
        if (m == "line")    return Value(ev.line);
        if (m == "column")  return Value(ev.column);
        throwRuntimeError("Erro não possui o campo '" + m + "'", expr->token);
    }

    if (obj.kind() == Value::Kind::STRUCT) {   // C3
        const StructValue& sv = *obj.asStruct();
        const int i = expr->field_index;   // B2
        if (i >= 0 && i < static_cast<int>(sv.fields.size())) return sv.fields[static_cast<size_t>(i)];
        throwRuntimeError("Campo '" + expr->member_name + "' não encontrado no struct",
                          expr->token);
    }

    throwRuntimeError("Acesso a membro em tipo inválido", expr->token);
}

Value Executor::evalIndexAccess(const IndexAccessExpr* expr) {
    Value obj = evalExpr(expr->object.get());
    Value idx = evalExpr(expr->index.get());
    try {
        return indexGet(obj, idx);   // CVM: regra compartilhada (operacoes.cpp)
    } catch (RuntimeError& err) {
        raise(RuntimeError(err.kind, err.message, expr->token.line, expr->token.column,
                           expr->token.file_id));
    }
}

Value Executor::evalNew(const NewExpr* expr) {
    // Coleta de ciclos: todo ciclo passa por objetos, então disparar aqui, na
    // criação de objetos, limita a memória presa em ciclos. É um ponto seguro:
    // quem chega aqui não guarda ponteiro cru para dentro de um contêiner sem
    // um Value forte segurando o dono (ver gc.h).
    gcMaybeCollect();
    // C3: construtor automático de struct — sem argumentos usa os valores
    // padrão; com argumentos, eles são os campos em ordem (checado no semântico)
    if (auto st_it = struct_registry.find(expr->class_name); st_it != struct_registry.end()) {
        const StructDecl* decl = st_it->second;
        auto sv  = std::make_shared<StructValue>();
        sv->decl = decl;
        if (expr->arguments.empty()) {
            // Valores padrão num frame próprio e sem objeto atual: o inicializador
            // não enxerga os locais de quem faz o new (A1) nem outros campos
            InstanceGuard ig{current_instance, current_self, Value()};
            FrameGuard    frame{*this, 0};   // inicializadores não declaram variáveis
            for (const auto& field : decl->fields) {
                if (field.initializer)                         sv->fields.push_back(evalExpr(field.initializer.get()));
                else if (field.type->kind == Type::Kind::LIST) sv->fields.push_back(makeList());
                else                                           sv->fields.push_back(makeDict());
            }
        } else {
            for (const auto& arg : expr->arguments)
                sv->fields.push_back(evalExpr(arg.get()));
        }
        return Value(std::move(sv));
    }

    auto cls_it = class_registry.find(expr->class_name);
    if (cls_it == class_registry.end())
        throwRuntimeError("Classe '" + expr->class_name + "' não registrada", expr->token);
    const ClassDecl* cls = cls_it->second;

    // Argumentos antes dos campos, direto no frame do construtor
    std::vector<Value> frame =
        evalArgs(expr->arguments, cls->constructor ? cls->constructor->num_slots : 0);

    Value instance_val = makeInstance(expr->class_name, cls);
    ClassInstance& instance = *instance_val.asInstance();

    // v2.00 #12: current_instance aponta para a instância em construção
    // durante a inicialização dos campos, para que expressões de inicialização
    // que referenciem outros campos funcionem corretamente.
    //
    // Invariante de campos:
    //   - campo com inicializador   → avalia a expressão e armazena o resultado
    //   - list<T>/dict<K,V> sem '=' → nascem como coleções vazias implicitamente
    //   - qualquer outro tipo sem   → erro interno (parser/semântico já deveria
    //     inicializador               ter rejeitado antes de chegar aqui)
    {
        InstanceGuard ig{current_instance, current_self, instance_val};
        FrameGuard    frame{*this, 0};   // inicializadores não declaram variáveis
        for (size_t i = 0; i < cls->fields.size(); ++i) {
            const auto& field = cls->fields[i];
            if (field.initializer) {
                instance.fields[i] = evalExpr(field.initializer.get());
            } else if (field.type->kind == Type::Kind::LIST) {
                instance.fields[i] = makeList();
            } else if (field.type->kind == Type::Kind::DICT) {
                instance.fields[i] = makeDict();
            } else {
                raise(RuntimeError(
                    "Erro interno: campo '" + field.name +
                    "' sem inicializador chegou ao executor para tipo inválido. "
                    "O parser/semântico deveria ter rejeitado esta declaração."));
            }
        }
    }  // InstanceGuard restaura current_instance antes do construtor

    if (cls->constructor)
        executeConstructor(instance_val, *cls->constructor, std::move(frame), expr->token.line);

    return instance_val;
}

Value Executor::evalListLiteral(const ListLiteralExpr* expr) {
    std::vector<Value> elements;
    elements.reserve(expr->elements.size());
    for (const auto& elem : expr->elements)
        elements.push_back(evalExpr(elem.get()));
    return makeList(std::move(elements));
}

Value Executor::evalDictLiteral(const DictLiteralExpr* expr) {
    Value dict_val = makeDict();
    auto& entries = dict_val.asDict()->entries;
    for (const auto& [key_expr, val_expr] : expr->pairs) {
        Value key = evalExpr(key_expr.get());
        Value val = evalExpr(val_expr.get());
        entries[std::move(key)] = std::move(val);
    }
    return dict_val;
}

Value Executor::evalPairLiteral(const PairLiteralExpr* expr) {
    return makePair(evalExpr(expr->first.get()), evalExpr(expr->second.get()));
}

// A2: conversão implícita inserida pelo semântico (hoje só int → decimal)
Value Executor::evalCast(const CastExpr* expr) {
    Value v = evalExpr(expr->operand.get());
    // op<...>: conferência do tipo guardado (TypeError) e int → decimal se preciso
    if (expr->operand->resolved_type->is(TypeInfo::Kind::Op) ||
        expr->resolved_type->is(TypeInfo::Kind::Op))
        return narrow(std::move(v), expr->operand->resolved_type, expr->resolved_type,
                      expr->token);
    if (expr->resolved_type->is(TypeInfo::Kind::Decimal) && v.kind() == Value::Kind::INT)
        return Value(static_cast<double>(v.asInt()));
    return v;
}

// ============================================================================
// EXECUCAO DE FUNCOES E METODOS
//
// Padrao uniforme nos tres:
//   1. Salva current_instance e configura o novo contexto
//   2. FrameGuard abre um frame novo (A1): o corpo nao enxerga os
//      locais de quem chamou, so campos de current_instance e globais
//   3. Define parametros no escopo inicial do frame
//   4. executeBody roda o corpo inline (sem executeBlock — evita escopo
//      duplo) ate um Flow::Return e devolve return_value (B4)
//   5. FrameGuard fecha o frame no destrutor (ate em excecao)
//   6. Restaura current_instance
// ============================================================================

// B4: corpo de funcao/metodo/construtor, no frame ja aberto pelo chamador
Value Executor::executeBody(const Stmt* body) {
    bool returned = false;
    if (body->node_kind == NodeKind::Block) {
        for (const auto& s : static_cast<const BlockStmt*>(body)->statements) {
            returned = executeStmt(s.get()) == Flow::Return;
            if (returned) break;
        }
    } else {
        returned = executeStmt(body) == Flow::Return;
    }

    if (!returned) return Value();          // corpo terminou sem return (void)
    Value result = std::move(return_value);
    return_value = Value();
    return result;
}

Value Executor::executeFunction(const FunctionDecl* fn,
                                 std::vector<Value> frame, int call_line) {
    CallGuard call{*this, fn->trace_name, call_line, fn->token};

    // v2.00 #11: InstanceGuard garante restauração mesmo com exceção
    InstanceGuard ig{current_instance, current_self, Value()};

    // Aridade e tipos já checados pelo semântico; os argumentos estão nos
    // slots 0..n-1 do frame (evalArgs)
    FrameGuard guard{*this, std::move(frame)};
    return executeBody(fn->body.get());
}

void Executor::executeConstructor(const Value& self,
                                   const ClassDecl::Constructor& ctor,
                                   std::vector<Value> frame, int call_line) {
    CallGuard call{*this, ctor.trace_name, call_line, ctor.token};

    // v2.00 #11: InstanceGuard RAII
    InstanceGuard ig{current_instance, current_self, self};

    FrameGuard guard{*this, std::move(frame)};
    executeBody(ctor.body.get());   // 'return;' encerra o construtor (A10)
}

Value Executor::executeMethod(const Value& self,
                               const FunctionDecl* method,
                               std::vector<Value> frame, int call_line) {
    CallGuard call{*this, method->trace_name, call_line, method->token};

    // v2.00 #11: InstanceGuard RAII
    InstanceGuard ig{current_instance, current_self, self};

    FrameGuard guard{*this, std::move(frame)};
    return executeBody(method->body.get());
}

// ============================================================================
// BUILT-INS DE COLECOES
// ============================================================================

// CVM: métodos embutidos compartilhados com a VM (operacoes.cpp)
Value Executor::callCollectionMethod(Value& obj, const std::string& method,
                                     const std::vector<Value>& args, const Token& tok) {
    try {
        return callBuiltin(builtinFor(obj.kind(), method), obj, std::span<const Value>(args));
    } catch (RuntimeError& err) {
        raise(RuntimeError(err.kind, err.message, tok.line, tok.column, tok.file_id));
    }
}



// ============================================================================
// op<...> EM RUNTIME
//
// Um op guarda um Value comum; o tipo real sai do próprio valor. Coleções não
// guardam o tipo dos elementos, mas a regra 9 garante no máximo um list, um
// dict e um pair por op, então o tipo vem do op.
// ============================================================================

TypeRef Executor::runtimeType(const Value& v, TypeRef st) {
    auto& t = TypeContext::instance();
    using K = TypeInfo::Kind;

    // Erro guardado como Error (a raiz): o tipo real é o do erro
    if (v.kind() == Value::Kind::ERROR) return t.errorType(v.asError()->kind);
    // Objeto guardado como interface: o tipo real é a classe
    if (v.kind() == Value::Kind::INSTANCE && st->is(K::Interface))
        return t.classType(v.asInstance()->class_name);
    if (!st->is(K::Op)) return st;

    auto doOp = [&](K kind) -> TypeRef {
        for (TypeRef m : st->params) if (m->is(kind)) return m;
        return t.varType();   // não acontece: o semântico só deixa entrar tipos do op
    };
    switch (v.kind()) {
        case Value::Kind::INT:      return t.intType();
        case Value::Kind::DECIMAL:  return t.decimalType();
        case Value::Kind::STRING:   return t.stringType();
        case Value::Kind::BOOL:     return t.boolType();
        case Value::Kind::TYPE:     return t.typeType();
        case Value::Kind::LIST:     return doOp(K::List);
        case Value::Kind::DICT:     return doOp(K::Dict);
        case Value::Kind::PAIR:     return doOp(K::Pair);
        case Value::Kind::INSTANCE: return t.classType(v.asInstance()->class_name);
        case Value::Kind::STRUCT:   return t.structType(v.asStruct()->decl->name);
        case Value::Kind::ENUM:     return t.enumType(v.asEnum().decl->name);
        default:                    return t.voidType();
    }
}

Value Executor::narrow(Value v, TypeRef from, TypeRef to, const Token& tok) const {
    using K = TypeInfo::Kind;
    TypeRef real = runtimeType(v, from);
    if (!TypeChecker::isAssignable(to, real))
        throwRuntimeError("TypeError: esperado '" + stripModulePrefixes(to->str()) +
                          "', mas o valor é '" + stripModulePrefixes(real->str()) + "'", tok);

    // int → decimal: destino decimal, ou op que tem decimal e não tem int
    if (v.kind() == Value::Kind::INT) {
        const bool quer_decimal = to->is(K::Decimal) ||
            (to->is(K::Op) && to->hasMember(TypeContext::instance().decimalType()) &&
             !to->hasMember(TypeContext::instance().intType()));
        if (quer_decimal) return Value(static_cast<double>(v.asInt()));
    }
    return v;
}


// op<...>: o valor novo de quem já travou mantém o tipo travado. A única
// conversão implícita da linguagem é int → decimal, então basta ela.
Value Executor::keepLock(const Value& atual, Value novo) {
    if (atual.kind() == Value::Kind::DECIMAL && novo.kind() == Value::Kind::INT)
        return Value(static_cast<double>(novo.asInt()));
    return novo;
}

// ============================================================================
// RESOLUCAO DE NOMES (A1 + B2)
//
// O semântico já decidiu onde cada variável vive: slot do frame atual, campo
// do objeto atual, slot global ou o próprio objeto (self). O frame de quem
// chamou nunca é alcançável.
// ============================================================================

Value* Executor::slotOf(const IdentifierExpr* id) {
    switch (id->res.kind) {
        case Resolution::Kind::Local:
            return &locals[id->res.slot];
        case Resolution::Kind::Field:
            if (!current_instance || id->res.slot >= current_instance->fields.size())
                return nullptr;
            return &current_instance->fields[id->res.slot];
        case Resolution::Kind::Global:
            return &globals[id->res.slot];
        case Resolution::Kind::Self:
            return &current_self;
        case Resolution::Kind::None:
        case Resolution::Kind::Type:   // tipo não é lugar (evalIdentifier trata)
            break;
    }
    return nullptr;
}

} // namespace cinza
