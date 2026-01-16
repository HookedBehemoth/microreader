#include "EpubLoader.hpp"

#include "EpubContentResolver.hpp"
#include "XmlParser.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <optional>
#include "stringview.h"
#include "log.h"
#include "Allocator.hpp"
#include "Fs.hpp"
#include "ZipParser.hpp"
#include "EpubCssParser.hpp"
#include "xml/TocNcx.hpp"

constinit std::byte g_buffer[mem::AllocatorSize];
constinit mem::Allocator g_allocator(g_buffer);

namespace {
  std::optional<FILE*> g_epubFile;
  std::optional<std::span<zip::ZipFileEntry>> g_zipEntries;
  std::optional<StringView> g_title;
  std::optional<StringView> g_author;
  std::optional<StringView> g_language;
  std::optional<StringView> g_coverId;
  std::optional<StringView> g_contentPath;
  std::optional<epub::ContentResolver> g_contentResolver;
  std::optional<uint16_t> g_tocZipIndex;
  std::optional<std::span<uint16_t>> g_spineZipIndices;
  std::optional<std::span<uint16_t>> g_spineTocIndices;
  std::optional<xml::toc::ncx::Contents> g_tocContents;
  // Separate array for spine->toc mapping to avoid std::optional bloat in SpineEntry
  constexpr uint16_t NO_TOC_ENTRY = UINT16_MAX;

  std::optional<std::span<Book::CssFile>> g_cssRules;

  constexpr StringView ExtractedBase = "microreader";
  std::optional<StringView> g_cacheDirectory;

  template<typename T>
  using Result = std::expected<T, Book::EpubLoadResult>;

  // std::optional<StringView> loadTempFile(StringView filePath);
  Book::EpubLoadResult ensureCacheDirectory(StringView path);
  // Book::EpubLoadResult parseTableOfContents(const zip::ZipFileEntry& entry);
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
      case zip::ZipError::TooManyFiles:
        return EpubLoadResult::InvalidFormat;
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
    g_contentResolver = epub::ContentResolver { .entries = *g_zipEntries };
    size_t lastSlashIndex = g_contentPath->findLast('/');
    if (lastSlashIndex != g_contentPath->size()) {
      g_contentResolver->basePath = g_contentPath->subString(0, lastSlashIndex);
    }

