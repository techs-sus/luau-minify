#include <Luau/Ast.h>
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <limits>
#include <reflex/matcher.h>
#include <string_view>
#include <system_error>

#include "minifier.h"
#include "syntax.h"

// Creates and appends a variable name for an AstLocal, based on state's current
// totalLocals, which should get incremented before this function call.
void handleAstLocalAssignment(const Luau::AstLocal *local, State &state) {
  const std::string name = getNameAtIndex(state.totalLocals);

  state.blockInfo->locals[local->name.value] = name;
  state.output.append(name);
}

// Calls the closure in the scope of block. block is added to the state's
// blockInfo children, and block's parent is set to the state's blockInfo
// pointer.
void callAsChildBlock(State &state, BlockInfo *block,
                      std::function<void()> closure) {
  BlockInfo *currentInfo = state.blockInfo;

  currentInfo->children.emplace_back(block);
  block->parent = currentInfo;

  state.blockInfo = block;
  closure();
  state.blockInfo = currentInfo;
};

void handleNode(const Luau::AstNode *node, State &state) {
  if (node->is<Luau::AstStatBlock>()) {
    // top level block, do blocks, functions
    const auto block = node->as<Luau::AstStatBlock>();

    for (const auto &node : block->body) {
      handleNode(node, state);
    }

    // TODO: Is this code relevant?
    /*  for (const auto &[depth, _] : state.locals) {
          if (depth - 1 > blockDepth) {
            state.locals[depth] = {};
          }
        }
    */

    addWhitespaceIfNeeded(state.output);
  } else if (node->is<Luau::AstStatExpr>()) {
    addWhitespaceIfNeeded(state.output);
    handleNode(node->as<Luau::AstStatExpr>()->expr, state);
  } else if (node->is<Luau::AstExprCall>()) {
    const auto call = node->as<Luau::AstExprCall>();
    addWhitespaceIfNeeded(state.output);

    handleNode(call->func, state);

    state.output.append("(");

    for (size_t index = 0; index < call->args.size; index++) {
      const auto argument = call->args.data[index];

      handleNode(argument, state);

      if (index < call->args.size - 1) {
        state.output.append(",");
      }
    }

    state.output.append(")");
  } else if (node->is<Luau::AstStatLocal>()) {
    const auto statement = node->as<Luau::AstStatLocal>();
    addWhitespaceIfNeeded(state.output);
    State assignValuesState = State{.output = "",
                                    .totalLocals = state.totalLocals,
                                    .globals = state.globals,
                                    .strings = state.strings,
                                    .blockInfo = state.blockInfo};

    // don't emit more values than there are variables
    size_t totalAssignments =
        std::min(statement->values.size, statement->vars.size);

    state.output.append("local ");

    for (size_t index = 0; index < totalAssignments; index++) {
      const auto value = statement->values.data[index];
      const auto local = statement->vars.data[index];

      state.totalLocals++;
      handleAstLocalAssignment(local, state);
      handleNode(value, assignValuesState);

      if (index < totalAssignments - 1) {
        assignValuesState.output.append(",");
        state.output.append(",");
      }
    }

    state.output.append("=");
    state.output.append(assignValuesState.output);
    addWhitespaceIfNeeded(state.output);
  } else if (node->is<Luau::AstExprLocal>()) {
    const auto local = node->as<Luau::AstExprLocal>()->local;

    /*
      Traverse the function hierachy in order to find renamed local variables at
      higher function stacks. Start at the current function, if it exists, and
      go backward in the hierachy, checking each function stack to see if it has
      this local variable's name. Stops when a stack is found, or when info ==
      nullptr (a parent of nullptr is the root node).
    */
    BlockInfo *info = state.blockInfo;
    while (info != nullptr) {
      if (info->locals.contains(local->name.value)) {
        state.output.append(info->locals[local->name.value]);
        return;
      }

      info = info->parent;
    }

    state.output.append("unknown");
  } else if (node->is<Luau::AstStatAssign>()) {
    const auto assign = node->as<Luau::AstStatAssign>();
    addWhitespaceIfNeeded(state.output);

    State assignedValuesState = State{
        .output = "",
        .totalLocals = state.totalLocals,
        .globals = state.globals,
        .strings = state.strings,
        .blockInfo = state.blockInfo,
    };

    for (size_t index = 0; index < assign->values.size; index++) {
      const auto value = assign->values.data[index];
      handleNode(value, assignedValuesState);
      if (index < assign->values.size - 1) {
        assignedValuesState.output.append(",");
      }
    }

    for (size_t index = 0; index < assign->vars.size; index++) {
      const auto expr = assign->vars.data[index];

      if (expr->is<Luau::AstExprLocal>()) {
        state.totalLocals++;
      }

      handleNode(expr, state);

      if (index < assign->vars.size - 1) {
        state.output.append(",");
      }
    }

    if (assign->values.size > 0) {
      state.output.append("=");
    }

    state.output.append(assignedValuesState.output);
    addWhitespaceIfNeeded(state.output);
  } else if (node->is<Luau::AstExprVarargs>()) {
    state.output.append("...");
  } else if (node->is<Luau::AstExprGlobal>()) {
    const auto expr = node->as<Luau::AstExprGlobal>();

    // originalName -> translatedName
    state.output.append(state.globals[expr->name.value]);
  } else if (node->is<Luau::AstExprConstantNumber>()) {
    const auto expr = node->as<Luau::AstExprConstantNumber>();

    if (expr->parseResult == Luau::ConstantNumberParseResult::Imprecise) {
      state.output.append("1.7976931348623157e+308");
    } else if (expr->parseResult ==
                   Luau::ConstantNumberParseResult::HexOverflow ||
               expr->parseResult ==
                   Luau::ConstantNumberParseResult::BinOverflow) {
      state.output.append("0xffffffffffffffff");
    } else if (expr->parseResult == Luau::ConstantNumberParseResult::Ok) {
      std::string characterDataBuffer(
          std::numeric_limits<double>::max_digits10 + 2, (char)0);

      auto result = std::to_chars(
          characterDataBuffer.data(),
          characterDataBuffer.data() + characterDataBuffer.size(), expr->value);
      if (result.ec == std::errc::value_too_large) {
        // TODO: is this really right?
        state.output.append("1.7976931348623157e+308");
        return;
      }
      state.output.append(std::string(characterDataBuffer.data(), result.ptr));
    };
  } else if (node->is<Luau::AstExprConstantString>()) {
    const auto expr = node->as<Luau::AstExprConstantString>();
    std::string_view view(expr->value.begin(), expr->value.end());

    if (state.strings.contains(view)) {
      state.output.append(state.strings[view]);
      return;
    }

    state.output.append("\"");

    if (expr->value.size != 0) {
      appendRawString(state.output, replaceAll(std::string(expr->value.begin(),
                                                           expr->value.end()),
                                               "\"", "\\\""));
    }

    state.output.append("\"");
  } else if (node->is<Luau::AstExprConstantBool>()) {
    const auto expr = node->as<Luau::AstExprConstantBool>();
    if (expr->value) {
      state.output.append("true");
    } else {
      state.output.append("1==0"); // false = 5 chars, 1==0 = 4 chars
    }
  } else if (node->is<Luau::AstExprConstantNil>()) {
    state.output.append("nil");
  } else if (node->is<Luau::AstExprInterpString>()) {
    const auto expr = node->as<Luau::AstExprInterpString>();

    state.output.append("`");

    for (size_t index = 0; index < expr->strings.size; index++) {
      auto string = expr->strings.data[index];
      if (string.size != 0) {
        appendRawString(
            state.output,
            replaceAll(std::string(string.begin(), string.end()), "`", "\\`"));
      };

      // the last string never has a corresponding expression
      if (index != expr->strings.size - 1) {
        auto expression = expr->expressions.data[index];

        state.output.append("{");
        handleNode(expression, state);
        state.output.append("}");
      }
    }

    state.output.append("`");
  } else if (node->is<Luau::AstExprTable>()) {
    const auto expr = node->as<Luau::AstExprTable>();
    state.output.append("{");

    for (size_t index = 0; index < expr->items.size; index++) {
      const auto &item = expr->items.data[index];
      if (item.key != nullptr) {
        state.output.append("[");
        handleNode(item.key, state);
        state.output.append("]=");
      }

      handleNode(item.value, state);

      if (index < expr->items.size - 1) {
        state.output.append(",");
      }
    }

    state.output.append("}");
  } else if (node->is<Luau::AstExprIndexName>()) {
    const auto expr = node->as<Luau::AstExprIndexName>();

    handleNode(expr->expr, state);
    state.output.append(1, expr->op);
    state.output.append(expr->index.value);
  } else if (node->is<Luau::AstStatCompoundAssign>()) {
    const auto expr = node->as<Luau::AstStatCompoundAssign>();

    handleNode(expr->var, state);
    state.output.append(compoundSymbols[expr->op]);
    state.output.append("=");
    handleNode(expr->value, state);

    addWhitespaceIfNeeded(state.output);
  } else if (node->is<Luau::AstExprUnary>()) {
    const auto unary = node->as<Luau::AstExprUnary>();

    state.output.append(compoundSymbols[unary->op]);
    handleNode(unary->expr, state);
  } else if (node->is<Luau::AstExprBinary>()) {
    const auto binary = node->as<Luau::AstExprBinary>();

    handleNode(binary->left, state);
    state.output.append(compoundSymbols[binary->op]);
    handleNode(binary->right, state);
  } else if (node->is<Luau::AstStatIf>()) {
    /*
      This only covers the following snippet:
      if <cond> then
        <MANDATORY_THEN_BODY>
      else
        <ELSE_BODY_CAN_BE_NULLPTR>
      end
    */
    const auto ifStatement = node->as<Luau::AstStatIf>();

    state.output.append("if ");
    handleNode(ifStatement->condition, state);
    addWhitespaceIfNeeded(state.output);

    BlockInfo thenBlock = {};
    BlockInfo elseBlock = {};

    state.output.append("then ");
    callAsChildBlock(state, &thenBlock,
                     [&] { handleNode(ifStatement->thenbody, state); });

    if (ifStatement->elsebody != nullptr) {
      addWhitespaceIfNeeded(state.output);
      state.output.append("else ");

      callAsChildBlock(state, &elseBlock,
                       [&] { handleNode(ifStatement->elsebody, state); });
    }

    addWhitespaceIfNeeded(state.output);
    state.output.append("end ");
  } else if (node->is<Luau::AstExprIfElse>()) {
    const auto expr = node->as<Luau::AstExprIfElse>();

    state.output.append("if ");
    handleNode(expr->condition, state);
    addWhitespaceIfNeeded(state.output);
    state.output.append("then ");

    handleNode(expr->trueExpr, state);
    if (expr->hasElse) {
      state.output.append(" else");
      state.output.append((expr->falseExpr->is<Luau::AstExprIfElse>() ||
                                   expr->falseExpr->is<Luau::AstStatIf>()
                               ? ""
                               : " "));
      handleNode(expr->falseExpr, state);
    }
  } else if (node->is<Luau::AstStatLocalFunction>()) {
    const auto local_function = node->as<Luau::AstStatLocalFunction>();

    addWhitespaceIfNeeded(state.output);
    state.output.append("local ");
    state.totalLocals++;
    handleAstLocalAssignment(local_function->name, state);

    state.output.append("=");
    handleNode(local_function->func, state);
  } else if (node->is<Luau::AstStatFunction>()) {
    const auto function = node->as<Luau::AstStatFunction>();

    addWhitespaceIfNeeded(state.output);
    handleNode(function->name, state);
    state.output.append("=");
    handleNode(function->func, state);
  } else if (node->is<Luau::AstExprFunction>()) {
    const auto expr = node->as<Luau::AstExprFunction>();

    state.output.append("function(");

    BlockInfo functionBlock = {};

    // handle function arguments and body in same block, to prevent leakage onto
    // the state's current block info
    callAsChildBlock(state, &functionBlock, [&] {
      for (size_t index = 0; index < expr->args.size; index++) {
        const auto functionArgument = expr->args.data[index];

        state.totalLocals++;
        handleAstLocalAssignment(functionArgument, state);

        if (index < expr->args.size - 1) {
          state.output.append(",");
        }
      }

      if (expr->vararg) {
        if (expr->args.size > 0) {
          state.output.append(",");
        }
        state.output.append("...");
      }

      state.output.append(")");

      handleNode(expr->body, state);
    });

    state.output.append("end");
  } else if (node->is<Luau::AstExprIndexExpr>()) {
    const auto expr = node->as<Luau::AstExprIndexExpr>();
    addWhitespaceIfNeeded(state.output);

    handleNode(expr->expr, state);

    state.output.append("[");
    handleNode(expr->index, state);
    state.output.append("]");

  } else if (node->is<Luau::AstStatWhile>()) {
    const auto while_statement = node->as<Luau::AstStatWhile>();

    BlockInfo whileBlockInfo = {};

    addWhitespaceIfNeeded(state.output);

    state.output.append("while ");
    handleNode(while_statement->condition, state);
    addWhitespaceIfNeeded(state.output);
    state.output.append("do ");

    callAsChildBlock(state, &whileBlockInfo,
                     [&] { handleNode(while_statement->body, state); });

    addWhitespaceIfNeeded(state.output);
    state.output.append("end ");
  } else if (node->is<Luau::AstExprGroup>()) {
    const auto group = node->as<Luau::AstExprGroup>();
    state.output.append("(");
    handleNode(group->expr, state);
    state.output.append(")");
  } else if (node->is<Luau::AstStatFor>()) {
    const auto forStatement = node->as<Luau::AstStatFor>();

    addWhitespaceIfNeeded(state.output);
    state.output.append("for ");
    state.totalLocals++;

    State forLoopState{.output = "",
                       .totalLocals = state.totalLocals,
                       .globals = state.globals,
                       .strings = state.strings,
                       .blockInfo = state.blockInfo};

    handleAstLocalAssignment(forStatement->var, forLoopState);
    forLoopState.output.append("=");
    handleNode(forStatement->from, forLoopState);
    forLoopState.output.append(",");
    handleNode(forStatement->to, forLoopState);
    forLoopState.output.append(" ");

    if (forStatement->step != nullptr) {
      forLoopState.output.append(",");
      handleNode(forStatement->step, forLoopState);
      forLoopState.output.append(" ");
    }

    BlockInfo forStatementBlockInfo = {};

    addWhitespaceIfNeeded(state.output);
    forLoopState.output.append("do ");

    callAsChildBlock(state, &forStatementBlockInfo,
                     [&] { handleNode(forStatement->body, forLoopState); });

    addWhitespaceIfNeeded(state.output);
    forLoopState.output.append("end ");
    state.output.append(forLoopState.output);
  } else if (node->is<Luau::AstStatForIn>()) {
    const auto forInStatement = node->as<Luau::AstStatForIn>();

    addWhitespaceIfNeeded(state.output);
    state.output.append("for ");

    for (size_t index = 0; index < forInStatement->vars.size; index++) {
      auto localVariable = forInStatement->vars.data[index];
      state.totalLocals++;

      handleAstLocalAssignment(localVariable, state);

      if (index < forInStatement->vars.size - 1) {
        state.output.append(",");
      }
    }

    addWhitespaceIfNeeded(state.output);
    state.output.append("in ");

    for (size_t index = 0; index < forInStatement->values.size; index++) {
      const auto value = forInStatement->values.data[index];
      handleNode(value, state);
      if (index < forInStatement->values.size - 1) {
        state.output.append(",");
      }
    }

    BlockInfo forInStatementBlock = {};

    addWhitespaceIfNeeded(state.output);
    state.output.append("do ");

    callAsChildBlock(state, &forInStatementBlock,
                     [&] { handleNode(forInStatement->body, state); });

    addWhitespaceIfNeeded(state.output);
    state.output.append("end ");

  } else if (node->is<Luau::AstStatRepeat>()) {
    const auto repeatStatement = node->as<Luau::AstStatRepeat>();

    addWhitespaceIfNeeded(state.output);
    state.output.append("repeat ");

    BlockInfo repeatStatementBlock = {};

    callAsChildBlock(state, &repeatStatementBlock,
                     [&] { handleNode(repeatStatement->body, state); });

    addWhitespaceIfNeeded(state.output);
    state.output.append("until ");
    handleNode(repeatStatement->condition, state);
    addWhitespaceIfNeeded(state.output);
  } else if (node->is<Luau::AstStatBreak>()) {
    addWhitespaceIfNeeded(state.output);
    state.output.append("break;");
  } else if (node->is<Luau::AstStatReturn>()) {
    const auto return_statement = node->as<Luau::AstStatReturn>();

    addWhitespaceIfNeeded(state.output);
    state.output.append("return ");

    for (size_t index = 0; index < return_statement->list.size; index++) {
      const auto node = return_statement->list.data[index];
      handleNode(node, state);

      if (index < return_statement->list.size - 1) {
        state.output.append(",");
      }
    }

    state.output.append(";");
  } else if (node->is<Luau::AstStatContinue>()) {
    addWhitespaceIfNeeded(state.output);
    state.output.append("continue;");
  } else {
    // unhandled node
    return;
  }
}

std::string processAstRoot(Luau::AstStatBlock *root) {
  AstTracking tracking;
  root->visit(&tracking);

  Glue glue = initGlue(tracking);
  BlockInfo rootBlockInfo = {.parent = nullptr};

  State state = {.output = glue.init,
                 .totalLocals = glue.nameIndex,
                 .globals = glue.globals,
                 .strings = glue.strings,
                 .blockInfo = &rootBlockInfo};

  handleNode(root, state);

  return state.output;
}
