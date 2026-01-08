#include "TocBrowserScreen.h"

#include <content/epub/EpubReader.h>
#include <rendering/TextRenderer.h>
#include <resources/fonts/FontDefinitions.h>
#include <core/EInkDisplay.h>
#include <ui/UIManager.h>

void TocBrowserScreen::handleButtons(Buttons& buttons) {
  if (buttons.isPressed(Buttons::VOLUME_UP) || buttons.isPressed(Buttons::RIGHT)) {
    --index;
    if (index < 0) {
      index = std::max(0, tocCount - 1);
    }
    show();
  } else if (buttons.isPressed(Buttons::VOLUME_DOWN) || buttons.isPressed(Buttons::LEFT)) {
    ++index;
    if (index >= tocCount) {
      index = 0;
    }
    show();
  } else if (buttons.isPressed(Buttons::CONFIRM)) {
    provider->setChapter(reader->tocIndexToSpineIndex(index));
    g_uiManager.showScreen(g_uiManager.getPreviousScreen());
  } else if (buttons.isPressed(Buttons::BACK)) {
    // Go back to previous screen
    g_uiManager.showScreen(g_uiManager.getPreviousScreen());
  }
}

void TocBrowserScreen::show() {
  g_einkDisplay.clearScreen(0xFF);

  TextRenderer textRenderer;
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFontFamily(&menuFontSmallFamily);
  textRenderer.setFrameBuffer(g_einkDisplay.getFrameBuffer());
  textRenderer.setBitmapType(TextRenderer::BITMAP_BW);
  
  if (reader == nullptr) {
    showErrorMessage("No EPUB loaded.");
    return;
  }

  if (tocCount == 0) {
    showErrorMessage("No TOC available.");
    return;
  }

  Serial.printf("TocBrowserScreen: show TOC with %d items, index=%d\n", tocCount, index);

  const int start = 120;
  const int steps = 30;
  int firstItem = std::max(0, std::min(index - 10, tocCount - 20));
  int lastItem = std::min(tocCount, firstItem + 20);
  if (firstItem != 0) {
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds("...", 0, 0, &x1, &y1, &w, &h);

    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, 90);
    textRenderer.print("...");
  }
  for (int i = firstItem; i < lastItem; i++) {
    const TocItem* item = reader->getTocItem(i);
    const String& title = item->title;
    const char* ellipses = title.length() > 50 ? "..." : "";
    char buffer[64];
    if (i == index) {
      snprintf(buffer, sizeof(buffer), ">%.50s%s<", title.c_str(), ellipses);
    } else {
      snprintf(buffer, sizeof(buffer), "%.50s%s", title.c_str(), ellipses);
    }
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds(buffer, 0, 0, &x1, &y1, &w, &h);

    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, start + (i - firstItem) * steps);
    textRenderer.print(buffer);
  }
  if (lastItem < tocCount) {
    int16_t x1, y1;
    uint16_t w, h;
    textRenderer.getTextBounds("...", 0, 0, &x1, &y1, &w, &h);

    int16_t centerX = (480 - (int)w) / 2;
    textRenderer.setCursor(centerX, start + 20 * steps);
    textRenderer.print("...");
  }

  g_einkDisplay.displayBuffer(EInkDisplay::FAST_REFRESH);
}

void TocBrowserScreen::setToc(WordProvider* provider_, EpubReader* reader_, int spineIndex_) {
  provider = provider_;
  reader = reader_;
  tocCount = reader_ ? reader_->getTocCount() : 0;
  int tocIndex = reader_->spineIndexToTocIndex(spineIndex_);
  index = std::max(0, std::min(tocIndex, tocCount - 1));
}

void TocBrowserScreen::showErrorMessage(const char* msg) {
  g_einkDisplay.clearScreen(0xFF);

  TextRenderer textRenderer;
  textRenderer.setTextColor(TextRenderer::COLOR_BLACK);
  textRenderer.setFontFamily(&bookerly26Family);
  textRenderer.setFontStyle(FontStyle::ITALIC);

  int16_t x1, y1;
  uint16_t w, h;
  textRenderer.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  int16_t centerX = (480 - w) / 2;
  int16_t centerY = (800 - h) / 2;
  textRenderer.setCursor(centerX, centerY);
  textRenderer.print(msg);

  g_einkDisplay.displayBuffer(EInkDisplay::FAST_REFRESH);
}
