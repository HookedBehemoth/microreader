#include "ImageViewerScreen.h"

#include <Arduino.h>

#include "../../core/Buttons.h"
#include "../../resources/images/bebop_image.h"
#include "../../resources/images/test_image.h"

static const int NUM_SCREENS = 4;

void ImageViewerScreen::handleButtons(Buttons& buttons) {
  if (buttons.isPressed(Buttons::LEFT)) {
    index = (index - 1 + NUM_SCREENS) % NUM_SCREENS;
    show();
  } else if (buttons.isPressed(Buttons::RIGHT)) {
    index = (index + 1) % NUM_SCREENS;
    show();
  } else if (buttons.isPressed(Buttons::VOLUME_UP)) {
    g_uiManager.showScreen(UIManager::ScreenId::FileBrowser);
  } else if (buttons.isPressed(Buttons::VOLUME_DOWN)) {
    g_einkDisplay.refreshDisplay(EInkDisplay::FULL_REFRESH);
  } else if (buttons.isPressed(Buttons::BACK)) {
    g_einkDisplay.grayscaleRevert();
  }
}

void ImageViewerScreen::show() {
  switch (index % NUM_SCREENS) {
    case 0:
      Serial.printf("[%lu] ImageViewer: IMAGE 0\n", millis());
      g_einkDisplay.setFramebuffer(test_image);
      g_einkDisplay.displayBuffer(EInkDisplay::FAST_REFRESH);
      g_einkDisplay.copyGrayscaleBuffers(test_image_lsb, test_image_msb);
      g_einkDisplay.displayGrayBuffer();
      break;
    case 1:
      Serial.printf("[%lu] ImageViewer: IMAGE 1\n", millis());
      g_einkDisplay.setFramebuffer(bebop_image);
      g_einkDisplay.displayBuffer(EInkDisplay::FAST_REFRESH);
      g_einkDisplay.copyGrayscaleBuffers(bebop_image_lsb, bebop_image_msb);
      g_einkDisplay.displayGrayBuffer();
      break;
    case 2:
      Serial.printf("[%lu] ImageViewer: WHITE\n", millis());
      g_einkDisplay.clearScreen(0xFF);
      g_einkDisplay.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
    case 3:
      Serial.printf("[%lu] ImageViewer: BLACK\n", millis());
      g_einkDisplay.clearScreen(0x00);
      g_einkDisplay.displayBuffer(EInkDisplay::FAST_REFRESH);
      break;
  }
}