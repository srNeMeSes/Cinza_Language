#include "executor.h"
#include <iostream>
#include <cmath>

namespace cinza {

// ============================================================================
// SCOPE GUARD (RAII)
//
// Garante que env.popScope() seja chamado mesmo quando uma exceção
// (ReturnSignal ou RuntimeError) propaga através de um bloco.
//
// Sem isso, cada ReturnSignal que passa por executeBlock ou executeFor
// deixa um escopo empilhado permanentemente — causando leituras de
// variáveis no nível de escopo errado (bug do fibonacci/recursão).
// ============================================================================

struct ScopeGuard {
    Environment& env;
    explicit ScopeGuard(Environment& e) : env(e) { env.pushScope(); }
    ~ScopeGuard() noexcept { env.popScope(); }

    ScopeGuard(const ScopeGuard&)            = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
};

// v2.00 #11: InstanceGuard — RAII para current_instance
// Garante restauração automática de current_instance mesmo com exceções.
struct InstanceGuard {
    ClassInstance*& slot;
    ClassInstance*  saved;

    explicit InstanceGuard(ClassInstance*& s, ClassInstance* new_val)
        : slot(s), saved(s) { slot = new_val; }
    ~InstanceGuard() noexcept { slot = saved; }

    InstanceGuard(const InstanceGuard&)            = delete;
    InstanceGuard& operator=(const InstanceGuard&) = delete;
};

// ============================================================================
// PONTO DE ENTRADA
// ============================================================================

void Executor::execute(const Program& program) {
    preRegisterGlobals(program);
    for (const auto& stmt : program.statements)
        executeStmt(stmt.get());
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
    }
}

// ============================================================================
// UTILITARIO DE ERRO
// ============================================================================

[[noreturn]] void Executor::throwRuntimeError(const std::string& msg,
                                               const Token& tok) const {
    throw RuntimeError(msg, tok.line, tok.column);
}

// ============================================================================
// DISPATCHER DE STATEMENTS
// ============================================================================

void Executor::executeStmt(const Stmt* stmt) {
    if (!stmt) return;

    if (auto s = dynamic_cast<const VarDeclStmt*>        (stmt)) { executeVarDecl(s);         return; }
    if (auto s = dynamic_cast<const AssignmentStmt*>     (stmt)) { executeAssignment(s);       return; }
    if (auto s = dynamic_cast<const IndexAssignmentStmt*>(stmt)) { executeIndexAssignment(s);  return; }  // v2.00 #8
    if (auto s = dynamic_cast<const ExprStmt*>           (stmt)) { executeExprStmt(s);         return; }
    if (auto s = dynamic_cast<const BlockStmt*>          (stmt)) { executeBlock(s);            return; }
    if (auto s = dynamic_cast<const IfStmt*>             (stmt)) { executeIf(s);               return; }
    if (auto s = dynamic_cast<const WhileStmt*>          (stmt)) { executeWhile(s);            return; }
    if (auto s = dynamic_cast<const ForStmt*>            (stmt)) { executeFor(s);              return; }
    if (auto s = dynamic_cast<const ReturnStmt*>         (stmt)) { executeReturn(s);           return; }

    if (dynamic_cast<const FunctionDecl*>(stmt)) return;
    if (dynamic_cast<const ClassDecl*>   (stmt)) return;

    throw RuntimeError("Tipo de statement nao reconhecido pelo executor");
}

// ============================================================================
// STATEMENTS
// ============================================================================

// Bloco { stmt* }
// ScopeGuard garante popScope() mesmo se ReturnSignal propagar para fora.
void Executor::executeBlock(const BlockStmt* block) {
    ScopeGuard guard{env};
    for (const auto& s : block->statements)
        executeStmt(s.get());
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
            throw RuntimeError(
                "Erro interno: variável '" + stmt->name +
                "' sem inicializador chegou ao executor para tipo inválido. "
                "O parser/semântico deveria ter rejeitado esta declaração.");
    }

    env.define(stmt->name, std::move(val));
}

