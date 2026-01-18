#pragma once

#include <content/css/EpubCssParser.hpp>
#include <StringView.hpp>
#include <content/zip/ZipParser.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace Book {

struct CssFile {
  uint16_t zipIndex;
  std::span<css::CssRule> rules;
};

enum class EpubLoadResult : uint8_t {
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

std::optional<uint16_t> getSpineEntryCount();
std::optional<uint16_t> getSpineZipFileIndex(uint16_t spineIndex);
std::optional<uint16_t> getTocForSpineEntry(uint16_t spineIndex); 
std::optional<uint16_t> getTocEntryCount();
std::optional<StringView> getTocLabel(uint16_t tocIndex);
std::optional<zip::ZipFileEntry> getZipFileEntry(uint16_t zipIndex);
std::optional<std::span<Book::CssFile>> getCssRules();

void unload();
void deleteCache();
void deleteCacheForFile(StringView filePath);

} // namespace Book
