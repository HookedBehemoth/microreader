#pragma once

#include <cstdint>
#include <cstdio>

#include <Optional.hpp>
#include <Log.hpp>

namespace css {

/**
 * Text alignment values supported by the reader
 */
enum class TextAlign : uint8_t {
  None,    // Default none alignment
  Left,    // Left alignment
  Right,   // Right alignment
  Center,  // Center alignment
  Justify  // Justified text (both edges aligned)
};

/**
 * Font style values (italic)
 */
enum class CssFontStyle : uint8_t {
  None,    // Default none style
  Normal,  // Default normal style
  Italic   // Italic text
};

/**
 * Font weight values (bold)
 */
enum class CssFontWeight : uint8_t {
  None,    // Default none weight
  Normal,  // Default normal weight
  Bold     // Bold text
};

/**
 * CssStyle - Represents supported CSS properties for a selector
 *
 * This structure holds the subset of CSS properties that the reader supports.
 * Currently supported:
 * - text-align: left, right, center, justify
 *
 * Properties can be extended in the future to support:
 * - font-style: normal, italic
 * - font-weight: normal, bold
 *
 * Future extensions:
 * - text-indent
 * - margin-top/bottom (for paragraph spacing)
 */
struct CssStyle {
  tiny::optional<TextAlign, TextAlign::None> textAlign;
  tiny::optional<CssFontStyle, CssFontStyle::None> fontStyle;
  tiny::optional<CssFontWeight, CssFontWeight::None> fontWeight;
  // Text-indent support (in CSS units, stored here as pixels approximation)
  tiny::optional<uint8_t, 0xFF> textIndent;

  // Merge another style into this one (other style takes precedence)
  void merge(const CssStyle& other) {
    if (other.textAlign.has_value()) {
      textAlign = other.textAlign;
    }
    if (other.fontStyle.has_value()) {
      fontStyle = other.fontStyle;
    }
    if (other.fontWeight.has_value()) {
      fontWeight = other.fontWeight;
    }
    if (other.textIndent.has_value()) {
      textIndent = other.textIndent;
    }
  }

  constexpr bool any() const {
    return textAlign.has_value() ||
           fontStyle.has_value() ||
           fontWeight.has_value() ||
           textIndent.has_value();
  }

  // Reset to default values
  void reset() {
    textAlign.reset();
    fontStyle.reset();
    fontWeight.reset();
    textIndent.reset();
  }

  void dump() const {
    PrintF("CssStyle {\n");
    if (textAlign.has_value()) {
      PrintF("  text-align: ");
      switch (*textAlign) {
        case TextAlign::Left:    PrintF("left\n"); break;
        case TextAlign::Right:   PrintF("right\n"); break;
        case TextAlign::Center:  PrintF("center\n"); break;
        case TextAlign::Justify: PrintF("justify\n"); break;
        default:                 PrintF("none\n"); break;
      }
    }
    if (fontStyle.has_value()) {
      PrintF("  font-style: %s\n", (*fontStyle == CssFontStyle::Italic) ? "italic" : "normal");
    }
    if (fontWeight.has_value()) {
      PrintF("  font-weight: %s\n", (*fontWeight == CssFontWeight::Bold) ? "bold" : "normal");
    }
    if (textIndent.has_value()) {
      PrintF("  text-indent: %hhupx\n", *textIndent);
    }
    PrintF("}\n");
  }
};

} // namespace css