    printf("Container parsed (used memory: %zu bytes, max used memory: %zu bytes)\n", g_allocator.usedMemory(), g_allocator.maxUsedMemory());
  }
  g_allocator.subCanary("___ContentPath__");

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
          if (!g_contentResolver->resolve(path)) {
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

    g_allocator.subCanary("____Metadata____");

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
      StringView mediaType;
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
        entry.mediaType = manifest->getAttribute("media-type");

        // resolve and manifest entry href to file entry index
        auto href = manifest->getAttribute("href");
        
        auto fileEntryIndex = g_contentResolver->resolve(href);
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
        println("TOC path in spine does not exist in manifest: ", *tocPath);
        return EpubLoadResult::InvalidFormat;
      }
      g_tocZipIndex = *tocIndex;
    }

    // permanent spine allocation
    uint16_t* spineZipIndices = g_allocator.subAlloc<uint16_t>(spineEntryCount);
    if (!spineZipIndices) {
      printf("Out of memory allocating spine entries\n");
      return EpubLoadResult::OutOfMemory;
    }
    g_allocator.subCanary("__SpineZipIdx___");

    StringView* spineIds = g_allocator.bumpAlloc<StringView>(spineEntryCount);

    g_spineZipIndices = std::span<uint16_t>(spineZipIndices, spineEntryCount);

    size_t spineIdx = 0;
    while (true) {
      auto ty = spine->next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;
      if (ty == xml::XmlParser::NodeType::Element) {
        if (!spine->name().caseCmp("itemref"))
          continue;
        
        auto idref = spine->getAttribute("idref");
        spineIds[spineIdx] = idref;

        auto indexOpt = resolveManifestEntry(idref);
        if (!indexOpt.has_value()) {
          println("Spine entry refers to missing manifest id: ", idref);
          return EpubLoadResult::InvalidFormat;
        }
        spineZipIndices[spineIdx] = *indexOpt;
        spineIdx++;
      } else if (ty == xml::XmlParser::NodeType::EndElement) {
        if (spine->name().caseCmp("spine")) {
          break;
        }
      }
    }

    // Parse CSS files
    size_t cssFileCount = 0;
    for (size_t j = 0; j < manifestEntryCount; j++) {
      const auto& entry = manifestEntries[j];
      if (entry.mediaType == "text/css") {
        cssFileCount++;
      }
    }
    printf("Found %zu CSS files in manifest\n", cssFileCount);
    auto scope = g_allocator.beginFrontScope();
    auto cssFiles = g_allocator.bumpAlloc<CssFile>(cssFileCount);
    if (!cssFiles) {
      printf("Out of memory allocating CSS file entries\n");
    } else {
      cssFileCount = 0;
      for (size_t j = 0; j < manifestEntryCount; j++) {
        const auto& entry = manifestEntries[j];
        if (entry.mediaType == "text/css") {
          auto scope = g_allocator.beginFrontScope();
          auto zipEntry = (*g_zipEntries)[entry.zipEntryIndex];
          auto cssFileData = zip::loadTempEntry(*g_epubFile, zipEntry, g_allocator);
          if (!cssFileData.has_value()) {
            println("Failed to load CSS file: ", zipEntry.fileName);
            continue;
          }
          StringView cssFileView = StringView { (char*)cssFileData->data(), cssFileData->size() };
          auto cssRules = css::parseSheet(cssFileView, g_allocator);
          if (cssRules.size() == 0) {
            println("No CSS rules found in file: ", zipEntry.fileName);
            continue;
          }
          cssFiles[cssFileCount] = CssFile {
            .zipIndex = entry.zipEntryIndex,
            .rules = cssRules
          };
          cssFileCount++;
        }
      }

      // Move actual rules to permanent storage
      if (cssFileCount > 0) {
        printf("Storing %zu CSS files permanently\n", cssFileCount);
        auto permanentCssFiles = g_allocator.subAlloc<CssFile>(cssFileCount);
        if (permanentCssFiles) {
            std::memcpy(permanentCssFiles, cssFiles, sizeof(CssFile) * cssFileCount);
            g_cssRules = std::span<CssFile> { permanentCssFiles, cssFileCount };
        }
        g_allocator.subCanary("____CssFiles____");
      }
    }
    printf("Content parsed (used memory: %zu bytes, max used memory: %zu bytes)\n", g_allocator.usedMemory(), g_allocator.maxUsedMemory());
  }

  if (g_tocZipIndex.has_value()) {
    // auto tocScope = g_allocator.beginFrontScope();
    const auto& tocEntry = (*g_zipEntries)[*g_tocZipIndex];
    // // auto parseBuffer = g_allocator.bumpAlloc<char>(0x1000);
    // auto tocFile = zip::loadTempEntry(*g_epubFile, tocEntry, g_allocator);
    // if (!tocFile.has_value()) {
    //   printf("Failed to load TOC file at index: %u\n", *g_tocZipIndex);
    //   return fromZipError(tocFile.error());
    // }
    // StringView tocView = StringView { (char*)tocFile->data(), tocFile->size() };

    auto result = xml::toc::ncx::parse(fp, tocEntry, g_allocator, *g_contentResolver);
    if (!result) {
      return EpubLoadResult::InvalidFormat; // TODO
    }
    g_tocContents = *result;

    // Separate array for spine->toc mapping (UINT16_MAX = no toc entry)
    size_t spineEntryCount = g_spineZipIndices->size();
    uint16_t* spineTocIndices = g_allocator.subAlloc<uint16_t>(spineEntryCount);
    if (!spineTocIndices) {
      printf("Out of memory allocating spine toc indices\n");
      return EpubLoadResult::OutOfMemory;
    }
    g_allocator.subCanary("__SpineTocIdx___");
    // Initialize all to NO_TOC_ENTRY
    for (size_t i = 0; i < spineEntryCount; i++) {
      spineTocIndices[i] = NO_TOC_ENTRY;
    }

    // Assign ToC entries to Spine entries using separate index array
    // Each spine entry gets the most recent matching TOC entry (for chapter grouping)
    uint16_t currentTocIndex = NO_TOC_ENTRY;
    size_t searchStart = 0;
    size_t tocCount = g_tocContents->zipIndices.size();
    for (size_t spineIdx = 0; spineIdx < spineEntryCount; spineIdx++) {
      const auto& spineZipEntry = (*g_spineZipIndices)[spineIdx];
      for (size_t i = searchStart; i < tocCount; i++) {
        auto& tocEntry = g_tocContents->zipIndices[i];
        if (tocEntry == spineZipEntry) {
          currentTocIndex = static_cast<uint16_t>(i);
          searchStart = i + 1;
          break;
        }
      }
      spineTocIndices[spineIdx] = currentTocIndex;
    }

    g_spineTocIndices = std::span<uint16_t>(spineTocIndices, spineEntryCount);
    printf("TOC parsed (used memory: %zu bytes, max used memory: %zu bytes)\n", g_allocator.usedMemory(), g_allocator.maxUsedMemory());
  }

  println("Finished parsing EPUB");