// Atribuicao:  variavel = expr;
void Executor::executeAssignment(const AssignmentStmt* stmt) {
    Value val = evalExpr(stmt->value.get());
    assignVariable(stmt->variable_name, std::move(val), stmt->token);
}

// v2.00 #8: Atribuição por índice:  nome[idx] = valor
void Executor::executeIndexAssignment(const IndexAssignmentStmt* stmt) {
    Value idx = evalExpr(stmt->index.get());
    Value val = evalExpr(stmt->value.get());

    // Tenta no Environment primeiro, depois nos campos de current_instance
    auto doAssign = [&](Value& obj) -> bool {
        if (obj.kind == Value::Kind::LIST) {
            int i  = idx.asInt();
            int sz = static_cast<int>(obj.asList()->elements.size());
            if (i < 0)
                throw RuntimeError("RuntimeError: índice negativo em lista não é permitido",
                                   stmt->token.line, stmt->token.column);
            if (i >= sz)
                throw RuntimeError("RuntimeError [linha " + std::to_string(stmt->token.line) +
                                   ", col " + std::to_string(stmt->token.column) +
                                   "]: IndexError: índice " + std::to_string(i) +
                                   " fora dos limites (tamanho: " + std::to_string(sz) + ")");
            obj.asList()->elements[static_cast<size_t>(i)] = std::move(val);
            return true;
        }
        if (obj.kind == Value::Kind::DICT) {
            // v2.00: subscript assignment só ATUALIZA chaves existentes.
            // Para inserir nova entrada, use .add({"chave", valor}).
            auto& entries = obj.asDict()->entries;
            auto it = entries.find(idx);
            if (it == entries.end())
                throw RuntimeError(
                    "RuntimeError [linha " + std::to_string(stmt->token.line) +
                    ", col " + std::to_string(stmt->token.column) +
                    "]: Chave '" + idx.toString() + "' não existe no dicionário. "
                    "Use .add({\"" + idx.toString() + "\", valor}) para inserir novas entradas.",
                    stmt->token.line, stmt->token.column);
            it->second = std::move(val);
            return true;
        }
        return false;
    };

    // Procura no Environment — get+assign é o caminho necessário porque
    // shared_ptr garante que a mutação seja visível para todos os donos do objeto.
    bool done = false;
    if (env.exists(stmt->object_name)) {
        Value obj = env.get(stmt->object_name);
        if (doAssign(obj)) {
            env.assign(stmt->object_name, std::move(obj));
            done = true;
        }
    }

    if (!done && current_instance) {
        auto it = current_instance->fields.find(stmt->object_name);
        if (it != current_instance->fields.end()) {
            doAssign(it->second);
            done = true;
        }
    }

    if (!done)
        throwRuntimeError("Variável '" + stmt->object_name + "' não encontrada para atribuição por índice",
                          stmt->token);
}

// Expressao statement
void Executor::executeExprStmt(const ExprStmt* stmt) {
    evalExpr(stmt->expression.get());
}

// if (cond) then [else else_branch]
void Executor::executeIf(const IfStmt* stmt) {
    Value cond = evalExpr(stmt->condition.get());
    if (cond.asBool())
        executeStmt(stmt->then_branch.get());
    else if (stmt->else_branch)
        executeStmt(stmt->else_branch.get());
}

// while (cond) body
void Executor::executeWhile(const WhileStmt* stmt) {
    while (evalExpr(stmt->condition.get()).asBool())
        executeStmt(stmt->body.get());
}

// for (tipo iter in iteravel) body
// ScopeGuard por iteracao garante popScope() mesmo que ReturnSignal saia do corpo.
void Executor::executeFor(const ForStmt* stmt) {
    Value iterable = evalExpr(stmt->iterable.get());

    if (iterable.kind != Value::Kind::LIST)
        throwRuntimeError("'for' esperava lista como iteravel", stmt->token);

    // Copia os elementos para nao ser afetado por mutacoes dentro do loop
    const std::vector<Value> elements = iterable.asList()->elements;

    for (const Value& elem : elements) {
        ScopeGuard guard{env};
        env.define(stmt->iterator_name, elem);
        executeStmt(stmt->body.get());
    }
}

