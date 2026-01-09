#include "XmlParser.h"

#include <cstdio>
#include <cstdlib>

using namespace xml;

void Assert(bool condition, const char* expression, const char* file, int line)
{
  if (!condition) {
    std::printf("Assertion failed: '%s', file %s:%d\n", expression, file, line);
    std::abort();
  }
}
#define ASSERT(cond) Assert((cond), #cond, __FILE__, __LINE__)

void TestAttributeReader()
{
  AttributeReader reader(" key1  =\t\r\n\"value1\" key2='value2' key3= \"value3\"/>");

  ASSERT(reader.next());
  ASSERT(reader.name() == "key1");
  ASSERT(reader.value() == "value1");

  ASSERT(reader.next());
  ASSERT(reader.name() == "key2");
  ASSERT(reader.value() == "value2");

  ASSERT(reader.next());
  ASSERT(reader.name() == "key3");
  ASSERT(reader.value() == "value3");

  ASSERT(!reader.next());
}

void TestXmlParser()
{
  constexpr StringView xmlData = R"xml(
<root attr1="value1" attr2='value2'>
  <!-- This is a comment -->
  <child>Some <nested> text </nested> content</child>
  <![CDATA[<notatag>]]>
  <?pi processing instruction?>
</root>)xml";

  XmlParser parser { xmlData };

  ASSERT(parser.next() == XmlParser::NodeType::Element);
  ASSERT(parser.name() == "root");
  auto attrs = parser.attributes();
  ASSERT(attrs.next());
  ASSERT(attrs.name() == "attr1");
  ASSERT(attrs.value() == "value1");
  ASSERT(attrs.next());
  ASSERT(attrs.name() == "attr2");
  ASSERT(attrs.value() == "value2");
  ASSERT(!attrs.next());
  ASSERT(parser.next() == XmlParser::NodeType::Comment);
  ASSERT(parser.comment() == "This is a comment");
  ASSERT(parser.next() == XmlParser::NodeType::Element);
  ASSERT(parser.name() == "child");
  ASSERT(parser.next() == XmlParser::NodeType::Text);
  ASSERT(parser.text() == "Some");
  ASSERT(parser.next() == XmlParser::NodeType::Element);
  ASSERT(parser.name() == "nested");
  ASSERT(parser.next() == XmlParser::NodeType::Text);
  ASSERT(parser.text() == "text");
  ASSERT(parser.next() == XmlParser::NodeType::EndElement);
  ASSERT(parser.name() == "nested");
  ASSERT(parser.next() == XmlParser::NodeType::Text);
  ASSERT(parser.text() == "content");
  ASSERT(parser.next() == XmlParser::NodeType::EndElement);
  ASSERT(parser.name() == "child");
  ASSERT(parser.next() == XmlParser::NodeType::CDATA);
  ASSERT(parser.cdata() == "<notatag>");
  ASSERT(parser.next() == XmlParser::NodeType::ProcessingInstruction);
  ASSERT(parser.processingInstruction() == "pi processing instruction");
  ASSERT(parser.next() == XmlParser::NodeType::EndElement);
  ASSERT(parser.name() == "root");
  ASSERT(parser.next() == XmlParser::NodeType::EndOfFile);
}

int main() {
  TestAttributeReader();
  TestXmlParser();
  return 0;
}