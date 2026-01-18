#include "xml_util.hpp"

#include <expat.h>

namespace xml {

static_assert(sizeof(char) == sizeof(XML_Char), "XML_Char is not char");
std::optional<StringView> getAttribute(const char** attrs, StringView name) {
  for (size_t i = 0; attrs[i]; i += 2) {
    StringView attrName = StringView::fromCStr(attrs[i]);
    if (attrName.caseCmp(name)) {
      return StringView::fromCStr(attrs[i + 1]);
    }
  }
  return std::nullopt;
}

}
