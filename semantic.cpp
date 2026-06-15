#include "semantic.h"
#include <sstream>
#include <algorithm>

namespace cinza {

// ============================================================================
// HELPERS INTERNOS (arquivo local)
// ============================================================================

// Remove espaços do início e fim de uma string.
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

// ============================================================================
// TYPE CHECKER
// ============================================================================

// v2.00 #19: Promoção numérica centralizada — única fonte de verdade
// int+int→int  |  int+decimal→decimal  |  decimal+int→decimal  |  decimal+decimal→decimal
static std::string promoteNumeric(const std::string& l, const std::string& r) {
    return (l == "decimal" || r == "decimal") ? "decimal" : "int";
}

// r: "int", "decimal", "string", "bool", "void", "var",
//    "list<int>", "dict<string, int>", "pair<int, string>", "Pessoa"
std::string TypeChecker::typeToString(const Type* type) {
    if (!type) return "void";
    return type->toString();  // delega para a implementação existente em ast.cpp
}

// r: {"int"}, {"string", "int"}, {"string", "list<int>"}
// Lida com aninhamento: "dict<string, list<int>>" → {"string", "list<int>"}
std::vector<std::string> TypeChecker::extractTypeParams(const std::string& type_str) {
    size_t lt = type_str.find('<');
    if (lt == std::string::npos) return {};

    size_t gt = type_str.rfind('>');
    if (gt == std::string::npos || gt <= lt) return {};

    std::string inner = type_str.substr(lt + 1, gt - lt - 1);

    std::vector<std::string> result;
    int         depth   = 0;
    std::string current;

    for (char c : inner) {
        if (c == '<') {
            depth++;
            current += c;
        } else if (c == '>') {
            depth--;
            current += c;
        } else if (c == ',' && depth == 0) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }

    std::string last = trim(current);
    if (!last.empty()) result.push_back(last);

    return result;
}

// r: true se "int" ou "decimal"
bool TypeChecker::isNumeric(const std::string& t) {
    return t == "int" || t == "decimal";
}

// r: true se primitivo puro (sem genérico)
bool TypeChecker::isPrimitive(const std::string& t) {
    return t == "int" || t == "decimal" || t == "string" || t == "bool";
}

// Checa se `value_type` é atribuível a `declared_type`.
// Regras de coerção da Cinza v2:
//   - tipos iguais → sempre OK
//   - "var" declarado → aceita qualquer PRIMITIVO (int/decimal/string/bool)
//   - int → decimal (promoção numérica)
//   - list<S> → list<T>  se S atribuível a T
bool TypeChecker::isAssignable(const std::string& declared, const std::string& value) {
    if (declared == value)     return true;

    // v2.00 #2: var só aceita primitivos — não aceita list/dict/pair/class
    if (declared == "var") {
        return isPrimitive(value);
    }
    if (value == "var") return true; // valor var primitivo ainda não resolvido

    // Promoção numérica: int pode ser atribuído a decimal (v2.00 #19)
    if (declared == "decimal" && value == "int") return true;

    // Promoção em listas: list<S> → list<T> se S atribuível a T (v2.00 #19)
    if (declared.substr(0, 5) == "list<" && value.substr(0, 5) == "list<") {
        auto dp = extractTypeParams(declared);
        auto vp = extractTypeParams(value);
        if (dp.size() == 1 && vp.size() == 1) {
            return isAssignable(dp[0], vp[0]);
        }
    }

    // Promoção em dicts: dict<K,S> → dict<K,T> se S atribuível a T
    if (declared.substr(0, 5) == "dict<" && value.substr(0, 5) == "dict<") {
        auto dp = extractTypeParams(declared);
        auto vp = extractTypeParams(value);
        if (dp.size() == 2 && vp.size() == 2) {
            return isAssignable(dp[0], vp[0]) && isAssignable(dp[1], vp[1]);
        }
    }

    return false;
}

// Retorna o tipo resultante de uma operação binária, ou lança SemanticError.
std::string TypeChecker::checkBinaryOp(const std::string& left,
                                       TokenType          op,
                                       const std::string& right,
                                       const Token&       tok) {
    switch (op) {

        // ── Operadores aritméticos ───────────────────────────────────────
        case TokenType::OP_PLUS:
            if (isNumeric(left) && isNumeric(right))   return promoteNumeric(left, right);
            if (left == "string" && right == "string") return "string";
            // v2.00 #3: concatenação string + primitivo
            if (left == "string" && isPrimitive(right)) return "string";
            if (isPrimitive(left) && right == "string") return "string";
            throw SemanticError(
                "Operador '+' não é suportado entre '" + left + "' e '" + right + "'", tok);

        case TokenType::OP_MINUS:
        case TokenType::OP_MULTIPLY:
        case TokenType::OP_DIVIDE:
        case TokenType::OP_MODULO:
            if (isNumeric(left) && isNumeric(right)) return promoteNumeric(left, right);
            throw SemanticError(
                "Operador '" + tokenTypeToOperatorString(op) +
                "' requer tipos numéricos, mas recebeu '" + left + "' e '" + right + "'", tok);

        // ── Comparadores de ordem ────────────────────────────────────────
        case TokenType::OP_LESS:
        case TokenType::OP_LESS_EQUAL:
        case TokenType::OP_GREATER:
        case TokenType::OP_GREATER_EQUAL:
            if (isNumeric(left) && isNumeric(right))   return "bool";
            if (left == "string" && right == "string") return "bool";
            throw SemanticError(
                "Comparação '" + tokenTypeToOperatorString(op) +
                "' não é suportada entre '" + left + "' e '" + right + "'", tok);

        // ── Igualdade ────────────────────────────────────────────────────
        case TokenType::OP_EQUAL:
        case TokenType::OP_NOT_EQUAL:
            if (left == right)                         return "bool";
            if (isNumeric(left) && isNumeric(right))   return "bool";
            throw SemanticError(
                "Comparação '" + tokenTypeToOperatorString(op) +
                "' não é suportada entre '" + left + "' e '" + right + "'", tok);

        // ── Operadores lógicos ───────────────────────────────────────────
        case TokenType::OP_AND:
        case TokenType::OP_OR:
            if (left == "bool" && right == "bool") return "bool";
            throw SemanticError(
                "Operador lógico '" + tokenTypeToOperatorString(op) +
                "' requer 'bool' nos dois lados, mas recebeu '" + left + "' e '" + right + "'", tok);

        default:
            throw SemanticError("Operador binário desconhecido", tok);
    }
}

// Retorna o tipo resultante de uma operação unária, ou lança SemanticError.
std::string TypeChecker::checkUnaryOp(TokenType          op,
                                      const std::string& operand,
                                      const Token&       tok) {
    switch (op) {
        case TokenType::OP_MINUS:
            if (isNumeric(operand)) return operand;
            throw SemanticError(
                "Operador '-' unário requer tipo numérico, mas recebeu '" + operand + "'", tok);

        case TokenType::OP_NOT:
            if (operand == "bool") return "bool";
            throw SemanticError(
                "Operador '!' requer 'bool', mas recebeu '" + operand + "'", tok);

        default:
            throw SemanticError("Operador unário desconhecido", tok);
    }
}

// ============================================================================
// SYMBOL TABLE
// ============================================================================

// cria um novo escopo vazio no topo da pilha
void SymbolTable::pushScope() {
    scopes.emplace_back(); 
}

// remove o escopo atual, preservando o escopo global
void SymbolTable::popScope() {
    if (scopes.size() > 1) scopes.pop_back(); 
}

// decrara os Symbols e evita redecração
void SymbolTable::declare(const Symbol& sym, const Token& tok) {
    auto& current = scopes.back();                          // Obtém o escopo atual (topo da pilha)
    if (current.count(sym.name)) {                          // Verifica se já existe um símbolo com o mesmo nome neste escopo
        throw SemanticError(
            "Declaração duplicada: '" + sym.name +          // Erro semântico: não é permitido redeclarar no mesmo escopo
            "' já foi declarado neste escopo", tok);
    }
    current[sym.name] = sym;                                // Insere o símbolo no escopo atual
}

// faz uma busca por um IDENTIFIER no escopo atual até a base (global)
Symbol* SymbolTable::resolve(const std::string& name) {
    // Percorre do topo (mais interno) para a base (global)
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
        // Procura o nome no escopo atual
        auto found = it->find(name);         
        // Se encontrou, retorna ponteiro para o símbolo  
        if (found != it->end()) return &found->second;
    }
     // Não encontrado em nenhum escopo
    return nullptr;
}

