/*
 * pkpass_parser.cpp - Apple Wallet PKPass parser implementation
 *
 * Parser for .pkpass files using zip_reader for ZIP operations
 */

#include "pkpass_parser.h"
#include "../zip/zip_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ARDUINO
#include <ArduinoJson.h>
#else
/* For non-Arduino builds, you'd need a JSON parser like cJSON or similar */
#error "Non-Arduino builds require a JSON parser implementation"
#endif

/* PKPass reader structure */
struct pkpass_reader {
  zip_reader* zip;
  pkpass_info info;
  int info_loaded;
};

/* Helper to extract pass.json and parse it */
static pkpass_error load_pass_info(pkpass_reader* reader) {
  if (reader->info_loaded) {
    return PKPASS_OK;
  }

  /* Locate pass.json */
  uint32_t file_index;
  zip_error zip_err = zip_locate_file(reader->zip, "pass.json", &file_index);
  if (zip_err != ZIP_OK) {
    return PKPASS_ERROR_NOT_A_PKPASS;
  }

  /* Get file info to check size */
  zip_file_info file_info;
  zip_err = zip_get_file_info(reader->zip, file_index, &file_info);
  if (zip_err != ZIP_OK) {
    return PKPASS_ERROR_CORRUPTED;
  }

  /* Allocate buffer for pass.json (max 10KB) */
  const size_t MAX_JSON_SIZE = 10 * 1024;
  if (file_info.uncompressed_size > MAX_JSON_SIZE) {
    return PKPASS_ERROR_PARSE_FAILED;
  }

  char* json_buffer = (char*)malloc(file_info.uncompressed_size + 1);
  if (!json_buffer) {
    return PKPASS_ERROR_OUT_OF_MEMORY;
  }

  /* Extract pass.json to buffer */
  zip_stream_context* ctx = zip_start_streaming(reader->zip, file_index, 0);
  if (!ctx) {
    free(json_buffer);
    return PKPASS_ERROR_EXTRACTION_FAILED;
  }

  size_t total_read = 0;
  int bytes_read;
  while ((bytes_read = zip_read_chunk(ctx, json_buffer + total_read, 
                                       file_info.uncompressed_size - total_read)) > 0) {
    total_read += bytes_read;
  }
  zip_end_streaming(ctx);

  if (bytes_read < 0 || total_read != file_info.uncompressed_size) {
    free(json_buffer);
    return PKPASS_ERROR_EXTRACTION_FAILED;
  }

  json_buffer[total_read] = '\0';

#ifdef ARDUINO
  /* Parse JSON using ArduinoJson */
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json_buffer, total_read);
  free(json_buffer);

  if (err) {
    return PKPASS_ERROR_PARSE_FAILED;
  }

  /* Extract metadata */
  memset(&reader->info, 0, sizeof(pkpass_info));

  const char* desc = doc["description"];
  if (desc) {
    strncpy(reader->info.description, desc, sizeof(reader->info.description) - 1);
  }

  const char* org = doc["organizationName"];
  if (org) {
    strncpy(reader->info.organization_name, org, sizeof(reader->info.organization_name) - 1);
  }

  const char* pass_type = doc["passTypeIdentifier"];
  if (pass_type) {
    strncpy(reader->info.pass_type_id, pass_type, sizeof(reader->info.pass_type_id) - 1);
  }

  const char* serial = doc["serialNumber"];
  if (serial) {
    strncpy(reader->info.serial_number, serial, sizeof(reader->info.serial_number) - 1);
  }

  const char* team = doc["teamIdentifier"];
  if (team) {
    strncpy(reader->info.team_id, team, sizeof(reader->info.team_id) - 1);
  }

  int format_ver = doc["formatVersion"] | 1;
  snprintf(reader->info.format_version, sizeof(reader->info.format_version), "%d", format_ver);

  /* Extract barcode information */
  JsonVariant barcode = doc["barcode"];
  if (!barcode.isNull()) {
    reader->info.has_barcode = 1;

    const char* format = barcode["format"];
    if (format) {
      strncpy(reader->info.barcode.format, format, sizeof(reader->info.barcode.format) - 1);
    }

    const char* message = barcode["message"];
    if (message) {
      strncpy(reader->info.barcode.message, message, sizeof(reader->info.barcode.message) - 1);
    }

    const char* encoding = barcode["messageEncoding"];
    if (encoding) {
      strncpy(reader->info.barcode.encoding, encoding, sizeof(reader->info.barcode.encoding) - 1);
    }

    const char* alt_text = barcode["altText"];
    if (alt_text) {
      strncpy(reader->info.barcode.alt_text, alt_text, sizeof(reader->info.barcode.alt_text) - 1);
    }
  } else {
    reader->info.has_barcode = 0;
  }

  reader->info_loaded = 1;
  return PKPASS_OK;
