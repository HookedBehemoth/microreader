#include "UIManager.h"

#include <resources/fonts/FontManager.h>

#include "core/Settings.h"
#include "resources/images/bebop_image.h"
#include "ui/screens/FileBrowserScreen.h"
#include "ui/screens/ImageViewerScreen.h"
#include "ui/screens/PkPassViewerScreen.h"
#include "ui/screens/SettingsScreen.h"
#include "ui/screens/TocBrowserScreen.h"
#include "ui/screens/TextViewerScreen.h"

namespace {
  FileBrowserScreen fileBrowser;
  ImageViewerScreen imageViewer;
  TextViewerScreen textViewer;
  TocBrowserScreen tocBrowser;
  PkPassViewerScreen pkPassViewer;
  SettingsScreen settingsScreen;

  std::array<Screen*, static_cast<size_t>(UIManager::ScreenId::Count)> screens = {
    &fileBrowser,
    &imageViewer,
    &textViewer,
    &tocBrowser,
    &pkPassViewer,
    &settingsScreen,
  };

  Screen* getScreen(UIManager::ScreenId id) {
    size_t index = static_cast<size_t>(id);
    if (index < screens.size()) {
      return screens[index];
    }
    return nullptr;
  }
}

void UIManager::begin() {
  Serial.printf("[%lu] UIManager: begin() called\n", millis());

  // Initialize screens using generic Screen interface
  for (auto& p : screens) {
    p->begin();
  }

  // Restore last-visible screen (use consolidated settings when available)
  currentScreen = ScreenId::FileBrowser;
  ScreenId savedPreviousScreen = ScreenId::FileBrowser;

  if (g_sdManager.ready()) {
    int saved = 0;
    if (Settings::getInt(IntSetting::UI_SCREEN, saved)) {
      if (saved >= 0 && saved < static_cast<int>(ScreenId::Count)) {
        currentScreen = static_cast<ScreenId>(saved);
        Serial.printf("[%lu] UIManager: Restored screen %d from settings\n", millis(), saved);
      } else {
        Serial.printf("[%lu] UIManager: Invalid saved screen %d; using default\n", millis(), saved);
      }
    } else {
      Serial.printf("[%lu] UIManager: No saved screen state found; using default\n", millis());
    }

    // Restore previous screen (will apply after showScreen)
    int prevSaved = 0;
    if (Settings::getInt(IntSetting::UI_PREVIOUS_SCREEN, prevSaved)) {
      if (prevSaved >= 0 && prevSaved < static_cast<int>(ScreenId::Count)) {
        savedPreviousScreen = static_cast<ScreenId>(prevSaved);
        Serial.printf("[%lu] UIManager: Restored previous screen %d from settings\n", millis(), prevSaved);
      }
    }
  } else {
    Serial.printf("[%lu] UIManager: SD not ready; using default start screen\n", millis());
  }

  Serial.printf("[%lu] UIManager: Showing initial screen %d\n", millis(), static_cast<int>(currentScreen));
  showScreen(currentScreen);

  // Apply saved previousScreen after showScreen (which modifies previousScreen)
  previousScreen = savedPreviousScreen;

  Serial.printf("[%lu] UIManager initialized\n", millis());
}

void UIManager::handleButtons(Buttons& buttons) {
  // Pass buttons to the current screen
  // Directly forward to the active screen (must exist)
  getScreen(currentScreen)->handleButtons(buttons);
}

void UIManager::showSleepScreen() {
  Serial.printf("[%lu] Showing SLEEP screen\n", millis());
  g_einkDisplay.clearScreen(0xFF);

  // Draw bebop image centered
  g_einkDisplay.drawImage(bebop_image, 0, 0, BEBOP_IMAGE_WIDTH, BEBOP_IMAGE_HEIGHT, true);

  // Add "Sleeping..." text at the bottom
  {
    TextRenderer textRenderer;
    textRenderer.setFrameBuffer(g_einkDisplay.getFrameBuffer());
    textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
    textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
    textRenderer.setFont(getMainFont());

    const char* sleepText = "Sleeping...";
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(sleepText, 0, 0, &x1, &y1, &w, &h);
    int16_t centerX = (480 - w) / 2;

    textRenderer.setCursor(centerX, 780);
    textRenderer.print(sleepText);
  }

  // show the image with the grayscale antialiasing
  g_einkDisplay.displayBuffer(EInkDisplay::FULL_REFRESH);
  g_einkDisplay.copyGrayscaleBuffers(bebop_image_lsb, bebop_image_msb);
  g_einkDisplay.displayGrayBuffer(true);
}

void UIManager::prepareForSleep() {
  // Notify the active screen that the device is powering down so it can
  // persist any state (e.g. current reading position).
  if (getScreen(currentScreen))
    getScreen(currentScreen)->shutdown();
  // Persist which screen was active so we can restore it on next boot.
  if (g_sdManager.ready()) {
    Serial.printf("[%lu] UIManager: Saving current screen %d to settings\n", millis(),
                  static_cast<int>(currentScreen));
    Settings::setInt(IntSetting::UI_SCREEN, static_cast<int>(currentScreen));
    Settings::setInt(IntSetting::UI_PREVIOUS_SCREEN, static_cast<int>(previousScreen));
    if (!Settings::save()) {
      Serial.println("UIManager: Failed to write settings.cfg to SD");
    }
  } else {
    Serial.println("UIManager: SD not ready; skipping save of current screen");
  }
}

void UIManager::openTextFile(const String& sdPath) {
  Serial.printf("UIManager: openTextFile %s\n", sdPath.c_str());
  // Directly access TextViewerScreen and open the file (guaranteed to exist)
  static_cast<TextViewerScreen*>(getScreen(ScreenId::TextViewer))->openFile(sdPath);
  showScreen(ScreenId::TextViewer);
}

void UIManager::openPkPassFile(const String& sdPath) {
  static_cast<PkPassViewerScreen*>(getScreen(ScreenId::PkPassViewer))->openFile(sdPath);
  showScreen(ScreenId::PkPassViewer);
}

void UIManager::showScreen(ScreenId id) {
  // Directly show the requested screen (assumed present)
  previousScreen = currentScreen;
  currentScreen = id;
  // Call activate so screens can perform any work needed when they become
  // active (this also ensures TextViewerScreen::activate is invoked to open
  // any pending file that was loaded during begin()).
  getScreen(id)->activate();
  getScreen(id)->show();
}

Screen* UIManager::getScreen(ScreenId id) {
  return ::getScreen(id);
}
