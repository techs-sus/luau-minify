#include <Luau/Ast.h>
#include <algorithm>
#include <string>
#include <string_view>

#include "ankerl/unordered_dense.h"
#include "graph/block.hpp"
#include "graph/statement.hpp"
#include "minifier.h"
#include "syntax.h"
#include "tracking.h"

struct TrackingState {
  Block *currentBlock = nullptr;

  global_usage_map globalUses = global_usage_map();
  string_usage_map stringUses = string_usage_map();
  size_t totalLocals = 0;
};

void trackAstLocalAssignment(const Luau::AstLocal *local,
                             TrackingState &state) {
  state.currentBlock->locals[local->name.value].uses++;
}

void trackCallWithBlock(TrackingState &state, Block *block,
                        std::function<void()> closure) {
  Block *current = state.currentBlock;

  current->pushChild(block);
  block->parent = current;

  state.currentBlock = block;
  closure();
  state.currentBlock = current;
};

void traverse(const Luau::AstNode *node, TrackingState &state) {
  if (node->is<Luau::AstExprGlobal>()) {
    const auto expr = node->as<Luau::AstExprGlobal>();
    state.globalUses[expr->name.value]++;

    Block *block = state.currentBlock;

    while (block->parent != nullptr) {
      block = block->parent;
    }

    // root shouldn't depend on itself
    if (block != state.currentBlock)
      state.currentBlock->dependencies[expr->name.value] = block;

    return;
  }

  if (node->is<Luau::AstExprConstantString>()) {
    const auto expr = node->as<Luau::AstExprConstantString>();

    const std::string_view view(expr->value.begin(), expr->value.end());
    state.stringUses[view]++;

    return;
  }

  if (auto block = node->as<Luau::AstStatBlock>()) {
    for (const auto &statement : block->body) {
      if (statement->is<Luau::AstStatBlock>()) {
        DoBlock *block = new DoBlock();
        trackCallWithBlock(state, block, [&] { traverse(statement, state); });
        continue;
      }

      traverse(statement, state);
    }

    return;
  }

  if (node->is<Luau::AstStatExpr>()) {
    auto stat = node->as<Luau::AstStatExpr>();

    auto trackingStatement = new ExpressionStatement{};
    trackingStatement->value = stat->expr;
    state.currentBlock->pushStatement(trackingStatement);

    traverse(stat->expr, state);
    return;
  }

  if (node->is<Luau::AstExprCall>()) {
    auto expr = node->as<Luau::AstExprCall>();
    traverse(expr->func, state);

    for (const auto arg : expr->args) {
      traverse(arg, state);
    };

    return;
  }

  if (node->is<Luau::AstExprFunction>()) {
    auto expr = node->as<Luau::AstExprFunction>();
    traverse(expr->body, state);

    for (const auto arg : expr->args) {
      trackAstLocalAssignment(arg, state);
    };

    return;
  }

  if (node->is<Luau::AstExprGroup>()) {
    auto expr = node->as<Luau::AstExprGroup>();

    traverse(expr->expr, state);
    return;
  }

  if (auto stat = node->as<Luau::AstStatWhile>()) {
    auto *block = new SingleConditionBlock(SingleConditionBlock::Type::While,
                                           stat->condition);

    trackCallWithBlock(state, block, [&] { traverse(stat->body, state); });
    return;
  }

  if (auto repeat = node->as<Luau::AstStatRepeat>()) {
    auto *block = new SingleConditionBlock(SingleConditionBlock::Type::Repeat,
                                           repeat->condition);

    trackCallWithBlock(state, block, [&] { traverse(repeat->body, state); });
    return;
  }

  if (auto stat = node->as<Luau::AstStatFor>()) {
    auto *block = new ForBlock(stat->var, stat->from, stat->to, stat->step);

    trackCallWithBlock(state, block, [&] { traverse(stat->body, state); });
    return;
  }

  if (auto stat = node->as<Luau::AstStatForIn>()) {
    auto *block = new ForInBlock(&stat->vars, &stat->values);

    trackCallWithBlock(state, block, [&] { traverse(stat->body, state); });
    return;
  }

  if (auto statement = node->as<Luau::AstStatLocalFunction>()) {
    auto block =
        new LocalFunctionBlock{statement->name->name.value,
                               statement->func->vararg, &statement->func->args};

    // ensure this scope can access fn
    trackAstLocalAssignment(statement->name, state);
    trackCallWithBlock(state, block, [&] { traverse(statement->func, state); });

    return;
  }

  if (auto statement = node->as<Luau::AstStatFunction>()) {
    const char *ptr;

    if (auto global = statement->name->as<Luau::AstExprGlobal>()) {
      ptr = global->name.value;
    } else if (auto local = statement->name->as<Luau::AstExprLocal>()->local) {
      ptr = local->name.value;
    } else {
      ptr = "<idk>";
    }

    auto block = new LocalFunctionBlock{ptr, statement->func->vararg,
                                        &statement->func->args};

    traverse(statement->name, state);
    trackCallWithBlock(state, block, [&] { traverse(statement->func, state); });

    return;
  }

  if (auto statement = node->as<Luau::AstStatIf>()) {
    auto block = new IfStatementBlock();

    trackCallWithBlock(state, block, [&] {
      auto thenBlock = new IfBlock();
      thenBlock->type = IfBlock::Type::Then;

      block->thenBody = thenBlock;

      trackCallWithBlock(state, thenBlock,
                         [&] { traverse(statement->thenbody, state); });

      if (statement->elsebody == nullptr) {
        return;
      };

      if (statement->elsebody->is<Luau::AstStatIf>()) {
        std::vector<std::pair<Luau::AstStatBlock *, Luau::AstExpr *>> elseifs;
        Luau::AstStatIf *ptr = statement->elsebody->as<Luau::AstStatIf>();

        while (ptr != nullptr) {
          elseifs.emplace_back(ptr->thenbody, ptr->condition);

          if (ptr->elsebody == nullptr) {
            break;
          }

          if (ptr->elsebody->is<Luau::AstStatIf>()) {
            ptr = ptr->elsebody->as<Luau::AstStatIf>();
          } else {
            elseifs.emplace_back(ptr->elsebody->as<Luau::AstStatBlock>(),
                                 nullptr);
            break;
          };
        }

        for (const auto &[node, condition] : elseifs) {
          auto newBlock = new IfBlock();

          if (condition == nullptr) {
            newBlock->type = IfBlock::Type::Else;
            block->elseBody = newBlock;
          } else {
            newBlock->type = IfBlock::Type::Elseif;
            block->elseifs.emplace_back(newBlock, condition);
          }

          trackCallWithBlock(state, newBlock, [&] { traverse(node, state); });
        };
        return;
      } else {
        auto elseBlock = new IfBlock();
        elseBlock->type = IfBlock::Type::Else;
        block->elseBody = elseBlock;

        trackCallWithBlock(state, elseBlock,
                           [&] { traverse(statement->elsebody, state); });
      };
    });

    return;
  };

  if (auto assign = node->as<Luau::AstStatAssign>()) {
    auto assignStatement = new AssignStatement();

    const size_t assignments = std::min(assign->values.size, assign->vars.size);
    for (size_t index = 0; index < assignments; index++) {
      const auto var = assign->vars.data[index];
      const auto value = assign->values.data[index];

      assignStatement->vars.emplace_back(var);
      assignStatement->values.emplace_back(value);

      traverse(var, state);
      traverse(value, state);
    }

    state.currentBlock->pushStatement(assignStatement);

    return;
  }

  if (node->is<Luau::AstStatReturn>()) {
    auto ret = node->as<Luau::AstStatReturn>();

    auto trackingStatement = new ReturnStatement{};

    for (const auto value : ret->list) {
      trackingStatement->values.emplace_back(value);
      traverse(value, state);
    }

    state.currentBlock->pushStatement(trackingStatement);
    return;
  }

  if (node->is<Luau::AstStatBreak>()) {
    auto breakStatement = new BreakStatement{};
    state.currentBlock->pushStatement(breakStatement);
    return;
  }

  if (node->is<Luau::AstStatContinue>()) {
    auto continueStatement = new ContinueStatement{};
    state.currentBlock->pushStatement(continueStatement);
    return;
  }

  if (auto assign = node->as<Luau::AstStatCompoundAssign>()) {
    auto compoundAssignStatement = new CompoundAssignStatement{};
    compoundAssignStatement->op = assign->op;
    compoundAssignStatement->var = assign->var;
    compoundAssignStatement->value = assign->value;
    state.currentBlock->pushStatement(compoundAssignStatement);

    traverse(assign->var, state);
    traverse(assign->value, state);

    return;
  }

  if (auto local = node->as<Luau::AstStatLocal>()) {
    auto localAssignStatement = new LocalAssignStatement{};

    const size_t assignments = std::min(local->values.size, local->vars.size);

    for (size_t index = 0; index < assignments; index++) {
      const auto var = local->vars.data[index];
      const auto value = local->values.data[index];

      localAssignStatement->vars.emplace_back(var);
      localAssignStatement->values.emplace_back(value);

      if (value->is<Luau::AstExprFunction>()) {
        auto localFunction = Luau::AstStatLocalFunction(
            local->location, var, value->as<Luau::AstExprFunction>());
        traverse(&localFunction, state);
        continue;
      };

      trackAstLocalAssignment(var, state);
      traverse(value, state);
    }

    state.currentBlock->pushStatement(localAssignStatement);
    return;
  };

  if (auto local = node->as<Luau::AstExprLocal>()) {
    const char *localName = local->local->name.value;
    // not in current scope, find the scope

    if (state.currentBlock->locals.contains(localName) ||
        state.currentBlock->dependencies.contains(localName)) {
      return;
    };

    Block *block = state.currentBlock;

    while (block != nullptr) {
      if (block->locals.contains(localName)) {
        state.currentBlock->dependencies[localName] = block;
        return;
      }

      block = block->parent;
    }

    return;
  }
}

