#pragma once

#include <Luau/Ast.h>
#include <ankerl/unordered_dense.h>
#include <cstddef>
#include <vector>

#include "rtti.hpp"
#include "statement.hpp"

enum class Type {
  Nil = 0,
  Number,
  String,
  Tables,
  Complex, // deoptimization where we mark some global functions returns as
           // "complex", meaning we cannot predict their value
  /* Userdata, thread, maybe vector require getfenv tracking */
};

struct LocalInfo {
  size_t uses = 0;
  std::vector<Type> types = {};
};

class Block {
public:
  const int classIndex;

  explicit Block(int classIndex, Block *parent)
      : classIndex(classIndex), parent(parent) {}
  explicit Block(int classIndex) : classIndex(classIndex) {}

  ankerl::unordered_dense::map<const char *, LocalInfo> locals = {};
  ankerl::unordered_dense::map<const char *, Block *> dependencies = {};
  std::vector<Statement *> statements = {};
  std::vector<Block *> children = {};
  std::vector<bool> order = {}; // 1 = statement, 0 = child (read order)

  Block *parent = nullptr;

  virtual ~Block() {
    for (auto *statement : statements) {
      delete statement;
    }

    statements.clear();

    for (auto *child : children) {
      delete child;
    }

    children.clear();
  };

  inline const void pushStatement(Statement *s) {
    statements.emplace_back(s);
    order.emplace_back(1);
  }

  inline const void pushChild(Block *block) {
    children.emplace_back(block);
    block->parent = this;
    order.emplace_back(0);
  }

  template <typename T> T *as() {
    return this->classIndex == T::ClassIndex() ? static_cast<T *>(this)
                                               : nullptr;
  }

  template <typename T> bool is() {
    return this->classIndex == T::ClassIndex();
  }
};

// Root block
class RootBlock : public Block {
public:
  RTTI(RootBlock)

  explicit RootBlock();
};

// Do block as in do <body> end
class DoBlock : public Block {
public:
  RTTI(DoBlock)

  explicit DoBlock();
};

// Conditional block, which depends on only one condition
// While, Repeat
class SingleConditionBlock : public Block {
public:
  RTTI(SingleConditionBlock)

  enum Type { While = 0, Repeat = 1 };

  Type type;

  explicit SingleConditionBlock(Type type, Luau::AstExpr *condition);
  explicit SingleConditionBlock(Type &type, Luau::AstExpr *condition);

  Luau::AstExpr *condition;
};

// for <var> = <from>, <to>, <step> do
class ForBlock : public Block {
public:
  RTTI(ForBlock);

  explicit ForBlock(Luau::AstLocal *variable, Luau::AstExpr *from,
                    Luau::AstExpr *to, Luau::AstExpr *step);

  Luau::AstLocal *variable;
  Luau::AstExpr *from;
  Luau::AstExpr *to;
  Luau::AstExpr *step;
};

// for <vars,>+ in <values,>+ do
class ForInBlock : public Block {
public:
  RTTI(ForInBlock)

  explicit ForInBlock(const Luau::AstArray<Luau::AstLocal *> *vars,
                      const Luau::AstArray<Luau::AstExpr *> *values);

  const Luau::AstArray<Luau::AstLocal *> *vars;
  const Luau::AstArray<Luau::AstExpr *> *values;
};

// functions
class FunctionBlock : public Block {
public:
  RTTI(FunctionBlock)

  explicit FunctionBlock(char *name, bool variadic,
                         Luau::AstArray<Luau::AstLocal *> *args);

  char *name;
  bool variadic;
  Luau::AstArray<Luau::AstLocal *> *arguments;
};

// functions declared locally
class LocalFunctionBlock : public FunctionBlock {
public:
  RTTI(LocalFunctionBlock)

  explicit LocalFunctionBlock(const char *name, bool variadic,
                              Luau::AstArray<Luau::AstLocal *> *args)
      : FunctionBlock((char *)name, variadic, args) {};
};

class IfStatementBlock : public Block {
public:
  RTTI(IfStatementBlock)

  explicit IfStatementBlock();
  ~IfStatementBlock() {
    // no double free's! hash_sets can only hold one version of one value at
    // once
    ankerl::unordered_dense::set<Block *> targets;
    targets.insert(thenBody);
    targets.insert(elseBody);

    for (const auto [ptr, _] : elseifs) {
      targets.insert(ptr);
    };

    for (const auto ptr : children) {
      targets.insert(ptr);
    }

    for (const auto ptr : targets) {
      delete ptr;
    }

    elseifs.clear();
    children.clear();
  }

  // root condition
  Luau::AstExpr *condition;
  Block *thenBody;
  Block *elseBody;

  // else if condition is in pair->second
  std::vector<std::pair<Block *, Luau::AstExpr *>> elseifs;
};

class IfBlock : public Block {
public:
  RTTI(IfBlock)

  enum Type {
    Then = 0,
    Else,
    Elseif,
  };

  Type type;

  explicit IfBlock();
};
