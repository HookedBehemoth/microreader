#pragma once

#include <expected>
#include <cstdint>
#include <span>
#include "Allocator.hpp"
#include "stringview.h"

namespace zip {

enum class ZipError {
  Success,
  InvalidFormat,
  MissingFile,
  OutOfMemory,
  IoFailure
};

template<typename T>
using Result = std::expected<T, ZipError>;

struct ZipFileEntry {
  StringView fileName;
  uint64_t compressedSize;
  uint64_t uncompressedSize;
  uint32_t localHeaderOffset;
  uint16_t compressionMethod;
};

Result<std::span<ZipFileEntry>> parseZip(FILE* fp, mem::Allocator& allocator);
using UnpackWriteCallback = size_t (*)(const void* data, size_t size, size_t count, void* user_data); 
Result<void> unpackEntry(
  FILE* fp, const ZipFileEntry& entry,
  StringView target,
  mem::Allocator& allocator
);
Result<void> unpackFully(
  FILE* fp, std::span<ZipFileEntry> entries,
  StringView target,
  mem::Allocator& allocator
);
Result<std::span<std::byte>> loadTempEntry(
  FILE* fp, const ZipFileEntry& entry,
  mem::Allocator& allocator
);
Result<std::span<std::byte>> loadTempEntry(
  FILE* fp, std::span<ZipFileEntry> entries, StringView path,
  mem::Allocator& allocator
);
Result<ZipFileEntry> findFileEntry(std::span<ZipFileEntry> entries, StringView path);
bool fileExists(std::span<ZipFileEntry> entries, StringView path);

} // namespace zip
