#include <algorithm>
#include <string>
#include <string_view>

#include "Luau/Ast.h"
#include "ankerl/unordered_dense.h"
#include "syntax.h"
#include "tracking.h"

enum class BlockType {
  Root = 0,
  While,
  For,
  ForIn,
  Repeat,
  LocalFunction,
  Function,
  IfStatement,
  IfStatementThen,
  IfStatementElse,
  IfStatementElseif,
  DoBlock, // or unknown
};

struct BlockLocalTracking {
  BlockInfo *block = nullptr;
  BlockType type;
  std::string metadata = ""; // empty unless type == LocalFunction or
                             // Function; points to a std::string
  ankerl::unordered_dense::map<const char *, BlockLocalTracking *>
      dependencies = {}; // locals which are dependent on the block's parent

  BlockLocalTracking *parent = nullptr;
  std::vector<BlockLocalTracking *> children = {};
};

struct TrackingState {
  BlockLocalTracking *currentTracking = nullptr;
  global_usage_map globalUses = global_usage_map();
  string_usage_map stringUses = string_usage_map();
  size_t totalLocals = 0;
};

void trackAstLocalAssignment(TrackingState &state,
                             const Luau::AstLocal *local) {
  const std::string name = getNameAtIndex(++state.totalLocals);
  state.currentTracking->block->locals[local->name.value] = name;
}

void trackCall(TrackingState &state, BlockLocalTracking *tracking,
               std::function<void()> closure) {
  BlockLocalTracking *currentTracking = state.currentTracking;

  currentTracking->children.emplace_back(tracking);
  currentTracking->block->children.emplace_back(tracking->block);

  tracking->parent = currentTracking;
  tracking->block->parent = currentTracking->block;

  state.currentTracking = tracking;
  closure();
  state.currentTracking = currentTracking;
};

