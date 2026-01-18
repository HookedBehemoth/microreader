#include "EpubLoader.hpp"

#include <StringView.hpp>
#include <Log.hpp>
#include <mem/Allocator.hpp>
#include <fs/Fs.hpp>
#include <content/zip/ZipParser.hpp>
#include <content/epub/Container.hpp>
#include <content/epub/Content.hpp>
#include <content/epub/FileResolver.hpp>
#include <content/epub/TocNcx.hpp>
#include <Expected.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>

constinit std::byte g_buffer[mem::AllocatorSize];
constinit mem::Allocator g_allocator(g_buffer);

namespace {
  std::optional<FILE*> g_epubFile;
  std::optional<std::span<zip::ZipFileEntry>> g_zipEntries;
  std::optional<StringView> g_contentPath;
  std::optional<epub::FileResolver> g_fileResolver;
  std::optional<epub::content::Data> g_contentData;
  std::optional<std::span<uint16_t>> g_spineTocIndices;
  std::optional<epub::toc::ncx::Data> g_tocContents;
  // Separate array for spine->toc mapping to avoid std::optional bloat in SpineEntry
  constexpr uint16_t NO_TOC_ENTRY = UINT16_MAX;

  std::optional<std::span<Book::CssFile>> g_cssRules;

  constexpr StringView ExtractedBase = BASEDIR "microreader";
  std::optional<StringView> g_cacheDirectory;

  template<typename T>
  using Result = util::Expected<T, Book::EpubLoadResult>;

  // std::optional<StringView> loadTempFile(StringView filePath);
  Book::EpubLoadResult ensureCacheDirectory(StringView path);
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

    auto containerEntry = zip::findFileEntry(*g_zipEntries, "META-INF/container.xml");
    if (!containerEntry.has_value()) {
      PrintF("Failed to load container.xml\n");
      return EpubLoadResult::MissingFile;
    }

    std::optional<StringView> contentPath = epub::container::parse(fp, *containerEntry, g_allocator);
    
    if (!contentPath.has_value()) {
      PrintF("Failed to find content path in container.xml\n");
      return EpubLoadResult::InvalidFormat;
    }
    println("Content path: ", *contentPath);

    g_contentPath = contentPath;
    g_fileResolver = epub::FileResolver { .entries = *g_zipEntries };
    size_t lastSlashIndex = g_contentPath->findLast('/');
    if (lastSlashIndex != g_contentPath->size()) {
      g_fileResolver->basePath = g_contentPath->subString(0, lastSlashIndex);
    }

