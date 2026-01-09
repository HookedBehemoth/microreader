
#include <cstdio>
#include <initializer_list>

#include "stringview.h"
#include "stopwatch.h"
#include "XmlParser.h"

// #include <
using namespace xml;

void print(StringView str) {
  fwrite(str.data(), 1, str.size(), stdout);
}

// Only allow Args to be StringView
// template<class T, T... Args>
void println(int indent, std::initializer_list<StringView> args) {
  for (int i = 0; i < indent; i++) {
    printf("  ");
  }
  for (auto arg : args)
    print(arg);
  // (..., print(args));
  printf("\n");
}

void testTocParsing(StringView toc);

int main(/* int argc, char *argv[] */) {
  FILE *tocFile = fopen("toc1.ncx", "rb");
  // FILE *tocFile = fopen("toc2.ncx", "rb");
  // FILE *tocFile = fopen("toc3.ncx", "rb");
  // FILE *tocFile = fopen("toc4.ncx", "rb");
  if (!tocFile) {
    printf("Failed to open TOC file\n");
    return 1;
  }

  fseeko(tocFile, 0, SEEK_END);
  size_t tocSize = ftello(tocFile);
  fseeko(tocFile, 0, SEEK_SET);

  char* tocBuffer = new char[tocSize];
  fread(tocBuffer, 1, tocSize, tocFile);

  fclose(tocFile);
  // return 0;

  StringView tocView { tocBuffer, tocSize };
  XmlParser parser { tocView };

  int indent = 0;

  while (true) {
    auto ty = parser.next();
    if (ty == XmlParser::NodeType::EndOfFile)
      break;
    
    switch (ty) {
      case XmlParser::NodeType::Element:
        println(indent, {parser.name()});
        {
          auto attrs = parser.attributes();
          while (attrs.next()) {
            println(indent + 1, {attrs.name(), " = ", attrs.value()});
          }
        }
        indent++;
        break;
      case XmlParser::NodeType::Text:
        println(indent, {"\"", parser.text(), "\""});
        break;
      case XmlParser::NodeType::Comment:
        println(indent, {"Comment: \"", parser.comment(), "\""});
        break;
      case XmlParser::NodeType::CDATA:
        println(indent, {"CDATA: \"", parser.cdata(), "\""});
        break;
      case XmlParser::NodeType::ProcessingInstruction:
        println(indent, {"Processing Instruction: \"", parser.processingInstruction(), "\""});
        break;
      case XmlParser::NodeType::EndElement:
        indent--;
        println(indent, {"End Element: \"", parser.name(), "\""});
        break;
      default:
        break;
    }
  }

  printf("\n");

  testTocParsing(tocView);

  return 0;
}

struct TocEntry {
  StringView label;
  StringView src;
};

void parseToc(
  StringView toc,
  StringView* title,
  TocEntry* entries,
  char* buffer,
  size_t& tocs,
  size_t& neededMemory);

void testTocParsing(StringView toc) {
  size_t tocs = 0;
  size_t neededMemory = 0;

  Stopwatch sw;

  parseToc(toc, nullptr, nullptr, nullptr, tocs, neededMemory);

  printf("Parsing TOC took %ld microseconds\n", sw.elapsedMicroseconds());

  printf("TOC entries: %zu (%zu bytes)\n", tocs, tocs * sizeof(TocEntry));
  printf("Needed memory: %zu bytes\n\n", neededMemory);

  size_t bufferSize = sizeof(StringView) + tocs * sizeof(TocEntry) + neededMemory;
  char* mem = new(std::align_val_t(alignof(std::max_align_t))) char[bufferSize];
  std::memset(mem, 0, bufferSize);

  StringView* title = reinterpret_cast<StringView*>(mem);
  TocEntry* entries = reinterpret_cast<TocEntry*>(mem + sizeof(StringView));
  char* stringBuffer = mem + sizeof(StringView) + tocs * sizeof(TocEntry);
  tocs = 0;
  neededMemory = 0;

  sw.reset();
  parseToc(toc, title, entries, stringBuffer, tocs, neededMemory);
  printf("Parsing TOC (2nd pass) took %ld microseconds\n", sw.elapsedMicroseconds());

  println(0, {"Title: \"", *title, "\""});
  printf("Parsed TOC:\n");
  for (size_t i = 0; i < tocs; i++) {
    println(1, {entries[i].label, " -> ", entries[i].src});
  }
}

