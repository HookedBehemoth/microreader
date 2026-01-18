#pragma once

#include <content/zip/ZipParser.hpp>
#include <content/epub/FileResolver.hpp>
#include <mem/Allocator.hpp>
#include <content/xml/Stream.hpp>
#include <StringView.hpp>
#include <Expected.hpp>

namespace epub::toc::ncx {

template<typename T = void>
using Result = util::Expected<T, xml::Error>;

struct Data {
  std::span<StringView> labels;
  std::span<uint16_t> zipIndices;
};

Result<Data> parse(
  FILE* zipFile, const zip::ZipFileEntry& fileEntry,
  mem::Allocator& allocator,
  const epub::FileResolver& resolver);

}
