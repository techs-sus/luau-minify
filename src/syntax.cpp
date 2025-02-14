#include "syntax.h"
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
