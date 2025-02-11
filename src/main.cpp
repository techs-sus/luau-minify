#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luau/Location.h"
#include "Luau/ParseOptions.h"
#include "Luau/Parser.h"
#include "minifier.h"

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

  std::string output = processAstRoot(parseResult.root);
  std::cout << output << std::endl;

  return 0;
}