const std::string blockTypeToString(Block *type) {
  if (type->is<RootBlock>()) {
    return "Root";
  } else if (auto block = type->as<SingleConditionBlock>()) {
    if (block->type == SingleConditionBlock::Type::While) {
      return "While";
    } else if (block->type == SingleConditionBlock::Type::Repeat) {
      return "Repeat";
    }

    return "unknown";
  } else if (type->is<IfStatementBlock>()) {
    return "IfStatement";
  } else if (type->is<IfBlock>()) {
    switch (type->as<IfBlock>()->type) {
    case IfBlock::Type::Then:
      return "IfStatementTruthy";
    case IfBlock::Type::Else:
      return "IfStatementFalsy";
    case IfBlock::Type::Elseif:
      return "IfStatementElseif";
    default:
      return "IfStatementUnknown";
    }
  } else if (type->is<LocalFunctionBlock>()) {
    return "LocalFunction";
  } else if (type->is<FunctionBlock>()) {
    return "Function";
  } else if (type->is<ForBlock>()) {
    return "For";
  } else if (type->is<ForInBlock>()) {
    return "ForIn";
  } else if (type->is<DoBlock>()) {
    return "Do";
  }
  return "Unknown";
}

const std::string getBlockColor(Block *type) {
  // basic structural blocks - bold base colors
  if (type->is<RootBlock>()) {
    return "#FF1493"; // deep pink
  } else if (type->is<DoBlock>()) {
    return "#FF4500"; // orange red
  }

  // loop blocks - electric purples/pinks
  else if (type->is<SingleConditionBlock>()) {
    return "#8A2BE2"; // blue violet
  } else if (type->is<ForBlock>()) {
    return "#9400D3"; // dark violet
  } else if (type->is<ForInBlock>()) {
    return "#FF00FF"; // magenta
  }
  // function blocks - bright yellows/oranges
  else if (type->is<FunctionBlock>()) {
    return "#FFD700"; // gold
  } else if (type->is<LocalFunctionBlock>()) {
    return "#FFA500"; // orange
  }

  // conditional blocks - vivid greens/cyans
  else if (type->is<IfStatementBlock>()) {
    return "#00FF00"; // lime
  }

  return "#FF69B4"; // hot pink (default)
}

