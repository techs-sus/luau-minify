#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ios>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Luau/Location.h"
#include "Luau/ParseOptions.h"
#include "Luau/Parser.h"
#include "minifier.h"

static void displayHelp(const char *program_name) {
  printf("Usage: %s [file]\nDotviz generator: %s --dotviz [file]\n",
         program_name, program_name);
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
  std::ifstream file{name, std::ios_base::binary};

  if (!file.is_open()) {
    return std::nullopt;
  }

  std::string contents;
  std::string line;

  while (std::getline(file, line)) {
    // if line is a shebang, skip
    if (!(line.length() > 2 && line.at(0) == '#' && line.at(1) == '!')) {
      contents.append(line);
      contents.append("\n");
    }
  }

  file.close();

  return contents;
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
  } else if (argc == 3 && strcmp(argv[2], "--dotviz") == 0) {
    // don't display help
  } else if (argc < 2) {
    displayHelp(argv[0]);
    return 1;
  }

  const char *name = (argc == 3) ? argv[2] : argv[1];
  std::string source;

  if (strcmp(name, "-") == 0) {
    // read from stdin
    std::string line;
    while (std::getline(std::cin, line)) {
      source.append(line);
      source.append("\n");
    }
  } else {
    // read from file
    std::optional<std::string> fileContents = readFile(name);

    if (fileContents == std::nullopt) {
      std::cerr << "failed reading file: " << name << std::endl;
      return 1;
    }

    source = fileContents.value();
  }

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

  if (argc != 3) {
    std::cout << processAstRoot(parseResult.root) << std::endl;
  } else {
    std::cout << generateDot(parseResult.root) << std::endl;
  }

  return 0;
}
