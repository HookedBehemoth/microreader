#pragma once

#include "slice.h"

namespace xml {

/// Consuming Attribute reader
/// Call next() to advance to the next attribute
class AttributeReader {
 public:
  constexpr AttributeReader(StringView data_)
      : data(data_) {}
  StringView data;
  
  constexpr bool next();
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

  constexpr NodeType next();
  constexpr StringView name() const;
  constexpr AttributeReader attributes() const;
  constexpr StringView text() const;
  constexpr StringView comment() const;
  constexpr StringView processingInstruction() const; // TODO: is this really needed
  constexpr StringView cdata() const;

private:
  StringView data;
  size_t position = 0;

  NodeType currentNode = NodeType::None;
};

}
