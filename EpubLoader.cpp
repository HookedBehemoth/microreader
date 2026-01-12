#include "EpubLoader.hpp"

#include "XmlParser.h"

#include <cstddef>
#include <cstdio>
#include <expected>
#include <optional>
#include "stringview.h"
#include "log.h"
#include "Allocator.hpp"
#include "Fs.hpp"
#include "ZipParser.hpp"

namespace {
  mem::Allocator g_allocator;

  std::optional<FILE*> g_epubFile;
  std::optional<std::span<zip::ZipFileEntry>> g_zipEntries;
  std::optional<StringView> g_title;
  std::optional<StringView> g_author;
  std::optional<StringView> g_language;
  std::optional<StringView> g_coverId;
  std::optional<StringView> g_contentPath;
  std::optional<StringView> g_contentBasePath;
  std::optional<uint16_t> g_tocZipIndex;
  std::optional<std::span<Book::SpineEntry>> g_spine;
  std::optional<std::span<Book::TocEntry>> g_toc;
  // Separate array for spine->toc mapping to avoid std::optional bloat in SpineEntry
  constexpr uint16_t NO_TOC_ENTRY = UINT16_MAX;
  std::optional<std::span<uint16_t>> g_spineTocIndices;

  constexpr StringView ExtractedBase = "microreader";
  std::optional<StringView> g_cacheDirectory;

  template<typename T>
  using Result = std::expected<T, Book::EpubLoadResult>;

  std::optional<StringView> loadTempFile(StringView filePath);
  Book::EpubLoadResult ensureCacheDirectory(StringView path);
  Book::EpubLoadResult parseTableOfContents(StringView toc);
}