    PrintF("Container parsed (used memory: %zu bytes, max used memory: %zu bytes)\n", g_allocator.usedMemory(), g_allocator.maxUsedMemory());
  }
  g_allocator.subCanary("___ContentPath__");

  // parse content obf file
  {
    // Parse CSS files
    size_t cssFileCount = 0;
    for (const auto& entry : *g_zipEntries) {
      if (entry.fileName.endsWith(".css")) {
        cssFileCount++;
      }
    }
    PrintF("Found %zu CSS files in manifest\n", cssFileCount);
    auto scope = g_allocator.beginFrontScope();
    auto cssFiles = g_allocator.bumpAlloc<CssFile>(cssFileCount);
    if (!cssFiles) {
      PrintF("Out of memory allocating CSS file entries\n");
    } else {
      cssFileCount = 0;
      for (uint16_t zipEntryIndex = 0; zipEntryIndex < g_zipEntries->size(); ++zipEntryIndex) {
        const auto& zipEntry = (*g_zipEntries)[zipEntryIndex];
        if (!zipEntry.fileName.endsWith(".css")) {
          continue;
        }
        auto scope = g_allocator.beginFrontScope();
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
          .zipIndex = zipEntryIndex,
          .rules = cssRules
        };
        cssFileCount++;
      }

      // Move actual rules to permanent storage
      if (cssFileCount > 0) {
        PrintF("Storing %zu CSS files permanently\n", cssFileCount);
        auto permanentCssFiles = g_allocator.subAlloc<CssFile>(cssFileCount);
        if (permanentCssFiles) {
            std::memcpy(permanentCssFiles, cssFiles, sizeof(CssFile) * cssFileCount);
            g_cssRules = std::span<CssFile> { permanentCssFiles, cssFileCount };
        }
        g_allocator.subCanary("____CssFiles____");
      }
    }

    auto contentFileEntry = zip::findFileEntry(*g_zipEntries, *g_contentPath);
    if (!contentFileEntry.has_value()) {
      PrintF("Failed to load content file\n");
      return EpubLoadResult::MissingFile;
    }
    g_contentData = epub::content::parse(
      fp, *contentFileEntry,
      *g_fileResolver,
      g_allocator);
    if (!g_contentData.has_value()) {
      PrintF("Failed to parse content file\n");
      return EpubLoadResult::InvalidFormat;
    }
    PrintF("Content parsed (used memory: %zu bytes, max used memory: %zu bytes)\n", g_allocator.usedMemory(), g_allocator.maxUsedMemory());
  }

  if (g_contentData->tocZipIndex.has_value()) {
    const auto& tocEntry = (*g_zipEntries)[*g_contentData->tocZipIndex];

    auto result = epub::toc::ncx::parse(fp, tocEntry, g_allocator, *g_fileResolver);
    if (!result) {
      return EpubLoadResult::InvalidFormat; // TODO
    }
    g_tocContents = *result;

    // Separate array for spine->toc mapping (UINT16_MAX = no toc entry)
    size_t spineEntryCount = g_contentData->spineZipIndices.size();
    uint16_t* spineTocIndices = g_allocator.subAlloc<uint16_t>(spineEntryCount);
    if (!spineTocIndices) {
      PrintF("Out of memory allocating spine toc indices\n");
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
      const auto& spineZipEntry = g_contentData->spineZipIndices[spineIdx];
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
    PrintF("TOC parsed (used memory: %zu bytes, max used memory: %zu bytes)\n", g_allocator.usedMemory(), g_allocator.maxUsedMemory());
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
  PrintF("Unloading EPUB\n");
  g_allocator.dumpState();
  g_allocator.reset();
  g_cacheDirectory.reset();
  // g_cssRules.reset();
  g_spineTocIndices.reset();
  // g_spineZipIndices.reset();
  g_tocContents.reset();
  g_contentData.reset();
  // g_tocZipIndex.reset();
  g_fileResolver.reset();
  g_contentPath.reset();
  // g_coverId.reset();
  // g_language.reset();
  // g_author.reset();
  // g_title.reset();
  g_zipEntries.reset();
  if (g_epubFile.has_value()) {
    fclose(*g_epubFile);
    g_epubFile.reset();
  }
}

std::optional<StringView> getTitle() {
  return g_contentData.has_value() ? g_contentData->title : std::nullopt;
}

std::optional<StringView> getAuthor() {
  return g_contentData.has_value() ? g_contentData->author : std::nullopt;
}

std::optional<StringView> getLanguage() {
  return g_contentData.has_value() ? g_contentData->language : std::nullopt;
}

std::optional<uint16_t> getSpineEntryCount() {
  if (!g_contentData.has_value() || g_contentData->spineZipIndices.size() > UINT16_MAX) {
    return std::nullopt;
  }
  return static_cast<uint16_t>(g_contentData->spineZipIndices.size());
}

std::optional<uint16_t> getSpineZipFileIndex(uint16_t spineIndex) {
  if (!g_contentData.has_value() || spineIndex >= g_contentData->spineZipIndices.size()) {
    return std::nullopt;
  }
  return g_contentData->spineZipIndices[spineIndex];
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
  // return g_cssRules;
  return std::nullopt;
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

  #pragma endregion TOC
}