#else
  free(json_buffer);
  return PKPASS_ERROR_PARSE_FAILED;
#endif
}

pkpass_error pkpass_open(const char* filepath, pkpass_reader** out_reader) {
  if (!filepath || !out_reader) {
    return PKPASS_ERROR_INVALID_PARAM;
  }

  pkpass_reader* reader = (pkpass_reader*)calloc(1, sizeof(pkpass_reader));
  if (!reader) {
    return PKPASS_ERROR_OUT_OF_MEMORY;
  }

  /* Open as ZIP file */
  zip_error zip_err = zip_open(filepath, &reader->zip);
  if (zip_err != ZIP_OK) {
    free(reader);
    switch (zip_err) {
      case ZIP_ERROR_FILE_NOT_FOUND:
        return PKPASS_ERROR_FILE_NOT_FOUND;
      case ZIP_ERROR_NOT_A_ZIP:
        return PKPASS_ERROR_NOT_A_PKPASS;
      case ZIP_ERROR_OUT_OF_MEMORY:
        return PKPASS_ERROR_OUT_OF_MEMORY;
      default:
        return PKPASS_ERROR_CORRUPTED;
    }
  }

  reader->info_loaded = 0;
  *out_reader = reader;
  return PKPASS_OK;
}

void pkpass_close(pkpass_reader* reader) {
  if (reader) {
    if (reader->zip) {
      zip_close(reader->zip);
    }
    free(reader);
  }
}

pkpass_error pkpass_get_info(pkpass_reader* reader, pkpass_info* info) {
  if (!reader || !info) {
    return PKPASS_ERROR_INVALID_PARAM;
  }

  /* Load pass.json if not already loaded */
  pkpass_error err = load_pass_info(reader);
  if (err != PKPASS_OK) {
    return err;
  }

  /* Copy info to output */
  memcpy(info, &reader->info, sizeof(pkpass_info));
  return PKPASS_OK;
}

pkpass_error pkpass_extract_file(pkpass_reader* reader, const char* filename, 
                                  void* buffer, size_t buffer_size, size_t* out_size) {
  if (!reader || !filename || !buffer || !out_size) {
    return PKPASS_ERROR_INVALID_PARAM;
  }

  /* Locate file in ZIP */
  uint32_t file_index;
  zip_error zip_err = zip_locate_file(reader->zip, filename, &file_index);
  if (zip_err != ZIP_OK) {
    return PKPASS_ERROR_FILE_NOT_FOUND;
  }

  /* Get file info */
  zip_file_info file_info;
  zip_err = zip_get_file_info(reader->zip, file_index, &file_info);
  if (zip_err != ZIP_OK) {
    return PKPASS_ERROR_CORRUPTED;
  }

  /* Check buffer size */
  if (file_info.uncompressed_size > buffer_size) {
    *out_size = file_info.uncompressed_size;
    return PKPASS_ERROR_OUT_OF_MEMORY;
  }

  /* Extract file */
  zip_stream_context* ctx = zip_start_streaming(reader->zip, file_index, 0);
  if (!ctx) {
    return PKPASS_ERROR_EXTRACTION_FAILED;
  }

  size_t total_read = 0;
  int bytes_read;
  while ((bytes_read = zip_read_chunk(ctx, (uint8_t*)buffer + total_read, 
                                       buffer_size - total_read)) > 0) {
    total_read += bytes_read;
  }
  zip_end_streaming(ctx);

  if (bytes_read < 0) {
    return PKPASS_ERROR_EXTRACTION_FAILED;
  }

  *out_size = total_read;
  return PKPASS_OK;
}

const char* pkpass_get_error_string(pkpass_error error) {
  switch (error) {
    case PKPASS_OK:
      return "Success";
    case PKPASS_ERROR_FILE_NOT_FOUND:
      return "PKPass file not found";
    case PKPASS_ERROR_NOT_A_PKPASS:
      return "Not a valid PKPass file";
    case PKPASS_ERROR_CORRUPTED:
      return "PKPass file is corrupted";
    case PKPASS_ERROR_OUT_OF_MEMORY:
      return "Out of memory";
    case PKPASS_ERROR_INVALID_PARAM:
      return "Invalid parameter";
    case PKPASS_ERROR_PARSE_FAILED:
      return "Failed to parse pass.json";
    case PKPASS_ERROR_NO_BARCODE:
      return "No barcode found in pass";
    default:
      return "Unknown error";
  }
}
