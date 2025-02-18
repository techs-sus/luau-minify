#pragma once

#include <Luau/Ast.h>
#include <Luau/DenseHash.h>
#include <ankerl/unordered_dense.h>
#include <reflex/matcher.h>
#include <string>

static const ankerl::unordered_dense::set<const char *> luauKeywords = {
    "do",    "end",    "while",  "repeat",   "until", "if",
    "then",  "else",   "elseif", "for",      "in",    "function",
    "local", "return", "break",  "continue", "true",  "false",
    "nil",   "and",    "or",     "not",
};

static const ankerl::unordered_dense::set<char> whitespaceCharacters = {
    ' ', ';', '}', '{', ')', '(', ',', ']', '[', '.',  '=',
    '+', '-', '*', '/', '%', '^', '#', '"', '`', '\'',
};

static const char *compoundSymbols[Luau::AstExprBinary::Op__Count] = {
    "+",  "-",  "*", "/",  "//", "%",  "^",     "..",
    "~=", "==", "<", "<=", ">",  ">=", " and ", " or ",
};

inline static bool isLuauKeyword(const char *target) {
  return luauKeywords.contains(target);
};

inline static bool isWhitespaceCharacter(const char character) {
  return whitespaceCharacters.contains(character);
}

static const std::string stringSafeRegex = reflex::Matcher::convert(
    "[A-Za-z0-9!@#$%^&*()_+| }{:\"?><\\[\\]\\;\\\\',./\\-`~=]+");

inline void addWhitespaceIfNeeded(std::string &string) {
  // if the string is empty, then no whitespace is needed
  if (string.empty()) {
    return;
  };

  if (!isWhitespaceCharacter(string.back())) {
    string.append(" ");
  }
}

const std::string getNameAtIndex(size_t count);

// in "str", all references of "from" are replaced with "to"
const std::string replaceAll(std::string str, const std::string &from,
                             const std::string &to);

// callee's are expected to escape quotes themselves
void appendRawString(std::string &output, std::string_view string);
size_t calculateEffectiveLength(std::string_view string);
