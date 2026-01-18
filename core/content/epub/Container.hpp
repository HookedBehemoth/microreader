#pragma once

#include <content/zip/ZipParser.hpp>
#include <mem/Allocator.hpp>
#include <StringView.hpp>

#include <cstdio>
#include <optional>

namespace epub::container {

std::optional<StringView> parse(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  mem::Allocator& allocator);

}