namespace Book {

namespace {
  EpubLoadResult fromZipError(zip::ZipError err) {
    switch (err) {
      case zip::ZipError::Success:
        return EpubLoadResult::Success;
      case zip::ZipError::InvalidFormat:
        return EpubLoadResult::InvalidFormat;
      case zip::ZipError::OutOfMemory:
        return EpubLoadResult::OutOfMemory;
      case zip::ZipError::IoFailure:
        return EpubLoadResult::IoFailure;
      case zip::ZipError::MissingFile:
        return EpubLoadResult::MissingFile;
      default:
        return EpubLoadResult::InvalidState;
    }
  }
}

EpubLoadResult loadEpub(StringView filePath) {
  unload();

  EpubLoadResult result = EpubLoadResult::Success;

  FILE* fp = std::fopen(filePath.data(), "rb");
  if (!fp) {
    return EpubLoadResult::MissingFile;
  }
  g_epubFile = fp;

  result = ensureCacheDirectory(filePath);
  if (result != EpubLoadResult::Success) {
    return result;
  }

  auto zipEntries = zip::parseZip(fp, g_allocator);
  if (!zipEntries.has_value()) {
    return fromZipError(zipEntries.error());
  }
  g_zipEntries = *zipEntries;

  // auto unzipResult = zip::unpackFully(fp, *g_zipEntries, *g_cacheDirectory, g_allocator);
  // if (!unzipResult.has_value()) {
  //   return fromZipError(unzipResult.error());
  // }
  {
    auto containerScope = g_allocator.beginFrontScope();

    std::optional<StringView> contentPath;

    StringView containerFile = "META-INF/container.xml";
    auto containerData = loadTempEntry(*g_epubFile, *g_zipEntries, containerFile, g_allocator);
    if (!containerData.has_value()) {
      printf("Failed to load container.xml\n");
      return EpubLoadResult::MissingFile;
    }

    StringView containerView = StringView { (char*)containerData->data(), containerData->size() };
    xml::XmlParser parser { containerView };
    while (true) {
      auto ty = parser.next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;
      
      if (ty != xml::XmlParser::NodeType::Element)
        continue;

      if (!parser.name().caseCmp("rootfile"))
        continue;

      contentPath = parser.getAttribute("full-path");
      break;
    }
    
    if (!contentPath.has_value()) {
      printf("Failed to find content path in container.xml\n");
      return EpubLoadResult::InvalidFormat;
    }
    println("Content path: ", *contentPath);

    g_contentPath = g_allocator.retain(*contentPath);
    
    size_t lastSlashIndex = g_contentPath->findLast('/');
    printf("Last slash index: %zu (vs %zu)\n", lastSlashIndex, g_contentPath->size());
    if (lastSlashIndex != g_contentPath->size()) {
      g_contentBasePath = g_contentPath->subString(0, lastSlashIndex);
    }
  }

  // parse content obf file
  {
    auto contentScope = g_allocator.beginFrontScope();

    auto contentFile = zip::loadTempEntry(*g_epubFile, *g_zipEntries, *g_contentPath, g_allocator);
    if (!contentFile.has_value()) {
      printf("Failed to load content file\n");
      return fromZipError(contentFile.error());
    }

    StringView contentView = StringView { (char*)contentFile->data(), contentFile->size() };
    xml::XmlParser parser { contentView };

    // First pass, quantify manifest and spine entries, save references
    std::optional<xml::XmlParser> manifest;
    bool inManifest = false;
    size_t manifestEntryCount = 0;
    std::optional<xml::XmlParser> spine;
    std::optional<StringView> tocPath;
    bool inSpine = false;
    size_t spineEntryCount = 0;
    bool inMetadata = false;
    std::optional<StringView> title;
    std::optional<StringView> author;
    std::optional<StringView> language;
    std::optional<StringView> coverId;

    while (true) {
      auto ty = parser.next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;

      if (ty == xml::XmlParser::NodeType::Element) {
        if (parser.name().caseCmp("metadata")) {
          inMetadata = true;
          continue;
        }
        if (parser.name().caseCmp("manifest")) {
          manifest = parser;
          inManifest = true;
          continue;
        }
        if (parser.name().caseCmp("spine")) {
          spine = parser;
          inSpine = true;
          tocPath = parser.getAttribute("toc");
          continue;
        }

        if (inMetadata) {
          if (parser.name().caseCmp("dc:title")) {
            parser.next();
            title = parser.text();
          } else if (parser.name().caseCmp("dc:creator")) {
            parser.next();
            author = parser.text();
          } else if (parser.name().caseCmp("dc:language")) {
            parser.next();
            language = parser.text();
          } else if (parser.name().caseCmp("meta")) {
            auto nameAttr = parser.getAttribute("name");
            if (nameAttr.caseCmp("cover")) {
              coverId = parser.getAttribute("content");
            }
          }
        }

        if (parser.name().caseCmp("item") && inManifest) {
          auto path = parser.getAttribute("href");
          char pathBuffer[512];
          if (g_contentBasePath.has_value()) {
            path = join(pathBuffer, sizeof(pathBuffer),
              *g_contentBasePath, StringView("/"), path);
          }

          if (!zip::fileExists(*g_zipEntries, path)) {
            println("Manifest entry refers to missing file: ", path);
            return EpubLoadResult::MissingFile;
          }
          manifestEntryCount++;
        }

        if (parser.name().caseCmp("itemref") && inSpine) {
          spineEntryCount++;
        }
      } else if (ty == xml::XmlParser::NodeType::EndElement) {
        if (parser.name().caseCmp("spine")) {
          inSpine = false;
        }
        if (parser.name().caseCmp("manifest")) {
          inManifest = false;
        }
        if (parser.name().caseCmp("metadata")) {
          inMetadata = false;
        }
      }
    }

    if (title.has_value()) g_title = g_allocator.retain(*title);
    if (author.has_value()) g_author = g_allocator.retain(*author);
    if (language.has_value()) g_language = g_allocator.retain(*language);
    if (coverId.has_value()) g_coverId = g_allocator.retain(*coverId);

    if (manifestEntryCount == 0) {
      printf("No manifest entries found in content file\n");
      return EpubLoadResult::InvalidFormat;
    }

    if (spineEntryCount == 0) {
      printf("No spine entries found in content file\n");
      return EpubLoadResult::InvalidFormat;
    }

    printf("Expecting %zu manifest entries\n", manifestEntryCount);
    printf("Expecting %zu spine entries\n", spineEntryCount);

    // temporary allocation
    struct ManifestEntry {
      StringView id;
      uint16_t zipEntryIndex;
    };
    ManifestEntry* manifestEntries = g_allocator.bumpAlloc<ManifestEntry>(manifestEntryCount);
  
    ManifestEntry* manifestIt = manifestEntries;
    while (true) {
      auto ty = manifest->next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;
      if (ty == xml::XmlParser::NodeType::Element) {
        if (!manifest->name().caseCmp("item"))
          continue;
        
        auto &entry = *manifestIt++;
        
        entry.id = manifest->getAttribute("id");

        // resolve and manifest entry href to file entry index
        auto href = manifest->getAttribute("href");

        char pathBuffer[512];
        if (g_contentBasePath.has_value()) {
          href = join(pathBuffer, sizeof(pathBuffer),
            *g_contentBasePath, StringView("/"), href);
        }

        auto fileEntryIndex = zip::findFileEntryIndex(*g_zipEntries, href);
        if (!fileEntryIndex.has_value()) {
          println("Manifest entry refers to missing file: ", href);
          return fromZipError(fileEntryIndex.error());
        }

        entry.zipEntryIndex = *fileEntryIndex;
      } else if (ty == xml::XmlParser::NodeType::EndElement) {
        if (manifest->name().caseCmp("manifest")) {
          break;
        }
      }
    }

    auto resolveManifestEntry = [&](StringView id) -> std::optional<uint16_t> {
      for (size_t j = 0; j < manifestEntryCount; j++) {
        if (manifestEntries[j].id == id) {
          return manifestEntries[j].zipEntryIndex;
        }
      }
      return std::nullopt;
    };

    // resolve ncx entry to path
    if (tocPath.has_value()) {
      auto tocIndex = resolveManifestEntry(*tocPath);
      if (!tocIndex.has_value()) {
        printf("TOC path in spine does not exist in manifest: ");
        fwrite(tocPath->data(), 1, tocPath->size(), stdout);
        printf("\n");
        return EpubLoadResult::InvalidFormat;
      }
      g_tocZipIndex = *tocIndex;
      printf("Found TOC path in content file: ");
      fwrite(tocPath->data(), 1, tocPath->size(), stdout);
      printf("\n");
    }

    // permanent spine allocation
    SpineEntry* spineEntries = g_allocator.subAlloc<SpineEntry>(spineEntryCount);
    if (!spineEntries) {
      printf("Out of memory allocating spine entries\n");
      return EpubLoadResult::OutOfMemory;
    }
    
    // Separate array for spine->toc mapping (UINT16_MAX = no toc entry)
    uint16_t* spineTocIndices = g_allocator.subAlloc<uint16_t>(spineEntryCount);
    if (!spineTocIndices) {
      printf("Out of memory allocating spine toc indices\n");
      return EpubLoadResult::OutOfMemory;
    }
    // Initialize all to NO_TOC_ENTRY
    for (size_t i = 0; i < spineEntryCount; i++) {
      spineTocIndices[i] = NO_TOC_ENTRY;
    }

    g_spine = std::span<SpineEntry>(spineEntries, spineEntryCount);
    g_spineTocIndices = std::span<uint16_t>(spineTocIndices, spineEntryCount);

    SpineEntry* spineIt = spineEntries;
    while (true) {
      auto ty = spine->next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;
      if (ty == xml::XmlParser::NodeType::Element) {
        if (!spine->name().caseCmp("itemref"))
          continue;
        
        auto &entry = *spineIt++;
        
        auto idref = spine->getAttribute("idref");
        entry.idref = idref;

        auto indexOpt = resolveManifestEntry(idref);
        if (!indexOpt.has_value()) {
          printf("Spine entry refers to missing manifest id: ");
          fwrite(idref.data(), 1, idref.size(), stdout);
          printf("\n");
          return EpubLoadResult::InvalidFormat;
        }
        entry.zipEntryIndex = *indexOpt;
        printf("Spine entry: ");
        fwrite(entry.idref.data(), 1, entry.idref.size(), stdout);
        printf(" -> %u\n", entry.zipEntryIndex);
      } else if (ty == xml::XmlParser::NodeType::EndElement) {
        if (spine->name().caseCmp("spine")) {
          break;
        }
      }
    }
  }

  if (g_tocZipIndex.has_value()) {
    auto tocScope = g_allocator.beginFrontScope();
    const auto& tocEntry = (*g_zipEntries)[*g_tocZipIndex];
    auto tocFile = zip::loadTempEntry(*g_epubFile, tocEntry, g_allocator);
    if (!tocFile.has_value()) {
      printf("Failed to load TOC file at index: %u\n", *g_tocZipIndex);
      return fromZipError(tocFile.error());
    }
    StringView tocView = StringView { (char*)tocFile->data(), tocFile->size() };

    result = parseTableOfContents(tocView);
    if (result != EpubLoadResult::Success) {
      return result;
    }

    printf("Table of Contents loaded, %zu entries\n", g_toc->size());
    for (const auto &entry : *g_toc) {
      printf("TOC Entry: ");
      fwrite(entry.label.data(), 1, entry.label.size(), stdout);
      printf(" -> %u\n", entry.zipEntryIndex);
    }

    // Assign ToC entries to Spine entries using separate index array
    // Each spine entry gets the most recent matching TOC entry (for chapter grouping)
    uint16_t currentTocIndex = NO_TOC_ENTRY;
    size_t searchStart = 0;
    for (size_t spineIdx = 0; spineIdx < g_spine->size(); spineIdx++) {
      const auto& spineEntry = (*g_spine)[spineIdx];
      for (size_t i = searchStart; i < g_toc->size(); i++) {
        auto& tocEntry = (*g_toc)[i];
        if (tocEntry.zipEntryIndex == spineEntry.zipEntryIndex) {
          currentTocIndex = static_cast<uint16_t>(i);
          searchStart = i + 1;
          break;
        }
      }
      (*g_spineTocIndices)[spineIdx] = currentTocIndex;
    }

    for (size_t i = 0; i < g_spine->size(); i++) {
      const auto& spineEntry = (*g_spine)[i];
      printf("Spine Entry: %u", spineEntry.zipEntryIndex);
      uint16_t tocIdx = (*g_spineTocIndices)[i];
      if (tocIdx != NO_TOC_ENTRY) {
        const auto& tocEntry = (*g_toc)[tocIdx];
        printf(" -> TOC: ");
        fwrite(tocEntry.label.data(), 1, tocEntry.label.size(), stdout);
      } else {
        printf(" -> TOC: (none)");
      }
      printf("\n");
    }
  }

  println("Finished parsing EPUB");

  return EpubLoadResult::Success;
}

void unload() {
  printf("Unloading EPUB\n");
  g_allocator.dumpState();
  g_allocator.reset();
  g_cacheDirectory.reset();
  g_spineTocIndices.reset();
  g_spine.reset();
  g_toc.reset();
  g_tocZipIndex.reset();
  g_contentBasePath.reset();
  g_contentPath.reset();
  g_coverId.reset();
  g_language.reset();
  g_author.reset();
  g_title.reset();
  g_zipEntries.reset();
  if (g_epubFile.has_value()) {
    fclose(*g_epubFile);
    g_epubFile.reset();
  }
}

std::optional<StringView> getTitle() {
  return g_title;
}

std::optional<StringView> getAuthor() {
  return g_author;
}

std::optional<StringView> getLanguage() {
  return g_language;
}

void deleteCache() {
}

void deleteCacheForFile(StringView filePath) {
  (void)filePath;
}

} // namespace Book