// return [expr];  — lanca ReturnSignal, capturado pelo chamador
void Executor::executeReturn(const ReturnStmt* stmt) {
    Value ret_val;
    if (stmt->value)
        ret_val = evalExpr(stmt->value.get());
    throw ReturnSignal(std::move(ret_val));
}

// print(args...)
void Executor::executePrint(const CallExpr* call) {
    for (size_t i = 0; i < call->arguments.size(); ++i) {
        Value v = evalExpr(call->arguments[i].get());
        std::cout << v.toString();
        if (i + 1 < call->arguments.size()) std::cout << " ";
    }
    std::cout << "\n";
}

// ============================================================================
// DISPATCHER DE EXPRESSOES
// ============================================================================

Value Executor::evalExpr(const Expr* expr) {
    if (!expr) return Value();

    if (auto e = dynamic_cast<const LiteralExpr*>      (expr)) return evalLiteral(e);
    if (auto e = dynamic_cast<const IdentifierExpr*>   (expr)) return evalIdentifier(e);
    if (auto e = dynamic_cast<const BinaryExpr*>       (expr)) return evalBinary(e);
    if (auto e = dynamic_cast<const UnaryExpr*>        (expr)) return evalUnary(e);
    if (auto e = dynamic_cast<const CallExpr*>         (expr)) return evalCall(e);
    if (auto e = dynamic_cast<const MethodCallExpr*>   (expr)) return evalMethodCall(e);
    if (auto e = dynamic_cast<const MemberAccessExpr*> (expr)) return evalMemberAccess(e);
    if (auto e = dynamic_cast<const IndexAccessExpr*>  (expr)) return evalIndexAccess(e);
    if (auto e = dynamic_cast<const NewExpr*>          (expr)) return evalNew(e);
    if (auto e = dynamic_cast<const ListLiteralExpr*>  (expr)) return evalListLiteral(e);
    if (auto e = dynamic_cast<const DictLiteralExpr*>  (expr)) return evalDictLiteral(e);
    if (auto e = dynamic_cast<const PairLiteralExpr*>  (expr)) return evalPairLiteral(e);

    throw RuntimeError("Tipo de expressao nao reconhecido pelo executor");
}

// ============================================================================
// EXPRESSOES
// ============================================================================

Value Executor::evalLiteral(const LiteralExpr* expr) {
    if (std::holds_alternative<int>        (expr->value)) return Value(std::get<int>(expr->value));
    if (std::holds_alternative<double>     (expr->value)) return Value(std::get<double>(expr->value));
    if (std::holds_alternative<std::string>(expr->value)) return Value(std::get<std::string>(expr->value));
    if (std::holds_alternative<bool>       (expr->value)) return Value(std::get<bool>(expr->value));
    return Value();
}

