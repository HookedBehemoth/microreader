#pragma once

#include "ZipParser.hpp"

namespace epub {

struct ContentResolver {
  std::span<zip::ZipFileEntry> entries;
  StringView basePath = {};

  zip::Result<uint16_t> resolve(StringView href) const;
};

}
