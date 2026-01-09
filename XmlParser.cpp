#include "XmlParser.h"

namespace xml {

constexpr StringView WhiteSpaceChars = " \t\r\n";

bool AttributeReader::next()
{
  // Skip whitespace
  auto it = data.skipAll(WhiteSpaceChars);

  // No attributes
  if (StringView("/>\0").contains(it[0]))
    return false;

  size_t nameLength = it.findAny("\0 \t\n\r>/=");
  if (nameLength == 0)
    return false;
  auto name = it[0, nameLength];

  // Consume delimiter
  it = it.skip(nameLength).skipAll(WhiteSpaceChars);
  if (it[0] != '=')
    return false;
  it = it.skip(1).skipAll(WhiteSpaceChars);

  // Consume starting quote
  if (it[0] != '"' && it[0] != '\'')
    return false;
  char quoteChar = it[0];
  it = it.skip(1);

  // Consume value
  auto valueLength = it.find(quoteChar);
  auto value = it[0, valueLength];

  // Update state
  currentName = name;
  currentValue = value;
  data = it.skip(valueLength + 1); // +1 for closing quote
  return true;
}

XmlParser::NodeType XmlParser::next()
{
  auto it = data.skip(position).skipAll(WhiteSpaceChars);

  // Skip to the next node
  if (currentNode == NodeType::Text)
  {
    it = it.skipUntil('<');
  }
  else if (currentNode == NodeType::Comment)
  {
    auto commentEndPos = it.find("-->");
    it = it.skip(commentEndPos + 3);
  }
  else if (currentNode == NodeType::CDATA)
  {
    auto cdataEndPos = it.find("]]>");
    it = it.skip(cdataEndPos + 3);
  }
  else if (currentNode == NodeType::ProcessingInstruction)
  {
    auto elmEndPos = it.find("?>");
    it = it.skip(elmEndPos + 2);
  }
  else if (currentNode == NodeType::Element)
  {
    it = it.skip(name().size());
    auto attr = AttributeReader(it);
    if (attr.next()) {
      while (attr.next()) { /* ... */ }
      it = it.skip(attr.data.data() - it.data());
    }
    auto elmEndPos = it.find('>');
    // Switch states while not actually touching position
    if (elmEndPos > 0 && it[elmEndPos - 1] == '/') {
      return currentNode = NodeType::EndElement;
    }

    it = it.skip(elmEndPos + 1);
  }
  else if (currentNode == NodeType::EndElement)
  {
    it = it.skip(name().size());
    auto elmEndPos = it.find('>');
    it = it.skip(elmEndPos + 1);
  }

  it = it.skipAll(WhiteSpaceChars);
  
  if (it.size() == 0) {
    position = data.size();
    return currentNode = NodeType::EndOfFile;
  }

  constexpr StringView cdataStart = "<![CDATA[";
  constexpr StringView commentStart = "<!--";
  constexpr StringView processingInstructionStart = "<?";

  if (it.startsWith(cdataStart)) {
    position = data.size() - it.size() + cdataStart.size();
    return currentNode = NodeType::CDATA;
  }
  else if (it.startsWith(commentStart)) {
    position = data.size() - it.size() + commentStart.size();
    return currentNode = NodeType::Comment;
  }
  else if (it.startsWith(processingInstructionStart)) {
    position = data.size() - it.size() + processingInstructionStart.size();
    return currentNode = NodeType::ProcessingInstruction;
  }
  else if (it[0] == '<') {
    if (it[1] == '/') {
      position = data.size() - it.size() + 2;
      return currentNode = NodeType::EndElement;
    }
    else {
      position = data.size() - it.size() + 1;
      return currentNode = NodeType::Element;
    }
  }
  else {
    position = data.size() - it.size();
    return currentNode = NodeType::Text;
  }

  return NodeType::EndOfFile;
}

StringView XmlParser::name() const
{
  if (currentNode != NodeType::Element &&
      currentNode != NodeType::EndElement)
  {
    return "";
  }

  // Extract name
  auto it = data.skip(position).skipAll(WhiteSpaceChars);
  size_t nameLength = it.findAny(" \t\r\n/>");
  return it[0, nameLength];
}

AttributeReader XmlParser::attributes() const
{
  auto it = data.skip(position + name().size());
  return AttributeReader(it);
}

StringView XmlParser::text() const
{
  if (currentNode != NodeType::Text)
  {
    return "";
  }

  auto it = data.skip(position);
  size_t textLength = it.find('<');
  return it[0, textLength].trimEnd(WhiteSpaceChars);
}

StringView XmlParser::comment() const
{
  if (currentNode != NodeType::Comment)
  {
    return "";
  }

  auto it = data.skip(position);
  it = it.skipAll(WhiteSpaceChars);
  auto commentLength = it.find("-->");
  return it[0, commentLength].trimEnd(WhiteSpaceChars);
}

StringView XmlParser::processingInstruction() const
{
  if (currentNode != NodeType::ProcessingInstruction)
  {
    return "";
  }

  auto it = data.skip(position);
  auto piLength = it.find("?>");
  return it[0, piLength].trimEnd(WhiteSpaceChars);
}

StringView XmlParser::cdata() const
{
  if (currentNode != NodeType::CDATA)
  {
    return "";
  }

  auto it = data.skip(position);
  auto cdataLength = it.find("]]>");
  return it[0, cdataLength].trimEnd(WhiteSpaceChars);
}

}