const std::string getStatementColor(Statement *type) {
  // assignment statements - electric neons
  if (type->is<AssignStatement>()) {
    return "#39FF14"; // neon green
  } else if (type->is<LocalAssignStatement>()) {
    return "#00FF00"; // lime green
  } else if (type->is<CompoundAssignStatement>()) {
    return "#7FFF00"; // electric chartreuse
  }

  // control flow statements - electric blues/purples
  else if (type->is<BreakStatement>()) {
    return "#00FFFF"; // electric cyan
  } else if (type->is<ContinueStatement>()) {
    return "#1F51FF"; // electric blue
  } else if (type->is<ReturnStatement>()) {
    return "#FF00FF"; // electric magenta
  }

  // other statements
  else if (type->is<ExpressionStatement>()) {
    return "#FF10F0"; // hot magenta
  }

  return "#FF2E89"; // electric rose (default)
}

const std::string statementTypeToString(Statement *type) {
  if (type->is<AssignStatement>()) {
    return "Assign";
  } else if (type->is<LocalAssignStatement>()) {
    return "LocalAssign";
  } else if (type->is<CompoundAssignStatement>()) {
    return "CompoundAssign";
  } else if (type->is<BreakStatement>()) {
    return "Break";
  } else if (type->is<ContinueStatement>()) {
    return "Continue";
  } else if (type->is<ReturnStatement>()) {
    return "Return";
  } else if (type->is<ExpressionStatement>()) {
    return "Expression";
  }
  return "Unknown";
}

