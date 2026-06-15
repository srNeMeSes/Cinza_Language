#include "ast.h"
#include <sstream>

namespace cinza {

// ============================================================================
// TYPE IMPLEMENTATION
// ============================================================================

// r: int, string, decimal, bool, list<T>, dict<K,V>, pair<F,S> ...  
std::string Type::toString() const { 
    switch (kind) {
        case Kind::INT: return "int";
        case Kind::DECIMAL: return "decimal";
        case Kind::STRING: return "string";
        case Kind::BOOL: return "bool";
        case Kind::VOID: return "void";
        case Kind::VAR: return "var";
        case Kind::LIST: {
            std::string result = "list";
            if (!type_params.empty()) {
                result += "<" + type_params[0]->toString() + ">";
            }
            return result;
        }
        case Kind::DICT: {
            std::string result = "dict";
            if (type_params.size() >= 2) {
                result += "<" + type_params[0]->toString() + ", " + 
                          type_params[1]->toString() + ">";
            }
            return result;
        }
        case Kind::PAIR: {
            std::string result = "pair";
            if (type_params.size() >= 2) {
                result += "<" + type_params[0]->toString() + ", " +
                          type_params[1]->toString() + ">";
            }
            return result;
        }
        case Kind::CUSTOM: return name; // classe
        default: return "unknown";
    }
}

bool Type::isNumeric() const {
    return kind == Kind::INT || kind == Kind::DECIMAL;
}

bool Type::isComparable() const {
    return isNumeric() || kind == Kind::STRING || kind == Kind::BOOL;
}

// ============================================================================
// EXPRESSION IMPLEMENTATIONS
// ============================================================================

// r: Literal("valor"), Literal(1), Literal(true), Literal(3.14)
std::string LiteralExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "Literal(";
    
    if (std::holds_alternative<int>(value)) {
        oss << std::get<int>(value);
    } else if (std::holds_alternative<double>(value)) {
        oss << std::get<double>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        oss << "\"" << std::get<std::string>(value) << "\"";
    } else if (std::holds_alternative<bool>(value)) {
        oss << (std::get<bool>(value) ? "true" : "false");
    }
    
    oss << ")";
    return oss.str();
}

// r: Identifier(variavel_name), Identifier(function_name), Identifier(class_name)
std::string IdentifierExpr::toString(int indent_level) const {
    return indent(indent_level) + "Identifier(" + name + ")";
}

// r: BinaryExpr(op: "+", left: 2, right: 2), BinaryExpr(op: "==", left: "joca", right: "joka")
std::string BinaryExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "BinaryExpr(\n";
    oss << indent(indent_level + 1) << "op: " << tokenTypeToOperatorString(op) << "\n";
    oss << indent(indent_level + 1) << "left:\n" << left->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "right:\n" << right->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: UnaryExpr(op: -, operand: 3), UnaryExpr(op: !, operand: variavel)
std::string UnaryExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "UnaryExpr(\n";
    oss << indent(indent_level + 1) << "op: " << tokenTypeToOperatorString(op) << "\n";
    oss << indent(indent_level + 1) << "operand:\n" << operand->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: CallExpr(function: somar, [2, 3]), CallExpr(function: somar, [val1, val2])
std::string CallExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "CallExpr(\n";
    oss << indent(indent_level + 1) << "function: " << function_name << "\n";
    oss << indent(indent_level + 1) << "arguments: [\n";
    for (const auto& arg : arguments) {
        oss << arg->toString(indent_level + 2) << "\n";
    }
    oss << indent(indent_level + 1) << "]\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: MemberAccess(object: Identifier(lista), member: len), MemberAccess(object: Identifier(lista), member: add)
std::string MemberAccessExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "MemberAccess(\n";
    oss << indent(indent_level + 1) << "object:\n" << object->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "member: " << member_name << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: ListLiteral(Literal(1), Literal(2), Literal(3))
std::string ListLiteralExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "ListLiteral([\n";
    for (const auto& elem : elements) {
        oss << elem->toString(indent_level + 1) << "\n";
    }
    oss << indent(indent_level) << "])";
    return oss.str();
}

// r: DictLiteral({key: Literal("maçãs"), value: Literal(2)})
std::string DictLiteralExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "DictLiteral({\n";
    for (const auto& [key, value] : pairs) {
        oss << indent(indent_level + 1) << "[key:\n" << key->toString(indent_level + 2) << "\n";
        oss << indent(indent_level + 1) << "value:\n" << value->toString(indent_level + 2) << "]\n";
    }
    oss << indent(indent_level) << "})";
    return oss.str();
}

