#pragma once

#include "../../core/EInkDisplay.h"
#include "../UIManager.h"
#include "Screen.h"
#include "../../content/pkpass/pkpass_parser.h"

class PkPassViewerScreen : public Screen {
 public:
  void begin();
  void activate() override;
  void shutdown() override;
  void handleButtons(class Buttons& buttons) override;
  void show() override;

 public:
  void openFile(const String& path);

 private:
  void loadSettingsFromFile();
  void saveSettingsToFile();

  bool hasQRCode = false;
  pkpass_info passInfo = {};
  String currentFilePath;
  String pendingOpenPath;

  int index = 0;
};