// sobrecarga de 'resolve' versão constante
const Symbol* SymbolTable::resolve(const std::string& name) const {
    for (auto it = scopes.crbegin(); it != scopes.crend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

// verifica se um IDENTIFIER existe no escopo atual
bool SymbolTable::existsInCurrentScope(const std::string& name) const {
    return scopes.back().count(name) > 0;
}

// ============================================================================
// SEMANTIC ANALYZER — UTILIDADES
// ============================================================================

[[noreturn]] void SemanticAnalyzer::throwError(const std::string& msg,
                                               const Token& tok) const {
    throw SemanticError(msg, tok);
}

[[noreturn]] void SemanticAnalyzer::throwError(const std::string& msg,
                                               int line, int col) const {
    throw SemanticError(msg, line, col);
}

// ============================================================================
// SEMANTIC ANALYZER — PRÉ-REGISTRO
// Percorre o topo da AST e registra nomes de funções e classes no escopo
// global ANTES de analisar corpos. Isso permite referências para frente:
//   fn a() { b(); }
//   fn b() { a(); }
// ============================================================================

// faz o pré registro de funções e classes
void SemanticAnalyzer::preRegisterDeclarations(const Program& program) {
    for (const auto& stmt : program.statements) {
        if (auto fn = dynamic_cast<FunctionDecl*>(stmt.get())) {
            registerFunction(*fn);
        } else if (auto cls = dynamic_cast<ClassDecl*>(stmt.get())) {
            registerClass(*cls);
        }
    }
}

// Registra uma função no escopo global como Symbol::Kind::FUNCTION.
void SemanticAnalyzer::registerFunction(const FunctionDecl& fn) {
    Symbol sym;
    sym.name          = fn.name;
    sym.kind          = Symbol::Kind::FUNCTION;
    
    sym.return_type   = TypeChecker::typeToString(fn.return_type.get());
    sym.resolved_type = sym.return_type;
    sym.line          = fn.token.line;
    sym.column        = fn.token.column;
    sym.decl_ptr      = &fn;  // v2.00 #4: guarda ponteiro para AST

    for (const auto& param : fn.parameters) {
        sym.param_types.push_back(TypeChecker::typeToString(param.type.get()));
    }

    symbol_table.declare(sym, fn.token);
}

// Registra uma classe em class_table E como Symbol::Kind::CLASS no escopo global.
void SemanticAnalyzer::registerClass(const ClassDecl& cls) {
    ClassInfo info;
    info.name = cls.class_name;
    info.line = cls.token.line;

    // Campos privados (fora do pub{})
    for (const auto& field : cls.fields) {
        FieldInfo fi;
        fi.type_str  = TypeChecker::typeToString(field.type.get());
        fi.is_public = false;
        fi.line      = field.token.line;
        info.fields[field.name] = fi;
    }

    // Construtor
    if (cls.constructor) {
        for (const auto& param : cls.constructor->parameters) {
            info.ctor_param_types.push_back(
                TypeChecker::typeToString(param.type.get()));
        }
    }

    // Métodos privados (fora do pub{})
    for (const auto& mptr : cls.priv_methods) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(mptr.get())) {
            MethodInfo mi;
            mi.return_type = TypeChecker::typeToString(fn->return_type.get());
            mi.is_public   = false;
            mi.line        = fn->token.line;
            for (const auto& p : fn->parameters) {
                mi.param_types.push_back(TypeChecker::typeToString(p.type.get()));
            }
            info.methods[fn->name] = mi;
        }
    }

    // Métodos públicos (dentro do pub{})
    for (const auto& mptr : cls.pub_methods) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(mptr.get())) {
            MethodInfo mi;
            mi.return_type = TypeChecker::typeToString(fn->return_type.get());
            mi.is_public   = true;
            mi.line        = fn->token.line;
            for (const auto& p : fn->parameters) {
                mi.param_types.push_back(TypeChecker::typeToString(p.type.get()));
            }
            info.methods[fn->name] = mi;
        }
    }

    // Registra na class_table
    class_table[cls.class_name] = std::move(info);

    // Registra no escopo global como CLASS (para checar em declarações de variável)
    Symbol sym;
    sym.name          = cls.class_name;
    sym.kind          = Symbol::Kind::CLASS;
    
    sym.resolved_type = cls.class_name;
    sym.line          = cls.token.line;
    sym.column        = cls.token.column;
    symbol_table.declare(sym, cls.token);
}

// ============================================================================
// SEMANTIC ANALYZER — PONTO DE ENTRADA
// ============================================================================

void SemanticAnalyzer::analyze(Program& program) {
    // Passo 1: registrar todos os nomes de topo antes de analisar corpos.
    preRegisterDeclarations(program);

    // Passo 2: analisar cada statement na ordem.
    for (const auto& stmt : program.statements) {
        analyzeStmt(stmt.get());
    }
}

// ============================================================================
// SEMANTIC ANALYZER — DISPATCHER DE STATEMENTS
// ============================================================================

// verifica qual o tipo do stmt e chama a função específica pra analisar
void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (!stmt) return;

    if (auto s = dynamic_cast<VarDeclStmt*>       (stmt)) { analyzeVarDecl(s);         return; }
    if (auto s = dynamic_cast<AssignmentStmt*>    (stmt)) { analyzeAssignment(s);       return; }
    if (auto s = dynamic_cast<IndexAssignmentStmt*>(stmt)){ analyzeIndexAssignment(s);  return; }
    if (auto s = dynamic_cast<ExprStmt*>          (stmt)) { analyzeExprStmt(s);         return; }
    if (auto s = dynamic_cast<BlockStmt*>         (stmt)) { analyzeBlock(s);            return; }
    if (auto s = dynamic_cast<IfStmt*>            (stmt)) { analyzeIf(s);               return; }
    if (auto s = dynamic_cast<WhileStmt*>         (stmt)) { analyzeWhile(s);            return; }
    if (auto s = dynamic_cast<ForStmt*>           (stmt)) { analyzeFor(s);              return; }
    if (auto s = dynamic_cast<ReturnStmt*>        (stmt)) { analyzeReturn(s);           return; }
    if (auto s = dynamic_cast<FunctionDecl*>      (stmt)) { analyzeFunctionDecl(s);     return; }
    if (auto s = dynamic_cast<ClassDecl*>         (stmt)) { analyzeClassDecl(s);        return; }

    throwError("Tipo de statement não reconhecido pelo analisador semântico",
               stmt->token);
}

// ============================================================================
// SEMANTIC ANALYZER — STATEMENTS
// ============================================================================

// Bloco { stmt* } — empurra e desempilha seu próprio escopo
void SemanticAnalyzer::analyzeBlock(BlockStmt* block) {
    symbol_table.pushScope();
    for (const auto& s : block->statements) {
        analyzeStmt(s.get());
    }
    symbol_table.popScope();
}

