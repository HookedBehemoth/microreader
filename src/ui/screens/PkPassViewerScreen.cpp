#include "PkPassViewerScreen.h"

#include <Arduino.h>
#include <qrcode.h>
#include <core/SDCardManager.h>
#include "../../content/pkpass/pkpass_parser.h"
#include "../../rendering/TextRenderer.h"
#include "../../resources/fonts/FontDefinitions.h"
#include "../../core/EInkDisplay.h"
#include "../../core/Settings.h"
#include "../../core/Buttons.h"
#include "../../resources/images/test_image.h"

static const int NUM_SCREENS = 4;

PkPassViewerScreen::PkPassViewerScreen(EInkDisplay& display, SDCardManager& sdCardManager, UIManager& uiManager)
    : display(display), sdCardManager(sdCardManager), uiManager(uiManager) {}

void PkPassViewerScreen::begin() {
  // Load persisted settings (last opened file) if present
  loadSettingsFromFile();
}

void PkPassViewerScreen::loadSettingsFromFile() {
  if (!sdCardManager.ready())
    return;

  Settings& s = uiManager.getSettings();
  String savedPath = s.getString(String("pkpass.lastPath"), String(""));
  if (savedPath.length() > 0) {
    pendingOpenPath = savedPath;
  }
}

void PkPassViewerScreen::saveSettingsToFile() {
  if (!sdCardManager.ready())
    return;

  Settings& s = uiManager.getSettings();
  s.setString(String("pkpass.lastPath"), currentFilePath);

  if (!s.save()) {
    Serial.println("PkPassViewerScreen: Failed to write settings.cfg");
  }
}

void PkPassViewerScreen::activate() {
  // If a file was pending to open from settings, open it now
  if (pendingOpenPath.length() > 0 && currentFilePath.length() == 0) {
    String toOpen = pendingOpenPath;
    pendingOpenPath = String("");
    openFile(toOpen);
  }
}

void PkPassViewerScreen::shutdown() {
  // Persist the current file path
  saveSettingsToFile();
}

void PkPassViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.isPressed(Buttons::BACK)) {
    // Save current file path before leaving
    saveSettingsToFile();
    uiManager.showScreen(UIManager::ScreenId::FileBrowser);
  }
}

void PkPassViewerScreen::show() {
  display.clearScreen();

  // QR code parameters - Version 2 for lower resolution, 8px per module for similar size
  // Version 2 = 25x25 modules, at 8px = 200x200 pixels (vs Version 4 = 33x33 at 6px = 198x198)
  const uint8_t qr_version = 2;
  const uint8_t px = 12;  // pixels per module
  
  // Setup text renderer
  TextRenderer textRenderer(display);
  textRenderer.setFont(&Font27);
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);  // Black text
  
  // Set framebuffer to BW buffer for rendering
  textRenderer.setFrameBuffer(display.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);

  // Display pass information at top
  int textY = 50;
  
  if (passInfo.organization_name[0] != '\0') {
    textRenderer.setCursor(20, textY);
    textRenderer.print(passInfo.organization_name);
    textY += 40;
  }
  
  if (passInfo.description[0] != '\0') {
    textRenderer.setCursor(20, textY);
    textRenderer.print(passInfo.description);
    textY += 40;
  }
  
  if (passInfo.serial_number[0] != '\0') {
    textRenderer.setFont(&Font14);
    textRenderer.setCursor(20, textY);
    textRenderer.print(passInfo.serial_number);
    textY += 30;
  }
  
  // Calculate QR code position (centered)
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(qr_version)];
  
  // Confusing coordinate system, as the entire screen is rotated
  if (hasQRCode) {
    qrcode_initText(&qrcode, qrcodeBytes, qr_version, ECC_LOW, passInfo.barcode.message);
    
    // Calculate QR code size and center it
    const uint16_t qr_pixel_size = qrcode.size * px;
    const int qr_x = (EInkDisplay::DISPLAY_WIDTH - qr_pixel_size) / 2;
    const int qr_y = (EInkDisplay::DISPLAY_HEIGHT - qr_pixel_size) / 2;
    
    // Draw QR code
    for (uint8_t cy = 0; cy < qrcode.size; cy++) {
      for (uint8_t cx = 0; cx < qrcode.size; cx++) {
        if (qrcode_getModule(&qrcode, cx, cy)) {
          display.drawRectangle(qr_x + px * cx, qr_y + px * cy, px, px);
        }
      }
    }
    
    // Display barcode alt text below QR code if available
    if (passInfo.barcode.alt_text[0] != '\0') {
      textRenderer.setFont(&Font14);
      int16_t x1, y1;
      uint16_t w, h;
      textRenderer.getTextBounds(passInfo.barcode.alt_text, 0, 0, &x1, &y1, &w, &h);
      const int alt_text_x = (EInkDisplay::DISPLAY_HEIGHT - w) / 2;
      const int alt_text_y = qr_x + qr_pixel_size + 20;
      
      textRenderer.setCursor(alt_text_x, alt_text_y);
      textRenderer.print(passInfo.barcode.alt_text);
    }
  } else {
    // No QR code data - display message
    textRenderer.setFont(&Font27);
    const char* msg = "No QR Code Available";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
    textRenderer.setCursor((EInkDisplay::DISPLAY_HEIGHT - w) / 2, EInkDisplay::DISPLAY_WIDTH / 2);
    textRenderer.print(msg);
  }
  
  display.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void PkPassViewerScreen::openFile(const String& path) {
  Serial.printf("[%lu] PkPassViewer: opening %s\n", millis(), path.c_str());

  // Clear previous data
  memset(&passInfo, 0, sizeof(passInfo));
  hasQRCode = false;
  currentFilePath = path;

  /* Open .pkpass file using pkpass_parser */
  pkpass_reader* reader = NULL;
  pkpass_error err = pkpass_open(path.c_str(), &reader);
  if (err != PKPASS_OK) {
    Serial.printf("[%lu] PkPassViewer: failed to open %s: %s\n", millis(), path.c_str(), pkpass_get_error_string(err));
    return;
  }
  Serial.printf("[%lu] PkPassViewer: opened successfully\n", millis());

  /* Get pass information including barcode */
  err = pkpass_get_info(reader, &passInfo);
  if (err != PKPASS_OK) {
    Serial.printf("[%lu] PkPassViewer: failed to get pass info: %s\n", millis(), pkpass_get_error_string(err));
    pkpass_close(reader);
    return;
  }

  /* Check if pass has a QR barcode */
  if (passInfo.has_barcode && strcmp(passInfo.barcode.format, "PKBarcodeFormatQR") == 0) {
    hasQRCode = true;
    Serial.printf("[%lu] PkPassViewer: extracted QR message: %s\n", millis(), passInfo.barcode.message);
  } else {
    Serial.printf("[%lu] PkPassViewer: no QR barcode found in %s\n", millis(), path.c_str());
  }

  pkpass_close(reader);
}