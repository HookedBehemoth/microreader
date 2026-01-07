#pragma once

#include <Arduino.h>

#include <vector>

#include "Screen.h"

class UIManager;

struct SdFile {
  using OpenFn = void(UIManager::*)(const String&);
  String name;
  OpenFn callback;
  SdFile(String _name, OpenFn _cb) : name(_name), callback(_cb) {}
};

class FileBrowserScreen : public Screen {
 public:
  void show() override;
  void activate() override;

  void handleButtons(class Buttons& buttons) override;

  // Input helpers
  void confirm();
  void selectNext();
  void selectPrev();
  void offsetSelection(int offset);

 private:
  void loadFolder(int maxFiles = 200);
  void renderSdBrowser();

  std::vector<SdFile> sdFiles;
  int sdSelectedIndex = 0;
  int sdScrollOffset = 0;

  static const int SD_LINES_PER_SCREEN = 8;
};