void generateDotNode(Block *block, std::string &output) {
  // generate unique node identifier
  std::string nodeId = std::to_string(reinterpret_cast<uintptr_t>(block));
  std::string nodeDefinition = blockTypeToString(block);

  if (block->is<LocalFunctionBlock>() || block->is<FunctionBlock>()) {
    FunctionBlock *fn = static_cast<FunctionBlock *>(block);

    nodeDefinition.append(" (\\\"");
    nodeDefinition.append(fn->name);
    nodeDefinition.append("\\\")");
  }

  const auto color = getBlockColor(block);
  std::string prefix = "    ";
  nodeDefinition.insert(0, prefix + nodeId + " [shape=Mrecord," + "color=\"" +
                               color + "\"," + "label=\"");

  if (!block->locals.empty() || !block->dependencies.empty()) {
    nodeDefinition += "|";
  }

  // add local variables to node label
  auto locals = block->locals.values();
  for (size_t index = 0; index < locals.size(); index++) {
    const auto &localName = locals[index].first;

    nodeDefinition += "<local_" + nodeId + "_" + localName + ">";
    nodeDefinition += "local " + std::string(localName);
    if (index < locals.size() - 1 || !block->dependencies.empty()) {
      nodeDefinition += "|";
    }
  }

  // add dependency information
  auto deps = block->dependencies.values();
  for (size_t index = 0; index < deps.size(); index++) {
    const auto dep = deps[index];
    nodeDefinition += "<dep_" +
                      std::to_string(reinterpret_cast<uintptr_t>(dep.second)) +
                      "_" + dep.first + ">";
    nodeDefinition += "importUpvalue " + std::string(dep.first);

    if (index < deps.size() - 1) {
      nodeDefinition += "|";
    }
  }

  nodeDefinition += "\"];\n";

  output += nodeDefinition;
  std::string last = nodeId;
  size_t statementIndex = 0;
  size_t childIndex = 0;

  for (const bool &isStatement : block->order) {
    if (isStatement) {
      const auto &statement = block->statements[statementIndex++];
      std::string statementId =
          std::to_string(reinterpret_cast<uintptr_t>(statement));

      std::string label = statementTypeToString(statement);
      auto fields = getFields(statement);
      if (!fields.empty())
        label.append("|");

      for (size_t index = 0; index < fields.size(); index++) {
        label += fields.at(index);
        if (index < fields.size() - 1) {
          label += "|";
        }
      }

      output += prefix + statementId + " [shape=Mrecord,color=\"" +
                getStatementColor(statement) + "\",label=\"" + label + "\"]\n";
      output += prefix + last + " -> " + statementId + ";\n";
      last = statementId;
    } else {
      // block
      const auto &child = block->children[childIndex++];
      std::string childId = std::to_string(reinterpret_cast<uintptr_t>(child));
      generateDotNode(child, output);

      output += prefix + last + " -> " + childId + ";\n";
      last = childId;
    }
  }
  // generate edges to dependencies
  for (const auto &[dependencyName, dependencySource] : block->dependencies) {
    if (dependencySource != block) {
      std::string dependencyBlockId =
          std::to_string(reinterpret_cast<uintptr_t>(dependencySource));
      output += prefix + nodeId + ":dep_" + dependencyBlockId + "_" +
                dependencyName + " -> " + dependencyBlockId + ":local_" +
                dependencyBlockId + "_" + dependencyName +
                " [style=dashed,color=blue,label=\"  uses " + dependencyName +
                "\"];\n";
    }
  }
}

