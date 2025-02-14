#pragma once

#include <Luau/Ast.h>
#include <Luau/DenseHash.h>
#include <cstddef>
#include <string>

typedef Luau::DenseHashMap<const char *, std::string> global_map;

typedef Luau::DenseHashMap<const char *, size_t> global_uses;

class AstGlobalTracking : public Luau::AstVisitor {
public:
  global_uses globalUses = global_uses(nullptr);
  size_t globalIndex = 0;

  bool visit(Luau::AstExprGlobal *node) override {
    if (globalUses.contains(node->name.value)) {
      // in use, increment counter
      globalUses[node->name.value]++;
    } else {
      globalUses[node->name.value] = 1;
      globalIndex++;
    }

    return true;
  }
};
