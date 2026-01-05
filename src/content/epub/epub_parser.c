/*
 * epub_parser.c - Minimal EPUB parser implementation
 *
 * Now uses the generic zip_reader for ZIP operations.
 */

#include "epub_parser.h"
#include "../zip/zip_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EPUB reader structure - wraps zip_reader */
struct epub_reader {
  zip_reader* zip;
  epub_error last_error;
};

/* Pull-based streaming context - wraps zip_stream_context */
struct epub_stream_context {
  zip_stream_context* zip_ctx;
};

/* Convert zip_error to epub_error */
static epub_error convert_zip_error(zip_error err) {
  switch (err) {
    case ZIP_OK:
      return EPUB_OK;
    case ZIP_ERROR_FILE_NOT_FOUND:
      return EPUB_ERROR_FILE_NOT_FOUND;
    case ZIP_ERROR_NOT_A_ZIP:
      return EPUB_ERROR_NOT_AN_EPUB;
    case ZIP_ERROR_CORRUPTED:
      return EPUB_ERROR_CORRUPTED;
    case ZIP_ERROR_OUT_OF_MEMORY:
      return EPUB_ERROR_OUT_OF_MEMORY;
    case ZIP_ERROR_INVALID_PARAM:
      return EPUB_ERROR_INVALID_PARAM;
    case ZIP_ERROR_EXTRACTION_FAILED:
      return EPUB_ERROR_EXTRACTION_FAILED;
    case ZIP_ERROR_FILE_NOT_IN_ARCHIVE:
      return EPUB_ERROR_FILE_NOT_IN_ARCHIVE;
    default:
      return EPUB_ERROR_CORRUPTED;
  }
}

/* -------------------- Public API -------------------- */

epub_error epub_open(const char* filepath, epub_reader** out_reader) {
  if (!filepath || !out_reader) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  epub_reader* reader = (epub_reader*)calloc(1, sizeof(epub_reader));
  if (!reader) {
    return EPUB_ERROR_OUT_OF_MEMORY;
  }

  /* Open as ZIP file using zip_reader */
  zip_error zip_err = zip_open(filepath, &reader->zip);
  if (zip_err != ZIP_OK) {
    free(reader);
    return convert_zip_error(zip_err);
  }

  *out_reader = reader;
  return EPUB_OK;
}

void epub_close(epub_reader* reader) {
  if (reader) {
    if (reader->zip) {
      zip_close(reader->zip);
    }
    free(reader);
  }
}

uint32_t epub_get_file_count(epub_reader* reader) {
  return reader ? zip_get_file_count(reader->zip) : 0;
}

epub_error epub_get_file_info(epub_reader* reader, uint32_t index, epub_file_info* info) {
  if (!reader || !info) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  zip_file_info zip_info;
  zip_error zip_err = zip_get_file_info(reader->zip, index, &zip_info);
  if (zip_err != ZIP_OK) {
    return convert_zip_error(zip_err);
  }

  /* Copy zip_file_info to epub_file_info */
  strncpy(info->filename, zip_info.filename, sizeof(info->filename) - 1);
  info->filename[sizeof(info->filename) - 1] = '\0';
  info->compressed_size = zip_info.compressed_size;
  info->uncompressed_size = zip_info.uncompressed_size;
  info->file_offset = zip_info.file_offset;
  info->compression = zip_info.compression;

  return EPUB_OK;
}

epub_error epub_locate_file(epub_reader* reader, const char* filename, uint32_t* out_index) {
  if (!reader || !filename || !out_index) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  zip_error zip_err = zip_locate_file(reader->zip, filename, out_index);
  return convert_zip_error(zip_err);
}

epub_error epub_extract_streaming(epub_reader* reader, uint32_t file_index, epub_data_callback callback,
                                  void* user_data, size_t chunk_size) {
  if (!reader || !callback) {
    return EPUB_ERROR_INVALID_PARAM;
  }

  /* Cast epub callback to zip callback (they have the same signature) */
  zip_data_callback zip_callback = (zip_data_callback)callback;
  zip_error zip_err = zip_extract_streaming(reader->zip, file_index, zip_callback, user_data, chunk_size);
  return convert_zip_error(zip_err);
}

/* -------------------- Pull-based Streaming API -------------------- */

epub_stream_context* epub_start_streaming(epub_reader* reader, uint32_t file_index, size_t chunk_size) {
  if (!reader) {
    return NULL;
  }

  epub_stream_context* ctx = (epub_stream_context*)calloc(1, sizeof(epub_stream_context));
  if (!ctx) {
    return NULL;
  }

  ctx->zip_ctx = zip_start_streaming(reader->zip, file_index, chunk_size);
  if (!ctx->zip_ctx) {
    free(ctx);
    return NULL;
  }

  return ctx;
}

int epub_read_chunk(epub_stream_context* ctx, void* buffer, size_t max_size) {
  if (!ctx || !ctx->zip_ctx) {
    return -1;
  }

  return zip_read_chunk(ctx->zip_ctx, buffer, max_size);
}

void epub_end_streaming(epub_stream_context* ctx) {
  if (ctx) {
    if (ctx->zip_ctx) {
      zip_end_streaming(ctx->zip_ctx);
    }
    free(ctx);
  }
}

const char* epub_get_error_string(epub_error error) {
  switch (error) {
    case EPUB_OK:
      return "Success";
    case EPUB_ERROR_FILE_NOT_FOUND:
      return "File not found";
    case EPUB_ERROR_NOT_AN_EPUB:
      return "Not a valid EPUB/ZIP file";
    case EPUB_ERROR_CORRUPTED:
      return "File is corrupted";
    case EPUB_ERROR_OUT_OF_MEMORY:
      return "Out of memory";
    case EPUB_ERROR_INVALID_PARAM:
      return "Invalid parameter";
    case EPUB_ERROR_EXTRACTION_FAILED:
      return "Extraction failed";
    case EPUB_ERROR_FILE_NOT_IN_ARCHIVE:
      return "File not found in archive";
    default:
      return "Unknown error";
  }
}
