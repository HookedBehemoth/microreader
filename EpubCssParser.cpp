#include "EpubCssParser.hpp"
#include "EpubCssStyle.hpp"
#include "stringview.h"
#include <cstdlib>

namespace css {

std::span<std::pair<StringView, CssStyle>> parseSheet(
  StringView sheet,
  mem::Allocator& allocator
) {

}

CssStyle parseInline(StringView styleStr) {
  CssStyle style;
  TocIterator it { styleStr, ";"};

  while (it.hasNext()) {
    StringView decl = it.next();
    size_t eqPos = decl.find(':');
    if (eqPos == decl.size()) {
      continue;
    }
    StringView name = decl[0, eqPos].trimWhitespace();
    StringView value = decl[eqPos + 1, decl.size()].trimWhitespace();

    if (name.caseCmp("text-align")) {
      if (value.caseCmp("left") || value.caseCmp("start")) {
        style.textAlign = TextAlign::Left;
      } else if (value.caseCmp("right") || value.caseCmp("end")) {
        style.textAlign = TextAlign::Right;
      } else if (value.caseCmp("center")) {
        style.textAlign = TextAlign::Center;
      } else if (value.caseCmp("justify")) {
        style.textAlign = TextAlign::Justify;
      }
    } else if (name.caseCmp("font-style")) {
      if (value.caseCmp("italic") || value.caseCmp("oblique")) {
        style.fontStyle = CssFontStyle::Italic;
      } else {
        style.fontStyle = CssFontStyle::Normal;
      }
    } else if (name.caseCmp("font-weight")) {
      if (value.caseCmp("bold") || value.caseCmp("bolder") || value.caseCmp("700") || value.caseCmp("800") || value.caseCmp("900")) {
        style.fontWeight = CssFontWeight::Bold;
      } else {
        style.fontWeight = CssFontWeight::Normal;
      }
    } else if (name.caseCmp("text-indent")) {
      // parse as float pixels for now
      float factor = 1.0f;
      if (value.endsWith("px")) {
        value = value[0, value.size() - 2];
        factor = 1.0f;
      } else if (value.endsWith("em")) {
        value = value[0, value.size() - 2];
        factor = 16.0f; // assume 1em = 16px
      } else if (value.endsWith("%")) {
        value = value[0, value.size() - 1];
        factor = 0.16f; // assume 100% = 16px
      }
      value = value.trimWhitespace();
      if (value.size() == 0) {
        continue;
      }
      char parseBuffer[32] = {};
      StringView indentStr = join(parseBuffer, sizeof(parseBuffer), value, "\0");
      float indentValue = static_cast<float>(atof(indentStr.data())) * factor;
      if (indentValue > 0.0f) {
        style.textIndent = indentValue;
      }
    }
  }

  return style;
}

CssStyle getCombined(StringView className) {

}

} // namespace css
