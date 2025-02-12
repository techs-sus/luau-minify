#include "minifier.h"
#include "syntax.h"

#include <Luau/Ast.h>
#include <charconv>
#include <cstdio>
#include <limits>
#include <reflex/matcher.h>
#include <string_view>
#include <system_error>

const std::string getLocalName(size_t totalLocals) {
  std::string letters;
  while (totalLocals != 0) {
    totalLocals--;
    const char letter = (97 + (totalLocals % 26));

    letters.insert(0, &letter);
    totalLocals /= 26;
  }

  if (isLuauKeyword(letters.c_str())) {
    letters.insert(0, "_");
  }

  return letters;
}

// We differentiate in the output by adding two underscores, which is NOT in the
// character range of getLocalName().
static const char *globalNamePrefix = "__";
const std::string getGlobalName(size_t totalGlobals) {
  std::string localName = getLocalName(totalGlobals);
  localName.insert(0, globalNamePrefix);

  return localName;
};

std::string replaceAll(std::string str, const std::string &from,
                       const std::string &to) {
  if (from.empty())
    return str;
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
  return str;
}

void handleAstLocal(Luau::AstLocal *local, State *state, size_t localDepth) {
  auto name = getLocalName(state->totalLocals);

  state->locals[localDepth][local->name.value] = name;
  state->output.append(name);
}

