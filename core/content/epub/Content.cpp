#include "Content.hpp"

#include <content/xml/Stream.hpp>
#include <content/xml/xml_util.hpp>
#include <Log.hpp>

namespace epub::content {

namespace {

#pragma region First Pass

enum class FirstParseState : uint8_t {
  None,
  Metadata,
  Title,
  Creator,
  Language,
  Manifest,
  Spine
};

struct FirstPassParseContext : xml::StreamContext {
  mem::Allocator& allocator;
  FileResolver const& resolver;

  FirstParseState state = FirstParseState::None;
  size_t manifestEntryCount = 0;
  size_t spineEntryCount = 0;

  std::optional<StringView> title = std::nullopt;
  std::optional<StringView> author = std::nullopt;
  std::optional<StringView> language = std::nullopt;
  std::optional<StringView> cover = std::nullopt;
};

void first_startElement(void* user, const char* namePtr, const char** attsPtr) {
  auto& ctx = *(FirstPassParseContext*)user;
  auto name = StringView::fromCStr(namePtr);

  if (name.caseCmp("metadata")) {
    ctx.state = FirstParseState::Metadata;
    return;
  } else if (name.caseCmp("manifest")) {
    ctx.state = FirstParseState::Manifest;
    return;
  } else if (name.caseCmp("spine")) {
    ctx.state = FirstParseState::Spine;
    return;
  }

  switch (ctx.state) {
  case FirstParseState::Metadata:
    if (name.caseCmp("dc:title")) {
      ctx.state = FirstParseState::Title;
    } else if (name.caseCmp("dc:creator")) {
      ctx.state = FirstParseState::Creator;
    } else if (name.caseCmp("dc:language")) {
      ctx.state = FirstParseState::Language;
    } else if (name.caseCmp("meta")) {
      auto nameAttr = xml::getAttribute(attsPtr, "name");
      if (nameAttr.has_value() && nameAttr->caseCmp("cover")) {
        auto contentAttr = xml::getAttribute(attsPtr, "content");
        if (contentAttr.has_value() && ctx.cover == std::nullopt) {
          ctx.cover = ctx.allocator.retain(*contentAttr);
        }
      }
    }
    break;
  case FirstParseState::Manifest:
    if (name.caseCmp("item")) {
      auto href = xml::getAttribute(attsPtr, "href");
      if (!href.has_value())
        break;

      auto fileEntryIndex = ctx.resolver.resolve(*href);
      if (!fileEntryIndex.has_value()) {
        println("Manifest entry refers to missing file: ", *href);
        break;
      }

      ctx.manifestEntryCount++;
    }
    break;
  case FirstParseState::Spine:
    if (name.caseCmp("itemref")) {
      auto idref = xml::getAttribute(attsPtr, "idref");
      if (!idref.has_value())
        break;
      ctx.spineEntryCount++;
    }
    break;
  default:
    break;
  }
}

void first_text(void* user, const char* dataPtr, int len) {
  auto& ctx = *(FirstPassParseContext*)user;
  StringView data = StringView { dataPtr, static_cast<size_t>(len) };

  switch (ctx.state) {
  case FirstParseState::Title:
    if (!ctx.title.has_value()) ctx.title = ctx.allocator.retain(data);
    ctx.state = FirstParseState::Metadata;
    break;
  case FirstParseState::Creator:
    if (!ctx.author.has_value()) ctx.author = ctx.allocator.retain(data);
    ctx.state = FirstParseState::Metadata;
    break;
  case FirstParseState::Language:
    if (!ctx.language.has_value()) ctx.language = ctx.allocator.retain(data);
    ctx.state = FirstParseState::Metadata;
    break;
  default:
    break;
  }
}

void first_endElement(void* user, const char* namePtr) {
  auto& ctx = *(FirstPassParseContext*)user;
  auto name = StringView::fromCStr(namePtr);

  if (name.caseCmp("metadata") || name.caseCmp("manifest") || name.caseCmp("spine")) {
    ctx.state = FirstParseState::None;
  }

  switch (ctx.state) {
  case FirstParseState::Title:
  case FirstParseState::Creator:
  case FirstParseState::Language:
    ctx.state = FirstParseState::Metadata;
    break;
  default:
    break;
  }
}

#pragma endregion First Pass

#pragma region Second Pass

struct ManifestEntry {
  StringView id;
  uint16_t zipEntryIndex;
};

enum class SecondParseState : uint8_t {
  None,
  Manifest,
  Spine
};

struct SecondPassParseContext : xml::StreamContext {
  mem::Allocator& allocator;
  FileResolver const& resolver;

  SecondParseState state = SecondParseState::None;
  ManifestEntry* manifestEntries;
  size_t manifestIdx = 0;
  uint16_t* spineZipIndices;
  size_t spineIdx = 0;
  std::optional<uint16_t> tocZipIndex = std::nullopt;
  std::optional<StringView> cover;
  std::optional<uint16_t> coverZipIndex = std::nullopt;

