#pragma once

#include <content/zip/ZipParser.hpp>
#include <mem/Allocator.hpp>
#include <Expected.hpp>

#include <stdio.h>
#include <expat.h>

namespace xml {

enum class Error {
  InvalidFormat,
  OutOfMemory,
  IoError
};

template<typename T = void>
using Result = util::Expected<T, Error>;

struct StreamContext {
  bool bail = false;
};

Result<void> parseFileStream(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  StreamContext* userData,
  XML_StartElementHandler startElement,
  XML_EndElementHandler endElement,
  XML_CharacterDataHandler charData,
  mem::Allocator& allocator);

}
