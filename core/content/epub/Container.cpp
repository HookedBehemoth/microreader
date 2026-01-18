#include "Container.hpp"

#include <content/xml/Stream.hpp>
#include <content/xml/xml_util.hpp>

namespace epub::container {

namespace {

struct ParseContext : xml::StreamContext {
  mem::Allocator& allocator;
  std::optional<StringView> outContentPath = std::nullopt;
};

void startElement(void* user, const char* namePtr, const char** attsPtr) {
  auto& ctx = *(ParseContext*)user;

  // avoid double parse
  if (ctx.outContentPath.has_value()) {
    return;
  }

  auto name = StringView::fromCStr(namePtr);
  if (!name.caseCmp("rootfile")) {
    return;
  }
  auto contentPath = xml::getAttribute(attsPtr, "full-path");
  if (!contentPath.has_value()) {
    return;
  }

  ctx.outContentPath = ctx.allocator.retain(*contentPath);
  ctx.bail = true;
}

}

std::optional<StringView> parse(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  mem::Allocator& allocator)
{
  ParseContext parseCtx {
    .allocator = allocator,
  };
  xml::parseFileStream(
    fp, fileEntry, &parseCtx, startElement, nullptr, nullptr, allocator);
  return parseCtx.outContentPath;
}

}
