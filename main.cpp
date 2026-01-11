
#include <cstdio>

#include "log.h"

#include "stringview.h"
#include "stopwatch.h"

#include "EpubLoader.hpp"

int main(/* int argc, char *argv[] */) {
  StringView testFiles[] = {
    "1. We are Legion - Dennis E. Taylor.epub",
    "2. For We Are Many - Dennis E. Taylor.epub",
    "3. All These Worlds - Dennis E Taylor.epub",
    "4. Heaven's River - Dennis E. Taylor.epub",
    "ohler.epub"
  };
  for (const auto& file : testFiles) {
    printf("Loading EPUB file: %.*s\n", (int)file.size(), file.data());
    Stopwatch sw;
    auto result = Book::loadEpub(file);
    printf("Loading took %ld microseconds\n", sw.elapsedMicroseconds());

    if (result != Book::EpubLoadResult::Success) {
      printf("Failed to load EPUB: %d\n", (int)result);
      return 1;
    }
    println("\"", *Book::getTitle(), "\" by \"", *Book::getAuthor(), "\" (", *Book::getLanguage(), ")");
    
    Book::unload();
    printf("Successfully loaded and unloaded EPUB file: %.*s\n\n", (int)file.size(), file.data());
  }
  return 0;
}
