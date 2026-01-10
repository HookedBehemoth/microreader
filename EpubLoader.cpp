#include "EpubLoader.hpp"

#include "XmlParser.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <sys/stat.h>
#include <sys/types.h>
#include "miniz.h"
#include "stringview.h"

namespace {
  constexpr size_t BufferSize = 200 * 1024;
  std::byte g_buffer[BufferSize];
  std::size_t g_bumpFront = 0;
  std::size_t g_bumpBack = BufferSize;

  std::size_t availableMemory() {
    return g_bumpBack - g_bumpFront;
  }

  // aligned temporary bump allocator
  template<typename T>
  inline T* bumpAlloc(size_t count = 1) {
    size_t size = sizeof(T) * count;
    size_t space = availableMemory();
    void* ptr = g_buffer + g_bumpFront;
    void* aligned = std::align(alignof(T), size, ptr, space);
    if (!aligned) {
      return nullptr;
    }
    if ((std::byte*)aligned + size > (std::byte*)g_buffer + g_bumpBack) {
      return nullptr;
    }
    g_bumpFront = (std::byte*)aligned + size - g_buffer;
    printf("bumpAlloc: requested %zu bytes, new bump front: %zu\n", size, g_bumpFront);
    return (T*)aligned;
  }

  // semi-permanent sub-allocator which goes downwards
  template<typename T>
  inline T* subAlloc(size_t count) {
    size_t size = sizeof(T) * count;
    size_t space = availableMemory();
    std::size_t newBumpBack = g_bumpBack - size;
    void* ptr = g_buffer + newBumpBack;
    void* aligned = std::align(alignof(T), size, ptr, space);
    if (!aligned) {
      return nullptr;
    }
    if ((std::byte*)aligned < (std::byte*)g_buffer + g_bumpFront) {
      return nullptr;
    }
    g_bumpBack = (std::byte*)aligned - g_buffer;
    printf("subAlloc: requested %zu bytes, new bump back: %zu\n", size, g_bumpBack);
    return (T*)aligned;
  }

  class FrontBumpScope {
    public:
    FrontBumpScope() : m_oldBumpFront(g_bumpFront) {}
    ~FrontBumpScope() { g_bumpFront = m_oldBumpFront; }
    void disable() { enabled = false; }
    void reset() { g_bumpFront = m_oldBumpFront; disable(); }
    private:
    std::size_t m_oldBumpFront;
    bool enabled = true;
  };
  class BackBumpScope {
    public:
    BackBumpScope() : m_oldBumpBack(g_bumpBack) {}
    ~BackBumpScope() { g_bumpBack = m_oldBumpBack; }
    void disable() { enabled = false; }
    void reset() { g_bumpBack = m_oldBumpBack; disable(); }
    private:
    std::size_t m_oldBumpBack;
    bool enabled = true;
  };

  struct ZipFileEntry {
    StringView fileName;
    uint64_t compressedSize;
    uint64_t uncompressedSize;
    uint32_t localHeaderOffset;
    uint16_t compressionMethod;
  };

  std::optional<FILE*> g_epubFile;
  std::optional<std::span<ZipFileEntry>> g_zipEntries;
  std::optional<Book::Book> g_book;
  std::optional<StringView> g_contentPath;
  std::optional<StringView> g_tocPath;
  std::optional<std::span<Book::SpineEntry>> g_spine;
  std::optional<std::span<Book::TocEntry>> g_toc;

  constexpr StringView ExtractedBase = "microreader";
  std::optional<StringView> g_cacheDirectory;

  // template<typename T>
  // struct Result : public std::expected<T, Book::EpubLoadResult> {};
  template<typename T>
  using Result = std::expected<T, Book::EpubLoadResult>;

  std::optional<StringView> retain(StringView str) {
    char* mem = subAlloc<char>(str.size());
    if (!mem) {
      return std::nullopt;
    }
    std::memcpy(mem, str.data(), str.size());
    return StringView { mem, str.size() };
  }

  std::optional<StringView> loadTempFile(StringView filePath);
  Book::EpubLoadResult ensureCacheDirectory(StringView path);
  Book::EpubLoadResult parseZip();
  Book::EpubLoadResult unpackFully();
  Book::EpubLoadResult unpackEntry(const ZipFileEntry& entry);
  Result<std::span<std::byte>> loadTempEntry(const ZipFileEntry& entry);
  Result<std::span<std::byte>> loadTempEntry(StringView path);
  Result<ZipFileEntry> findFileEntry(StringView path);
  bool fileExists(StringView path);
  Book::EpubLoadResult parseTableOfContents(StringView toc);
}