// r: PairLiteral(first: Literal("chave"), second: Literal(3.14))
std::string PairLiteralExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "PairLiteral(\n";
    oss << indent(indent_level + 1) << "first:\n"  << first->toString(indent_level + 2)  << "\n";
    oss << indent(indent_level + 1) << "second:\n" << second->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: IndexAccess(object: Identifier(nomes), index: Literal(0))
//    IndexAccess(object: Identifier(casas), index: Literal("casa1"))
std::string IndexAccessExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "IndexAccess(\n";
    oss << indent(indent_level + 1) << "object:\n"  << object->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "index:\n"   << index->toString(indent_level + 2)  << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: MethodCall(object: Identifier(Obejto), method: add, args: [Literal("Fernanda")])
std::string MethodCallExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "MethodCall(\n";
    oss << indent(indent_level + 1) << "object:\n" << object->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "method: " << method_name << "\n";
    oss << indent(indent_level + 1) << "arguments: [\n";
    for (const auto& arg : arguments) {
        oss << arg->toString(indent_level + 2) << "\n";
    }
    oss << indent(indent_level + 1) << "]\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// ============================================================================
// STATEMENT IMPLEMENTATIONS
// ============================================================================

// r: VarDecl(type: string, name: ola, initializer: Literal("Ola mundo!")), VarDecl(type: int, name: num)
std::string VarDeclStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "VarDecl(\n";
    oss << indent(indent_level + 1) << "type: " << type->toString() << "\n";
    oss << indent(indent_level + 1) << "name: " << name << "\n";
    oss << indent(indent_level + 1) << "initializer:\n";
    if (initializer) {
        oss << initializer->toString(indent_level + 2) << "\n";
    } else {
        oss << indent(indent_level + 2) << "UNBOUND\n";    // nullptr
    }
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: Assignment(variable: num, value: 11)
std::string AssignmentStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "Assignment(\n";
    oss << indent(indent_level + 1) << "variable: " << variable_name << "\n";
    oss << indent(indent_level + 1) << "value:\n" << value->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// v2.00 #8
std::string IndexAssignmentStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "IndexAssignment(\n";
    oss << indent(indent_level + 1) << "object: " << object_name << "\n";
    oss << indent(indent_level + 1) << "index:\n" << index->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "value:\n" << value->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r:  ExprStmt(CallExpr(function: somar, arguments: [ Literal(3), Literal(4.4) ]))
std::string ExprStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "ExprStmt(\n";
    oss << expression->toString(indent_level + 1) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: Block{ ... }
std::string BlockStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "Block {\n";
    for (const auto& stmt : statements) {
        oss << stmt->toString(indent_level + 1) << "\n";
    }
    oss << indent(indent_level) << "}";
    return oss.str();
}

// r: IfStmt(condition: Expr, then: Block {...}, else: Block {...})
std::string IfStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "IfStmt(\n";
    oss << indent(indent_level + 1) << "condition:\n" << condition->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "then:\n" << then_branch->toString(indent_level + 2) << "\n";
    if (else_branch) {
        oss << indent(indent_level + 1) << "else:\n" << else_branch->toString(indent_level + 2) << "\n";
    }
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: WhileStmt(conditon: Expr, body: Block {...})
std::string WhileStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "WhileStmt(\n";
    oss << indent(indent_level + 1) << "condition:\n" << condition->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "body:\n" << body->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: ForStmt(type: int, iterator: ovo, iterable: cartela, body: Block {...})
std::string ForStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "ForStmt(\n";
    oss << indent(indent_level + 1) << "type: " << type_iterator->toString() << "\n";
    oss << indent(indent_level + 1) << "iterator: " << iterator_name << "\n";
    oss << indent(indent_level + 1) << "iterable:\n" << iterable->toString(indent_level + 2) << "\n";
    oss << indent(indent_level + 1) << "body:\n" << body->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: ReturnStmt(Literal(...))
std::string ReturnStmt::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "ReturnStmt(";
    if (value) {
        oss << "\n" << value->toString(indent_level + 1) << "\n" << indent(indent_level);
    }
    oss << ")";
    return oss.str();
}

