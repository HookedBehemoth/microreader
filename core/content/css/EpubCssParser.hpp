#pragma once

#include <mem/Allocator.hpp>
#include <StringView.hpp>
#include <content/css/EpubCssStyle.hpp>
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
