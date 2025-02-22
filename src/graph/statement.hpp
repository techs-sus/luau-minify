#pragma once

#include <Luau/Ast.h>
#include <string>
#include <vector>

#include "rtti.hpp"

class Statement {
public:
  RTTI(Statement)
  const int classIndex;

  virtual ~Statement() = default;

  explicit Statement(int classIndex) : classIndex(classIndex) {};

  template <typename T> T *as() {
    return this->classIndex == T::ClassIndex() ? static_cast<T *>(this)
                                               : nullptr;
  }

  template <typename T> bool is() {
    return this->classIndex == T::ClassIndex();
  }
};

class AssignStatement : public Statement {
public:
  RTTI(AssignStatement)
  explicit AssignStatement();

  std::vector<Luau::AstExpr *> values;
  std::vector<Luau::AstExpr *> vars; // can be AstExprLocal or AstExprGlobal
};

class LocalAssignStatement : public Statement {
public:
  RTTI(LocalAssignStatement)
  explicit LocalAssignStatement();

  std::vector<Luau::AstExpr *> values;
  std::vector<Luau::AstLocal *> vars;
};

class CompoundAssignStatement : public Statement {
public:
  RTTI(CompoundAssignStatement)
  explicit CompoundAssignStatement();

  Luau::AstExpr *var;
  Luau::AstExpr *value;
  Luau::AstExprBinary::Op op;
};

class BreakStatement : public Statement {
public:
  RTTI(BreakStatement)
  explicit BreakStatement();
};

class ContinueStatement : public Statement {
public:
  RTTI(ContinueStatement)
  explicit ContinueStatement();
};

class ReturnStatement : public Statement {
public:
  RTTI(ReturnStatement)
  explicit ReturnStatement();

  std::vector<Luau::AstExpr *> values;
};

class ExpressionStatement : public Statement {
public:
  RTTI(ExpressionStatement)
  explicit ExpressionStatement();

  Luau::AstExpr *value;
};

const std::vector<std::string> getFields(Statement *statement);
