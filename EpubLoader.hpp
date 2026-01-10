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

struct TableOfContents {
  std::span<TocEntry> entries;
  std::span<SpineEntry> spineEntries;
};

struct Book {
  StringView title;
};

enum class EpubLoadResult {
  Success,
  SdCardAccessFailed,
  InvalidFormat,
  MissingFile,
  OutOfMemory,
  InvalidState,
  Bogus = 69
};

EpubLoadResult loadEpub(StringView filePath);

void unload();
void deleteCache();
void deleteCacheForFile(StringView filePath);

} // namespace Book