void handleNode(Luau::AstNode *node, State *state, size_t localDepth) {
  if (!state->locals.contains(localDepth)) {
    state->locals[localDepth] = {};
  };

  if (node->is<Luau::AstStatBlock>()) {
    // top level, list of AstStat's
    auto block = node->as<Luau::AstStatBlock>();

    for (auto node : block->body) {
      handleNode(node, state, localDepth + 1);
    }

    for (const auto &[depth, localMap] : state->locals) {
      if (depth - 1 > localDepth) {
        state->locals[depth] = {};
      }
    }

    addWhitespaceIfNeeded(&state->output);
  } else if (node->is<Luau::AstStatExpr>()) {
    addWhitespaceIfNeeded(&state->output);
    handleNode(node->as<Luau::AstStatExpr>()->expr, state, localDepth + 1);
  } else if (node->is<Luau::AstExprCall>()) {
    addWhitespaceIfNeeded(&state->output);
    auto call = node->as<Luau::AstExprCall>();

    handleNode(call->func, state, localDepth + 1);

    state->output.append("(");

    for (size_t index = 0; index < call->args.size; index++) {
      auto argument = call->args.data[index];

      handleNode(argument, state, localDepth + 1);

      if (index < call->args.size - 1) {
        state->output.append(",");
      }
    }

    state->output.append(")");
  } else if (node->is<Luau::AstStatLocal>()) {
    addWhitespaceIfNeeded(&state->output);
    state->output.append("local ");
    auto statement = node->as<Luau::AstStatLocal>();
    State newState =
        State{.output = "",
              .locals = state->locals,
              .totalLocals = state->totalLocals,
              .global_variables_state = state->global_variables_state};

    // don't add more values than there are variables
    if (statement->values.size > statement->vars.size) {
      statement->values.size = statement->vars.size;
    }

    for (size_t index = 0; index < statement->values.size; index++) {
      auto value = statement->values.data[index];
      handleNode(value, &newState, localDepth + 1);
      if (index < statement->values.size - 1) {
        newState.output.append(",");
      }
    }

    for (size_t index = 0; index < statement->vars.size; index++) {
      auto astLocal = statement->vars.data[index];

      state->totalLocals++;
      handleAstLocal(astLocal, state, localDepth + 1);

      if (index < statement->vars.size - 1) {
        state->output.append(",");
      }
    }

    if (statement->values.size > 0) {
      state->output.append("=");
    }

    state->output.append(newState.output);
    addWhitespaceIfNeeded(&state->output);
  } else if (node->is<Luau::AstExprLocal>()) {
    auto expr = node->as<Luau::AstExprLocal>();

    /*
      Perform a backward search in order to find renamed variables in higher
      stacks. Start at localDepth because that is the current local stack, and
      go backward, checking each local stack if it has this local variable's
      name.
    */
    for (size_t depth = localDepth; depth > 0; depth--) {
      if (state->locals.contains(depth) &&
          state->locals[depth].contains(expr->local->name.value)) {
        state->output.append(state->locals[depth][expr->local->name.value]);
        return;
      }
    }

    state->output.append("unknown");
  } else if (node->is<Luau::AstStatAssign>()) {
    addWhitespaceIfNeeded(&state->output);

    auto assign = node->as<Luau::AstStatAssign>();
    State assignedValuesState =
        State{.output = "",
              .locals = state->locals,
              .totalLocals = state->totalLocals,

              .global_variables_state = state->global_variables_state};

    for (size_t index = 0; index < assign->values.size; index++) {
      auto value = assign->values.data[index];
      handleNode(value, &assignedValuesState, localDepth + 1);
      if (index < assign->values.size - 1) {
        assignedValuesState.output.append(",");
      }
    }

    for (size_t index = 0; index < assign->vars.size; index++) {
      auto expr = assign->vars.data[index];

      if (expr->is<Luau::AstExprLocal>()) {
        state->totalLocals++;
      }

      handleNode(expr, state, localDepth + 1);

      if (index < assign->vars.size - 1) {
        state->output.append(",");
      }
    }

    if (assign->values.size > 0) {
      state->output.append("=");
    }

    state->output.append(assignedValuesState.output);
    addWhitespaceIfNeeded(&state->output);
  } else if (node->is<Luau::AstExprVarargs>()) {
    state->output.append("...");
  } else if (node->is<Luau::AstExprGlobal>()) {
    auto expr = node->as<Luau::AstExprGlobal>();

    GlobalVariablesState *global_variables_state =
        state->global_variables_state;

    if (!state->global_variables_state->map.contains(expr->name.value)) {
      auto globalName = getGlobalName(state->global_variables_state->total);
      global_variables_state->map[expr->name.value] = globalName;
      global_variables_state->total++;
      state->output.append(globalName);
    } else {
      state->output.append(global_variables_state->map[expr->name.value]);
    }
  } else if (node->is<Luau::AstExprConstantNumber>()) {
    auto expr = node->as<Luau::AstExprConstantNumber>();

    if (expr->parseResult == Luau::ConstantNumberParseResult::Imprecise) {
      state->output.append("1.7976931348623157e+308");
    } else if (expr->parseResult ==
                   Luau::ConstantNumberParseResult::HexOverflow ||
               expr->parseResult ==
                   Luau::ConstantNumberParseResult::BinOverflow) {
      state->output.append("0xffffffffffffffff");
    } else if (expr->parseResult == Luau::ConstantNumberParseResult::Ok) {
      std::string characterDataBuffer(
          std::numeric_limits<double>::max_digits10 + 2, '\0');

      auto result = std::to_chars(
          characterDataBuffer.data(),
          characterDataBuffer.data() + characterDataBuffer.size(), expr->value);
      if (result.ec == std::errc::value_too_large) {
        // TODO: is this really right?
        state->output.append("1.7976931348623157e+308");
        return;
      }
      state->output.append(std::string(characterDataBuffer.data(), result.ptr));
    };
  } else if (node->is<Luau::AstExprConstantString>()) {
    auto expr = node->as<Luau::AstExprConstantString>();
    state->output.append("\"");

    reflex::Matcher matcher(stringSafeRegex, expr->value.data);

    if (expr->value.size >= 1) {
      size_t matches = matcher.matches();
      std::string_view matchedStringView = matcher.strview();

      // write all safe string data
      state->output.append(
          replaceAll(std::string(matchedStringView), "\"", "\\\""));

      // if any unsafe bytes are left over, manually encode them
      if (!matches) {
        for (size_t index = matcher.size(); index < expr->value.size; index++) {
          // ** BUFFER OVERFLOW IF CHARACTER IS SIGNED CHAR **
          unsigned char character = expr->value.data[index];
          char buf[5]; // \x takes 2 bytes; %02x takes 2 bytes; and null byte
                       // overhead
          sprintf(buf, "\\x%02x", character);
          state->output.append(buf);
        }
      }
    }

    state->output.append("\"");
  } else if (node->is<Luau::AstExprConstantBool>()) {
    auto expr = node->as<Luau::AstExprConstantBool>();
    if (expr->value) {
      state->output.append("true");
    } else {
      state->output.append("false");
    }
  } else if (node->is<Luau::AstExprConstantNil>()) {
    state->output.append("nil");
  } else if (node->is<Luau::AstExprInterpString>()) {
    auto expr = node->as<Luau::AstExprInterpString>();

    state->output.append("`");

    // much of this logic is reused from AstExprConstantString
    for (size_t index = 0; index < expr->strings.size - 1; index++) {
      auto string = expr->strings.data[index];
      reflex::Matcher matcher(stringSafeRegex, string.data);
      size_t matches = matcher.matches();
      std::string_view matchedStringView = matcher.strview();

      // write all safe string data
      state->output.append(
          replaceAll(std::string(matchedStringView), "`", "\\`"));

      if (!matches) {
        for (size_t index = matcher.size(); index < string.size; index++) {
          // ** BUFFER OVERFLOW IF CHARACTER IS SIGNED CHAR **
          unsigned char character = string.data[index];
          char buf[5]; // \x takes 2 bytes; %02x takes 2 bytes; and null byte
                       // overhead
          sprintf(buf, "\\x%02x", character);
          state->output.append(buf);
        }
      }

      auto expression = expr->expressions.data[index];
      state->output.append("{");
      handleNode(expression, state, localDepth + 1);
      state->output.append("}");
    }

    state->output.append("`");
  } else if (node->is<Luau::AstExprTable>()) {
    auto expr = node->as<Luau::AstExprTable>();
    state->output.append("{");

    for (size_t index = 0; index < expr->items.size; index++) {
      auto item = expr->items.data[index];
      if (item.key != nullptr) {
        state->output.append("[");
        handleNode(item.key, state, localDepth + 1);
        state->output.append("]=");
      }

      handleNode(item.value, state, localDepth + 1);

      if (index < expr->items.size - 1) {
        state->output.append(",");
      }
    }

    state->output.append("}");
  } else if (node->is<Luau::AstExprIndexName>()) {
    auto expr = node->as<Luau::AstExprIndexName>();
    handleNode(expr->expr, state, localDepth + 1);
    state->output.append(&expr->op);
    state->output.append(expr->index.value);
  } else if (node->is<Luau::AstStatCompoundAssign>()) {
    auto expr = node->as<Luau::AstStatCompoundAssign>();

    handleNode(expr->var, state, localDepth + 1);
    state->output.append(compoundSymbols[expr->op]);
    state->output.append("=");
    handleNode(expr->value, state, localDepth + 1);

    addWhitespaceIfNeeded(&state->output);
  } else if (node->is<Luau::AstExprUnary>()) {
    auto unary = node->as<Luau::AstExprUnary>();

    state->output.append(compoundSymbols[unary->op]);
    handleNode(unary->expr, state, localDepth + 1);
  } else if (node->is<Luau::AstExprBinary>()) {
    auto binary = node->as<Luau::AstExprBinary>();

    handleNode(binary->left, state, localDepth + 1);
    state->output.append(compoundSymbols[binary->op]);
    handleNode(binary->right, state, localDepth + 1);
  } else if (node->is<Luau::AstStatIf>()) {
    /*
      This only covers the following snippet:
      if <cond> then
        <MANDATORY_THEN_BODY>
      else
        <ELSE_BODY_CAN_BE_NULLPTR>
      end
    */
    auto if_statement = node->as<Luau::AstStatIf>();
    state->output.append("if ");
    handleNode(if_statement->condition, state, localDepth + 1);

    addWhitespaceIfNeeded(&state->output);
    state->output.append("then ");
    handleNode(if_statement->thenbody, state, localDepth + 1);

    if (if_statement->elsebody != nullptr) {
      addWhitespaceIfNeeded(&state->output);
      handleNode(if_statement->elsebody, state, localDepth + 1);
    }

    addWhitespaceIfNeeded(&state->output);
    state->output.append("end ");
  } else if (node->is<Luau::AstExprIfElse>()) {
    auto expr = node->as<Luau::AstExprIfElse>();

    state->output.append("if ");
    handleNode(expr->condition, state, localDepth + 1);
    addWhitespaceIfNeeded(&state->output);
    state->output.append("then ");

    handleNode(expr->trueExpr, state, localDepth + 1);
    if (expr->hasElse) {
      state->output.append(" else");
      state->output.append((expr->falseExpr->is<Luau::AstExprIfElse>() ||
                                    expr->falseExpr->is<Luau::AstStatIf>()
                                ? ""
                                : " "));
      handleNode(expr->falseExpr, state, localDepth + 1);
    }
  } else if (node->is<Luau::AstStatLocalFunction>()) {
    auto local_function = node->as<Luau::AstStatLocalFunction>();

    addWhitespaceIfNeeded(&state->output);
    state->output.append("local ");
    state->totalLocals++;
    handleAstLocal(local_function->name, state, localDepth + 1);

    state->output.append("=");
    handleNode(local_function->func, state, localDepth + 1);
  } else if (node->is<Luau::AstStatFunction>()) {
    addWhitespaceIfNeeded(&state->output);
    auto function = node->as<Luau::AstStatFunction>();

    handleNode(function->name, state, localDepth + 1);
    state->output.append("=");
    handleNode(function->func, state, localDepth + 1);
  } else if (node->is<Luau::AstExprFunction>()) {
    auto expr = node->as<Luau::AstExprFunction>();

    state->output.append("function(");

    size_t index = 0;
    for (auto arg : expr->args) {
      state->totalLocals++;
      handleAstLocal(arg, state, localDepth + 3);

      if (index < expr->args.size - 1) {
        state->output.append(",");
      }

      index += 1;
    }

    if (expr->vararg) {
      if (expr->args.size > 0) {
        state->output.append(",");
      }
      state->output.append("...");
    }

    state->output.append(")");
    handleNode(expr->body, state, localDepth + 1);
    state->output.append("end");
  } else if (node->is<Luau::AstExprIndexExpr>()) {
    auto expr = node->as<Luau::AstExprIndexExpr>();
    addWhitespaceIfNeeded(&state->output);

    handleNode(expr->expr, state, localDepth + 1);
    state->output.append("[");
    handleNode(expr->index, state, localDepth + 1);
    state->output.append("]");
  } else if (node->is<Luau::AstStatWhile>()) {
    auto while_statement = node->as<Luau::AstStatWhile>();

    addWhitespaceIfNeeded(&state->output);
    state->output.append("while ");
    handleNode(while_statement->condition, state, localDepth + 1);
    addWhitespaceIfNeeded(&state->output);
    state->output.append("do ");
    handleNode(while_statement->body, state, localDepth + 1);
    addWhitespaceIfNeeded(&state->output);
    state->output.append("end ");
  } else if (node->is<Luau::AstExprGroup>()) {
    auto expr_group = node->as<Luau::AstExprGroup>();
    state->output.append("(");
    handleNode(expr_group->expr, state, localDepth + 1);
    state->output.append(")");
  } else if (node->is<Luau::AstStatFor>()) {
    auto for_statement = node->as<Luau::AstStatFor>();
    addWhitespaceIfNeeded(&state->output);
    state->output.append("for ");
    state->totalLocals++;

    State forLoopState{.output = "",
                       .locals = state->locals,
                       .totalLocals = state->totalLocals,

                       .global_variables_state = state->global_variables_state};

    handleAstLocal(for_statement->var, &forLoopState, localDepth + 1);
    forLoopState.output.append("=");
    handleNode(for_statement->from, &forLoopState, localDepth + 1);
    forLoopState.output.append(",");
    handleNode(for_statement->to, &forLoopState, localDepth + 1);
    forLoopState.output.append(" ");

    if (for_statement->step != nullptr) {
      forLoopState.output.append(",");
      handleNode(for_statement->step, &forLoopState, localDepth + 1);
      forLoopState.output.append(" ");
    }

    addWhitespaceIfNeeded(&state->output);
    forLoopState.output.append("do ");
    handleNode(for_statement->body, &forLoopState, localDepth + 1);
    addWhitespaceIfNeeded(&state->output);
    forLoopState.output.append("end ");

    state->output.append(forLoopState.output);
  } else if (node->is<Luau::AstStatForIn>()) {
    auto for_in_statement = node->as<Luau::AstStatForIn>();

    addWhitespaceIfNeeded(&state->output);
    state->output.append("for ");

    size_t index = 0;

    for (auto var : for_in_statement->vars) {
      state->totalLocals++;

      handleAstLocal(var, state, localDepth + 1);

      if (index < for_in_statement->vars.size - 1) {
        state->output.append(",");
      }

      index += 1;
    }

    index = 0;

    addWhitespaceIfNeeded(&state->output);
    state->output.append("in ");

    for (auto value : for_in_statement->values) {
      handleNode(value, state, localDepth + 1);
      if (index < for_in_statement->values.size - 1) {
        state->output.append(",");
      }

      index += 1;
    }

    addWhitespaceIfNeeded(&state->output);
    state->output.append("do ");
    handleNode(for_in_statement->body, state, localDepth + 1);
    addWhitespaceIfNeeded(&state->output);
    state->output.append("end ");
  } else if (node->is<Luau::AstStatRepeat>()) {
    auto repeat_statement = node->as<Luau::AstStatRepeat>();
    addWhitespaceIfNeeded(&state->output);

    state->output.append("repeat ");

    for (auto statement : repeat_statement->body->body) {
      handleNode(statement, state, localDepth + 1);
    }

    addWhitespaceIfNeeded(&state->output);
    state->output.append("until ");
    handleNode(repeat_statement->condition, state, localDepth + 1);
    addWhitespaceIfNeeded(&state->output);
  } else if (node->is<Luau::AstStatBreak>()) {
    addWhitespaceIfNeeded(&state->output);
    state->output.append("break;");
  } else if (node->is<Luau::AstStatReturn>()) {
    auto return_statement = node->as<Luau::AstStatReturn>();

    addWhitespaceIfNeeded(&state->output);
    state->output.append("return ");

    size_t index = 0;
    for (auto node : return_statement->list) {
      handleNode(node, state, localDepth + 1);

      if (index < return_statement->list.size - 1) {
        state->output.append(",");
      }

      index++;
    }

    state->output.append(";");
  } else if (node->is<Luau::AstStatContinue>()) {
    addWhitespaceIfNeeded(&state->output);
    state->output.append("continue;");
  } else {
    // unhandled node
    return;
  }
}

std::string processAstRoot(Luau::AstStatBlock *root) {
  GlobalVariablesState global_variables_state = {};

  State state = {.global_variables_state = &global_variables_state};

  handleNode(root, &state, 0);

  // skip emitting global glue if no globals are used
  if (state.global_variables_state->map.empty()) {
    return state.output;
  }

  std::string globalMapOutput = "local ";

  size_t index = 0;
  for (const auto &[_, translatedName] : state.global_variables_state->map) {
    globalMapOutput.append(translatedName);

    if (index < state.global_variables_state->map.size() - 1) {
      globalMapOutput.append(",");
    }
    index++;
  }

  // we don't need a space because the equal sign counts as enough whitespace to
  // produce valid code
  globalMapOutput.append("=");

  index = 0;
  for (const auto &[originalName, _] : state.global_variables_state->map) {
    globalMapOutput.append(originalName);

    if (index < state.global_variables_state->map.size() - 1) {
      globalMapOutput.append(",");
    }
    index++;
  }

  // add semicolon because identifiers are not whitespace
  globalMapOutput.append(";");

  // append global map setup to the start of the root output
  state.output.insert(0, globalMapOutput);

  return state.output;
}
