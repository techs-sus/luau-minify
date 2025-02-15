#include "tracking.h"
#include "syntax.h"
#include <algorithm>
#include <string>
#include <string_view>

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

  if (!stringUses.empty()) {
    output.append(",");
    originalNameMapping.append(",");
  }

  size_t nameIndex = globalUses.size();

  for (size_t index = 0; index < stringUses.size(); index++) {
    const auto &[string, uses] = stringUses[index];
    const std::string localName = getNameAtIndex(++nameIndex);

    const int32_t variableUseCost = localName.size();
    const int32_t variableInitCost = variableUseCost + 1; // comma = 1
    const int32_t stringCost = (string.size() + 2);       // quotes = 2

    const int32_t regularCost = (stringCost * uses);

    const int32_t withVariablesCost =
        variableInitCost + (uses * variableUseCost);

    // if the cost with variables is more expensive than the regular cost,
    // continue to the next iteration
    if (withVariablesCost > regularCost) {
      continue;
    }

    output.append(localName);

    originalNameMapping.append("\"");
    appendRawString(originalNameMapping,
                    replaceAll(string.data(), "\"", "\\\"").c_str());
    originalNameMapping.append("\"");

    glue.strings[string] = localName;

    if (index < stringUses.size() - 1) {
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
