#include <algorithm>
#include <string>
#include <string_view>

#include "syntax.h"
#include "tracking.h"

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