namespace {
  Book::EpubLoadResult ensureCacheDirectory(StringView path) {
    auto backBumpScope = g_allocator.beginBackScope();
    auto cacheDirectory = g_allocator.join(
      ExtractedBase, "/epub_", path, "/"
    );
    if (!cacheDirectory.has_value()) {
      return Book::EpubLoadResult::OutOfMemory;
    }

    g_cacheDirectory = cacheDirectory;
    bool ok = fs::ensurePath(*g_cacheDirectory);
    if (!ok) {
      backBumpScope.reset();
      g_cacheDirectory.reset();
      return Book::EpubLoadResult::IoFailure;
    }
    backBumpScope.disable();
    return Book::EpubLoadResult::Success;
  }

  // Load a temporary file into a bump-allocated buffer
  // NOTE: ensure you store the current bump pointer before calling this,
  //       so you can roll back when you are done with the file.
  std::optional<StringView> loadTempFile(StringView filePath) {
    std::FILE* f = std::fopen(filePath.data(), "rb");
    if (!f) {
      return std::nullopt;
    }
    fseeko(f, 0, SEEK_END);
    size_t fileSize = ftello(f);
    fseeko(f, 0, SEEK_SET);

    auto frontBumpScope = g_allocator.beginFrontScope();
    char* buffer = g_allocator.bumpAlloc<char>(fileSize);
    if (!buffer) {
      std::fclose(f);
      return std::nullopt;
    }

    size_t readSize = std::fread(buffer, 1, fileSize, f);
    std::fclose(f);
    if (readSize != fileSize) {
      frontBumpScope.reset();
      return std::nullopt;
    }
    frontBumpScope.disable();
    return StringView { buffer, fileSize };
  }

