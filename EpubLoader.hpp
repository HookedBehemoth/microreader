#pragma once

#include "stringview.h"
#include <optional>
#include <span>

namespace Book {

struct TocEntry {
  StringView label;
  StringView src;
};

struct SpineEntry {
  StringView idref;
  StringView src;
  std::optional<TocEntry*> tocEntry;
};

enum class EpubLoadResult {
  Success,
  InvalidFormat,
  OutOfMemory,
  IoFailure,
  MissingFile,
  InvalidState,
  Bogus = 69
};

EpubLoadResult loadEpub(StringView filePath);

std::optional<StringView> getTitle();
std::optional<StringView> getAuthor();
std::optional<StringView> getLanguage();

void unload();
void deleteCache();
void deleteCacheForFile(StringView filePath);

} // namespace Book
