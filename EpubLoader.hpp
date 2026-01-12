#pragma once

#include "stringview.h"
#include <cstdint>
#include <optional>
#include <span>

namespace Book {

struct TocEntry {
  StringView label;
  uint32_t zipEntryIndex;
};

struct SpineEntry {
  StringView idref;
  uint32_t zipEntryIndex;
  std::optional<uint32_t> tocEntryIndex;
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