// Prioridade: Environment (locais/params) -> campos de current_instance
Value Executor::evalIdentifier(const IdentifierExpr* expr) {
    if (env.exists(expr->name))
        return env.get(expr->name);

    if (current_instance) {
        auto it = current_instance->fields.find(expr->name);
        if (it != current_instance->fields.end())
            return it->second;
    }

    throw RuntimeError("Variavel '" + expr->name + "' nao encontrada em runtime",
                       expr->token.line, expr->token.column);
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

    auto toDouble = [](const Value& v) -> double {
        return (v.kind == Value::Kind::INT)
            ? static_cast<double>(v.asInt()) : v.asDecimal();
    };

    // Preserva int quando ambos os operandos sao int
    auto numResult = [&](double result) -> Value {
        if (left.kind == Value::Kind::INT && right.kind == Value::Kind::INT)
            return Value(static_cast<int>(result));
        return Value(result);
    };

    switch (expr->op) {
        case TokenType::OP_PLUS:
            // Concatenação string + string
            if (left.kind == Value::Kind::STRING && right.kind == Value::Kind::STRING)
                return Value(left.asString() + right.asString());
            // v2.00 #3: string + primitivo (int, decimal, bool)
            if (left.kind == Value::Kind::STRING)
                return Value(left.asString() + right.toString());
            if (right.kind == Value::Kind::STRING)
                return Value(left.toString() + right.asString());
            return numResult(toDouble(left) + toDouble(right));

        case TokenType::OP_MINUS:
            return numResult(toDouble(left) - toDouble(right));

        case TokenType::OP_MULTIPLY:
            return numResult(toDouble(left) * toDouble(right));

        case TokenType::OP_DIVIDE: {
            double d = toDouble(right);
            if (d == 0.0) throwRuntimeError("Divisao por zero", expr->token);
            return numResult(toDouble(left) / d);
        }

        case TokenType::OP_MODULO: {
            if (right.kind == Value::Kind::INT && right.asInt() == 0)
                throwRuntimeError("Modulo por zero", expr->token);
            if (left.kind == Value::Kind::INT && right.kind == Value::Kind::INT)
                return Value(left.asInt() % right.asInt());
            return Value(std::fmod(toDouble(left), toDouble(right)));
        }

        case TokenType::OP_LESS:          return Value(left <  right);
        case TokenType::OP_LESS_EQUAL:    return Value(left <= right);
        case TokenType::OP_GREATER:       return Value(left >  right);
        case TokenType::OP_GREATER_EQUAL: return Value(left >= right);
        case TokenType::OP_EQUAL:         return Value(left == right);
        case TokenType::OP_NOT_EQUAL:     return Value(left != right);

        default: throwRuntimeError("Operador binario desconhecido", expr->token);
    }
}

Value Executor::evalUnary(const UnaryExpr* expr) {
    Value operand = evalExpr(expr->operand.get());

    switch (expr->op) {
        case TokenType::OP_MINUS:
            if (operand.kind == Value::Kind::INT) return Value(-operand.asInt());
            return Value(-operand.asDecimal());
        case TokenType::OP_NOT:
            return Value(!operand.asBool());
        default:
            throwRuntimeError("Operador unario desconhecido", expr->token);
    }
}

Value Executor::evalCall(const CallExpr* expr) {
    if (expr->function_name == "print") {
        executePrint(expr);
        return Value();
    }

    // Chamada implicita a metodo da propria classe:  metodo() dentro de outro metodo
    if (function_registry.find(expr->function_name) == function_registry.end() &&
        current_instance != nullptr) {
        const ClassDecl* cls = current_instance->decl;
        if (cls) {
            auto findMethod = [&](const std::vector<StmtPtr>& methods) -> const FunctionDecl* {
                for (const auto& m : methods)
                    if (auto fn = dynamic_cast<const FunctionDecl*>(m.get()))
                        if (fn->name == expr->function_name) return fn;
                return nullptr;
            };
            const FunctionDecl* method = findMethod(cls->pub_methods);
            if (!method) method = findMethod(cls->priv_methods);
            if (method) {
                std::vector<Value> args;
                args.reserve(expr->arguments.size());
                for (const auto& arg : expr->arguments)
                    args.push_back(evalExpr(arg.get()));
                return executeMethod(*current_instance, method, args);
            }
        }
    }

    auto it = function_registry.find(expr->function_name);
    if (it == function_registry.end())
        throwRuntimeError("Funcao '" + expr->function_name + "' nao encontrada",
                          expr->token);

    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (const auto& arg : expr->arguments)
        args.push_back(evalExpr(arg.get()));

    return executeFunction(it->second, args);
}

