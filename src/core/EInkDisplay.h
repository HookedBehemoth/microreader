#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>
#else
#include "../test/mocks/platform_stubs.h"
#endif

class EInkDisplay {
 public:
  // Refresh modes (guarded to avoid redefinition in test builds)
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Initialize the display hardware and driver
  void begin();

  // Display dimensions
  static const uint16_t DISPLAY_WIDTH = 800;
  static const uint16_t DISPLAY_HEIGHT = 480;
  static const uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static const uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF);
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem = false);
  void drawRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  void swapBuffers();
  void setFramebuffer(const uint8_t* bwBuffer);

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);

  void displayBuffer(RefreshMode mode = FAST_REFRESH);
  void displayGrayBuffer(bool turnOffScreen = false);

  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);

  // debug function
  void grayscaleRevert();

  // LUT control
  void setCustomLUT(bool enabled, const unsigned char* lutData = nullptr);

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() {
    return frameBuffer;
  }

  // Save the current framebuffer to a PBM file (desktop/test builds only)
  void saveFrameBufferAsPBM(const char* filename);

 private:
  // Frame buffer (statically allocated)
  uint8_t frameBuffer0[BUFFER_SIZE];
  uint8_t frameBuffer1[BUFFER_SIZE];

  uint8_t* frameBuffer = nullptr;
  uint8_t* frameBufferActive = nullptr;

  // SPI settings
  SPISettings spiSettings;

  // State
  bool isScreenOn = false;
  bool customLutActive = false;
  bool inGrayscaleMode = false;
  bool drawGrayscale = false;

  // Low-level display control
  void resetDisplay();
  void sendCommand(uint8_t command);
  void sendData(uint8_t data);
  void sendData(const uint8_t* data, uint16_t length);
  void waitWhileBusy(const char* comment = nullptr);
  void initDisplayController();

  // Low-level display operations
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size);
};

extern EInkDisplay g_einkDisplay;
