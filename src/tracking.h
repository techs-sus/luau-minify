#pragma once

#include <Luau/Ast.h>
#include <Luau/DenseHash.h>
#include <ankerl/unordered_dense.h>
#include <cstddef>
#include <string>
#include <string_view>

typedef ankerl::unordered_dense::map<const char *, size_t> global_usage_map;
typedef ankerl::unordered_dense::map<std::string_view, size_t> string_usage_map;
typedef ankerl::unordered_dense::map<std::string_view, std::string> string_map;

#include "minifier.h"

class AstTracking : public Luau::AstVisitor {
public:
  global_usage_map globalUses = global_usage_map();
  string_usage_map stringUses = string_usage_map();

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
  rename_map globals = rename_map();
  string_map strings = string_map();

  std::string init = "";
  size_t nameIndex;
};

Glue initGlue(AstTracking &tracking);