Value Executor::evalMethodCall(const MethodCallExpr* expr) {
    Value obj = evalExpr(expr->object.get());

    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (const auto& arg : expr->arguments)
        args.push_back(evalExpr(arg.get()));

    if (obj.kind == Value::Kind::LIST)
        return callListMethod(obj, expr->method_name, args, expr->token);
    if (obj.kind == Value::Kind::DICT)
        return callDictMethod(obj, expr->method_name, args, expr->token);
    if (obj.kind == Value::Kind::STRING)
        return callStringMethod(obj, expr->method_name, args, expr->token);

    if (obj.kind == Value::Kind::INSTANCE) {
        auto instance = obj.asInstance();
        const ClassDecl* cls = instance->decl;
        if (!cls) throwRuntimeError("Instancia sem ClassDecl", expr->token);

        auto findMethod = [&](const std::vector<StmtPtr>& methods) -> const FunctionDecl* {
            for (const auto& m : methods)
                if (auto fn = dynamic_cast<const FunctionDecl*>(m.get()))
                    if (fn->name == expr->method_name) return fn;
            return nullptr;
        };

        const FunctionDecl* method = findMethod(cls->pub_methods);
        if (!method) method = findMethod(cls->priv_methods);
        if (!method)
            throwRuntimeError("Metodo '" + expr->method_name + "' nao encontrado em '" +
                              instance->class_name + "'", expr->token);

        return executeMethod(*instance, method, args);
    }

    throwRuntimeError("Tipo nao possui metodos", expr->token);
}

Value Executor::evalMemberAccess(const MemberAccessExpr* expr) {
    Value obj = evalExpr(expr->object.get());

    if (obj.kind == Value::Kind::PAIR) {
        const auto& p = *obj.asPair();
        if (expr->member_name == "first")  return p.first;
        if (expr->member_name == "second") return p.second;
        throwRuntimeError("'pair' nao possui o campo '" + expr->member_name + "'",
                          expr->token);
    }

    if (obj.kind == Value::Kind::INSTANCE) {
        const auto& fields = obj.asInstance()->fields;
        auto it = fields.find(expr->member_name);
        if (it != fields.end()) return it->second;
        throwRuntimeError("Campo '" + expr->member_name + "' nao encontrado em '" +
                         obj.asInstance()->class_name + "'", expr->token);
    }

    throwRuntimeError("Acesso a membro em tipo invalido", expr->token);
}

Value Executor::evalIndexAccess(const IndexAccessExpr* expr) {
    Value obj = evalExpr(expr->object.get());
    Value idx = evalExpr(expr->index.get());

    if (obj.kind == Value::Kind::LIST) {
        const auto& elems = obj.asList()->elements;
        if (idx.kind != Value::Kind::INT)
            throwRuntimeError("Indice de lista deve ser inteiro", expr->token);
        int i  = idx.asInt();
        int sz = static_cast<int>(elems.size());
        if (i < 0 || i >= sz)
            throwRuntimeError("IndexError: indice " + std::to_string(i) +
                             " fora dos limites (tamanho: " + std::to_string(sz) + ")",
                             expr->token);
        return elems[static_cast<size_t>(i)];
    }

    if (obj.kind == Value::Kind::DICT) {
        auto& entries = obj.asDict()->entries;
        auto it = entries.find(idx);
        if (it == entries.end())
            throwRuntimeError("KeyError: chave '" + idx.toString() +
                             "' nao encontrada no dicionario", expr->token);
        return it->second;
    }

    throwRuntimeError("Operador '[]' em tipo invalido", expr->token);
}