// Declaração de variável:  tipo nome [= inicializador];
void SemanticAnalyzer::analyzeVarDecl(VarDeclStmt* stmt) {
    std::string declared_type = TypeChecker::typeToString(stmt->type.get());

    // v2.00 #5/#15: void é ILEGAL em variáveis, listas, dicts, pares, parâmetros
    if (declared_type == "void" ||
        declared_type.find("void") != std::string::npos) {
        throwError(
            "Tipo 'void' não pode ser usado em variáveis. "
            "'void' é reservado exclusivamente para retorno de funções.",
            stmt->token);
    }

    // v2.00 #2: var proibido em tipos compostos
    if (stmt->type->kind == Type::Kind::LIST) {
        auto params = TypeChecker::extractTypeParams(declared_type);
        if (!params.empty() && params[0] == "var") {
            throwError(
                "Tipo 'var' não é permitido dentro de 'list<>'. "
                "Use um tipo primitivo explícito: list<int>, list<string>, etc.",
                stmt->token);
        }
    }
    // v2.00 #13: Chave de dict deve ser primitiva mesmo na declaração do tipo
    if (stmt->type->kind == Type::Kind::DICT) {
        auto params = TypeChecker::extractTypeParams(declared_type);
        if (!params.empty() && !TypeChecker::isPrimitive(params[0])) {
            throwError(
                "Chave de dict deve ser um tipo primitivo (int, decimal, string, bool), "
                "mas recebeu '" + params[0] + "'. "
                "Tipos compostos como list<>, pair<> e classes não podem ser chaves.",
                stmt->token);
        }
        for (auto& p : params) {
            if (p == "var") {
                throwError(
                    "Tipo 'var' não é permitido dentro de 'dict<>'. "
                    "Use tipos primitivos explícitos: dict<string, int>, etc.",
                    stmt->token);
            }
        }
    }
    if (stmt->type->kind == Type::Kind::PAIR) {
        auto params = TypeChecker::extractTypeParams(declared_type);
        for (auto& p : params) {
            if (p == "var") {
                throwError(
                    "Tipo 'var' não é permitido dentro de 'pair<>'. "
                    "Use tipos explícitos: pair<string, int>, etc.",
                    stmt->token);
            }
        }
    }

    // Tipo CUSTOM deve ser uma classe declarada
    if (stmt->type->kind == Type::Kind::CUSTOM) {
        if (!class_table.count(stmt->type->name)) {
            throwError("Tipo desconhecido '" + stmt->type->name + "'", stmt->token);
        }
    }

    Symbol sym;
    sym.name     = stmt->name;
    sym.kind     = Symbol::Kind::VAR;
    sym.line     = stmt->token.line;
    sym.column   = stmt->token.column;
    sym.is_const = stmt->is_const;

    if (stmt->initializer) {
        std::string init_type = analyzeExpr(stmt->initializer.get());

        if (declared_type == "var") {
            // var infere o tipo do inicializador — apenas primitivos
            if (!TypeChecker::isPrimitive(init_type)) {
                throwError(
                    "Inferência 'var' só é permitida para tipos primitivos "
                    "(int, decimal, string, bool), mas recebeu '" + init_type + "'. "
                    "Declare o tipo explicitamente.",
                    stmt->token);
            }
            sym.resolved_type = init_type;
        } else {
            if (!TypeChecker::isAssignable(declared_type, init_type)) {
                throwError(
                    "Tipo incompatível na declaração de '" + stmt->name +
                    "': declarado '" + declared_type + "', mas atribuído '" + init_type + "'",
                    stmt->token);
            }
            sym.resolved_type = declared_type;
        }
    } else {
        // Sem inicializador: somente list<T> e dict<K,V> devem chegar aqui,
        // porque o parser rejeita declarações sem '=' para qualquer outro tipo.
        // Blindagem defensiva: se um tipo não-coleção chegar até aqui, há
        // inconsistência na pipeline (parser permitiu o que não deveria).
        if (stmt->type->kind != Type::Kind::LIST &&
            stmt->type->kind != Type::Kind::DICT) {
            throwError(
                "Erro interno: variável '" + stmt->name + "' de tipo '" + declared_type +
                "' chegou ao semântico sem inicializador. "
                "O parser deveria ter rejeitado esta declaração.",
                stmt->token);
        }
        sym.resolved_type = declared_type;
    }

    symbol_table.declare(sym, stmt->token);
}

// Atribuição:  variavel = expressão;
//   - Verifica se a variável foi declarada
// Atribuição:  variavel = expressão;
void SemanticAnalyzer::analyzeAssignment(AssignmentStmt* stmt) {
    Symbol* sym = symbol_table.resolve(stmt->variable_name);

    if (!sym) {
        throwError("Variável '" + stmt->variable_name + "' não declarada",
                   stmt->token);
    }
    if (sym->kind == Symbol::Kind::FUNCTION) {
        throwError("'" + stmt->variable_name + "' é uma função e não pode ser reatribuída",
                   stmt->token);
    }
    if (sym->kind == Symbol::Kind::CLASS) {
        throwError("'" + stmt->variable_name + "' é uma classe e não pode ser atribuída",
                   stmt->token);
    }

    // const: reatribuição é sempre proibida
    if (sym->is_const) {
        throwError(
            "Constante '" + stmt->variable_name + "' não pode ser reatribuída. "
            "Remova o 'const' na declaração se precisar de variável mutável.",
            stmt->token);
    }

    std::string rhs_type = analyzeExpr(stmt->value.get());

    if (!TypeChecker::isAssignable(sym->resolved_type, rhs_type)) {
        throwError(
            "Tipo incompatível na atribuição de '" + stmt->variable_name +
            "': esperado '" + sym->resolved_type + "', mas recebeu '" + rhs_type + "'",
            stmt->token);
    }
}

// Expressão-statement:  chamada de função ou método usado como instrução.
void SemanticAnalyzer::analyzeExprStmt(ExprStmt* stmt) {
    analyzeExpr(stmt->expression.get());
}

// v2.00 #8: Atribuição por índice: nome[idx] = valor
//   - Para list<T>: idx deve ser int (não negativo verificado em runtime)
//   - Para dict<K,V>: idx deve ser K; valor deve ser V
void SemanticAnalyzer::analyzeIndexAssignment(IndexAssignmentStmt* stmt) {
    Symbol* sym = symbol_table.resolve(stmt->object_name);
    if (!sym) {
        throwError("Variável '" + stmt->object_name + "' não declarada", stmt->token);
    }
    // const: subscript assignment proibido
    if (sym->is_const) {
        throwError(
            "Constante '" + stmt->object_name + "' não pode ser modificada. "
            "Atribuição por índice '[]='' é proibida em coleções const.",
            stmt->token);
    }

    const std::string& obj_type = sym->resolved_type;
    std::string idx_type = analyzeExpr(stmt->index.get());
    std::string val_type = analyzeExpr(stmt->value.get());

    if (obj_type.substr(0, 5) == "list<") {
        // Índice deve ser int
        if (idx_type != "int") {
            throwError("Índice de lista deve ser 'int', mas recebeu '" + idx_type + "'",
                       stmt->token);
        }
        // Índice negativo literal é erro semântico.
        // Detecta tanto LiteralExpr(-1) quanto UnaryExpr(-, LiteralExpr(1))
        {
            bool is_negative_literal = false;
            if (auto* lit = dynamic_cast<LiteralExpr*>(stmt->index.get())) {
                if (std::holds_alternative<int>(lit->value) && std::get<int>(lit->value) < 0)
                    is_negative_literal = true;
            }
            if (auto* un = dynamic_cast<UnaryExpr*>(stmt->index.get())) {
                if (un->op == TokenType::OP_MINUS) {
                    if (auto* inner = dynamic_cast<LiteralExpr*>(un->operand.get())) {
                        if (std::holds_alternative<int>(inner->value))
                            is_negative_literal = true;
                    }
                }
            }
            if (is_negative_literal)
                throwError("SemanticError: índice negativo em lista não é permitido. "
                           "Índices de lista devem ser inteiros não-negativos.",
                           stmt->token);
        }
        auto params = TypeChecker::extractTypeParams(obj_type);
        std::string T = params.empty() ? "var" : params[0];
        if (!TypeChecker::isAssignable(T, val_type)) {
            throwError("Valor de tipo '" + val_type +
                       "' incompatível com elemento da lista '" + T + "'", stmt->token);
        }
    } else if (obj_type.substr(0, 5) == "dict<") {
        auto params   = TypeChecker::extractTypeParams(obj_type);
        std::string K = params.size() >= 1 ? params[0] : "var";
        std::string V = params.size() >= 2 ? params[1] : "var";

        if (!TypeChecker::isAssignable(K, idx_type)) {
            throwError("Chave de tipo '" + idx_type +
                       "' incompatível com K='" + K + "' do dicionário", stmt->token);
        }
        if (!TypeChecker::isAssignable(V, val_type)) {
            throwError("Valor de tipo '" + val_type +
                       "' incompatível com V='" + V + "' do dicionário", stmt->token);
        }
    } else {
        throwError("Operador '[]=' não é suportado no tipo '" + obj_type + "'", stmt->token);
    }
}

// If-else:  if (cond) then [else else_branch]
//   - Condição deve ser bool
void SemanticAnalyzer::analyzeIf(IfStmt* stmt) {
    std::string cond_type = analyzeExpr(stmt->condition.get());
    if (cond_type != "bool") {
        throwError(
            "Condição do 'if' deve ser 'bool', mas recebeu '" + cond_type + "'",
            stmt->token);
    }
    analyzeStmt(stmt->then_branch.get());
    if (stmt->else_branch) analyzeStmt(stmt->else_branch.get());
}

