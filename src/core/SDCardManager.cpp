#include "SDCardManager.h"

#include <SD.h>
#include <SPI.h>

#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)

#define SD_SPI_MISO 7
#define SD_SPI_CS 12  // SD Card Chip Select

SDCardManager::SDCardManager()
    : initialized(false) {}

bool SDCardManager::begin() {
  pinMode(SD_SPI_CS, OUTPUT);
  digitalWrite(SD_SPI_CS, HIGH);

  SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, SD_SPI_CS);
  if (!SD.begin(SD_SPI_CS, SPI, 40000000)) {
    Serial.print("\n SD card not detected\n");
    initialized = false;
  } else {
    Serial.print("\n SD card detected\n");
    initialized = true;
  }

  return initialized;
}

bool SDCardManager::ready() const {
  return initialized;
}

std::vector<String> SDCardManager::listFiles(const char* path, int maxFiles) {
  std::vector<String> ret;
  if (!initialized) {
    Serial.println("SDCardManager: not initialized, returning empty list");
    return ret;
  }

  File root = SD.open(path);
  if (!root) {
    Serial.println("Failed to open directory.");
    return ret;
  }
  if (!root.isDirectory()) {
    Serial.println("Path is not a directory.");
    root.close();
    return ret;
  }

  int count = 0;
  for (File f = root.openNextFile(); f && count < maxFiles; f = root.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    ret.push_back(String(f.name()));
    f.close();
    count++;
  }
  root.close();
  return ret;
}

String SDCardManager::readFile(const char* path) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    return String("");
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    return String("");
  }

  String content = "";
  size_t maxSize = 50000;  // Limit to 50KB
  size_t readSize = 0;
  while (f.available() && readSize < maxSize) {
    char c = (char)f.read();
    content += c;
    readSize++;
  }
  f.close();
  return content;
}

bool SDCardManager::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    return false;
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    return false;
  }

  const size_t localBufSize = 256;
  uint8_t buf[localBufSize];
  size_t toRead = (chunkSize == 0) ? localBufSize : (chunkSize < localBufSize ? chunkSize : localBufSize);

  while (f.available()) {
    int r = f.read(buf, toRead);
    if (r > 0) {
      out.write(buf, (size_t)r);
    } else {
      break;
    }
  }

  f.close();
  return true;
}

size_t SDCardManager::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  if (!buffer || bufferSize == 0)
    return 0;
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot read file");
    buffer[0] = '\0';
    return 0;
  }

  File f = SD.open(path);
  if (!f) {
    Serial.printf("Failed to open file: %s\n", path);
    buffer[0] = '\0';
    return 0;
  }

  size_t maxToRead = (maxBytes == 0) ? (bufferSize - 1) : min(maxBytes, bufferSize - 1);
  size_t total = 0;
  const size_t chunk = 64;

  while (f.available() && total < maxToRead) {
    size_t want = maxToRead - total;
    size_t readLen = (want < chunk) ? want : chunk;
    int r = f.read((uint8_t*)(buffer + total), readLen);
    if (r > 0) {
      total += (size_t)r;
    } else {
      break;
    }
  }

  buffer[total] = '\0';
  f.close();
  return total;
}

bool SDCardManager::writeFile(const char* path, std::string_view content) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot write file");
    return false;
  }

  // Remove existing file so we perform an overwrite rather than append
  if (SD.exists(path)) {
    SD.remove(path);
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("Failed to open file for write: %s\n", path);
    return false;
  }

  size_t written = f.write(reinterpret_cast<const uint8_t*>(content.data()), content.size());
  f.close();
  return (written == content.size());
}

bool SDCardManager::ensureDirectoryExists(const char* path) {
  if (!initialized) {
    Serial.println("SDCardManager: not initialized; cannot create directory");
    return false;
  }

  // Check if directory already exists
  if (SD.exists(path)) {
    File dir = SD.open(path);
    if (dir && dir.isDirectory()) {
      dir.close();
      Serial.printf("Directory already exists: %s\n", path);
      return true;
    }
    dir.close();
  }

  // Create the directory
  if (SD.mkdir(path)) {
    Serial.printf("Created directory: %s\n", path);
    return true;
  } else {
    Serial.printf("Failed to create directory: %s\n", path);
    return false;
  }
}