Value Executor::evalNew(const NewExpr* expr) {
    auto cls_it = class_registry.find(expr->class_name);
    if (cls_it == class_registry.end())
        throwRuntimeError("Classe '" + expr->class_name + "' nao registrada", expr->token);
    const ClassDecl* cls = cls_it->second;

    std::vector<Value> args;
    args.reserve(expr->arguments.size());
    for (const auto& arg : expr->arguments)
        args.push_back(evalExpr(arg.get()));

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
        InstanceGuard ig{current_instance, &instance};
        for (const auto& field : cls->fields) {
            if (field.initializer) {
                instance.fields[field.name] = evalExpr(field.initializer.get());
            } else if (field.type->kind == Type::Kind::LIST) {
                instance.fields[field.name] = makeList();
            } else if (field.type->kind == Type::Kind::DICT) {
                instance.fields[field.name] = makeDict();
            } else {
                throw RuntimeError(
                    "Erro interno: campo '" + field.name +
                    "' sem inicializador chegou ao executor para tipo inválido. "
                    "O parser/semântico deveria ter rejeitado esta declaração.");
            }
        }
    }  // InstanceGuard restaura current_instance antes do construtor

    if (cls->constructor)
        executeConstructor(instance, *cls->constructor, args);

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

// ============================================================================
// EXECUCAO DE FUNCOES E METODOS
//
// Padrao uniforme nos tres:
//   1. Salva current_instance e configura o novo contexto
//   2. ScopeGuard abre escopo dos parametros (e variaveis locais)
//   3. Define parametros no Environment
//   4. Executa o corpo inline (sem executeBlock — evita escopo duplo)
//   5. Captura ReturnSignal dentro do bloco try
//   6. ScopeGuard fecha escopo no destrutor (ate em excecao)
//   7. Restaura current_instance
// ============================================================================

Value Executor::executeFunction(const FunctionDecl* fn,
                                 const std::vector<Value>& args) {
    // v2.00 #11: InstanceGuard garante restauração mesmo com exceção
    InstanceGuard ig{current_instance, nullptr};

    // v2.00 #21: defesa em runtime (semântico já validou, mas garante)
    if (args.size() != fn->parameters.size())
        throwRuntimeError("Aridade incorreta em '" + fn->name + "': esperados " +
                          std::to_string(fn->parameters.size()) + ", recebidos " +
                          std::to_string(args.size()), fn->token);

    Value result;
    {
        ScopeGuard guard{env};
        for (size_t i = 0; i < fn->parameters.size(); ++i)
            env.define(fn->parameters[i].name, args[i]);
        try {
            if (auto* b = dynamic_cast<const BlockStmt*>(fn->body.get()))
                for (const auto& s : b->statements) executeStmt(s.get());
            else
                executeStmt(fn->body.get());
        } catch (ReturnSignal& sig) {
            result = std::move(sig.value);
        }
    }

    return result;
}

void Executor::executeConstructor(ClassInstance& instance,
                                   const ClassDecl::Constructor& ctor,
                                   const std::vector<Value>& args) {
    // v2.00 #11: InstanceGuard RAII
    InstanceGuard ig{current_instance, &instance};

    // v2.00 #21: defesa em runtime
    if (args.size() != ctor.parameters.size())
        throwRuntimeError("Aridade incorreta no construtor de '" +
                          instance.class_name + "': esperados " +
                          std::to_string(ctor.parameters.size()) + ", recebidos " +
                          std::to_string(args.size()), ctor.token);

    {
        ScopeGuard guard{env};
        for (size_t i = 0; i < ctor.parameters.size(); ++i)
            env.define(ctor.parameters[i].name, args[i]);
        try {
            if (auto* b = dynamic_cast<const BlockStmt*>(ctor.body.get()))
                for (const auto& s : b->statements) executeStmt(s.get());
            else
                executeStmt(ctor.body.get());
        } catch (ReturnSignal&) { /* construtor nao tem return */ }
    }
}