#ifdef MEMCANARY
  g_allocator.sanityCheck();

  char memDumpPath[512];
  join(memDumpPath, sizeof(memDumpPath), *g_cacheDirectory, "/memdump.bin\0");
  FILE* memDump = std::fopen(memDumpPath, "wb");
  if (memDump) {
    auto backSpan = g_allocator.getBackMemorySpan();
    std::fwrite(backSpan.data(), 1, backSpan.size(), memDump);
    std::fclose(memDump);
  }
#endif

  return EpubLoadResult::Success;
}

void unload() {
  printf("Unloading EPUB\n");
  g_allocator.dumpState();
  g_allocator.reset();
  g_cacheDirectory.reset();
  g_cssRules.reset();
  g_spineTocIndices.reset();
  g_spineZipIndices.reset();
  g_tocContents.reset();
  g_tocZipIndex.reset();
  g_contentResolver.reset();
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

std::optional<uint16_t> getSpineEntryCount() {
  if (!g_spineZipIndices.has_value() || g_spineZipIndices->size() > UINT16_MAX) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(g_spineZipIndices->size());
}

std::optional<uint16_t> getSpineZipFileIndex(uint16_t spineIndex) {
  if (!g_spineZipIndices.has_value() || spineIndex >= g_spineZipIndices->size()) {
    return std::nullopt;
  }
  return (*g_spineZipIndices)[spineIndex];
}

std::optional<uint16_t> getTocForSpineEntry(uint16_t spineIndex) {
  if (!g_spineTocIndices.has_value() || spineIndex >= g_spineTocIndices->size()) {
    return std::nullopt;
  }
  auto tocIndex = (*g_spineTocIndices)[spineIndex];
  if (tocIndex == NO_TOC_ENTRY) {
    return std::nullopt;
  }
  return tocIndex;
}

std::optional<uint16_t> getTocEntryCount() {
  if (!g_tocContents.has_value() || g_tocContents->zipIndices.size() > UINT16_MAX) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(g_tocContents->zipIndices.size());
}

std::optional<StringView> getTocLabel(uint16_t tocIndex) {
  if (!g_tocContents.has_value() || tocIndex >= g_tocContents->labels.size()) {
    return std::nullopt;
  }
  return g_tocContents->labels[tocIndex];
}

std::optional<zip::ZipFileEntry> getZipFileEntry(uint16_t zipIndex) {
  if (!g_zipEntries.has_value() || zipIndex >= g_zipEntries->size()) {
    return std::nullopt;
  }
  return (*g_zipEntries)[zipIndex];
}

std::optional<std::span<Book::CssFile>> getCssRules() {
  return g_cssRules;
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
  // std::optional<StringView> loadTempFile(StringView filePath) {
  //   std::FILE* f = std::fopen(filePath.data(), "rb");
  //   if (!f) {
  //     return std::nullopt;
  //   }
  //   fseeko(f, 0, SEEK_END);
  //   size_t fileSize = ftello(f);
  //   fseeko(f, 0, SEEK_SET);

  //   auto frontBumpScope = g_allocator.beginFrontScope();
  //   char* buffer = g_allocator.bumpAlloc<char>(fileSize);
  //   if (!buffer) {
  //     std::fclose(f);
  //     return std::nullopt;
  //   }

  //   size_t readSize = std::fread(buffer, 1, fileSize, f);
  //   std::fclose(f);
  //   if (readSize != fileSize) {
  //     frontBumpScope.reset();
  //     return std::nullopt;
  //   }
  //   frontBumpScope.disable();
  //   return StringView { buffer, fileSize };
  // }

  #pragma region Content
  #pragma endregion Content

  #pragma region TOC

  #pragma endregion TOC
}