// While:  while (cond) body
//   - Condição deve ser bool
void SemanticAnalyzer::analyzeWhile(WhileStmt* stmt) {
    std::string cond_type = analyzeExpr(stmt->condition.get());
    if (cond_type != "bool") {
        throwError(
            "Condição do 'while' deve ser 'bool', mas recebeu '" + cond_type + "'",
            stmt->token);
    }
    analyzeStmt(stmt->body.get());
}

// For:  for (tipo iterador in iterável) body
//   - Iterável deve ser list<T>
//   - Tipo do iterador deve ser compatível com T (ou `var` para inferir)
void SemanticAnalyzer::analyzeFor(ForStmt* stmt) {
    // tipo lista list<T>
    std::string iter_type = analyzeExpr(stmt->iterable.get());

    if (iter_type.substr(0, 5) != "list<") {
        throwError(
            "'for' requer um 'list<T>' como iterável, mas recebeu '" + iter_type + "'",
            stmt->token);
    }

    auto params    = TypeChecker::extractTypeParams(iter_type);
    // tipo da lista 'var' ou int, decimal, bool, str ...
    std::string elem_type = params.empty() ? "var" : params[0];
    // tipo do iterador
    std::string declared_iter = TypeChecker::typeToString(stmt->type_iterator.get());

    // Se `var`, infere pelo tipo do elemento
    if (declared_iter == "var") {
        declared_iter = elem_type;
    } else if (!TypeChecker::isAssignable(declared_iter, elem_type)) {
        throwError(
            "Tipo do iterador '" + declared_iter +
            "' é incompatível com o elemento da lista '" + elem_type + "'",
            stmt->token);
    }

    // Escopo próprio do for: o iterador só existe dentro do corpo
    symbol_table.pushScope();

    Symbol iter_sym;
    iter_sym.name          = stmt->iterator_name;
    iter_sym.kind          = Symbol::Kind::VAR;
    iter_sym.resolved_type = declared_iter;
    iter_sym.line          = stmt->token.line;
    iter_sym.column        = stmt->token.column;
    
    symbol_table.declare(iter_sym, stmt->token);

    analyzeStmt(stmt->body.get());

    symbol_table.popScope();
}

// Return:  return [expr];
//   - Dentro de função void: QUALQUER return é erro (filosofia Cinza)
//   - Dentro de função não-void: expr deve ter tipo compatível com retorno
//   - Fora de função: erro
void SemanticAnalyzer::analyzeReturn(ReturnStmt* stmt) {
    if (!inside_function) {
        throwError("'return' fora de função", stmt->token);
    }

    if (current_function_return_type == "void") {
        // Funções void não podem ter nenhum return
        throwError(
            "Função void não pode ter 'return'. "
            "Remova o statement 'return' ou mude o tipo de retorno.",
            stmt->token);
    }

    // Função não-void: precisa ter expressão de retorno
    if (!stmt->value) {
        throwError(
            "Função com retorno '" + current_function_return_type +
            "' precisa retornar um valor no 'return'",
            stmt->token);
    }

    std::string ret_type = analyzeExpr(stmt->value.get());
    if (!TypeChecker::isAssignable(current_function_return_type, ret_type)) {
        throwError(
            "Tipo de retorno incompatível: esperado '" +
            current_function_return_type + "', mas retornando '" + ret_type + "'",
            stmt->token);
    }
}

// Declaração de função:  fn nome(params) -> tipo { body }
//   - Não re-registra (já feito em preRegisterDeclarations)
//   - Empurra escopo para parâmetros
//   - Verifica regras de retorno
void SemanticAnalyzer::analyzeFunctionDecl(FunctionDecl* stmt) {
    std::string ret_type = TypeChecker::typeToString(stmt->return_type.get());

    // v2.00 #4: Se algum parâmetro é 'var', adiar análise do corpo para o call site.
    // O corpo será re-analisado com os tipos concretos em analyzeCall.
    bool has_var_params = false;
    for (const auto& param : stmt->parameters) {
        if (param.type->kind == Type::Kind::VAR) {
            has_var_params = true;
            break;
        }
    }
    // Retorno var também é adiado
    bool has_var_return = (stmt->return_type->kind == Type::Kind::VAR);

    if (has_var_params || has_var_return) {
        // Valida que os tipos não-var dos parâmetros existem, mas não analisa o corpo
        for (const auto& param : stmt->parameters) {
            if (param.type->kind == Type::Kind::CUSTOM &&
                !class_table.count(param.type->name)) {
                throwError("Tipo desconhecido '" + param.type->name +
                           "' no parâmetro '" + param.name + "'", param.token);
            }
        }
        return; // corpo analisado no call site
    }

    std::string saved_ret    = current_function_return_type;
    bool        saved_inside = inside_function;

    current_function_return_type = ret_type;
    inside_function              = true;

    symbol_table.pushScope();

    for (const auto& param : stmt->parameters) {
        std::string ptype = TypeChecker::typeToString(param.type.get());

        if (param.type->kind == Type::Kind::CUSTOM &&
            !class_table.count(param.type->name)) {
            throwError("Tipo desconhecido '" + param.type->name +
                       "' no parâmetro '" + param.name + "'", param.token);
        }

        Symbol p;
        p.name          = param.name;
        p.kind          = Symbol::Kind::PARAMETER;
        
        p.resolved_type = ptype;
        p.is_const      = param.is_const;  // const: parâmetro não pode ser reatribuído
        p.line          = param.token.line;
        p.column        = param.token.column;
        symbol_table.declare(p, param.token);
    }

    auto* body_block = dynamic_cast<BlockStmt*>(stmt->body.get());
    if (body_block) {
        for (const auto& s : body_block->statements)
            analyzeStmt(s.get());
    } else {
        analyzeStmt(stmt->body.get());
    }

    if (ret_type != "void") {
        if (!allPathsReturn(stmt->body.get())) {
            throwError(
                "Função '" + stmt->name +
                "' não retorna em todos os caminhos de execução",
                stmt->token);
        }
    }

    symbol_table.popScope();

    current_function_return_type = saved_ret;
    inside_function              = saved_inside;
}

// Declaração de classe:  class Nome { campos... pub { métodos... } }
//   - Não re-registra (já feito em registerClass)
//   - Analisa campos, construtor e métodos com acesso à classe
void SemanticAnalyzer::analyzeClassDecl(ClassDecl* stmt) {
    // Verifica que a classe foi registrada (sempre verdade se chegou aqui)
    auto it = class_table.find(stmt->class_name);
    if (it == class_table.end()) {
        throwError("Classe '" + stmt->class_name + "' não registrada", stmt->token);
    }

    std::string saved_class = current_class_name;
    current_class_name      = stmt->class_name;

    // ── Escopo dos campos (visível dentro de todos os métodos da classe) ──
    symbol_table.pushScope();

    // Registra campos no escopo da classe para que sejam visíveis
    // dentro de todos os métodos. Campos list<T>/dict<K,V> sem inicializador
    // têm inicialização vazia implícita; os demais obrigatoriamente possuem
    // inicializador (garantido pelo parser).
    for (const auto& field : stmt->fields) {
        Symbol fsym;
        fsym.name          = field.name;
        fsym.kind          = Symbol::Kind::VAR;
        fsym.resolved_type = TypeChecker::typeToString(field.type.get());
        fsym.line          = field.token.line;
        fsym.column        = field.token.column;

        // Analisa inicializador do campo (se houver).
        // Invariante: campo sem inicializador só é válido para list<T>/dict<K,V>.
        if (field.initializer) {
            std::string init_type = analyzeExpr(field.initializer.get());
            if (!TypeChecker::isAssignable(fsym.resolved_type, init_type)) {
                throwError(
                    "Campo '" + field.name + "': tipo '" + init_type +
                    "' é incompatível com '" + fsym.resolved_type + "'",
                    field.token);
            }
        } else {
            // Blindagem defensiva: campo não-coleção sem inicializador indica
            // inconsistência na pipeline — o parser deveria ter rejeitado.
            if (field.type->kind != Type::Kind::LIST &&
                field.type->kind != Type::Kind::DICT) {
                throwError(
                    "Erro interno: campo '" + field.name + "' de tipo '" +
                    fsym.resolved_type + "' chegou ao semântico sem inicializador. "
                    "O parser deveria ter rejeitado esta declaração.",
                    field.token);
            }
        }

        symbol_table.declare(fsym, field.token);
    }

    // ── Analisa o construtor ──────────────────────────────────────────────
    if (stmt->constructor) {
        auto& ctor = *stmt->constructor;

        std::string saved_ret    = current_function_return_type;
        bool        saved_inside = inside_function;
        current_function_return_type = "void";
        inside_function              = true;

        symbol_table.pushScope();

        for (const auto& param : ctor.parameters) {
            Symbol p;
            p.name          = param.name;
            p.kind          = Symbol::Kind::PARAMETER;
            
            p.resolved_type = TypeChecker::typeToString(param.type.get());
            p.is_const      = param.is_const;
            p.line          = param.token.line;
            p.column        = param.token.column;
            symbol_table.declare(p, param.token);
        }

        // Analisa o corpo do construtor
        if (auto* blk = dynamic_cast<BlockStmt*>(ctor.body.get())) {
            for (const auto& s : blk->statements) analyzeStmt(s.get());
        } else {
            analyzeStmt(ctor.body.get());
        }

        symbol_table.popScope();

        current_function_return_type = saved_ret;
        inside_function              = saved_inside;
    }

    // ── Analisa métodos privados ──────────────────────────────────────────
    for (const auto& mptr : stmt->priv_methods) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(mptr.get())) {
            analyzeFunctionDecl(fn);
        }
    }

    // ── Analisa métodos públicos ──────────────────────────────────────────
    for (const auto& mptr : stmt->pub_methods) {
        if (auto* fn = dynamic_cast<FunctionDecl*>(mptr.get())) {
            analyzeFunctionDecl(fn);
        }
    }

    // Remove o escopo dos campos da classe
    symbol_table.popScope();

    current_class_name = saved_class;
}

