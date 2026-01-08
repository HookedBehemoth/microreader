#pragma once

#include <memory>
#include <unordered_map>

#include "core/Buttons.h"
#include "core/EInkDisplay.h"
#include "rendering/TextRenderer.h"
#include "text/layout/LayoutStrategy.h"
#include "ui/screens/Screen.h"

class UIManager {
 public:
  // Typed screen identifiers so callers don't use raw indices
  enum class ScreenId { FileBrowser, ImageViewer, TextViewer, TocBrowser, PkPassViewer, Settings, Count };

  void begin();
  void handleButtons(Buttons& buttons);
  void showSleepScreen();
  // Prepare UI for power-off: notify active screen to persist state
  void prepareForSleep();

  // Show a screen by id
  void showScreen(ScreenId id);

  // Open a text file (path on SD) in the text viewer and switch to that screen.
  void openTextFile(const String& sdPath);

  void openPkPassFile(const String& sdPath);

 private:
  ScreenId currentScreen = ScreenId::FileBrowser;
  ScreenId previousScreen = ScreenId::FileBrowser;

 public:
  ScreenId getPreviousScreen() const {
    return previousScreen;
  }

  Screen* getScreen(ScreenId id);
};

extern UIManager g_uiManager;
