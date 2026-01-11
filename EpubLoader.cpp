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
  std::optional<StringView> g_tocPath;
  std::optional<std::span<Book::SpineEntry>> g_spine;
  std::optional<std::span<Book::TocEntry>> g_toc;

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
      StringView href;
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

        // resolve and manifest entry href to file entry
        auto href = manifest->getAttribute("href");

        char pathBuffer[512];
        if (g_contentBasePath.has_value()) {
          href = join(pathBuffer, sizeof(pathBuffer),
            *g_contentBasePath, StringView("/"), href);
        }

        auto fileEntry = zip::findFileEntry(*g_zipEntries, href);
        if (!fileEntry.has_value()) {
          println("Manifest entry refers to missing file: ", href);
          return fromZipError(fileEntry.error());
        }

        entry.href = fileEntry->fileName;
      } else if (ty == xml::XmlParser::NodeType::EndElement) {
        if (manifest->name().caseCmp("manifest")) {
          break;
        }
      }
    }

    auto resolveManifestEntry = [&](StringView id) -> std::optional<StringView> {
      for (size_t j = 0; j < manifestEntryCount; j++) {
        if (manifestEntries[j].id == id) {
          return manifestEntries[j].href;
        }
      }
      return std::nullopt;
    };

    // resolve ncx entry to path
    if (tocPath.has_value()) {
      g_tocPath = resolveManifestEntry(*tocPath);
      if (!g_tocPath.has_value()) {
        printf("TOC path in spine does not exist in manifest: ");
        fwrite(tocPath->data(), 1, tocPath->size(), stdout);
        printf("\n");
        return EpubLoadResult::InvalidFormat;
      }
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

    g_spine = std::span<SpineEntry>(spineEntries, spineEntryCount);

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

        auto hrefOpt = resolveManifestEntry(idref);
        if (!hrefOpt.has_value()) {
          printf("Spine entry refers to missing manifest id: ");
          fwrite(idref.data(), 1, idref.size(), stdout);
          printf("\n");
          return EpubLoadResult::InvalidFormat;
        }
        entry.src = *hrefOpt;
        printf("Spine entry: ");
        fwrite(entry.idref.data(), 1, entry.idref.size(), stdout);
        printf(" -> ");
        fwrite(entry.src.data(), 1, entry.src.size(), stdout);
        printf("\n");
      } else if (ty == xml::XmlParser::NodeType::EndElement) {
        if (spine->name().caseCmp("spine")) {
          break;
        }
      }
    }
  }

  if (g_tocPath.has_value()) {
    auto tocScope = g_allocator.beginFrontScope();
    auto tocFile = zip::loadTempEntry(*g_epubFile, *g_zipEntries, *g_tocPath, g_allocator);
    if (!tocFile.has_value()) {
      println("Failed to load TOC file: ", *g_tocPath);
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
      printf(" -> ");
      fwrite(entry.src.data(), 1, entry.src.size(), stdout);
      printf("\n");
    }

    // Assign ToC entries to Spine entries
    std::optional<TocEntry*> currentTocEntry;
    size_t tocIndex = 0;
    size_t srcSkip = 0;
    if (g_contentBasePath.has_value()) {
      srcSkip = g_contentBasePath->size() + 1; // +1 for slash
    }
    for (auto& spineEntry : *g_spine) {
      std::optional<TocEntry*> matchedTocEntry;
      for (size_t i = tocIndex; i < g_toc->size(); i++) {
        auto& tocEntry = (*g_toc)[i];
        if (tocEntry.src == spineEntry.src.skip(srcSkip)) {
          matchedTocEntry = &tocEntry;
          tocIndex = i + 1;
          break;
        }
      }
      if (matchedTocEntry != std::nullopt) {
        currentTocEntry = matchedTocEntry;
      }
      spineEntry.tocEntry = currentTocEntry;
    }

    for (const auto& spineEntry : *g_spine) {
      printf("Spine Entry: ");
      fwrite(spineEntry.src.data(), 1, spineEntry.src.size(), stdout);
      if (spineEntry.tocEntry.has_value()) {
        printf(" -> TOC: ");
        fwrite((*spineEntry.tocEntry)->label.data(), 1, (*spineEntry.tocEntry)->label.size(), stdout);
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
  g_spine.reset();
  g_toc.reset();
  g_tocPath.reset();
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
    size_t& tocs)
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
        if (entries) {
          auto entry = &entries[tocs];
          entry->label = *g_allocator.retain(currentTocText);
          entry->src = *g_allocator.retain(currentTocSrc);
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
    // determine how much space we need to reserve
    size_t entryCount = 0;
    parseToc(str, nullptr, entryCount);

    printf("Expected TOC entries: %zu\n", entryCount);

    Book::TocEntry* tocEntries = g_allocator.subAlloc<Book::TocEntry>(entryCount);
    entryCount = 0;
    parseToc(str, tocEntries, entryCount);

    g_toc = std::span(tocEntries, entryCount);

    return Book::EpubLoadResult::Success;
  }

  #pragma endregion TOC
}
