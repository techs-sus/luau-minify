#pragma once

#include "absl/container/flat_hash_map.h"
#include "tracking.h"
#include <Luau/Ast.h>
#include <Luau/DenseHash.h>
#include <cstddef>

typedef absl::flat_hash_map<size_t,
                            absl::flat_hash_map<const char *, std::string>>
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
