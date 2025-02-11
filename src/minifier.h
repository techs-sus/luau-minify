#pragma once
#include "Luau/Ast.h"
#include <Luau/DenseHash.h>

struct State {
  std::string output;

  // [depth][node.name] = getLocalName(&state.totalLocals);
  std::unordered_map<size_t, std::unordered_map<std::string, std::string>>
      locals;
  size_t totalLocals;

  Luau::DenseHashMap<std::string, std::string> globals;
  size_t totalGlobals;
};

void handleNode(Luau::AstNode *node, State *state, size_t localDepth);
std::string processAstRoot(Luau::AstStatBlock *root);
