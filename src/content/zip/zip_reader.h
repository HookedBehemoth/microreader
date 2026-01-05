/*
 * zip_reader.h - Generic ZIP archive reader
 *
 * Minimal ZIP reader for embedded systems with streaming support.
 * Uses tinfl (DEFLATE decompressor) directly.
 */

#ifndef ZIP_READER_H
#define ZIP_READER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
typedef enum {
  ZIP_OK = 0,
  ZIP_ERROR_FILE_NOT_FOUND = 1,
  ZIP_ERROR_NOT_A_ZIP = 2,
  ZIP_ERROR_CORRUPTED = 3,
  ZIP_ERROR_OUT_OF_MEMORY = 4,
  ZIP_ERROR_INVALID_PARAM = 5,
  ZIP_ERROR_EXTRACTION_FAILED = 6,
  ZIP_ERROR_FILE_NOT_IN_ARCHIVE = 7
} zip_error;

/* Forward declarations */
typedef struct zip_reader zip_reader;
typedef struct zip_stream_context zip_stream_context;

/* File information structure */
typedef struct {
  char filename[256];
  uint64_t compressed_size;
  uint64_t uncompressed_size;
  uint64_t file_offset;
  uint16_t compression; /* 0 = stored, 8 = DEFLATE */
} zip_file_info;

/* Callback for streaming extraction */
typedef int (*zip_data_callback)(const void* data, size_t size, void* user_data);

/* -------------------- Basic API -------------------- */

/**
 * Open a ZIP archive for reading
 * @param filepath Path to the ZIP file
 * @param out_reader Pointer to receive reader handle
 * @return ZIP_OK on success, error code otherwise
 */
zip_error zip_open(const char* filepath, zip_reader** out_reader);

/**
 * Close a ZIP archive and free resources
 * @param reader Reader handle to close
 */
void zip_close(zip_reader* reader);

/**
 * Get the number of files in the archive
 * @param reader Reader handle
 * @return Number of files, or 0 if reader is NULL
 */
uint32_t zip_get_file_count(zip_reader* reader);

/**
 * Get information about a file in the archive
 * @param reader Reader handle
 * @param index File index (0-based)
 * @param info Pointer to receive file information
 * @return ZIP_OK on success, error code otherwise
 */
zip_error zip_get_file_info(zip_reader* reader, uint32_t index, zip_file_info* info);

/**
 * Locate a file by name in the archive
 * @param reader Reader handle
 * @param filename Name of the file to find
 * @param out_index Pointer to receive file index
 * @return ZIP_OK on success, ZIP_ERROR_FILE_NOT_IN_ARCHIVE if not found
 */
zip_error zip_locate_file(zip_reader* reader, const char* filename, uint32_t* out_index);

/* -------------------- Push-based Streaming API -------------------- */

/**
 * Extract a file using a callback (push-based streaming)
 * @param reader Reader handle
 * @param file_index Index of file to extract
 * @param callback Function to receive decompressed data chunks
 * @param user_data User data passed to callback
 * @param chunk_size Size of chunks to decompress (0 = default 8KB)
 * @return ZIP_OK on success, error code otherwise
 */
zip_error zip_extract_streaming(zip_reader* reader, uint32_t file_index, 
                                 zip_data_callback callback, void* user_data, 
                                 size_t chunk_size);

/* -------------------- Pull-based Streaming API -------------------- */

/**
 * Start streaming extraction of a file (pull-based)
 * @param reader Reader handle
 * @param file_index Index of file to extract
 * @param chunk_size Size of chunks to process (0 = default 8KB)
 * @return Stream context handle, or NULL on error
 */
zip_stream_context* zip_start_streaming(zip_reader* reader, uint32_t file_index, size_t chunk_size);

/**
 * Read decompressed data from stream
 * @param ctx Stream context
 * @param buffer Buffer to receive data
 * @param max_size Maximum bytes to read
 * @return Number of bytes read, 0 on EOF, -1 on error
 */
int zip_read_chunk(zip_stream_context* ctx, void* buffer, size_t max_size);

/**
 * End streaming and free resources
 * @param ctx Stream context to close
 */
void zip_end_streaming(zip_stream_context* ctx);

/* -------------------- Utility Functions -------------------- */

/**
 * Get human-readable error string
 * @param error Error code
 * @return Error description string
 */
const char* zip_get_error_string(zip_error error);

#ifdef __cplusplus
}
#endif

#endif /* ZIP_READER_H */
