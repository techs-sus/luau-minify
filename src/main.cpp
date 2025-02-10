#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luau/Ast.h"
#include "Luau/Common.h"
#include "Luau/DenseHash.h"
#include "Luau/Location.h"
#include "Luau/ParseOptions.h"
#include "Luau/Parser.h"
#include "reflex/matcher.h"

static void displayHelp(const char *program_name) {
  printf("Usage: %s [file]\n", program_name);
}

static int assertionHandler(const char *expr, const char *file, int line,
                            const char *function) {
  printf("%s(%d): ASSERTION FAILED: %s\n", file, line, expr);
  return 1;
}

std::string formatLocation(const Luau::Location &location) {
  std::ostringstream out;

  Luau::Position begin = location.begin;
  Luau::Position end = location.end;

  out << begin.line << ":" << begin.column << " - " << end.line << ":"
      << end.column;

  return out.str();
}

struct State {
  std::string output;

  // [depth][node.name] = getLocalName(&state.totalLocals);
  std::unordered_map<size_t, std::unordered_map<std::string, std::string>>
      locals;
  size_t totalLocals;
};

static const Luau::DenseHashSet<const char *> createLuauKeywords() {
  auto set = Luau::DenseHashSet<const char *>("");

  set.insert("do");
  set.insert("end");
  set.insert("while");
  set.insert("repeat");
  set.insert("until");
  set.insert("if");
  set.insert("then");
  set.insert("else");
  set.insert("elseif");
  set.insert("for");
  set.insert("in");
  set.insert("function");
  set.insert("local");
  set.insert("return");
  set.insert("break");
  set.insert("continue");
  set.insert("true");
  set.insert("false");
  set.insert("nil");
  set.insert("and");
  set.insert("or");
  set.insert("not");

  return set;
};

static const Luau::DenseHashSet<char> createWhitespaceCharacters() {
  auto set = Luau::DenseHashSet<char>((char)255);

  set.insert(' ');
  set.insert(';');
  set.insert('}');
  set.insert('{');
  set.insert(')');
  set.insert('(');
  set.insert(',');
  set.insert(']');
  set.insert('[');
  set.insert('.');
  set.insert('=');
  set.insert('+');
  set.insert('-');
  set.insert('*');
  set.insert('/');
  set.insert('%');
  set.insert('^');
  set.insert('#');
  set.insert('"');
  set.insert('`');
  set.insert('\'');

  return set;
};

static const Luau::DenseHashSet<const char *> luauKeywords =
    createLuauKeywords();
static const Luau::DenseHashSet<char> whitespaceCharacters =
    createWhitespaceCharacters();

static const char *compoundSymbols[Luau::AstExprBinary::Op__Count] = {
    "+",  "-",  "*", "/",  "//", "%",  "^",     "..",
    "~=", "==", "<", "<=", ">",  ">=", " and ", " or ",
};

static const bool isLuauKeyword(const char *target) {
  return luauKeywords.contains(target);
}

static const bool isWhitespaceCharacter(const char character) {
  return whitespaceCharacters.contains(character);
}

std::string getLocalName(size_t number) {
  std::string letters;
  while (number != 0) {
    number--;
    const char letter = (97 + (number % 26));

    letters.insert(0, &letter);
    number /= 26;
  }

  if (isLuauKeyword(letters.c_str())) {
    letters.insert(0, "_");
  }

  return letters;
}

const void addWhitespaceIfNeeded(std::string *string) {
  if (string->empty()) {
    return;
  }

  // if the lastCharacter is not a whitespace character, add a space
  const char lastCharacter = string->back();
  if (!isWhitespaceCharacter(lastCharacter)) {
    string->append(" ");
  }
}

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

static const std::string stringSafeRegex = reflex::Matcher::convert(
    "^[A-Za-z0-9!@#$%^&*()_+| }{:\"?><\\[\\]\\;\\\\',./\\-`~=]+");

void handleAstLocal(Luau::AstLocal *local, State *state, size_t localDepth) {
  auto name = getLocalName(state->totalLocals);

  state->locals[localDepth][local->name.value] = name;
  state->output.append(name);
}

