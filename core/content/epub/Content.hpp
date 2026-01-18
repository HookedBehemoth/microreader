#pragma once

#include <StringView.hpp>
#include <content/zip/ZipParser.hpp>
#include <mem/Allocator.hpp>
#include <content/epub/FileResolver.hpp>

#include <cstdio>
#include <optional>

namespace epub::content {

struct Data {
  std::optional<StringView> title;
  std::optional<StringView> author;
  std::optional<StringView> language;
  std::optional<uint16_t> coverZipIndex;
  std::optional<uint16_t> tocZipIndex;
  std::span<uint16_t> spineZipIndices;
};

std::optional<Data> parse(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  const epub::FileResolver& resolver,
  mem::Allocator& allocator);

}