Value Executor::executeMethod(ClassInstance& instance,
                               const FunctionDecl* method,
                               const std::vector<Value>& args) {
    // v2.00 #11: InstanceGuard RAII
    InstanceGuard ig{current_instance, &instance};

    // v2.00 #21: defesa em runtime
    if (args.size() != method->parameters.size())
        throwRuntimeError("Aridade incorreta em '" + instance.class_name + "." +
                          method->name + "': esperados " +
                          std::to_string(method->parameters.size()) + ", recebidos " +
                          std::to_string(args.size()), method->token);

    Value result;
    {
        ScopeGuard guard{env};
        for (size_t i = 0; i < method->parameters.size(); ++i)
            env.define(method->parameters[i].name, args[i]);
        try {
            if (auto* b = dynamic_cast<const BlockStmt*>(method->body.get()))
                for (const auto& s : b->statements) executeStmt(s.get());
            else
                executeStmt(method->body.get());
        } catch (ReturnSignal& sig) {
            result = std::move(sig.value);
        }
    }

    return result;
}

// ============================================================================
// BUILT-INS DE COLECOES
// ============================================================================

Value Executor::callListMethod(Value& obj, const std::string& method,
                               const std::vector<Value>& args, const Token& tok) {
    auto list = obj.asList();

    if (method == "add")    { list->elements.push_back(args[0]); return Value(); }
    if (method == "size")   { return Value(static_cast<int>(list->elements.size())); }
    if (method == "has")    {
        int idx = args[0].asInt();
        return Value(idx >= 0 && idx < static_cast<int>(list->elements.size()));
    }
    if (method == "remove") {
        int idx = args[0].asInt();
        int sz  = static_cast<int>(list->elements.size());
        if (idx < 0 || idx >= sz)
            throwRuntimeError("IndexError: indice " + std::to_string(idx) +
                             " fora dos limites ao chamar 'remove' (tamanho: " +
                             std::to_string(sz) + ")", tok);
        list->elements.erase(list->elements.begin() + idx);
        return Value();
    }

    throwRuntimeError("Metodo '" + method + "' nao existe em list", tok);
}

Value Executor::callDictMethod(Value& obj, const std::string& method,
                               const std::vector<Value>& args, const Token& tok) {
    auto dict = obj.asDict();

    if (method == "add") {
        const auto& p = *args[0].asPair();
        dict->entries[p.first] = p.second;
        return Value();
    }
    if (method == "has")    { return Value(dict->entries.count(args[0]) > 0); }
    if (method == "remove") {
        auto it = dict->entries.find(args[0]);
        if (it == dict->entries.end())
            throwRuntimeError("KeyError: chave '" + args[0].toString() +
                             "' nao encontrada ao chamar 'remove'", tok);
        dict->entries.erase(it);
        return Value();
    }
    if (method == "size")   { return Value(static_cast<int>(dict->entries.size())); }
    if (method == "keys") {
        std::vector<Value> keys;
        keys.reserve(dict->entries.size());
        for (const auto& [k, _] : dict->entries) keys.push_back(k);
        return makeList(std::move(keys));
    }
    if (method == "values") {
        std::vector<Value> vals;
        vals.reserve(dict->entries.size());
        for (const auto& [_, v] : dict->entries) vals.push_back(v);
        return makeList(std::move(vals));
    }

    throwRuntimeError("Metodo '" + method + "' nao existe em dict", tok);
}

Value Executor::callStringMethod(Value& obj, const std::string& method,
                                  const std::vector<Value>& args, const Token& tok) {
    (void)args;
    if (method == "size") return Value(static_cast<int>(obj.asString().size()));
    throwRuntimeError("Metodo '" + method + "' nao existe em string", tok);
}

// ============================================================================
// ATRIBUICAO COM RESOLUCAO DE ESCOPO
//
// Ordem: Environment (locais/externos) → campos de current_instance
// ============================================================================

void Executor::assignVariable(const std::string& name, Value val, const Token& tok) {
    if (env.assign(name, val)) return;

    if (current_instance) {
        auto it = current_instance->fields.find(name);
        if (it != current_instance->fields.end()) {
            it->second = std::move(val);
            return;
        }
    }

    throwRuntimeError("Variavel '" + name + "' nao encontrada para atribuicao "
                     "(nao deveria ocorrer apos analise semantica)", tok);
}

} // namespace cinza
