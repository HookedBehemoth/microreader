#include "TocNcx.hpp"

#include <StringView.hpp>
#include <content/xml/xml_util.hpp>
#include <content/xml/Stream.hpp>

#include <expat.h>

namespace epub::toc::ncx {

namespace {

enum class NavDepth : uint8_t {
  Root, Ncx,
  NavMap, NavPoint, NavLabel, Content, NavText
};

constexpr size_t MaxLabelLength = 255;
struct TocNcxContext : xml::StreamContext {
  mem::Allocator* const allocator = nullptr;
  Data* const contents = nullptr;
  const epub::FileResolver& resolver;

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
    auto src = xml::getAttribute(attsPtr, "src");
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

}

Result<Data> parse(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  mem::Allocator& allocator,
  const epub::FileResolver& resolver) {
  // xml::g_allocated = 0;
  // determine how much space we need to reserve
  TocNcxContext szCtx { .resolver = resolver };
  xml::parseFileStream(
    fp, fileEntry,
    &szCtx,
    TocNcx_startElement,
    TocNcx_endElement,
    TocNcx_text,
    allocator);

  PrintF("Expecting %zu TOC entries\n", szCtx.entryCount);

  // Sanity check: ensure entry count fits in uint16_t for index storage
  if (szCtx.entryCount > UINT16_MAX) {
    PrintF("TOC entry count exceeds uint16_t limit\n");
    return xml::Error::InvalidFormat;
  }

  StringView* tocLabels = allocator.subAlloc<StringView>(szCtx.entryCount);
  allocator.subCanary("___TocLabels____");
  uint16_t* tocZipIndices = allocator.subAlloc<uint16_t>(szCtx.entryCount);
  allocator.subCanary("___TocIndices___");

  Data contents {
    .labels = std::span(tocLabels, szCtx.entryCount),
    .zipIndices = std::span(tocZipIndices, szCtx.entryCount),
  };
  TocNcxContext parseCtx {
    .allocator = &allocator,
    .contents = &contents,
    .resolver = resolver
  };

  // xml::g_allocated = 0;
  xml::parseFileStream(
    fp, fileEntry,
    &parseCtx,
    TocNcx_startElement,
    TocNcx_endElement,
    TocNcx_text,
    allocator);
  // PrintF("TOC parsing allocated %zu bytes\n", xml::g_allocated);
  allocator.subCanary("____TocNames____");

  return Data {
    .labels = std::span(tocLabels, szCtx.entryCount),
    .zipIndices = std::span(tocZipIndices, szCtx.entryCount),
  };
}

}