// ============================================================================
// SEMANTIC ANALYZER — DISPATCHER DE EXPRESSÕES
// Cada função de análise:
//   1. Analisa recursivamente os filhos
//   2. Determina o tipo resultante
//   3. Anota expr->resolved_type
//   4. Retorna o tipo
// ============================================================================
std::string SemanticAnalyzer::analyzeExpr(Expr* expr) {
    if (!expr) return "void";

    if (auto e = dynamic_cast<LiteralExpr*>      (expr)) return analyzeLiteral(e);
    if (auto e = dynamic_cast<IdentifierExpr*>   (expr)) return analyzeIdentifier(e);
    if (auto e = dynamic_cast<BinaryExpr*>       (expr)) return analyzeBinary(e);
    if (auto e = dynamic_cast<UnaryExpr*>        (expr)) return analyzeUnary(e);
    if (auto e = dynamic_cast<CallExpr*>         (expr)) return analyzeCall(e);
    if (auto e = dynamic_cast<MethodCallExpr*>   (expr)) return analyzeMethodCall(e);
    if (auto e = dynamic_cast<MemberAccessExpr*> (expr)) return analyzeMemberAccess(e);
    if (auto e = dynamic_cast<IndexAccessExpr*>  (expr)) return analyzeIndexAccess(e);
    if (auto e = dynamic_cast<NewExpr*>          (expr)) return analyzeNew(e);
    if (auto e = dynamic_cast<ListLiteralExpr*>  (expr)) return analyzeListLiteral(e);
    if (auto e = dynamic_cast<DictLiteralExpr*>  (expr)) return analyzeDictLiteral(e);
    if (auto e = dynamic_cast<PairLiteralExpr*>  (expr)) return analyzePairLiteral(e);

    throwError("Tipo de expressão não reconhecido pelo analisador semântico",
               expr->token);
}

// ============================================================================
// SEMANTIC ANALYZER — EXPRESSÕES
// ============================================================================

// Literal:  42, 3.14, "hello", true/false
std::string SemanticAnalyzer::analyzeLiteral(LiteralExpr* expr) {
    std::string type;

    if      (std::holds_alternative<int>        (expr->value)) type = "int";
    else if (std::holds_alternative<double>     (expr->value)) type = "decimal";
    else if (std::holds_alternative<std::string>(expr->value)) type = "string";
    else if (std::holds_alternative<bool>       (expr->value)) type = "bool";
    else throwError("Literal com tipo desconhecido", expr->token);

    return expr->resolved_type = type;
}

// Identificador:  variavel_name — deve estar declarado
std::string SemanticAnalyzer::analyzeIdentifier(IdentifierExpr* expr) {
    const Symbol* sym = symbol_table.resolve(expr->name);

    if (!sym) {
        throwError("Variável '" + expr->name + "' não declarada", expr->token);
    }
    return expr->resolved_type = sym->resolved_type;
}

// Operação binária:  left OP right
std::string SemanticAnalyzer::analyzeBinary(BinaryExpr* expr) {
    std::string left_type  = analyzeExpr(expr->left.get());
    std::string right_type = analyzeExpr(expr->right.get());

    std::string result = TypeChecker::checkBinaryOp(
        left_type, expr->op, right_type, expr->token);

    return expr->resolved_type = result;
}

// Operação unária:  OP operand
std::string SemanticAnalyzer::analyzeUnary(UnaryExpr* expr) {
    std::string operand_type = analyzeExpr(expr->operand.get());

    std::string result = TypeChecker::checkUnaryOp(
        expr->op, operand_type, expr->token);

    return expr->resolved_type = result;
}

