#include "syntax.h"
#include <algorithm>
#include <string>

const std::string getNameAtIndex(size_t count) {
  std::string letters;
  while (count != 0) {
    count--;
    const char letter = (97 + (count % 26));

    letters.insert(0, &letter);
    count /= 26;
  }

  if (isLuauKeyword(letters.c_str())) {
    letters.insert(0, "_");
  }

  return letters;
}

static reflex::Matcher stringSafeMatcher(stringSafeRegex, "");

void appendRawString(std::string &output, absl::string_view string) {
  static char unsafeByteBuffer[5]; // \x takes 2 bytes; %02x takes 2 bytes; and
                                   // null byte overhead

  std::vector<std::pair<std::pair<std::string, size_t>, bool>> blobs;

  // reset the matcher, but preserve the pattern and set input data
  stringSafeMatcher.input(string.data());

  while (stringSafeMatcher.find() != 0) {
    blobs.emplace_back(
        std::pair(stringSafeMatcher.text(), stringSafeMatcher.first()), true);
  }

  // reset the matcher, but preserve the pattern and set input data
  stringSafeMatcher.input(string.data());

  while (stringSafeMatcher.split() != 0) {
    blobs.emplace_back(
        std::pair(stringSafeMatcher.text(), stringSafeMatcher.first()), false);
  }

  std::sort(blobs.begin(), blobs.end(), [](const auto &a, const auto &b) {
    return a.first.second < b.first.second;
  });

  for (const auto &[pair, isStringSafe] : blobs) {
    if (isStringSafe) {
      // write all safe string data
      output.append(pair.first);
    } else {
      // if any unsafe bytes are left over, manually encode them
      for (unsigned char character : pair.first) {
        // buffer overflow isn't possible thanks to snprintf(),
        // but character should still be unsigned
        snprintf(unsafeByteBuffer, sizeof(unsafeByteBuffer), "\\x%02x",
                 character);
        output.append(unsafeByteBuffer);
      }
    }
  }
}

size_t calculateEffectiveLength(absl::string_view string) {
  size_t length = 0;

  // reset the matcher, but preserve the pattern and set input data
  stringSafeMatcher.input(string.data());

  while (stringSafeMatcher.find() != 0) {
    length += stringSafeMatcher.size();
  }

  // reset the matcher, but also preserve the pattern and input data
  stringSafeMatcher.input(string.data());

  while (stringSafeMatcher.split() != 0) {
    length += 4 * stringSafeMatcher.size();
  }

  return length;
}

const std::string replaceAll(std::string str, const std::string &from,
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
