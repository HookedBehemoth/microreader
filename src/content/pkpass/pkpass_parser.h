/*
 * pkpass_parser.h - Apple Wallet PKPass parser
 *
 * Parser for .pkpass files (Apple Wallet passes)
 * Uses zip_reader for underlying ZIP operations
 */

#ifndef PKPASS_PARSER_H
#define PKPASS_PARSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
typedef enum {
  PKPASS_OK = 0,
  PKPASS_ERROR_FILE_NOT_FOUND = 1,
  PKPASS_ERROR_NOT_A_PKPASS = 2,
  PKPASS_ERROR_CORRUPTED = 3,
  PKPASS_ERROR_OUT_OF_MEMORY = 4,
  PKPASS_ERROR_INVALID_PARAM = 5,
  PKPASS_ERROR_PARSE_FAILED = 6,
  PKPASS_ERROR_NO_BARCODE = 7,
  PKPASS_ERROR_EXTRACTION_FAILED = 8
} pkpass_error;

/* Forward declaration */
typedef struct pkpass_reader pkpass_reader;

/* Barcode information structure */
typedef struct {
  char format[32];      /* e.g., "PKBarcodeFormatQR" */
  char message[512];    /* Barcode data/message */
  char encoding[32];    /* e.g., "iso-8859-1" */
  char alt_text[128];   /* Alternative text */
} pkpass_barcode_info;

/* Pass metadata structure */
typedef struct {
  char description[128];
  char organization_name[128];
  char pass_type_id[128];
  char serial_number[128];
  char team_id[64];
  char format_version[16];
  int has_barcode;
  pkpass_barcode_info barcode;
} pkpass_info;

/**
 * Open a .pkpass file for reading
 * @param filepath Path to the .pkpass file
 * @param out_reader Pointer to receive reader handle
 * @return PKPASS_OK on success, error code otherwise
 */
pkpass_error pkpass_open(const char* filepath, pkpass_reader** out_reader);

/**
 * Close a .pkpass file and free resources
 * @param reader Reader handle to close
 */
void pkpass_close(pkpass_reader* reader);

/**
 * Get pass information including barcode data
 * @param reader Reader handle
 * @param info Pointer to receive pass information
 * @return PKPASS_OK on success, error code otherwise
 */
pkpass_error pkpass_get_info(pkpass_reader* reader, pkpass_info* info);

/**
 * Extract a file from the pass (e.g., icon.png, logo.png)
 * @param reader Reader handle
 * @param filename Name of file to extract (e.g., "icon.png")
 * @param buffer Buffer to receive file data
 * @param buffer_size Size of buffer
 * @param out_size Pointer to receive actual file size
 * @return PKPASS_OK on success, error code otherwise
 */
pkpass_error pkpass_extract_file(pkpass_reader* reader, const char* filename, 
                                  void* buffer, size_t buffer_size, size_t* out_size);

/**
 * Get human-readable error string
 * @param error Error code
 * @return Error description string
 */
const char* pkpass_get_error_string(pkpass_error error);

#ifdef __cplusplus
}
#endif

#endif /* PKPASS_PARSER_H */
