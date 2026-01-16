#include "TocNcx.hpp"

#include "../stringview.h"

#include <csetjmp>
#include <expat.h>

extern mem::Allocator g_allocator;

namespace xml::toc::ncx {

namespace {

static_assert(sizeof(char) == sizeof(XML_Char), "XML_Char is not char");
std::optional<StringView> getAttribute(const char** attrs, StringView name) {
  for (size_t i = 0; attrs[i]; i += 2) {
    StringView attrName = StringView::fromCStr(attrs[i]);
    if (attrName == name) {
      return StringView::fromCStr(attrs[i + 1]);
    }
  }
  return std::nullopt;
}

enum class NavDepth : uint8_t {
  Root, Ncx,
  NavMap, NavPoint, NavLabel, Content, NavText
};

constexpr size_t MaxLabelLength = 255;
struct TocNcxContext {
  mem::Allocator* const allocator = nullptr;
  Contents* const contents = nullptr;
  const epub::ContentResolver& resolver;

  // current parsing state
  NavDepth navDepth = NavDepth::Root;
  size_t entryCount = 0;
  std::optional<std::array<char, MaxLabelLength>> tocLabel = std::nullopt;
  std::optional<uint16_t> tocZipIndex = std::nullopt;

  void tryCommitNavPoint();
};

void TocNcxContext::tryCommitNavPoint() {
  if (!tocLabel.has_value() || !tocZipIndex.has_value()) {
    tocLabel.reset();
    tocZipIndex.reset();
    return;
  }

  if (contents) {
    auto label = StringView::fromCStr(tocLabel->data());
    contents->labels[entryCount] = *allocator->retain(label);
    contents->zipIndices[entryCount] = *tocZipIndex;
  }
  entryCount++;

  // ensure we don't reuse old data
  tocLabel.reset();
  tocZipIndex.reset();
};

void TocNcx_startElement(void* user, const char* namePtr, const char** attsPtr) {
  auto& ctx = *(TocNcxContext*)user;
  auto name = StringView::fromCStr(namePtr);
  if (ctx.navDepth == NavDepth::Root && name.caseCmp("ncx")) {
    ctx.navDepth = NavDepth::Ncx;
  } else if (ctx.navDepth == NavDepth::Ncx && name.caseCmp("navMap")) {
    ctx.navDepth = NavDepth::NavMap;
  } else if (ctx.navDepth == NavDepth::NavMap && name.caseCmp("navPoint")) {
    ctx.navDepth = NavDepth::NavPoint;
    ctx.tocLabel = std::nullopt;
    ctx.tocZipIndex = std::nullopt;
  } else if (ctx.navDepth == NavDepth::NavPoint && name.caseCmp("content")) {
    auto src = getAttribute(attsPtr, "src");
    auto zipIndex = ctx.resolver.resolve(*src);
    if (src && zipIndex) ctx.tocZipIndex = *zipIndex;
    ctx.navDepth = NavDepth::Content;
  } else if (ctx.navDepth == NavDepth::NavPoint && name.caseCmp("navPoint")) {
    ctx.navDepth = NavDepth::NavPoint;
    // flush when encountering a nested navPoint
    ctx.tryCommitNavPoint();
  } else if (ctx.navDepth == NavDepth::NavPoint && name.caseCmp("navLabel")) {
    ctx.navDepth = NavDepth::NavLabel;
  } else if (ctx.navDepth == NavDepth::NavLabel && name.caseCmp("text")) {
    ctx.navDepth = NavDepth::NavText;
  }
}

void TocNcx_text(void* user, const char* text, int len) {
  auto& ctx = *(TocNcxContext*)user;
  if (ctx.navDepth != NavDepth::NavText) {
    return;
  }

  ctx.tocLabel = std::array<char, MaxLabelLength> {};
  size_t copyLen = std::min(static_cast<size_t>(len), MaxLabelLength - 1);
  std::memcpy(ctx.tocLabel->data(), text, copyLen);
}

void TocNcx_endElement(void* user, const char* namePtr) {
  auto& ctx = *(TocNcxContext*)user;
  auto name = StringView::fromCStr(namePtr);
  if (ctx.navDepth == NavDepth::NavText && name.caseCmp("text")) {
    ctx.navDepth = NavDepth::NavLabel;
  } else if (ctx.navDepth == NavDepth::NavLabel && name.caseCmp("navLabel")) {
    ctx.navDepth = NavDepth::NavPoint;
  } else if (ctx.navDepth == NavDepth::Content && name.caseCmp("content")) {
    ctx.navDepth = NavDepth::NavPoint;
  } else if (ctx.navDepth == NavDepth::NavPoint && name.caseCmp("navPoint")) {
    ctx.navDepth = NavDepth::NavMap;
    ctx.tryCommitNavPoint();
  } else if (ctx.navDepth == NavDepth::NavMap && name.caseCmp("navMap")) {
    ctx.navDepth = NavDepth::Ncx;
  } else if (ctx.navDepth == NavDepth::Ncx && name.caseCmp("ncx")) {
    ctx.navDepth = NavDepth::Root;
  }
}

size_t allocated = 0;
void* bumpMalloc(size_t size) {
  allocated += size;
  void* ptr = g_allocator.bumpAlloc<std::byte>(size);
  printf("bumpMalloc called for size %zu -> %p\n", size, ptr);
  return ptr;
}
void* bumpRealloc(void* ptr, size_t newSize) {
  (void)ptr;
  (void)newSize;
  printf("bumpRealloc called!\n");
  std::abort();
}
void bumpFree(void* ptr) {
  printf("bumpFree called for %p\n", ptr);
  (void)ptr;
  // no-op, memory will be freed when bump scope ends
}

constexpr XML_Memory_Handling_Suite bumpMemorySuite = {
  bumpMalloc,
  bumpRealloc,
  bumpFree
};

Error parseToc(
  FILE* fp,
  TocNcxContext& ctx,
  const zip::ZipFileEntry& fileEntry)
{
  auto frontScope = g_allocator.beginFrontScope();

  auto parser = XML_ParserCreate_MM(nullptr, &bumpMemorySuite, nullptr);
  XML_SetUserData(parser, &ctx);
  XML_SetElementHandler(parser, TocNcx_startElement, TocNcx_endElement);
  XML_SetCharacterDataHandler(parser, TocNcx_text);

  struct StreamTocUserData {
    XML_Parser parser;
    size_t position;
    size_t totalSize;
    std::jmp_buf jumpBuf;
  } streamTocUser;
  streamTocUser.parser = parser;
  streamTocUser.position = 0;
  streamTocUser.totalSize = fileEntry.uncompressedSize;
  if (setjmp(streamTocUser.jumpBuf)) {
    // XML_ParserFree(parser); // no-op
    printf("Error parsing TOC NCX: %s at line %lu\n",
      XML_ErrorString(XML_GetErrorCode(parser)),
      XML_GetCurrentLineNumber(parser));
    return Error::InvalidFormat; // TODO
  }

  auto streamXml = [](const void* data, size_t size, size_t count, void* user_data) -> size_t {
    auto user = (StreamTocUserData*)user_data;
    size_t toRead = size * count;
    user->position += toRead;
    bool isFinal = user->position == user->totalSize;
    auto parseResult = XML_Parse(user->parser, (const char*)data, static_cast<int>(toRead), isFinal);
    if (parseResult == XML_STATUS_ERROR) {
      std::longjmp(user->jumpBuf, 1);
    }
    return toRead;
  };

  auto result = zip::streamFileEntry(
    fp, fileEntry,
    streamXml, &streamTocUser,
    g_allocator);
  // XML_ParserFree(parser); // no-op

  if (!result.has_value()) {
    return Error::InvalidFormat;
  }
  return {};
}

}

Result<Contents> parse(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  mem::Allocator& allocator,
  const epub::ContentResolver& resolver) {
  allocated = 0;
  // determine how much space we need to reserve
  TocNcxContext szCtx { .resolver = resolver };
  parseToc(fp, szCtx, fileEntry);

  printf("Expecting %zu TOC entries (used %zu bytes)\n", szCtx.entryCount, allocated);

  // Sanity check: ensure entry count fits in uint16_t for index storage
  if (szCtx.entryCount > UINT16_MAX) {
    printf("TOC entry count exceeds uint16_t limit\n");
    return std::unexpected(Error::InvalidFormat);
  }

  StringView* tocLabels = allocator.subAlloc<StringView>(szCtx.entryCount);
  allocator.subCanary("___TocLabels____");
  uint16_t* tocZipIndices = allocator.subAlloc<uint16_t>(szCtx.entryCount);
  allocator.subCanary("___TocIndices___");

  Contents contents {
    .labels = std::span(tocLabels, szCtx.entryCount),
    .zipIndices = std::span(tocZipIndices, szCtx.entryCount),
  };
  TocNcxContext parseCtx {
    .allocator = &allocator,
    .contents = &contents,
    .resolver = resolver
  };

  allocated = 0;
  parseToc(fp, parseCtx, fileEntry);
  printf("TOC parsing allocated %zu bytes\n", allocated);
  allocator.subCanary("____TocNames____");

  return Contents {
    .labels = std::span(tocLabels, szCtx.entryCount),
    .zipIndices = std::span(tocZipIndices, szCtx.entryCount),
  };
}

}