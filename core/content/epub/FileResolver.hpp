#pragma once

#include <StringView.hpp>
#include <content/zip/ZipParser.hpp>

#include <cstdint>
#include <span>

namespace epub {

struct FileResolver {
  std::span<zip::ZipFileEntry> entries;
  StringView basePath = {};

  zip::Result<uint16_t> resolve(StringView href) const;
};

}
