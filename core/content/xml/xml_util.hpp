#pragma once

#include <StringView.hpp>

#include <expat.h>
#include <optional>

namespace xml {

std::optional<StringView> getAttribute(const char** attrs, StringView name);
void forEachAttribute(const char** attrs, auto&& func) {
  for (size_t i = 0; attrs[i]; i += 2) {
    StringView attrName = StringView::fromCStr(attrs[i]);
    StringView attrValue = StringView::fromCStr(attrs[i + 1]);
    func(attrName, attrValue);
  }
}

}
