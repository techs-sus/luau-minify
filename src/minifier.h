#pragma once

#include "globals.h"

#include <Luau/Ast.h>
#include <Luau/DenseHash.h>
#include <cstddef>
#include <unordered_map>

typedef Luau::DenseHashMap<size_t,
                           std::unordered_map<const char *, std::string>>
    deep_local_map;

struct State {
  std::string output = "";

  // [depth][node.name] = getLocalName(&state.totalLocals);
  deep_local_map locals = deep_local_map(SIZE_MAX);
  size_t totalLocals = 0;

  global_map &globals;
};

void handleNode(Luau::AstNode *node, State *state, size_t localDepth);
std::string processAstRoot(Luau::AstStatBlock *root);
