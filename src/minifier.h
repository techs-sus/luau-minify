#pragma once

#include <Luau/Ast.h>
#include <ankerl/unordered_dense.h>
#include <cstddef>

#include "tracking.h"

typedef ankerl::unordered_dense::map<
    size_t, ankerl::unordered_dense::map<const char *, std::string>>
    deep_local_map;

struct State {
  std::string output = "";

  // [depth][node.name] = getLocalName(&state.totalLocals);
  deep_local_map locals = deep_local_map();
  size_t totalLocals = 0;

  global_map &globals;
  string_map &strings;
};

std::string processAstRoot(Luau::AstStatBlock *root);
