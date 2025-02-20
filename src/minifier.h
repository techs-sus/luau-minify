#pragma once

#include <Luau/Ast.h>
#include <ankerl/unordered_dense.h>
#include <cstddef>

typedef ankerl::unordered_dense::map<const char *, std::string> rename_map;

// Block as in, function bodies, while loop bodies, for loop bodies, do bodies,
// etc.
struct BlockInfo {
  BlockInfo *parent; // NULL if no parent
  std::vector<BlockInfo *> children;
  rename_map locals;
};

#include "tracking.h"

struct State {
  std::string output = "";

  // [depth][node.name] = getLocalName(&state.totalLocals);
  size_t totalLocals = 0;

  rename_map &globals;
  string_map &strings;
  BlockInfo *blockInfo; // MUST NOT BE NULL
};

std::string processAstRoot(Luau::AstStatBlock *root);