void handleNode(Luau::AstNode *node, State *state, size_t localDepth) {
  if (state->locals.find(localDepth) == state->locals.end()) {
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

    size_t index = 0;
    for (auto argument : call->args) {
      handleNode(argument, state, localDepth + 1);

      if (index < call->args.size - 1) {
        state->output.append(",");
      }

      index++;
    }

    state->output.append(")");
  } else if (node->is<Luau::AstStatLocal>()) {
    addWhitespaceIfNeeded(&state->output);
    state->output.append("local ");
    auto statement = node->as<Luau::AstStatLocal>();
    State newState = State{
        .output = "",
        .locals = state->locals,
        .totalLocals = state->totalLocals,
    };

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
      if (state->locals.find(depth) != state->locals.end() &&
          state->locals[depth].find(expr->local->name.value) !=
              state->locals[depth].end()) {
        state->output.append(state->locals[depth][expr->local->name.value]);
        return;
      }
    }

    state->output.append("unknown");
  } else if (node->is<Luau::AstStatAssign>()) {
    addWhitespaceIfNeeded(&state->output);

    auto assign = node->as<Luau::AstStatAssign>();
    State newState = State{
        .output = "",
        .locals = state->locals,
        .totalLocals = state->totalLocals,
    };

    size_t index = 0;
    for (auto value : assign->values) {
      handleNode(value, &newState, localDepth + 1);
      if (index < assign->values.size - 1) {
        newState.output.append(",");
      }

      index++;
    }

    index = 0;

    for (auto subNode : assign->vars) {
      state->totalLocals++;

      handleNode(subNode, state, localDepth + 1);

      if (index < assign->vars.size - 1) {
        state->output.append(",");
      }

      index++;
    }

    if (assign->values.size > 0) {
      state->output.append("=");
    }

    state->output.append(newState.output);
    addWhitespaceIfNeeded(&state->output);
  } else if (node->is<Luau::AstExprGlobal>()) {
    auto expr = node->as<Luau::AstExprGlobal>();
    state->output.append(expr->name.value);
  } else if (node->is<Luau::AstExprConstantNumber>()) {
    auto expr = node->as<Luau::AstExprConstantNumber>();
    state->output.append(std::to_string(expr->value));
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

    size_t index = 0;
    for (auto item : expr->items) {
      if (item.key != nullptr) {
        state->output.append("[");
        handleNode(item.key, state, localDepth + 1);
        state->output.append("]=");
      }

      handleNode(item.value, state, localDepth + 1);

      if (index < expr->items.size - 1) {
        state->output.append(",");
      }

      index++;
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

    State newExtraForState{
        .output = "",
        .locals = state->locals,
        .totalLocals = state->totalLocals,
    };

    handleAstLocal(for_statement->var, &newExtraForState, localDepth + 1);
    newExtraForState.output.append("=");
    handleNode(for_statement->from, &newExtraForState, localDepth + 1);
    newExtraForState.output.append(",");
    handleNode(for_statement->to, &newExtraForState, localDepth + 1);
    newExtraForState.output.append(" ");

    if (for_statement->step != nullptr) {
      newExtraForState.output.append(",");
      handleNode(for_statement->step, &newExtraForState, localDepth + 1);
      newExtraForState.output.append(" ");
    }

    addWhitespaceIfNeeded(&state->output);
    newExtraForState.output.append("do ");
    handleNode(for_statement->body, &newExtraForState, localDepth + 1);
    addWhitespaceIfNeeded(&state->output);
    newExtraForState.output.append("end ");

    state->output.append(newExtraForState.output);
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
    }

    state->output.append(";");
  } else if (node->is<Luau::AstExprVarargs>()) {
    state->output.append("...");
  } else if (node->is<Luau::AstStatContinue>()) {
    addWhitespaceIfNeeded(&state->output);
    state->output.append("continue;");
  } else {
    // unhandled node
    return;
  }
}

std::optional<std::string> readFile(const std::string &name) {
  FILE *file = fopen(name.c_str(), "rb");

  if (!file)
    return std::nullopt;

  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  if (length < 0) {
    fclose(file);
    return std::nullopt;
  }
  fseek(file, 0, SEEK_SET);

  std::string result(length, 0);

  size_t read = fread(result.data(), 1, length, file);
  fclose(file);

  if (read != size_t(length))
    return std::nullopt;

  // Skip first line if it's a shebang
  if (length > 2 && result[0] == '#' && result[1] == '!')
    result.erase(0, result.find('\n'));

  return result;
}

int main(int argc, char **argv) {
  Luau::assertHandler() = assertionHandler;

  for (Luau::FValue<bool> *flag = Luau::FValue<bool>::list; flag;
       flag = flag->next)
    if (strncmp(flag->name, "Luau", 4) == 0)
      flag->value = true;

  if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
    displayHelp(argv[0]);
    return 0;
  } else if (argc < 2) {
    displayHelp(argv[0]);
    return 1;
  }

  const char *name = argv[1];

  std::optional<std::string> fileContents = readFile(name);

  if (fileContents == std::nullopt) {
    fprintf(stderr, "failed reading file %s\n", name);
    return 1;
  }

  std::string source = fileContents.value();

  Luau::Allocator allocator;
  Luau::AstNameTable names(allocator);
  Luau::ParseOptions options;

  Luau::ParseResult parseResult = Luau::Parser::parse(
      source.data(), source.size(), names, allocator, options);

  if (!parseResult.errors.empty()) {
    std::cerr << "Parse errors were encountered:" << std::endl;
    for (const Luau::ParseError &error : parseResult.errors) {
      fprintf(stderr, "  %s - %s\n",
              formatLocation(error.getLocation()).c_str(),
              error.getMessage().c_str());
    }

    return 1;
  }

  std::string output;
  std::unordered_map<size_t, std::unordered_map<std::string, std::string>>
      locals;

  State state = {
      .output = output,
      .locals = locals,
      .totalLocals = 0,
  };

  handleNode(parseResult.root, &state, 0);

  std::cout << state.output << std::endl;

  return 0;
}