namespace Book {

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

  result = parseZip();
  if (result != EpubLoadResult::Success) {
    return result;
  }

  // result = unpackFully();
  // if (result != EpubLoadResult::Success) {
  //   unload();
  //   return result;
  // }
  {
    FrontBumpScope containerScope;

    std::optional<StringView> contentPath;

    StringView containerFile = "META-INF/container.xml";
    auto containerData = loadTempEntry(containerFile);
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

    printf("Content path: ");
    fwrite(contentPath->data(), 1, contentPath->size(), stdout);
    printf("\n");

    g_contentPath = retain(*contentPath);
  }

  // parse content obf file
  {
    FrontBumpScope contentScope;

    auto contentFile = loadTempEntry(*g_contentPath);;
    if (!contentFile.has_value()) {
      printf("Failed to load content file\n");
      return EpubLoadResult::MissingFile;
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

    while (true) {
      auto ty = parser.next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;

      if (ty == xml::XmlParser::NodeType::Element) {
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

        if (parser.name().caseCmp("item") && inManifest) {
          auto path = parser.getAttribute("href");
          if (!fileExists(path)) {
            printf("Manifest entry refers to missing file: ");
            fwrite(path.data(), 1, path.size(), stdout);
            printf("\n");
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
      }
    }

    if (manifestEntryCount == 0) {
      printf("No manifest entries found in content file\n");
      return EpubLoadResult::InvalidFormat;
    }

    if (spineEntryCount == 0) {
      printf("No spine entries found in content file\n");
      return EpubLoadResult::InvalidFormat;
    }

    // parser.reset();

    // temporary allocation
    struct ManifestEntry {
      StringView id;
      StringView href;
    };
    ManifestEntry* manifestEntries = bumpAlloc<ManifestEntry>(manifestEntryCount);
  
    size_t i = 0;

    while (true) {
      auto ty = manifest->next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;
      if (ty == xml::XmlParser::NodeType::Element) {
        if (!manifest->name().caseCmp("item"))
          continue;
        
        auto &entry = manifestEntries[i++];
        
        entry.id = manifest->getAttribute("id");

        // resolve and manifest entry href to file entry
        auto href = manifest->getAttribute("href");
        auto fileEntry = findFileEntry(href);
        if (!fileEntry.has_value()) {
          printf("Manifest entry refers to missing file: ");
          fwrite(href.data(), 1, href.size(), stdout);
          printf("\n");
          return EpubLoadResult::MissingFile;
        }

        entry.href = fileEntry->fileName;

        if (i >= manifestEntryCount) {
          break;
        }
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
    SpineEntry* spineEntries = subAlloc<SpineEntry>(spineEntryCount);

    while (true) {
      auto ty = spine->next();
      if (ty == xml::XmlParser::NodeType::EndOfFile)
        break;
      if (ty == xml::XmlParser::NodeType::Element) {
        if (!spine->name().caseCmp("itemref"))
          continue;
        
        auto &entry = spineEntries[i++];
        
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

        if (i >= spineEntryCount) {
          break;
        }
      } else if (ty == xml::XmlParser::NodeType::EndElement) {
        if (spine->name().caseCmp("spine")) {
          break;
        }
      }
    }

    g_spine = std::span<SpineEntry>(spineEntries, spineEntryCount);
  }

  if (g_tocPath.has_value()) {
    FrontBumpScope tocScope;
    auto tocFile = loadTempEntry(*g_tocPath);
    if (!tocFile.has_value()) {
      printf("Failed to load TOC file\n");
      return EpubLoadResult::MissingFile;
    }
    StringView tocView = StringView { (char*)tocFile->data(), tocFile->size() };

    result = parseTableOfContents(tocView);
  }

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
    // printf("  %.*s -> %.*s\n",
    //   (int)entry.label.size(), entry.label.data(),
    //   (int)entry.src.size(), entry.src.data());
  }

  return EpubLoadResult::Success;
}

void unload() {
  printf("Unloading EPUB\n");
  printf("Free memory: %zu bytes\n", availableMemory());
  printf("Used memory: %zu bytes\n", BufferSize - availableMemory());
  printf("Used front bump: %zu bytes\n", g_bumpFront);
  printf("Used back bump: %zu bytes\n", BufferSize - g_bumpBack);
  std::memset(g_buffer, 0, BufferSize);
  g_bumpFront = 0;
  g_bumpBack = BufferSize;
  g_cacheDirectory.reset();
  g_toc.reset();
  g_book.reset();
  g_zipEntries.reset();
  if (g_epubFile.has_value()) {
    fclose(*g_epubFile);
    g_epubFile.reset();
  }
}

void deleteCache() {
}

void deleteCacheForFile(StringView filePath) {
  (void)filePath;
}

} // namespace Book

namespace {
  bool ensurePath(StringView fullPath) {
    constexpr size_t MaxPathLength = 512;
    if (fullPath.size() >= MaxPathLength) {
      return false;
    }
    char pathBuffer[MaxPathLength] = {};
    size_t lastSlash = 0;
    while (lastSlash < fullPath.size()) {
      size_t segmentLength = fullPath.find('/', lastSlash + 1);
      if (segmentLength == fullPath.size()) {
        break;
      }
      std::memcpy(pathBuffer, fullPath.data(), segmentLength);
      pathBuffer[segmentLength] = '\0';
      int rc = mkdir(pathBuffer, 0755);
      if (rc != 0 && errno != EEXIST) {
        return false;
      }
      if (segmentLength + 1 >= fullPath.size()) {
        break;
      }
      lastSlash = segmentLength;
    }
    return true;
  }

  Book::EpubLoadResult ensureCacheDirectory(StringView path) {
    int rc = mkdir(ExtractedBase.data(), 0755);
    if (rc != 0 && errno != EEXIST) {
      return Book::EpubLoadResult::SdCardAccessFailed;
    }
    // [base]/epub_[name]
    size_t pathLength = ExtractedBase.size() + 6 + path.size() + 1;
    std::size_t oldBumpBack = g_bumpBack;
    char* extractedPath = subAlloc<char>(pathLength);
    if (extractedPath == nullptr) {
      return Book::EpubLoadResult::OutOfMemory;
    }
    std::memcpy(extractedPath, ExtractedBase.data(), ExtractedBase.size());
    std::memcpy(extractedPath + ExtractedBase.size(), "/epub_", 6);
    std::memcpy(extractedPath + ExtractedBase.size() + 6, path.data(), path.size());
    extractedPath[pathLength - 1] = '/';

    g_cacheDirectory = StringView { extractedPath, pathLength };
    bool ok = ensurePath(*g_cacheDirectory);
    if (!ok) {
      g_bumpBack = oldBumpBack;
      g_cacheDirectory.reset();
      return Book::EpubLoadResult::SdCardAccessFailed;
    }
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

    std::size_t oldBumpFront = g_bumpFront;
    char* buffer = bumpAlloc<char>(fileSize);
    if (!buffer) {
      std::fclose(f);
      return std::nullopt;
    }

    size_t readSize = std::fread(buffer, 1, fileSize, f);
    std::fclose(f);
    if (readSize != fileSize) {
      g_bumpFront = oldBumpFront;
      return std::nullopt;
    }

    return StringView { buffer, fileSize };
  }

  #pragma region Unpack

  /* Central directory file entry (on-disk format) */
  #pragma pack(push, 1)
  typedef struct {
    uint32_t signature;
    uint16_t version_made;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_start;
    uint16_t internal_attr;
    uint32_t external_attr;
    uint32_t local_header_offset;
  } zip_central_dir_entry;

  typedef struct {
    uint32_t signature;
    uint16_t disk_num;
    uint16_t central_dir_disk;
    uint16_t entries_this_disk;
    uint16_t total_entries;
    uint32_t central_dir_size;
    uint32_t central_dir_offset;
    uint16_t comment_len;
  } zip_end_central_dir;
  #pragma pack(pop)

  /* ZIP local file header signature */
  #define ZIP_LOCAL_HEADER_SIG 0x04034b50
  #define ZIP_CENTRAL_HEADER_SIG 0x02014b50
  #define ZIP_END_CENTRAL_SIG 0x06054b50

  #define FILE_HANDLE FILE*
  #define file_open_impl(path) fopen(path, "rb")
  #define file_close_impl(handle) fclose(handle)
  #define file_seek_impl(handle, offset, whence) fseek(handle, offset, whence)
  #define file_tell_impl(handle) ftell(handle)
  #define file_read_impl(ptr, size, count, handle) fread(ptr, size, count, handle)

  /* Find end of central directory record */
  static bool find_end_central_dir(FILE_HANDLE fp, zip_end_central_dir* eocd, size_t file_size) {
    uint8_t buf[1024];

    /* Search last 1KB for end signature */
    long search_start = (file_size > 1024) ? (file_size - 1024) : 0;
    file_seek_impl(fp, search_start, SEEK_SET);
    size_t read_size = file_read_impl(buf, 1, 1024, fp);

    /* Search backwards for signature */
    for (int i = read_size - 22; i >= 0; i--) {
      uint32_t* sig = (uint32_t*)&buf[i];
      if (*sig == ZIP_END_CENTRAL_SIG) {
        memcpy(eocd, &buf[i], sizeof(zip_end_central_dir));
        return true;
      }
    }

    return false;
  }

  Book::EpubLoadResult read_central_directory(FILE_HANDLE fp, zip_end_central_dir& eocd) {
    size_t file_count = eocd.total_entries;
    ZipFileEntry* entries = subAlloc<ZipFileEntry>(file_count);
    if (!entries) {
      return Book::EpubLoadResult::OutOfMemory;
    }

    file_seek_impl(fp, eocd.central_dir_offset, SEEK_SET);
    for (size_t i = 0; i < file_count; i++) {
      zip_central_dir_entry centry;
      size_t read_size = file_read_impl(&centry, sizeof(zip_central_dir_entry), 1, fp);
      if (read_size != 1 || centry.signature != ZIP_CENTRAL_HEADER_SIG) {
        return Book::EpubLoadResult::InvalidFormat;
      }

      char* filename = subAlloc<char>(centry.filename_len + 1);
      if (!filename) {
        return Book::EpubLoadResult::OutOfMemory;
      }
      read_size = file_read_impl(filename, 1, centry.filename_len, fp);
      if (read_size != centry.filename_len) {
        return Book::EpubLoadResult::InvalidFormat;
      }
      filename[centry.filename_len] = '\0';

      // Skip extra and comment
      file_seek_impl(fp, centry.extra_len + centry.comment_len, SEEK_CUR);

      entries[i].fileName = StringView { filename, centry.filename_len };
      entries[i].compressedSize = centry.compressed_size;
      entries[i].uncompressedSize = centry.uncompressed_size;
      entries[i].localHeaderOffset = centry.local_header_offset;
      entries[i].compressionMethod = centry.compression;
    }

    g_zipEntries = std::span(entries, file_count);

    return Book::EpubLoadResult::Success;
  }

  using UnpackWriteCallback = size_t (*)(const void* data, size_t size, size_t count, void* user_data); 
  Book::EpubLoadResult unpackFile(const ZipFileEntry& entry, UnpackWriteCallback writeCallback, void* userData) {
    if (!g_epubFile.has_value())
      return Book::EpubLoadResult::InvalidState;

    FILE* fp = *g_epubFile;
    (void)entry;
    /* Seek to local file header */
    file_seek_impl(*g_epubFile, entry.localHeaderOffset, SEEK_SET);

    /* Read local header to skip to data */
    uint32_t sig;
    uint16_t version_needed, flags, compression_method;
    file_read_impl(&sig, 4, 1, fp);
    if (sig != ZIP_LOCAL_HEADER_SIG) {
      return Book::EpubLoadResult::InvalidFormat;
    }

    file_read_impl(&version_needed, 2, 1, fp);
    file_read_impl(&flags, 2, 1, fp);
    file_read_impl(&compression_method, 2, 1, fp);

    file_seek_impl(fp, 16, SEEK_CUR);
    uint16_t filename_len, extra_len;
    file_read_impl(&filename_len, 2, 1, fp);
    file_read_impl(&extra_len, 2, 1, fp);

    file_seek_impl(fp, filename_len + extra_len, SEEK_CUR);

    FrontBumpScope scope;
    if (entry.compressionMethod == 0) {
      const size_t ChunkSize = 32 * 1024;
      auto* chunkBuffer = bumpAlloc<uint8_t>(ChunkSize);
      size_t remaining = entry.compressedSize;
      while (remaining > 0) {
        size_t toRead = (remaining > ChunkSize) ? ChunkSize : remaining;
        size_t readSize = file_read_impl(chunkBuffer, 1, toRead, fp);
        if (readSize != toRead) {
          return Book::EpubLoadResult::InvalidFormat;
        }
        size_t written = writeCallback(chunkBuffer, 1, toRead, userData);
        if (written != toRead) {
          return Book::EpubLoadResult::SdCardAccessFailed;
        }
        remaining -= toRead;
      }
    } else if (entry.compressionMethod == 8) {
      const size_t ChunkSize = 8 * 1024;
      auto* inflator = bumpAlloc<tinfl_decompressor>();
      auto* in_buf = bumpAlloc<uint8_t>(ChunkSize);
      auto* dict = bumpAlloc<uint8_t>(TINFL_LZ_DICT_SIZE);
      memset(inflator, 0, sizeof(tinfl_decompressor));
      memset(in_buf, 0, ChunkSize);
      memset(dict, 0, TINFL_LZ_DICT_SIZE);
      tinfl_init(inflator);

      size_t in_remaining = entry.compressedSize;
      size_t in_buf_size = 0;
      size_t in_buf_ofs = 0;
      size_t dict_ofs = 0;
      tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;

      while (status == TINFL_STATUS_NEEDS_MORE_INPUT || status == TINFL_STATUS_HAS_MORE_OUTPUT) {
        /* Read more compressed data if needed */
        if (in_buf_ofs >= in_buf_size && in_remaining > 0) {
          size_t to_read = (in_remaining < ChunkSize) ? in_remaining : ChunkSize;
          in_buf_size = file_read_impl(in_buf, 1, to_read, fp);
          if (in_buf_size == 0) {
            return Book::EpubLoadResult::InvalidFormat;
          }
          in_remaining -= in_buf_size;
          in_buf_ofs = 0;
        }

        size_t in_bytes = in_buf_size - in_buf_ofs;
        size_t out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;

        mz_uint32 flags = 0;
        if (in_remaining > 0) {
          flags |= TINFL_FLAG_HAS_MORE_INPUT;
        }

        status = tinfl_decompress_raw(
          inflator,
          in_buf + in_buf_ofs,
          &in_bytes,
          dict,
          dict + dict_ofs,
          &out_bytes,
          flags);

        in_buf_ofs += in_bytes;

        if (out_bytes > 0) {
          size_t cb_result = writeCallback(dict + dict_ofs, 1, out_bytes, userData);
          if (cb_result == 0) {
            return Book::EpubLoadResult::SdCardAccessFailed;
          }
          dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
        }

        if (status < TINFL_STATUS_DONE) {
          return Book::EpubLoadResult::SdCardAccessFailed;
        }
      }

    } else {
      return Book::EpubLoadResult::InvalidFormat;
    }

    return Book::EpubLoadResult::Success;
  }

  Book::EpubLoadResult parseZip() {
    if (!g_epubFile.has_value()) {
      return Book::EpubLoadResult::InvalidState;
    }
    fseeko(*g_epubFile, 0, SEEK_END);
    size_t fileSize = ftello(*g_epubFile);

    // find the central directory
    zip_end_central_dir eocd;
    if (!find_end_central_dir(*g_epubFile, &eocd, fileSize)) {
      return Book::EpubLoadResult::InvalidFormat;
    }

    Book::EpubLoadResult result = read_central_directory(*g_epubFile, eocd);
    if (result != Book::EpubLoadResult::Success) {
      return result;
    }

    auto &entries = *g_zipEntries;
    printf("Found %lu files in EPUB:\n", entries.size());

    return Book::EpubLoadResult::Success;
  }

  Book::EpubLoadResult unpackFully() {
    if (!g_epubFile.has_value() || !g_zipEntries.has_value()) {
      return Book::EpubLoadResult::InvalidState;
    }

    Book::EpubLoadResult result = Book::EpubLoadResult::Success;

    auto& entries = *g_zipEntries;
    for (const auto& entry : entries) {
      result = unpackEntry(entry);
      if (result != Book::EpubLoadResult::Success) {
        return result;
      }
    }

    return Book::EpubLoadResult::Success;
  }

  Book::EpubLoadResult unpackEntry(const ZipFileEntry& entry) {
    if (!g_cacheDirectory.has_value()) {
      return Book::EpubLoadResult::InvalidState;
    }

    char pathBuffer[512] = {};
    size_t pathSize = g_cacheDirectory->size() + entry.fileName.size();
    if (pathSize > sizeof(pathBuffer)) {
      return Book::EpubLoadResult::InvalidFormat;
    }
    std::memcpy(pathBuffer, g_cacheDirectory->data(), g_cacheDirectory->size());
    std::memcpy(pathBuffer + g_cacheDirectory->size(), entry.fileName.data(), entry.fileName.size());
    pathBuffer[pathSize] = '\0';
    StringView fullPath { pathBuffer, pathSize };
    bool ok = ensurePath(fullPath);
    if (!ok) {
      return Book::EpubLoadResult::SdCardAccessFailed;
    }

    if (entry.fileName.endsWith("/")) {
      return Book::EpubLoadResult::Success;
    }

    printf("Extracting to: ");
    fwrite(fullPath.data(), 1, fullPath.size(), stdout);
    printf("\n");

    FILE* targetFile = fopen(fullPath.data(), "wb");
    if (!targetFile) {
      return Book::EpubLoadResult::SdCardAccessFailed;
    }

    auto result = unpackFile(entry, (UnpackWriteCallback)&fwrite, (void*)targetFile);
    fclose(targetFile);
    return result;
  }

  Result<std::span<std::byte>> loadTempEntry(const ZipFileEntry& entry) {
    printf("Loading temp entry: ");
    fwrite(entry.fileName.data(), 1, entry.fileName.size(), stdout);
    printf("\n");
    if (!g_epubFile.has_value()) {
      return std::unexpected(Book::EpubLoadResult::InvalidState);
    }

    std::byte* buffer = bumpAlloc<std::byte>(entry.uncompressedSize);
    if (!buffer) {
      return std::unexpected(Book::EpubLoadResult::OutOfMemory);
    }

    auto writeCallback = [](const void* data, size_t size, size_t count, void* userData) -> size_t {
      auto* context = (std::byte**)userData;
      std::memcpy(*context, data, size * count);
      *context += size * count;
      return size * count;
    };
    std::byte* writePtr = buffer;
    Book::EpubLoadResult result = unpackFile(entry, writeCallback, (void*)&writePtr);
    if (result != Book::EpubLoadResult::Success) {
      return std::unexpected(result);
    }

    return std::span<std::byte>(buffer, entry.uncompressedSize);
  }

  Result<std::span<std::byte>> loadTempEntry(StringView path) {
    auto entry = findFileEntry(path);
    if (!entry.has_value()) {
      return std::unexpected(entry.error());
    }
    return loadTempEntry(*entry);
  }

  Result<ZipFileEntry> findFileEntry(StringView path) {
    if (!g_zipEntries.has_value()) {
      return std::unexpected(Book::EpubLoadResult::InvalidState);
    }

    auto& entries = *g_zipEntries;
    for (const auto& entry : entries) {
      if (entry.fileName == path) {
        return entry;
      }
    }

    return std::unexpected(Book::EpubLoadResult::MissingFile);
  }

  bool fileExists(StringView path) {
    return findFileEntry(path).has_value();
  }

  #pragma endregion Unpack

  #pragma region Content
  #pragma endregion Content

  #pragma region TOC

  void parseToc(
    StringView tocView,
    Book::TocEntry* entries,
    char* buffer,
    size_t& tocs,
    size_t& neededMemory)
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
        if (buffer && entries) {
          printf("TOC Entry: ");
          fwrite(currentTocText.data(), 1, currentTocText.size(), stdout);
          printf(" -> ");
          fwrite(currentTocSrc.data(), 1, currentTocSrc.size(), stdout);
          printf("\n");
          auto entry = &entries[tocs];
          std::memcpy(buffer, currentTocText.data(), currentTocText.size());
          entry->label = StringView(buffer, currentTocText.size());
          buffer += currentTocText.size();
          std::memcpy(buffer, currentTocSrc.data(), currentTocSrc.size());
          entry->src = StringView(buffer, currentTocSrc.size());
          buffer += currentTocSrc.size();
        }
        tocs++;
        neededMemory += currentTocSrc.size();
        neededMemory += currentTocText.size();
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
    size_t neededMemory = 0;
    parseToc(str, nullptr, nullptr, entryCount, neededMemory);

    printf("Expected TOC entries: %zu, needed memory: %zu bytes\n", entryCount, neededMemory);

    char *stringBuffer = subAlloc<char>(neededMemory);
    Book::TocEntry* tocEntries = subAlloc<Book::TocEntry>(entryCount);
    entryCount = 0;
    neededMemory = 0;
    parseToc(str, tocEntries, stringBuffer, entryCount, neededMemory);

    fwrite(stringBuffer, 1, neededMemory, stdout);
    printf("\n");

    g_toc = std::span(tocEntries, entryCount);

    return Book::EpubLoadResult::Success;
  }

  #pragma endregion TOC
}
