#pragma once

#include "../ZipParser.hpp"
#include "../EpubContentResolver.hpp"
#include "../Allocator.hpp"

namespace xml::toc::ncx {

enum class Error {
  InvalidFormat,
  OutOfMemory,
  IoError
};

template<typename T = void>
using Result = std::expected<T, Error>;

struct Contents {
  std::span<StringView> labels;
  std::span<uint16_t> zipIndices;
};

Result<Contents> parse(
  FILE* zipFile, const zip::ZipFileEntry& fileEntry,
  mem::Allocator& allocator,
  const epub::ContentResolver& resolver);

}
