#include "statement.hpp"

LocalAssignStatement::LocalAssignStatement()
    : Statement(LocalAssignStatement::ClassIndex()), values({}), vars({}) {}

AssignStatement::AssignStatement()
    : Statement(AssignStatement::ClassIndex()), values({}), vars({}) {}

CompoundAssignStatement::CompoundAssignStatement()
    : Statement(CompoundAssignStatement::ClassIndex()), var(nullptr),
      value(nullptr), op(Luau::AstExprBinary::Op::Add) {}

ReturnStatement::ReturnStatement()
    : Statement(ReturnStatement::ClassIndex()), values({}) {}

BreakStatement::BreakStatement() : Statement(BreakStatement::ClassIndex()) {}

ContinueStatement::ContinueStatement()
    : Statement(ContinueStatement::ClassIndex()) {}

ExpressionStatement::ExpressionStatement()
    : Statement(ExpressionStatement::ClassIndex()), value(nullptr) {}

const std::vector<std::string> getFields(Statement *statement) {
  std::vector<std::string> fields = {};

  if (auto locals = statement->as<LocalAssignStatement>()) {
    for (size_t index = 0; index < locals->vars.size(); index++) {
      std::string str = "";
      str.append(locals->vars[index]->name.value);
      str.append(" = <expr>");
      fields.emplace_back(str);
    }
  } else if (auto assign = statement->as<AssignStatement>()) {
    for (size_t index = 0; index < assign->vars.size(); index++) {
      const auto &var = assign->vars[index];
      // const auto &value = values[index];

      std::string variableName = "";

      if (auto local = var->as<Luau::AstExprLocal>()) {
        variableName.append(local->local->name.value);
      } else if (auto global = var->as<Luau::AstExprGlobal>()) {
        variableName.append(global->name.value);
      } else {
        variableName.append("unknown");
      };

      // TODO: display expression when we get an expression engine
      fields.emplace_back(variableName + " = <expr>");
    }
  } else if (auto stat = statement->as<ExpressionStatement>()) {
    if (auto expr = stat->value->as<Luau::AstExprCall>()) {
      std::string field = "";
      if (auto fn = expr->func->as<Luau::AstExprLocal>()) {
        field += fn->local->name.value;
      } else if (auto fn = expr->func->as<Luau::AstExprGlobal>()) {
        field += fn->name.value;
      } else {
        field += "unknown";
      };

      field += "(";
      // TODO: Display args here (when we make an expr displayer)
      field += ")";

      fields.emplace_back(field);
    }
  }

  return fields;
}
