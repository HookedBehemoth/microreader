#pragma once

#include "Allocator.hpp"
#include "EpubCssStyle.hpp"
#include <span>

namespace css {

struct CssRule {
  StringView selector;
  CssStyle style;
};

std::span<CssRule> parseSheet(StringView sheet, mem::Allocator& allocator);
CssStyle parseInline(StringView styleStr);
CssStyle getStyle(std::span<CssRule> rules, StringView className);
CssStyle getCombinedStyle(std::span<CssRule> rules, StringView classNames);

} // namespace css