void parseToc(
  StringView tocView,
  StringView* title,
  TocEntry* entries,
  char* buffer,
  size_t& tocs,
  size_t& neededMemory)
{
  enum class NavDepth {
    Root, Ncx,
    DocTitle, DocText,
    NavMap, NavPoint, NavLabel, Content, NavText
  };

  XmlParser parser { tocView };
  NavDepth navDepth = NavDepth::Root;
  StringView currentTitle;
  StringView currentTocSrc;
  StringView currentTocText;

  auto tryCommitNavPoint = [&]() {
    if (currentTocSrc.size() > 0 && currentTocText.size() > 0) {
      if (buffer && entries) {
        auto entry = &entries[tocs];
        std::memcpy(buffer, currentTocText.data(), currentTocText.size());
        entry->label = StringView(buffer, currentTocText.size());
        buffer += currentTocText.size();
        std::memcpy(buffer, currentTocSrc.data(), currentTocSrc.size());
        entry->src = StringView(buffer, currentTocSrc.size());
        buffer += currentTocSrc.size();
      }
      tocs++;
      neededMemory += currentTocSrc.size();
      neededMemory += currentTocText.size();
    }
  };

  while (true) {
    auto ty = parser.next();
    if (ty == XmlParser::NodeType::EndOfFile)
      break;
    
    StringView name;

    switch (ty) {
      case XmlParser::NodeType::Element:
        name = parser.name();
        if (navDepth == NavDepth::Root && name == "ncx") {
          navDepth = NavDepth::Ncx;
        } else if (navDepth == NavDepth::Ncx && name == "docTitle") {
          navDepth = NavDepth::DocTitle;
        } else if (navDepth == NavDepth::DocTitle && name == "text") {
          navDepth = NavDepth::DocText;
        } else if (navDepth == NavDepth::Ncx && name == "navMap") {
          navDepth = NavDepth::NavMap;
        } else if (navDepth == NavDepth::NavMap && name == "navPoint") {
          navDepth = NavDepth::NavPoint;
          currentTocSrc = "";
          currentTocText = "";
        } else if (navDepth == NavDepth::NavPoint && name == "content") {
          currentTocSrc = parser.getAttribute("src");
          navDepth = NavDepth::Content;
        } else if (navDepth == NavDepth::NavPoint && name == "navPoint") {
          navDepth = NavDepth::NavPoint;
          tryCommitNavPoint();
        } else if (navDepth == NavDepth::NavPoint && name == "navLabel") {
          navDepth = NavDepth::NavLabel;
        } else if (navDepth == NavDepth::NavLabel && name == "text") {
          navDepth = NavDepth::NavText;
        }
        break;
      case XmlParser::NodeType::Text:
        if (navDepth == NavDepth::NavText) {
          currentTocText = parser.text();
        } else if (navDepth == NavDepth::DocText) {
          currentTitle = parser.text();
        }
        break;
      case XmlParser::NodeType::EndElement:
        name = parser.name();
        if (navDepth == NavDepth::NavText && name == "text") {
          navDepth = NavDepth::NavLabel;
        } else if (navDepth == NavDepth::NavLabel && name == "navLabel") {
          navDepth = NavDepth::NavPoint;
        } else if (navDepth == NavDepth::Content && name == "content") {
          navDepth = NavDepth::NavPoint;
        } else if (navDepth == NavDepth::NavPoint && name == "navPoint") {
          navDepth = NavDepth::NavMap;
          tryCommitNavPoint();
        } else if (navDepth == NavDepth::NavMap && name == "navMap") {
          navDepth = NavDepth::Ncx;
        } else if (navDepth == NavDepth::DocText && name == "text") {
          navDepth = NavDepth::DocTitle;
        } else if (navDepth == NavDepth::DocTitle && name == "docTitle") {
          navDepth = NavDepth::Ncx;
        } else if (navDepth == NavDepth::Ncx && name == "ncx") {
          navDepth = NavDepth::Root;
        }
        break;
      default:
        break;
    }
  }
  if (title) {
    *title = currentTitle;
  } else {
    neededMemory += currentTitle.size();
  }
}