void traverse(const Luau::AstNode *node, TrackingState &state) {
  if (node->is<Luau::AstExprGlobal>()) {
    const auto expr = node->as<Luau::AstExprGlobal>();
    state.globalUses[expr->name.value]++;

    BlockLocalTracking *tracking = state.currentTracking;

    while (tracking->parent != nullptr) {
      tracking = tracking->parent;
    }

    if (tracking != state.currentTracking)
      state.currentTracking->dependencies[expr->name.value] = tracking;

    return;
  }

  if (node->is<Luau::AstExprConstantString>()) {
    const auto expr = node->as<Luau::AstExprConstantString>();

    const std::string_view view(expr->value.begin(), expr->value.end());
    state.stringUses[view]++;

    return;
  }

  if (node->is<Luau::AstStatBlock>()) {
    auto block = node->as<Luau::AstStatBlock>();

    for (const auto &statement : block->body) {
      traverse(statement, state);
    }

    return;
  }

  if (node->is<Luau::AstStatExpr>()) {
    auto stat = node->as<Luau::AstStatExpr>();
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
      trackAstLocalAssignment(state, arg);
    };

    return;
  }

  if (node->is<Luau::AstExprGroup>()) {
    auto expr = node->as<Luau::AstExprGroup>();
    traverse(expr->expr, state);
    return;
  }

  if (node->is<Luau::AstStatWhile>()) {
    BlockInfo *block = new BlockInfo{.parent = state.currentTracking->block};

    BlockLocalTracking *tracking = new BlockLocalTracking{
        .block = block,
        .type = BlockType::While,
        .dependencies = {},
        .parent = state.currentTracking,
        .children = {},
    };

    trackCall(state, tracking,
              [&] { traverse(node->as<Luau::AstStatWhile>()->body, state); });

    return;
  }

  if (node->is<Luau::AstStatFor>()) {
    BlockInfo *block = new BlockInfo{.parent = state.currentTracking->block};

    BlockLocalTracking *tracking = new BlockLocalTracking{
        .block = block,
        .type = BlockType::For,
        .dependencies = {},
        .parent = state.currentTracking,
        .children = {},
    };

    trackCall(state, tracking,
              [&] { traverse(node->as<Luau::AstStatFor>()->body, state); });

    return;
  }

  if (node->is<Luau::AstStatForIn>()) {
    BlockInfo *block = new BlockInfo{.parent = state.currentTracking->block};

    BlockLocalTracking *tracking = new BlockLocalTracking{
        .block = block,
        .type = BlockType::ForIn,
        .dependencies = {},
        .parent = state.currentTracking,
        .children = {},
    };

    trackCall(state, tracking,
              [&] { traverse(node->as<Luau::AstStatForIn>()->body, state); });

    return;
  }

  if (node->is<Luau::AstStatRepeat>()) {
    BlockInfo *block = new BlockInfo{.parent = state.currentTracking->block};

    BlockLocalTracking *tracking = new BlockLocalTracking{
        .block = block,
        .type = BlockType::Repeat,
        .dependencies = {},
        .parent = state.currentTracking,
        .children = {},
    };

    trackCall(state, tracking,
              [&] { traverse(node->as<Luau::AstStatRepeat>()->body, state); });

    return;
  }

  if (node->is<Luau::AstStatLocalFunction>()) {
    BlockInfo *block = new BlockInfo{.parent = state.currentTracking->block};

    BlockLocalTracking *tracking = new BlockLocalTracking{
        .block = block,
        .type = BlockType::LocalFunction,
        .dependencies = {},
        .parent = state.currentTracking,
        .children = {},
    };

    auto statement = node->as<Luau::AstStatLocalFunction>();

    // ensure this scope can access fn
    trackAstLocalAssignment(state, statement->name);

    trackCall(state, tracking, [&] {
      tracking->metadata.append(statement->name->name.value);
      traverse(statement->func, state);
    });

    return;
  }

  if (node->is<Luau::AstStatFunction>()) {
    BlockInfo *block = new BlockInfo{.parent = state.currentTracking->block};

    BlockLocalTracking *tracking = new BlockLocalTracking{
        .block = block,
        .type = BlockType::Function,
        .dependencies = {},
        .parent = state.currentTracking,
        .children = {},
    };

    auto statement = node->as<Luau::AstStatFunction>();

    traverse(statement->name, state);
    trackCall(state, tracking, [&] {
      if (statement->name->is<Luau::AstExprGlobal>()) {
        tracking->metadata.append(
            statement->name->as<Luau::AstExprGlobal>()->name.value);
      } else if (statement->name->is<Luau::AstExprLocal>()) {
        tracking->metadata.append(
            statement->name->as<Luau::AstExprLocal>()->local->name.value);
      }

      traverse(statement->func, state);
    });

    return;
  }

  if (node->is<Luau::AstStatIf>()) {
    BlockInfo *block = new BlockInfo{.parent = state.currentTracking->block};

    BlockLocalTracking *tracking = new BlockLocalTracking{
        .block = block,
        .type = BlockType::IfStatement,
        .dependencies = {},
        .parent = state.currentTracking,
        .children = {},
    };

    auto statement = node->as<Luau::AstStatIf>();

    trackCall(state, tracking, [&] {
      BlockInfo *truthyBlock = new BlockInfo{.parent = block};

      BlockLocalTracking *truthyTracking = new BlockLocalTracking{
          .block = truthyBlock,
          .type = BlockType::IfStatementThen,
          .dependencies = {},
          .parent = tracking,
          .children = {},
      };

      trackCall(state, truthyTracking,
                [&] { traverse(statement->thenbody, state); });

      if (statement->elsebody == nullptr) {
        return;
      };

      if (statement->elsebody->is<Luau::AstStatIf>()) {
        std::vector<std::pair<Luau::AstStatBlock *, BlockType>> elseifs;
        Luau::AstStatIf *ptr = statement->elsebody->as<Luau::AstStatIf>();

        while (ptr != nullptr) {
          elseifs.emplace_back(ptr->thenbody, BlockType::IfStatementElseif);

          if (ptr->elsebody->is<Luau::AstStatIf>()) {
            ptr = ptr->elsebody->as<Luau::AstStatIf>();
          } else if (ptr == nullptr) {
            break;
          } else {
            elseifs.emplace_back(ptr->elsebody->as<Luau::AstStatBlock>(),
                                 BlockType::IfStatementElse);
            break;
          };
        }

        for (const auto &[node, blockType] : elseifs) {
          BlockInfo *unknownBlock = new BlockInfo{.parent = block};

          BlockLocalTracking *unknownTracking = new BlockLocalTracking{
              .block = unknownBlock,
              .type = blockType,
              .dependencies = {},
              .parent = tracking,
              .children = {},
          };

          trackCall(state, unknownTracking, [&] { traverse(node, state); });
        };
        return;
      } else {
        BlockInfo *elseBlock = new BlockInfo{.parent = block};

        BlockLocalTracking *elseTracking = new BlockLocalTracking{
            .block = elseBlock,
            .type = BlockType::IfStatementElse,
            .dependencies = {},
            .parent = tracking,
            .children = {},
        };

        trackCall(state, elseTracking,
                  [&] { traverse(statement->elsebody, state); });
      };
    });

    return;
  };

  if (node->is<Luau::AstStatLocal>()) {
    auto local = node->as<Luau::AstStatLocal>();

    const size_t assignments = std::min(local->values.size, local->vars.size);

    for (size_t index = 0; index < assignments; index++) {
      const auto var = local->vars.data[index];
      const auto value = local->values.data[index];

      if (value->is<Luau::AstExprFunction>()) {
        auto localFunction = Luau::AstStatLocalFunction(
            local->location, var, value->as<Luau::AstExprFunction>());
        traverse(&localFunction, state);
        continue;
      };

      trackAstLocalAssignment(state, var);
      traverse(local->values.data[index], state);
    }

    return;
  };

  if (node->is<Luau::AstExprLocal>()) {
    auto local = node->as<Luau::AstExprLocal>()->local;

    const char *localName = local->name.value;
    // not in current scope, find the scope

    if (state.currentTracking->block->locals.contains(localName) ||
        state.currentTracking->dependencies.contains(localName)) {
      return;
    };

    BlockLocalTracking *tracking = state.currentTracking;

    while (tracking != nullptr) {
      if (tracking->block->locals.contains(localName)) {
        state.currentTracking->dependencies[localName] = tracking;
        return;
      }

      tracking = tracking->parent;
    }

    return;
  }
}