  std::optional<uint16_t> resolveManifestEntry(StringView id) {
    for (size_t j = 0; j < manifestIdx; j++) {
      if (manifestEntries[j].id == id) {
        return manifestEntries[j].zipEntryIndex;
      }
    }
    return std::nullopt;
  }
};

void second_startElement(void* user, const char* namePtr, const char** attsPtr) {
  auto& ctx = *(SecondPassParseContext*)user;
  auto name = StringView::fromCStr(namePtr);

  if (name.caseCmp("manifest")) {
    ctx.state = SecondParseState::Manifest;
    return;
  } else if (name.caseCmp("spine")) {
    ctx.state = SecondParseState::Spine;
    // resolve optional TOC attribute
    auto tocIdent = xml::getAttribute(attsPtr, "toc");
    if (tocIdent.has_value()) {
      ctx.tocZipIndex = ctx.resolveManifestEntry(*tocIdent);
    }
    return;
  }

  switch (ctx.state) {
  case SecondParseState::Manifest:
    if (name.caseCmp("item")) {
      auto href = xml::getAttribute(attsPtr, "href");
      if (!href.has_value())
        break;
      
      auto zipFileIndex = ctx.resolver.resolve(*href);
      if (!zipFileIndex.has_value())
        break;

      auto& entry = ctx.manifestEntries[ctx.manifestIdx++];
      auto entryId = xml::getAttribute(attsPtr, "id");
      if (entryId.has_value()) {
        entry.id = ctx.allocator.retainTemp(*entryId).value_or("");    
      }
      entry.zipEntryIndex = *zipFileIndex;
    }
    break;
  case SecondParseState::Spine:
    if (name.caseCmp("itemref")) {
      auto idref = xml::getAttribute(attsPtr, "idref");
      if (!idref.has_value())
        break;
      auto zipIndex = ctx.resolveManifestEntry(*idref);
      if (!zipIndex.has_value()) {
        println("Spine entry refers to missing manifest id: ", *idref);
        break;
      }
      ctx.spineZipIndices[ctx.spineIdx++] = *zipIndex;
    }
    break;
  default:
    break;
  }
}

void second_endElement(void* user, const char* namePtr) {
  auto& ctx = *(SecondPassParseContext*)user;
  auto name = StringView::fromCStr(namePtr);

  if (name.caseCmp("manifest") || name.caseCmp("spine")) {
    ctx.state = SecondParseState::None;
  }
}

#pragma endregion Second Pass

}

std::optional<Data> parse(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  const FileResolver& resolver,
  mem::Allocator& allocator) {
  FirstPassParseContext firstParseCtx {
    .allocator = allocator,
    .resolver = resolver,
  };

  auto result = xml::parseFileStream(
    fp, fileEntry,
    &firstParseCtx, first_startElement, first_endElement, first_text,
    allocator);
  if (!result.has_value()) {
    return std::nullopt;
  }
  allocator.subCanary("____Metadata____");

  if (firstParseCtx.manifestEntryCount == 0) {
    PrintF("No manifest entries found in content file\n");
    return std::nullopt;
  }

  if (firstParseCtx.spineEntryCount == 0) {
    PrintF("No spine entries found in content file\n");
    return std::nullopt;
  }

  PrintF("Expecting %zu manifest entries\n", firstParseCtx.manifestEntryCount);
  PrintF("Expecting %zu spine entries\n", firstParseCtx.spineEntryCount);

  auto frontScope = allocator.beginFrontScope();
  ManifestEntry* manifestEntries = allocator.bumpAlloc<ManifestEntry>(firstParseCtx.manifestEntryCount);
  if (!manifestEntries) {
    PrintF("Out of memory allocating manifest entries\n");
    return std::nullopt;
  }

  uint16_t* spineZipIndices = allocator.subAlloc<uint16_t>(firstParseCtx.spineEntryCount);
  if (!spineZipIndices) {
    PrintF("Out of memory allocating spine entries\n");
    return std::nullopt;
  }
  allocator.subCanary("__SpineZipIdx___");

  SecondPassParseContext secondParseCtx {
    .allocator = allocator,
    .resolver = resolver,
    .manifestEntries = manifestEntries,
    .spineZipIndices = spineZipIndices,
    .cover = firstParseCtx.cover
  };
  result = xml::parseFileStream(
    fp, fileEntry,
    &secondParseCtx, second_startElement, second_endElement, nullptr,
    allocator);
  if (!result.has_value()) {
    return std::nullopt;
  }

  return Data {
    .title = firstParseCtx.title,
    .author = firstParseCtx.author,
    .language = firstParseCtx.language,
    .coverZipIndex = secondParseCtx.coverZipIndex,
    .tocZipIndex = secondParseCtx.tocZipIndex,
    .spineZipIndices = std::span<uint16_t>(spineZipIndices, firstParseCtx.spineEntryCount)
  };
}

}
