#pragma once

#include "stringview.h"

namespace xml {

/// Consuming Attribute reader
/// Call next() to advance to the next attribute
class AttributeReader {
 public:
  constexpr AttributeReader(StringView data_)
      : data(data_) {}
  StringView data;
  
  bool next();
  constexpr StringView name() const { return currentName; }
  constexpr StringView value() const { return currentValue; }

 private:
  StringView currentName;
  StringView currentValue;
};

/// in-place XML parser
/// Call next() until EndOfFile is returned
/// Access attributes, name, text, comment, cdata, processingInstruction as needed
class XmlParser {
 public:
  constexpr XmlParser(StringView data_)
    : data(data_) {}

  void reset() {
    position = 0;
    currentNode = NodeType::None;
  }

  enum class NodeType {
    None = 0,
    Element,
    Text,
    EndElement,
    Comment,
    ProcessingInstruction,
    CDATA,
    EndOfFile
  };

  NodeType next();
  StringView name() const;
  AttributeReader attributes() const;
  StringView text() const;
  StringView comment() const;
  StringView processingInstruction() const;
  StringView cdata() const;

  constexpr StringView getAttribute(StringView attrName) const {
    auto attr = attributes();
    while (attr.next()) {
      if (attr.name().caseCmp(attrName)) {
        return attr.value().sliceUntil('#');
      }
    }
    return "";
  }

private:
  StringView data;
  size_t position = 0;

  NodeType currentNode = NodeType::None;
};

}