const std::string blockTypeToString(BlockType type) {
  switch (type) {
  case BlockType::Root:
    return "Root";
  case BlockType::While:
    return "While";
  case BlockType::For:
    return "For";
  case BlockType::ForIn:
    return "ForIn";
  case BlockType::Repeat:
    return "Repeat";
  case BlockType::Function:
    return "Function";
  case BlockType::IfStatement:
    return "IfStatement";
  case BlockType::IfStatementThen:
    return "IfStatementTruthy";
  case BlockType::IfStatementElse:
    return "IfStatementFalsy";
  case BlockType::IfStatementElseif:
    return "IfStatementElseif";
  case BlockType::LocalFunction:
    return "LocalFunction";
  case BlockType::DoBlock:
    return "DoBlock";
  default:
    return "Unknown";
  }
}

void generateDotvizNode(const BlockLocalTracking *tracking,
                        std::string &output) {
  // Generate unique node identifier
  std::string nodeId = std::to_string(reinterpret_cast<uintptr_t>(tracking));

  // Create node with block type and local variables
  std::string name;
  name.append(blockTypeToString(tracking->type));
  if (tracking->type == BlockType::LocalFunction ||
      tracking->type == BlockType::Function) {
    name.append(" (\\\"");
    name.append(tracking->metadata);
    name.append("\\\")");
  }

  output +=
      "    \"" + nodeId + "\" [shape=Mrecord,label=\"" + name +
      ((!tracking->block->locals.empty() || !tracking->dependencies.empty())
           ? "|"
           : "");

  // Add local variables to node label
  for (const auto &local : tracking->block->locals) {
    output += "local " + std::string(local.first) + "\\n";
  }

  // Add dependencies information
  for (const auto &dep : tracking->dependencies) {
    output += "importUpvalue " + std::string(dep.first) + "\\n";
  }

  output += "\"];\n";

  // Generate edges to children
  for (const auto *child : tracking->children) {
    std::string childId = std::to_string(reinterpret_cast<uintptr_t>(child));
    output += "    \"" + nodeId + "\" -> \"" + childId + "\";\n";

    // Recursively process children
    generateDotvizNode(child, output);
  }

  // generate edges to dependencies
  for (const auto &[dependencyName, dependencySource] :
       tracking->dependencies) {
    if (dependencySource != tracking) {
      std::string dependencyBlockId =
          std::to_string(reinterpret_cast<uintptr_t>(dependencySource));
      output += "    \"" + nodeId + "\" -> \"" + dependencyBlockId +
                "\" [style=dashed,color=blue,label=\"  uses " +
                std::string(dependencyName) + "\"];\n";
    }
  }
}

std::string generateDotviz(Luau::AstStatBlock *node) {
  AstTracking t;
  node->visit(&t);
  Glue glue = initGlue(t);

  BlockInfo rootBlock = {
      .parent = nullptr,
      .children = {},
      .locals = glue.globals,
  };

  BlockLocalTracking rootTracking = {
      .block = &rootBlock,
      .type = BlockType::Root,
      .metadata = "",
      .dependencies = {},
      .parent = nullptr,
      .children = {},
  };

  TrackingState state = {
      .currentTracking = &rootTracking,
  };

  traverse(node, state);

  std::string output = "digraph RootDAG {\n";

  // graph-wide settings
  output += "    rankdir=LR;\n"; // left -> right graph
  output += "    node [fontname=\"Helvetica\"];\n";
  output += "    edge [fontname=\"Helvetica\"];\n";

  // graph legend
  output += "    subgraph cluster_legend {\n";
  output += "        label=\"Legend\";\n";
  output += "        fontname=\"Helvetica\";\n";
  output += "        style=dashed;\n";
  output += "        \"legend_regular\" [shape=box,label=\"Block\"];\n";
  output += "        \"legend_dependency\" "
            "[shape=box,style=dashed,color=blue,label=\"Dependency\"];\n";
  output += "    }\n\n";

  generateDotvizNode(&rootTracking, output);

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
