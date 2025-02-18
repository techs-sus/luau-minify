#pragma once

#include <Luau/Ast.h>
#include <Luau/DenseHash.h>
#include <ankerl/unordered_dense.h>
#include <cstddef>
#include <string>
#include <string_view>

typedef ankerl::unordered_dense::map<const char *, std::string> global_map;
typedef ankerl::unordered_dense::map<const char *, size_t> global_uses;

typedef ankerl::unordered_dense::map<std::string_view, size_t> string_uses;
typedef ankerl::unordered_dense::map<std::string_view, std::string> string_map;

class AstTracking : public Luau::AstVisitor {
public:
  global_uses globalUses = global_uses();
  string_uses stringUses = string_uses();

  bool visit(Luau::AstExprGlobal *node) override {
    globalUses[node->name.value]++;

    return true;
  }

  bool visit(Luau::AstExprConstantString *node) override {
    const std::string_view view(node->value.begin(), node->value.end());
    stringUses[view]++;

    return true;
  }
};

struct Glue {
  global_map globals = global_map();
  string_map strings = string_map();

  std::string init = "";
  size_t nameIndex;
};

Glue initGlue(AstTracking &tracking);
