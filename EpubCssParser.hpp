#pragma once

#include "Allocator.hpp"
#include "EpubCssStyle.hpp"
#include <span>

namespace css {

std::span<std::pair<StringView, CssStyle>> parseSheet(StringView sheet, mem::Allocator& allocator);
CssStyle parseInline(StringView styleStr);
CssStyle getCombined(StringView className);

} // namespace css