// Chamada de função:  nome(args)
//   - Suporta built-in "print"
//   - Verifica existência, aridade e tipos dos argumentos
std::string SemanticAnalyzer::analyzeCall(CallExpr* expr) {
    // ── Built-in: print ───────────────────────────────────────────────────
    if (expr->function_name == "print") {
        for (const auto& arg : expr->arguments) {
            analyzeExpr(arg.get());  // aceita qualquer tipo
        }
        return expr->resolved_type = "void";
    }

    // ── Função declarada pelo usuário ─────────────────────────────────────
    const Symbol* sym = symbol_table.resolve(expr->function_name);

    // ── Chamada implícita a método da própria classe ──────────────────────
    // Dentro de um método, nome() sem objeto refere-se a this.nome().
    // O parser gera CallExpr, mas semanticamente é um MethodCall implícito.
    if (!sym && !current_class_name.empty()) {
        auto cls_it = class_table.find(current_class_name);
        if (cls_it != class_table.end()) {
            auto meth_it = cls_it->second.methods.find(expr->function_name);
            if (meth_it != cls_it->second.methods.end()) {
                const MethodInfo& mi = meth_it->second;
                if (expr->arguments.size() != mi.param_types.size()) {
                    throwError(
                        "Aridade incorreta em '" + expr->function_name +
                        "': esperados " + std::to_string(mi.param_types.size()) +
                        " argumento(s), mas recebeu " +
                        std::to_string(expr->arguments.size()), expr->token);
                }
                for (size_t i = 0; i < expr->arguments.size(); ++i) {
                    std::string at = analyzeExpr(expr->arguments[i].get());
                    if (!TypeChecker::isAssignable(mi.param_types[i], at)) {
                        throwError(
                            "Argumento " + std::to_string(i + 1) + " de '" +
                            expr->function_name + "': esperado '" +
                            mi.param_types[i] + "', mas recebeu '" + at + "'",
                            expr->token);
                    }
                }
                return expr->resolved_type = mi.return_type;
            }
        }
    }

    if (!sym) {
        throwError("Funcao '" + expr->function_name + "' nao declarada",
                   expr->token);
    }
    if (sym->kind != Symbol::Kind::FUNCTION) {
        throwError("'" + expr->function_name + "' não é uma função", expr->token);
    }

    // Verificação de aridade
    if (expr->arguments.size() != sym->param_types.size()) {
        throwError(
            "Aridade incorreta em '" + expr->function_name +
            "': esperados " + std::to_string(sym->param_types.size()) +
            " argumento(s), mas recebeu " +
            std::to_string(expr->arguments.size()),
            expr->token);
    }

    // Coleta tipos concretos dos argumentos
    std::vector<std::string> arg_types;
    arg_types.reserve(expr->arguments.size());
    for (const auto& arg : expr->arguments)
        arg_types.push_back(analyzeExpr(arg.get()));

    // v2.00 #4: Função com parâmetros 'var' → re-análise do corpo com tipos concretos
    bool has_var = false;
    for (auto& pt : sym->param_types)
        if (pt == "var") { has_var = true; break; }
    if (sym->return_type == "var") has_var = true;

    if (has_var && sym->decl_ptr) {
        const FunctionDecl* fn = sym->decl_ptr;

        // Valida: var só aceita primitivos
        for (size_t i = 0; i < fn->parameters.size(); ++i) {
            if (fn->parameters[i].type->kind == Type::Kind::VAR) {
                if (!TypeChecker::isPrimitive(arg_types[i])) {
                    throwError(
                        "Argumento " + std::to_string(i + 1) + " de '" +
                        expr->function_name + "': parâmetro 'var' só aceita "
                        "primitivos (int, decimal, string, bool), mas recebeu '" +
                        arg_types[i] + "'",
                        expr->token);
                }
            } else {
                // Parâmetro com tipo explícito — verifica normalmente
                if (!TypeChecker::isAssignable(sym->param_types[i], arg_types[i])) {
                    throwError(
                        "Argumento " + std::to_string(i + 1) + " de '" +
                        expr->function_name + "': esperado '" + sym->param_types[i] +
                        "', mas recebeu '" + arg_types[i] + "'",
                        expr->token);
                }
            }
        }

        // Re-analisa o corpo com os tipos concretos
        std::string saved_ret    = current_function_return_type;
        bool        saved_inside = inside_function;
        inside_function          = true;

        symbol_table.pushScope();

        // Injeta parâmetros com tipos concretos resolvidos
        for (size_t i = 0; i < fn->parameters.size(); ++i) {
            std::string resolved = (fn->parameters[i].type->kind == Type::Kind::VAR)
                                   ? arg_types[i]
                                   : sym->param_types[i];
            Symbol p;
            p.name          = fn->parameters[i].name;
            p.kind          = Symbol::Kind::PARAMETER;
            
            p.resolved_type = resolved;
            p.line          = fn->parameters[i].token.line;
            p.column        = fn->parameters[i].token.column;
            symbol_table.declare(p, fn->parameters[i].token);
        }

        // Retorno var: será inferido pelo tipo do return statement
        current_function_return_type =
            (fn->return_type->kind == Type::Kind::VAR) ? "__var_infer__" : sym->return_type;

        std::string inferred_return = "void";
        auto* body_block = dynamic_cast<const BlockStmt*>(fn->body.get());
        if (body_block) {
            for (const auto& s : body_block->statements) {
                // Captura tipo de return para inferência
                if (auto* ret = dynamic_cast<const ReturnStmt*>(s.get())) {
                    if (ret->value) {
                        inferred_return = analyzeExpr(
                            const_cast<Expr*>(ret->value.get()));
                    }
                } else {
                    analyzeStmt(const_cast<Stmt*>(s.get()));
                }
            }
        }

        symbol_table.popScope();
        current_function_return_type = saved_ret;
        inside_function              = saved_inside;

        // Verifica compatibilidade de operações (somar bool+int → deve dar erro)
        std::string call_return = (fn->return_type->kind == Type::Kind::VAR)
                                  ? inferred_return
                                  : sym->return_type;

        return expr->resolved_type = call_return;
    }

    // Função com tipos explícitos — caminho normal
    for (size_t i = 0; i < arg_types.size(); ++i) {
        if (!TypeChecker::isAssignable(sym->param_types[i], arg_types[i])) {
            throwError(
                "Argumento " + std::to_string(i + 1) + " de '" +
                expr->function_name + "': esperado '" + sym->param_types[i] +
                "', mas recebeu '" + arg_types[i] + "'",
                expr->token);
        }
    }

    return expr->resolved_type = sym->return_type;
}

// Chamada de método:  objeto.metodo(args)
//   - Suporta built-ins de list, dict, string
//   - Suporta métodos de classes definidas pelo usuário
std::string SemanticAnalyzer::analyzeMethodCall(MethodCallExpr* expr) {
    std::string obj_type = analyzeExpr(expr->object.get());

    // Helper: verifica se o objeto receptor é uma constante
    // Usado para bloquear métodos mutantes (.add, .remove) em const
    auto isConstObject = [&]() -> bool {
        if (auto* id = dynamic_cast<IdentifierExpr*>(expr->object.get())) {
            const Symbol* s = symbol_table.resolve(id->name);
            return s && s->is_const;
        }
        return false;
    };

    // ── list<T> built-ins ─────────────────────────────────────────────────
    if (obj_type.substr(0, 5) == "list<") {
        auto params    = TypeChecker::extractTypeParams(obj_type);
        std::string T  = params.empty() ? "var" : params[0];

        const std::string& m = expr->method_name;

        if (m == "size") {
            if (!expr->arguments.empty())
                throwError("'list.size()' não aceita argumentos", expr->token);
            return expr->resolved_type = "int";
        }
        if (m == "has") {
            if (expr->arguments.size() != 1)
                throwError("'list.has(idx)' requer exatamente 1 argumento", expr->token);
            std::string idx_type = analyzeExpr(expr->arguments[0].get());
            if (idx_type != "int")
                throwError("'list.has(idx)' requer índice 'int', recebeu '" + idx_type + "'",
                           expr->token);
            return expr->resolved_type = "bool";
        }
        if (m == "add") {
            if (isConstObject())
                throwError("Não é possível chamar '.add()' em lista const. "
                           "Listas const não podem ser modificadas.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'list.add(elem)' requer exatamente 1 argumento", expr->token);
            std::string elem_type = analyzeExpr(expr->arguments[0].get());
            if (!TypeChecker::isAssignable(T, elem_type))
                throwError("'list.add': tipo '" + elem_type + "' incompatível com elemento '" + T + "'",
                           expr->token);
            return expr->resolved_type = "void";
        }
        if (m == "remove") {
            if (isConstObject())
                throwError("Não é possível chamar '.remove()' em lista const. "
                           "Listas const não podem ser modificadas.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'list.remove(idx)' requer exatamente 1 argumento", expr->token);
            std::string idx_type = analyzeExpr(expr->arguments[0].get());
            if (idx_type != "int")
                throwError("'list.remove(idx)' requer índice 'int', recebeu '" + idx_type + "'",
                           expr->token);
            return expr->resolved_type = "void";
        }
        throwError("Método '" + m + "' não existe em 'list<T>'", expr->token);
    }

    // ── dict<K,V> built-ins ───────────────────────────────────────────────
    if (obj_type.substr(0, 5) == "dict<") {
        auto params   = TypeChecker::extractTypeParams(obj_type);
        std::string K = params.size() >= 1 ? params[0] : "var";
        std::string V = params.size() >= 2 ? params[1] : "var";

        const std::string& m = expr->method_name;

        if (m == "size") {
            if (!expr->arguments.empty())
                throwError("'dict.size()' não aceita argumentos", expr->token);
            return expr->resolved_type = "int";
        }
        if (m == "has") {
            if (expr->arguments.size() != 1)
                throwError("'dict.has(key)' requer exatamente 1 argumento", expr->token);
            std::string key_type = analyzeExpr(expr->arguments[0].get());
            if (!TypeChecker::isAssignable(K, key_type))
                throwError("'dict.has': chave '" + key_type + "' incompatível com K='" + K + "'",
                           expr->token);
            return expr->resolved_type = "bool";
        }
        if (m == "add") {
            if (isConstObject())
                throwError("Não é possível chamar '.add()' em dict const. "
                           "Dicionários const não podem ser modificados.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'dict.add(pair)' requer exatamente 1 argumento", expr->token);
            std::string pair_type = analyzeExpr(expr->arguments[0].get());
            std::string expected_pair = "pair<" + K + ", " + V + ">";
            if (!TypeChecker::isAssignable(expected_pair, pair_type))
                throwError("'dict.add': par '" + pair_type +
                           "' incompatível com '" + expected_pair + "'",
                           expr->token);
            return expr->resolved_type = "void";
        }
        if (m == "remove") {
            if (isConstObject())
                throwError("Não é possível chamar '.remove()' em dict const. "
                           "Dicionários const não podem ser modificados.", expr->token);
            if (expr->arguments.size() != 1)
                throwError("'dict.remove(key)' requer exatamente 1 argumento", expr->token);
            std::string key_type = analyzeExpr(expr->arguments[0].get());
            if (!TypeChecker::isAssignable(K, key_type))
                throwError("'dict.remove': chave '" + key_type + "' incompatível com K='" + K + "'",
                           expr->token);
            return expr->resolved_type = "void";
        }
        if (m == "keys") {
            if (!expr->arguments.empty())
                throwError("'dict.keys()' não aceita argumentos", expr->token);
            return expr->resolved_type = "list<" + K + ">";
        }
        if (m == "values") {
            if (!expr->arguments.empty())
                throwError("'dict.values()' não aceita argumentos", expr->token);
            return expr->resolved_type = "list<" + V + ">";
        }
        throwError("Método '" + expr->method_name + "' não existe em 'dict<K,V>'",
                   expr->token);
    }

    // ── string built-ins ──────────────────────────────────────────────────
    if (obj_type == "string") {
        if (expr->method_name == "size") {
            if (!expr->arguments.empty())
                throwError("'string.size()' não aceita argumentos", expr->token);
            return expr->resolved_type = "int";
        }
        throwError("Método '" + expr->method_name + "' não existe em 'string'",
                   expr->token);
    }

    // ── Método de classe definida pelo usuário ────────────────────────────
    auto it = class_table.find(obj_type);
    if (it == class_table.end()) {
        throwError("Tipo '" + obj_type + "' não possui métodos (não é uma classe conhecida)",
                   expr->token);
    }

    const ClassInfo& cls_info = it->second;
    auto meth_it = cls_info.methods.find(expr->method_name);
    if (meth_it == cls_info.methods.end()) {
        throwError("Método '" + expr->method_name + "' não encontrado na classe '" +
                   obj_type + "'", expr->token);
    }

    const MethodInfo& mi = meth_it->second;

    // Acesso a método privado só dentro da própria classe
    if (!mi.is_public && current_class_name != obj_type) {
        throwError("Método '" + expr->method_name + "' é privado na classe '" +
                   obj_type + "' e não pode ser chamado externamente", expr->token);
    }

    // Aridade
    if (expr->arguments.size() != mi.param_types.size()) {
        throwError(
            "Aridade incorreta em '" + obj_type + "." + expr->method_name +
            "': esperados " + std::to_string(mi.param_types.size()) +
            " argumento(s), mas recebeu " +
            std::to_string(expr->arguments.size()),
            expr->token);
    }

    // Tipos dos argumentos
    for (size_t i = 0; i < expr->arguments.size(); ++i) {
        std::string arg_type = analyzeExpr(expr->arguments[i].get());
        if (!TypeChecker::isAssignable(mi.param_types[i], arg_type)) {
            throwError(
                "Argumento " + std::to_string(i + 1) + " de '" +
                obj_type + "." + expr->method_name + "': esperado '" +
                mi.param_types[i] + "', mas recebeu '" + arg_type + "'",
                expr->token);
        }
    }

    return expr->resolved_type = mi.return_type;
}

