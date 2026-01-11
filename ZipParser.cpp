#include "ZipParser.hpp"

#include <stddef.h>
#include <stdint.h>
#include "Fs.hpp"
#include "miniz.h"
#include "log.h"

#include "Allocator.hpp"

namespace zip {

namespace {

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
static bool find_end_central_dir(
  FILE_HANDLE fp, size_t file_size, zip_end_central_dir* eocd
) {
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

Result<std::span<ZipFileEntry>> read_central_directory(
  FILE_HANDLE fp, zip_end_central_dir& eocd,
  mem::Allocator& allocator
) {
  size_t file_count = eocd.total_entries;
  ZipFileEntry* entries = allocator.subAlloc<ZipFileEntry>(file_count);
  if (!entries) {
    return std::unexpected(ZipError::OutOfMemory);
  }

  file_seek_impl(fp, eocd.central_dir_offset, SEEK_SET);
  for (size_t i = 0; i < file_count; i++) {
    zip_central_dir_entry centry;
    size_t read_size = file_read_impl(&centry, sizeof(zip_central_dir_entry), 1, fp);
    if (read_size != 1 || centry.signature != ZIP_CENTRAL_HEADER_SIG) {
      return std::unexpected(ZipError::InvalidFormat);
    }

    char* filename = allocator.subAlloc<char>(centry.filename_len + 1);
    if (!filename) {
      return std::unexpected(ZipError::OutOfMemory);
    }
    read_size = file_read_impl(filename, 1, centry.filename_len, fp);
    if (read_size != centry.filename_len) {
      return std::unexpected(ZipError::InvalidFormat);
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

  printf("Found %lu entries in ZIP:\n", file_count);

  return std::span { entries, file_count };
}

Result<void> unpackFile(
  FILE* fp, const ZipFileEntry& entry,
  UnpackWriteCallback writeCallback, void* userData,
  mem::Allocator& g_allocator
) {
  /* Seek to local file header */
  file_seek_impl(fp, entry.localHeaderOffset, SEEK_SET);

  /* Read local header to skip to data */
  uint32_t sig;
  uint16_t version_needed, flags, compression_method;
  file_read_impl(&sig, 4, 1, fp);
  if (sig != ZIP_LOCAL_HEADER_SIG) {
    return std::unexpected(ZipError::InvalidFormat);
  }

  file_read_impl(&version_needed, 2, 1, fp);
  file_read_impl(&flags, 2, 1, fp);
  file_read_impl(&compression_method, 2, 1, fp);

  file_seek_impl(fp, 16, SEEK_CUR);
  uint16_t filename_len, extra_len;
  file_read_impl(&filename_len, 2, 1, fp);
  file_read_impl(&extra_len, 2, 1, fp);

  file_seek_impl(fp, filename_len + extra_len, SEEK_CUR);

  auto frontBumpScope = g_allocator.beginFrontScope();
  if (entry.compressionMethod == 0) {
    const size_t ChunkSize = 32 * 1024;
    auto* chunkBuffer = g_allocator.bumpAlloc<uint8_t>(ChunkSize);
    size_t remaining = entry.compressedSize;
    while (remaining > 0) {
      size_t toRead = (remaining > ChunkSize) ? ChunkSize : remaining;
      size_t readSize = file_read_impl(chunkBuffer, 1, toRead, fp);
      if (readSize != toRead) {
        return std::unexpected(ZipError::IoFailure);
      }
      size_t written = writeCallback(chunkBuffer, 1, toRead, userData);
      if (written != toRead) {
        return std::unexpected(ZipError::IoFailure);
      }
      remaining -= toRead;
    }
  } else if (entry.compressionMethod == 8) {
    const size_t ChunkSize = 8 * 1024;
    auto* inflator = g_allocator.bumpAlloc<tinfl_decompressor>();
    auto* in_buf = g_allocator.bumpAlloc<uint8_t>(ChunkSize);
    auto* dict = g_allocator.bumpAlloc<uint8_t>(TINFL_LZ_DICT_SIZE);
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
          return std::unexpected(ZipError::IoFailure);
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
          return std::unexpected(ZipError::IoFailure);
        }
        dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
      }

      if (status < TINFL_STATUS_DONE) {
        return std::unexpected(ZipError::InvalidFormat);
      }
    }
  } else {
    return std::unexpected(ZipError::InvalidFormat);
  }

  return {};
}

}

Result<std::span<ZipFileEntry>> parseZip(FILE* fp, mem::Allocator& allocator) {
  fseeko(fp, 0, SEEK_END);
  size_t fileSize = ftello(fp);

  // find the central directory
  zip_end_central_dir eocd;
  if (!find_end_central_dir(fp, fileSize, &eocd)) {
    return std::unexpected(ZipError::InvalidFormat);
  }

  return read_central_directory(fp, eocd, allocator);
}

Result<void> unpackEntry(
  FILE* fp, const ZipFileEntry& entry,
  const StringView target,
  mem::Allocator& allocator
) {
  char pathBuffer[512] = {};
  size_t pathSize = target.size() + entry.fileName.size();
  if (pathSize > sizeof(pathBuffer)) {
    return std::unexpected(ZipError::InvalidFormat);
  }
  std::memcpy(pathBuffer, target.data(), target.size());
  std::memcpy(pathBuffer + target.size(), entry.fileName.data(), entry.fileName.size());
  pathBuffer[pathSize] = '\0';
  StringView fullPath { pathBuffer, pathSize };
  bool ok = fs::ensurePath(fullPath);
  if (!ok) {
    return std::unexpected(ZipError::IoFailure);
  }

  if (entry.fileName.endsWith("/")) {
    return {};
  }

  println("Extracting to: ", fullPath);

  FILE* targetFile = fopen(fullPath.data(), "wb");
  if (!targetFile) {
    return std::unexpected(ZipError::IoFailure);
  }

  auto result = unpackFile(
    fp, entry,
    (UnpackWriteCallback)&fwrite, (void*)targetFile,
    allocator);
  fclose(targetFile);
  return result;
}

Result<void> unpackFully(
  FILE* fp, std::span<ZipFileEntry> entries,
  StringView target,
  mem::Allocator& allocator
) {
  for (const auto& entry : entries) {
    auto result = unpackEntry(fp, entry, target, allocator);
    if (!result.has_value()) {
      return result;
    }
  }

  return {};
}

Result<std::span<std::byte>> loadTempEntry(
  FILE* fp, const ZipFileEntry& entry,
  mem::Allocator& allocator
) {
  std::byte* buffer = allocator.bumpAlloc<std::byte>(entry.uncompressedSize);
  if (!buffer) {
    return std::unexpected(ZipError::OutOfMemory);
  }

  auto writeCallback = [](const void* data, size_t size, size_t count, void* userData) -> size_t {
    auto* context = (std::byte**)userData;
    std::memcpy(*context, data, size * count);
    *context += size * count;
    return size * count;
  };
  std::byte* writePtr = buffer;
  auto result = unpackFile(fp, entry, writeCallback, (void*)&writePtr, allocator);
  if (!result.has_value()) {
    return std::unexpected(result.error());
  }

  return std::span<std::byte>(buffer, entry.uncompressedSize);
}

Result<std::span<std::byte>> loadTempEntry(
  FILE* fp, std::span<ZipFileEntry> entries, StringView path,
  mem::Allocator& allocator
) {
  auto entry = findFileEntry(entries, path);
  if (!entry.has_value()) {
    return std::unexpected(entry.error());
  }
  return loadTempEntry(fp, *entry, allocator);
}

Result<ZipFileEntry> findFileEntry(std::span<ZipFileEntry> entries, StringView path) {
  for (const auto& entry : entries) {
    if (entry.fileName.caseCmp(path)) {
      return entry;
    }
  }

  return std::unexpected(ZipError::MissingFile);
}

bool fileExists(std::span<ZipFileEntry> entries, StringView path) {
  return findFileEntry(entries, path).has_value();
}

}