  #pragma region Content
  #pragma endregion Content

  #pragma region TOC

  void parseToc(
    StringView tocView,
    Book::TocEntry* entries,
    size_t& tocs,
    StringView contentBasePath)
  {
    enum class NavDepth {
      Root, Ncx,
      NavMap, NavPoint, NavLabel, Content, NavText
    };

    xml::XmlParser parser { tocView };
    NavDepth navDepth = NavDepth::Root;
    StringView currentTocSrc;
    StringView currentTocText;

    auto tryCommitNavPoint = [&]() {
      if (currentTocSrc.size() > 0 && currentTocText.size() > 0) {
        // Strip any anchor (#...) from the src path
        StringView srcPath = currentTocSrc.sliceUntil('#');
        
        // Build full path relative to content base
        char pathBuffer[512];
        StringView fullPath;
        if (contentBasePath.size() > 0) {
          fullPath = join(pathBuffer, sizeof(pathBuffer),
            contentBasePath, StringView("/"), srcPath);
        } else {
          fullPath = srcPath;
        }

        // Try to find the zip entry index
        auto indexResult = zip::findFileEntryIndex(*g_zipEntries, fullPath);
        if (!indexResult.has_value()) {
          // File not found, skip this entry
          return;
        }

        if (entries) {
          auto entry = &entries[tocs];
          entry->label = *g_allocator.retain(currentTocText);
          entry->zipEntryIndex = *indexResult;
        }
        tocs++;
      }
    };

    while (true) {
      auto ty = parser.next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;
      
      StringView name;

      switch (ty) {
        case xml::XmlParser::NodeType::Element:
          name = parser.name();
          if (navDepth == NavDepth::Root && name.caseCmp("ncx")) {
            navDepth = NavDepth::Ncx;
          } else if (navDepth == NavDepth::Ncx && name.caseCmp("navMap")) {
            navDepth = NavDepth::NavMap;
          } else if (navDepth == NavDepth::NavMap && name.caseCmp("navPoint")) {
            navDepth = NavDepth::NavPoint;
            currentTocSrc = "";
            currentTocText = "";
          } else if (navDepth == NavDepth::NavPoint && name.caseCmp("content")) {
            currentTocSrc = parser.getAttribute("src");
            navDepth = NavDepth::Content;
          } else if (navDepth == NavDepth::NavPoint && name.caseCmp("navPoint")) {
            navDepth = NavDepth::NavPoint;
            tryCommitNavPoint();
          } else if (navDepth == NavDepth::NavPoint && name.caseCmp("navLabel")) {
            navDepth = NavDepth::NavLabel;
          } else if (navDepth == NavDepth::NavLabel && name.caseCmp("text")) {
            navDepth = NavDepth::NavText;
          }
          break;
        case xml::XmlParser::NodeType::Text:
          if (navDepth == NavDepth::NavText) {
            currentTocText = parser.text();
          }
          break;
        case xml::XmlParser::NodeType::EndElement:
          name = parser.name();
          if (navDepth == NavDepth::NavText && name.caseCmp("text")) {
            navDepth = NavDepth::NavLabel;
          } else if (navDepth == NavDepth::NavLabel && name.caseCmp("navLabel")) {
            navDepth = NavDepth::NavPoint;
          } else if (navDepth == NavDepth::Content && name.caseCmp("content")) {
            navDepth = NavDepth::NavPoint;
          } else if (navDepth == NavDepth::NavPoint && name.caseCmp("navPoint")) {
            navDepth = NavDepth::NavMap;
            tryCommitNavPoint();
          } else if (navDepth == NavDepth::NavMap && name.caseCmp("navMap")) {
            navDepth = NavDepth::Ncx;
          } else if (navDepth == NavDepth::Ncx && name.caseCmp("ncx")) {
            navDepth = NavDepth::Root;
          }
          break;
        default:
          break;
      }
    }
  }

  Book::EpubLoadResult parseTableOfContents(StringView str) {
    // Get the content base path for resolving relative paths
    StringView basePath = g_contentBasePath.has_value() ? *g_contentBasePath : StringView{};
    
    // determine how much space we need to reserve
    size_t entryCount = 0;
    parseToc(str, nullptr, entryCount, basePath);

    printf("Expected TOC entries: %zu\n", entryCount);

    // Sanity check: ensure entry count fits in uint16_t for index storage
    if (entryCount > UINT16_MAX) {
      printf("TOC entry count exceeds uint16_t limit\n");
      return Book::EpubLoadResult::InvalidFormat;
    }

    Book::TocEntry* tocEntries = g_allocator.subAlloc<Book::TocEntry>(entryCount);
    entryCount = 0;
    parseToc(str, tocEntries, entryCount, basePath);

    g_toc = std::span(tocEntries, entryCount);

    return Book::EpubLoadResult::Success;
  }

  #pragma endregion TOC
}
