#pragma once

#include <optional>
#include <cstdint>

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
  Normal,  // Default normal style
  Italic   // Italic text
};

/**
 * Font weight values (bold)
 */
enum class CssFontWeight : uint8_t {
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
  std::optional<TextAlign> textAlign;
  std::optional<CssFontStyle> fontStyle;
  std::optional<CssFontWeight> fontWeight;
  // Text-indent support (in CSS units, stored here as pixels approximation)
  std::optional<float> textIndent;

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

  // Reset to default values
  void reset() {
    textAlign.reset();
    fontStyle.reset();
    fontWeight.reset();
    textIndent.reset();
  }
};

} // namespace css