// r: FunctionDecl(name: fname, parameters: [int a, int b], return_type: int, body: Block {...})
std::string FunctionDecl::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "FunctionDecl(\n";
    oss << indent(indent_level + 1) << "name: " << name << "\n";
    oss << indent(indent_level + 1) << "parameters: [\n";
    for (const auto& param : parameters) {
        oss << indent(indent_level + 2) << param.type->toString() << " " << param.name << "\n";
    }
    oss << indent(indent_level + 1) << "]\n";
    oss << indent(indent_level + 1) << "return_type: " << return_type->toString() << "\n";
    oss << indent(indent_level + 1) << "body:\n" << body->toString(indent_level + 2) << "\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: NewExpr(class: Pessoa, args: [Literal("Jose"), Literal(22), Literal(3.4)])
std::string NewExpr::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "NewExpr(\n";
    oss << indent(indent_level + 1) << "class: " << class_name << "\n";
    oss << indent(indent_level + 1) << "arguments: [\n";
    for (const auto& arg : arguments) {
        oss << arg->toString(indent_level + 2) << "\n";
    }
    oss << indent(indent_level + 1) << "]\n";
    oss << indent(indent_level) << ")";
    return oss.str();
}

// r: ClassDecl(name: Pilha, fields: [...], constructor: ..., priv_methods: [...], pub_methods: [...])
std::string ClassDecl::toString(int indent_level) const {
    std::ostringstream oss;
    oss << indent(indent_level) << "ClassDecl(\n";
    oss << indent(indent_level + 1) << "name: " << class_name << "\n";

    // Campos privados
    oss << indent(indent_level + 1) << "fields: [\n";
    for (const auto& f : fields) {
        oss << indent(indent_level + 2) << f.type->toString() << " " << f.name;
        if (f.initializer) {
            oss << " =\n" << f.initializer->toString(indent_level + 3) << "\n";
        } else {
            oss << " (UNBOUND)\n";
        }
    }
    oss << indent(indent_level + 1) << "]\n";

    // Construtor (opcional)
    if (constructor) {
        oss << indent(indent_level + 1) << "constructor: " << class_name << "(\n";
        for (const auto& p : constructor->parameters) {
            oss << indent(indent_level + 2) << p.type->toString() << " " << p.name << "\n";
        }
        oss << indent(indent_level + 1) << ")\n";
        oss << indent(indent_level + 1) << "constructor_body:\n";
        oss << constructor->body->toString(indent_level + 2) << "\n";
    } else {
        oss << indent(indent_level + 1) << "constructor: false\n";
    }

    // Métodos privados — fora do pub{}
    if (!priv_methods.empty()) {
        oss << indent(indent_level + 1) << "priv_methods: [\n";
        for (const auto& m : priv_methods) {
            if (m) oss << m->toString(indent_level + 2) << "\n";
        }
        oss << indent(indent_level + 1) << "]\n";
    }

    // Métodos públicos — dentro do pub{}
    oss << indent(indent_level + 1) << "pub_methods: [\n";
    for (const auto& m : pub_methods) {
        if (m) oss << m->toString(indent_level + 2) << "\n";
    }
    oss << indent(indent_level + 1) << "]\n";

    oss << indent(indent_level) << ")";
    return oss.str();
}

// ============================================================================
// PROGRAM IMPLEMENTATION
// ============================================================================

// r: Program {...}
std::string Program::toString() const {
    std::ostringstream oss;
    oss << "Program {\n";
    for (const auto& stmt : statements) {
        oss << stmt->toString(1) << "\n\n";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// UTILITIES
// ============================================================================

// basicamente retorna identações, meio que nada haver
std::string indent(int level) {
    return std::string(level * 2, ' ');
}

// verifica qual TokenType é, e retorna a string equivalente
std::string tokenTypeToOperatorString(TokenType type) {
    switch (type) {
        case TokenType::OP_PLUS: return "+";
        case TokenType::OP_MINUS: return "-";
        case TokenType::OP_MULTIPLY: return "*";
        case TokenType::OP_DIVIDE: return "/";
        case TokenType::OP_MODULO: return "%";
        case TokenType::OP_EQUAL: return "==";
        case TokenType::OP_NOT_EQUAL: return "!=";
        case TokenType::OP_LESS: return "<";
        case TokenType::OP_LESS_EQUAL: return "<=";
        case TokenType::OP_GREATER: return ">";
        case TokenType::OP_GREATER_EQUAL: return ">=";
        case TokenType::OP_AND: return "&&";
        case TokenType::OP_OR: return "||";
        case TokenType::OP_NOT: return "!";
        case TokenType::OP_ASSIGN: return "=";
        default: return "<?>";
    }
}

} // namespace cinza
