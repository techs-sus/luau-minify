#pragma once

#include <Luau/Ast.h>
#include <Luau/DenseHash.h>
#include <reflex/matcher.h>
#include <string>

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

inline static const bool isLuauKeyword(const char *target) {
  return luauKeywords.contains(target);
};

inline static const bool isWhitespaceCharacter(const char character) {
  return whitespaceCharacters.contains(character);
}

static const std::string stringSafeRegex = reflex::Matcher::convert(
    "^[A-Za-z0-9!@#$%^&*()_+| }{:\"?><\\[\\]\\;\\\\',./\\-`~=]+");

inline const void addWhitespaceIfNeeded(std::string *string) {
  if (string->empty()) {
    return;
  }

  // if the lastCharacter is not a whitespace character, add a space
  const char lastCharacter = string->back();
  if (!isWhitespaceCharacter(lastCharacter)) {
    string->append(" ");
  }
}