std::string generateDot(Luau::AstStatBlock *node) {
  AstTracking t;
  node->visit(&t);
  Glue glue = initGlue(t);

  RootBlock block = RootBlock();

  TrackingState state = {
      .currentBlock = &block,
  };

  traverse(node, state);

  std::string output = "digraph RootDAG {\n";

  // graph-wide settings
  output += "    rankdir=LR;\n"; // left -> right graph
  output += "    compound=true;\n";
  output += "    node [fontname=\"Helvetica\",style=filled,fillcolor=white];\n";
  output += "    edge [fontname=\"Helvetica\",penwidth=1.2];\n";

  generateDotNode(&block, output);

  output += "}\n";

  return output;
}

Glue initGlue(AstTracking &tracking) {
  Glue glue = {};

  std::vector<std::pair<const char *, size_t>> globalUses(
      tracking.globalUses.begin(), tracking.globalUses.end());

  std::vector<std::pair<std::string_view, size_t>> stringUses(
      tracking.stringUses.begin(), tracking.stringUses.end());

  if (globalUses.empty() && stringUses.empty()) {
    return glue;
  };

  std::sort(globalUses.begin(), globalUses.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  std::sort(stringUses.begin(), stringUses.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  std::string output = "local ";
  std::string originalNameMapping = "=";

  for (size_t index = 0; index < globalUses.size(); index++) {
    const char *originalName = globalUses[index].first;
    const std::string translatedName = getNameAtIndex(index + 1);

    output.append(translatedName);
    originalNameMapping.append(originalName);
    glue.globals[originalName] = translatedName;

    if (index < globalUses.size() - 1) {
      output.append(",");
      originalNameMapping.append(",");
    }
  }

  size_t nameIndex = globalUses.size();
  std::vector<std::pair<std::string_view, std::string>> profitableStrings;

  for (size_t index = 0; index < stringUses.size(); index++) {
    const auto &[string, uses] = stringUses[index];
    const std::string localName = getNameAtIndex(++nameIndex);

    const size_t variableUseCost = localName.size();
    const size_t variableInitCost = variableUseCost + 1; // comma = 1
    const size_t effectiveStringCost =
        (calculateEffectiveLength(replaceAll(string.data(), "\"", "\\\"")) +
         2); // quotes = 2

    const size_t regularCost = effectiveStringCost * uses;

    const size_t withVariablesCost =
        variableInitCost + effectiveStringCost + (uses * variableUseCost);

    if (regularCost > withVariablesCost) {
      profitableStrings.emplace_back(string, localName);
    } else {
      nameIndex--;
    }
  }

  if (!profitableStrings.empty() && !globalUses.empty()) {
    output.append(",");
    originalNameMapping.append(",");
  }

  for (size_t index = 0; index < profitableStrings.size(); index++) {
    const auto &[string, localName] = profitableStrings[index];

    output.append(localName);

    originalNameMapping.append("\"");
    appendRawString(originalNameMapping,
                    replaceAll(string.data(), "\"", "\\\""));
    originalNameMapping.append("\"");

    glue.strings[string] = localName;

    if (index < profitableStrings.size() - 1) {
      output.append(",");
      originalNameMapping.append(",");
    }
  }

  output.append(originalNameMapping);

  // add semicolon because identifiers are not whitespace
  output.append(";");

  glue.nameIndex = nameIndex;
  glue.init = output;

  return glue;
}