// Acesso a membro:  objeto.campo
//   - pair<A,B>: .first → A, .second → B
//   - classe definida pelo usuário: verifica se o campo existe e é público
std::string SemanticAnalyzer::analyzeMemberAccess(MemberAccessExpr* expr) {
    std::string obj_type = analyzeExpr(expr->object.get());

    // ── pair<A,B> ─────────────────────────────────────────────────────────
    if (obj_type.substr(0, 5) == "pair<") {
        auto params = TypeChecker::extractTypeParams(obj_type);
        if (expr->member_name == "first") {
            std::string t = params.empty() ? "var" : params[0];
            return expr->resolved_type = t;
        }
        if (expr->member_name == "second") {
            std::string t = params.size() < 2 ? "var" : params[1];
            return expr->resolved_type = t;
        }
        throwError("'pair<A,B>' possui apenas os campos 'first' e 'second', "
                   "não '" + expr->member_name + "'", expr->token);
    }

    // ── Classe definida pelo usuário ──────────────────────────────────────
    auto it = class_table.find(obj_type);
    if (it == class_table.end()) {
        throwError("Tipo '" + obj_type + "' não possui campos (não é uma classe conhecida)",
                   expr->token);
    }

    const ClassInfo& cls_info = it->second;
    auto field_it = cls_info.fields.find(expr->member_name);
    if (field_it == cls_info.fields.end()) {
        throwError("Campo '" + expr->member_name + "' não encontrado na classe '" +
                   obj_type + "'", expr->token);
    }

    const FieldInfo& fi = field_it->second;

    // Acesso a campo privado só dentro da própria classe
    if (!fi.is_public && current_class_name != obj_type) {
        throwError("Campo '" + expr->member_name + "' é privado na classe '" +
                   obj_type + "' e não pode ser acessado externamente", expr->token);
    }

    return expr->resolved_type = fi.type_str;
}

// Acesso por índice:  objeto[idx]
//   - list<T>[int]  → T
//   - dict<K,V>[K]  → V
std::string SemanticAnalyzer::analyzeIndexAccess(IndexAccessExpr* expr) {
    std::string obj_type = analyzeExpr(expr->object.get());
    std::string idx_type = analyzeExpr(expr->index.get());

    if (obj_type.substr(0, 5) == "list<") {
        if (idx_type != "int") {
            throwError("Índice de lista deve ser 'int', mas recebeu '" + idx_type + "'",
                       expr->token);
        }
        // Índice negativo literal é erro semântico
        {
            bool neg = false;
            if (auto* lit = dynamic_cast<LiteralExpr*>(expr->index.get()))
                if (std::holds_alternative<int>(lit->value) && std::get<int>(lit->value) < 0) neg = true;
            if (auto* un = dynamic_cast<UnaryExpr*>(expr->index.get()))
                if (un->op == TokenType::OP_MINUS)
                    if (auto* inner = dynamic_cast<LiteralExpr*>(un->operand.get()))
                        if (std::holds_alternative<int>(inner->value)) neg = true;
            if (neg)
                throwError("Índice negativo em lista não é permitido. "
                           "Índices de lista devem ser inteiros não-negativos.", expr->token);
        }
        auto params = TypeChecker::extractTypeParams(obj_type);
        std::string elem = params.empty() ? "var" : params[0];
        return expr->resolved_type = elem;
    }

    if (obj_type.substr(0, 5) == "dict<") {
        auto params   = TypeChecker::extractTypeParams(obj_type);
        std::string K = params.size() >= 1 ? params[0] : "var";
        std::string V = params.size() >= 2 ? params[1] : "var";

        if (!TypeChecker::isAssignable(K, idx_type)) {
            throwError("Chave do dicionário deve ser '" + K +
                       "', mas recebeu '" + idx_type + "'", expr->token);
        }
        return expr->resolved_type = V;
    }

    throwError("Operador '[]' não é suportado no tipo '" + obj_type + "'",
               expr->token);
}

// Instanciação de objeto:  new NomeClasse(args)
//   - Classe deve existir
//   - Aridade e tipos do construtor
std::string SemanticAnalyzer::analyzeNew(NewExpr* expr) {
    auto it = class_table.find(expr->class_name);

    // checando se a classe existe
    if (it == class_table.end()) {
        throwError("Classe '" + expr->class_name + "' não declarada", expr->token);
    }

    const ClassInfo& cls = it->second;

    // checando aridade
    if (expr->arguments.size() != cls.ctor_param_types.size()) {
        throwError(
            "Construtor de '" + expr->class_name +
            "': esperados " + std::to_string(cls.ctor_param_types.size()) +
            " argumento(s), mas recebeu " +
            std::to_string(expr->arguments.size()),
            expr->token);
    }

    // checando a compatibilidade dos argumentos
    for (size_t i = 0; i < expr->arguments.size(); ++i) {
        std::string arg_type = analyzeExpr(expr->arguments[i].get());
        if (!TypeChecker::isAssignable(cls.ctor_param_types[i], arg_type)) {
            throwError(
                "Argumento " + std::to_string(i + 1) + " do construtor de '" +
                expr->class_name + "': esperado '" + cls.ctor_param_types[i] +
                "', mas recebeu '" + arg_type + "'",
                expr->token);
        }
    }

    // se tudo deu certo, o tipo da expressão NewExpr (instanciação) é o nome da classe
    // e retorna o tipo
    return expr->resolved_type = expr->class_name;
}

