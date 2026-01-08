#pragma once

#include "Screen.h"
#include <content/epub/EpubReader.h>
#include <content/providers/WordProvider.h>

class EpubReader;

class TocBrowserScreen : public Screen {
 public:
  void handleButtons(Buttons& buttons) override;
  void show() override;

  void setToc(WordProvider* provider, EpubReader* reader, int index_);
 
 private:
  WordProvider* provider = nullptr;
  EpubReader* reader = nullptr;
  int tocCount = 0;
  int index = 0;

  void showErrorMessage(const char* msg);
};