// Literal de lista:  [e1, e2, e3]
// v2.00 #20: Tipo inferido pelo conjunto inteiro, não só pelo primeiro elemento
// v2.00 #19: Promoção numérica centralizada
std::string SemanticAnalyzer::analyzeListLiteral(ListLiteralExpr* expr) {
    if (expr->elements.empty()) {
        return expr->resolved_type = "list<var>";
    }

    std::string unified = analyzeExpr(expr->elements[0].get());

    for (size_t i = 1; i < expr->elements.size(); ++i) {
        std::string t = analyzeExpr(expr->elements[i].get());

        // Promoção numérica
        if (TypeChecker::isNumeric(unified) && TypeChecker::isNumeric(t)) {
            unified = promoteNumeric(unified, t);
        }
        // Mesmo tipo OK
        else if (unified == t) {
            // nada
        }
        // int → decimal já coberto acima; decimal → int idem
        else if (TypeChecker::isAssignable(unified, t)) {
            // t é atribuível a unified (ex: int para decimal já declarado)
        }
        else if (TypeChecker::isAssignable(t, unified)) {
            unified = t; // unified foi promovido
        }
        else {
            throwError(
                "Literal de lista com tipos incompatíveis: elemento " +
                std::to_string(i + 1) + " tem tipo '" + t +
                "', incompatível com '" + unified + "'",
                expr->token);
        }
    }

    return expr->resolved_type = "list<" + unified + ">";
}

// Literal de dicionário:  {{k1,v1}, {k2,v2}}
// v2.00 #20: Tipo inferido pelo conjunto inteiro
// v2.00 #14: Chaves duplicadas → erro semântico
// v2.00 #13: Chaves restritas a primitivos comparáveis
std::string SemanticAnalyzer::analyzeDictLiteral(DictLiteralExpr* expr) {
    if (expr->pairs.empty()) {
        return expr->resolved_type = "dict<var, var>";
    }

    std::string key_type = analyzeExpr(expr->pairs[0].first.get());
    std::string val_type = analyzeExpr(expr->pairs[0].second.get());

    // v2.00 #13: Chaves só podem ser tipos primitivos
    if (!TypeChecker::isPrimitive(key_type)) {
        throwError(
            "Chave de dict deve ser um tipo primitivo (int, decimal, string, bool), "
            "mas recebeu '" + key_type + "'.",
            expr->token);
    }

    // v2.00 #14: Detector de chaves duplicadas (para literais de string e int)
    std::map<std::string, int> seen_keys;
    // Coleta chave do primeiro par
    {
        auto* lit = dynamic_cast<LiteralExpr*>(expr->pairs[0].first.get());
        if (lit) {
            std::string k;
            if (std::holds_alternative<int>(lit->value))
                k = std::to_string(std::get<int>(lit->value));
            else if (std::holds_alternative<std::string>(lit->value))
                k = std::get<std::string>(lit->value);
            else if (std::holds_alternative<bool>(lit->value))
                k = std::get<bool>(lit->value) ? "true" : "false";
            else
                k = std::to_string(std::get<double>(lit->value));
            seen_keys[k] = 0;
        }
    }

    for (size_t i = 1; i < expr->pairs.size(); ++i) {
        std::string kt = analyzeExpr(expr->pairs[i].first.get());
        std::string vt = analyzeExpr(expr->pairs[i].second.get());

        // v2.00 #13: chave deve ser primitiva
        if (!TypeChecker::isPrimitive(kt)) {
            throwError(
                "Chave " + std::to_string(i + 1) + " de dict deve ser primitiva, "
                "mas recebeu '" + kt + "'.", expr->token);
        }

        // v2.00 #20: unificação do tipo da chave
        if (TypeChecker::isNumeric(key_type) && TypeChecker::isNumeric(kt)) {
            key_type = promoteNumeric(key_type, kt);
        } else if (key_type != kt && !TypeChecker::isAssignable(key_type, kt)) {
            throwError("Chave " + std::to_string(i + 1) + " do dict tem tipo '" + kt +
                       "', mas esperava-se '" + key_type + "'", expr->token);
        }

        // v2.00 #20: unificação do tipo do valor
        if (TypeChecker::isNumeric(val_type) && TypeChecker::isNumeric(vt)) {
            val_type = promoteNumeric(val_type, vt);
        } else if (val_type != vt && !TypeChecker::isAssignable(val_type, vt)) {
            throwError("Valor " + std::to_string(i + 1) + " do dict tem tipo '" + vt +
                       "', mas esperava-se '" + val_type + "'", expr->token);
        }

        // v2.00 #14: chaves duplicadas
        auto* lit = dynamic_cast<LiteralExpr*>(expr->pairs[i].first.get());
        if (lit) {
            std::string k;
            if (std::holds_alternative<int>(lit->value))
                k = std::to_string(std::get<int>(lit->value));
            else if (std::holds_alternative<std::string>(lit->value))
                k = std::get<std::string>(lit->value);
            else if (std::holds_alternative<bool>(lit->value))
                k = std::get<bool>(lit->value) ? "true" : "false";
            else
                k = std::to_string(std::get<double>(lit->value));

            if (seen_keys.count(k)) {
                throwError(
                    "Chave duplicada '" + k + "' no literal de dicionário. "
                    "Cada chave deve aparecer exatamente uma vez.",
                    expr->pairs[i].first->token);
            }
            seen_keys[k] = static_cast<int>(i);
        }
    }

    return expr->resolved_type = "dict<" + key_type + ", " + val_type + ">";
}

// Literal de par:  {first, second}
//   - Retorna "pair<A, B>"
std::string SemanticAnalyzer::analyzePairLiteral(PairLiteralExpr* expr) {
    std::string first_type  = analyzeExpr(expr->first.get());
    std::string second_type = analyzeExpr(expr->second.get());
    return expr->resolved_type = "pair<" + first_type + ", " + second_type + ">";
}

// ============================================================================
// VERIFICAÇÃO DE FLUXO DE RETORNO
// ============================================================================

// Retorna true se TODOS os caminhos do stmt terminam com ReturnStmt.
// Usado para verificar funções não-void.
bool SemanticAnalyzer::allPathsReturn(const Stmt* stmt) const {
    if (!stmt) return false;

    // return expr; → garante retorno neste caminho
    if (dynamic_cast<const ReturnStmt*>(stmt)) return true;

    // Block → basta que qualquer statement no bloco garanta retorno
    // (tudo após o return é código morto, não verificamos)
    if (auto block = dynamic_cast<const BlockStmt*>(stmt)) {
        for (const auto& s : block->statements) {
            if (allPathsReturn(s.get())) return true;
        }
        return false;
    }

    // if-else → ambos os ramos devem garantir retorno (e else deve existir)
    if (auto if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
        if (!if_stmt->else_branch) return false;
        return allPathsReturn(if_stmt->then_branch.get()) &&
               allPathsReturn(if_stmt->else_branch.get());
    }

    // while/for: conservativamente não garantem retorno (podem não executar)
    return false;
}

// Retorna true se ALGUM statement no stmt é ou contém um ReturnStmt.
// Usado para detectar 'return' em funções void.
bool SemanticAnalyzer::hasAnyReturn(const Stmt* stmt) const {
    if (!stmt) return false;

    if (dynamic_cast<const ReturnStmt*>(stmt)) return true;

    if (auto block = dynamic_cast<const BlockStmt*>(stmt)) {
        for (const auto& s : block->statements) {
            if (hasAnyReturn(s.get())) return true;
        }
        return false;
    }
    if (auto if_stmt = dynamic_cast<const IfStmt*>(stmt)) {
        return hasAnyReturn(if_stmt->then_branch.get()) ||
               hasAnyReturn(if_stmt->else_branch.get());
    }
    if (auto w = dynamic_cast<const WhileStmt*>(stmt)) {
        return hasAnyReturn(w->body.get());
    }
    if (auto f = dynamic_cast<const ForStmt*>(stmt)) {
        return hasAnyReturn(f->body.get());
    }

    return false;
}

// ============================================================================
// LOOKUPS EM CLASS_TABLE
// ============================================================================

const MethodInfo* SemanticAnalyzer::resolveMethod(const std::string& obj_type,
                                                   const std::string& method_name) const {
    auto cls_it = class_table.find(obj_type);
    if (cls_it == class_table.end()) return nullptr;
    auto meth_it = cls_it->second.methods.find(method_name);
    if (meth_it == cls_it->second.methods.end()) return nullptr;
    return &meth_it->second;
}

const FieldInfo* SemanticAnalyzer::resolveField(const std::string& obj_type,
                                                  const std::string& field_name,
                                                  bool               require_public) const {
    auto cls_it = class_table.find(obj_type);
    if (cls_it == class_table.end()) return nullptr;
    auto field_it = cls_it->second.fields.find(field_name);
    if (field_it == cls_it->second.fields.end()) return nullptr;
    if (require_public && !field_it->second.is_public) return nullptr;
    return &field_it->second;
}

} // namespace cinza
