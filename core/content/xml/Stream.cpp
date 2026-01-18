#include "Stream.hpp"

#include <csetjmp>

extern mem::Allocator g_allocator;

namespace xml {

namespace {

size_t g_allocated = 0;
void* bumpMalloc(size_t size) {
  g_allocated += size;
  void* ptr = g_allocator.bumpAlloc<std::byte>(size);
  // PrintF("bumpMalloc called for size %zu -> %p\n", size, ptr);
  return ptr;
}
void* bumpRealloc(void* ptr, size_t newSize) {
  (void)ptr;
  (void)newSize;
  PrintF("bumpRealloc called!\n");
  std::abort();
}
void bumpFree(void* ptr) {
  // PrintF("bumpFree called for %p\n", ptr);
  (void)ptr;
  // no-op, memory will be freed when bump scope ends
}

const XML_Memory_Handling_Suite MemorySuite = {
  bumpMalloc,
  bumpRealloc,
  bumpFree
};
}

Result<void> parseFileStream(
  FILE* fp, const zip::ZipFileEntry& fileEntry,
  xml::StreamContext* userData,
  XML_StartElementHandler startElement,
  XML_EndElementHandler endElement,
  XML_CharacterDataHandler charData,
  mem::Allocator& allocator)
{
  g_allocated = 0;
  struct ScopeExit {
    ~ScopeExit() {
      PrintF("Expat allocated 0x%zx bytes during parse\n", g_allocated);
    }
  } scopeExit;
  auto frontScope = allocator.beginFrontScope();

  auto parser = XML_ParserCreate_MM(nullptr, &xml::MemorySuite, nullptr);
  XML_SetUserData(parser, userData);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, charData);

  struct StreamTocUserData {
    XML_Parser parser;
    size_t position;
    size_t totalSize;
    std::jmp_buf jumpBuf;
  } streamTocUser;
  streamTocUser.parser = parser;
  streamTocUser.position = 0;
  streamTocUser.totalSize = fileEntry.uncompressedSize;
  int returnCode = setjmp(streamTocUser.jumpBuf);
  if (returnCode < 0) {
    // XML_ParserFree(parser); // no-op
    PrintF("Error parsing xml: %s at line %lu\n",
      XML_ErrorString(XML_GetErrorCode(parser)),
      XML_GetCurrentLineNumber(parser));
    return Error::InvalidFormat; // TODO
  } else if (returnCode > 0) {
    PrintF("Bailing xml parse at user request\n");
    return {};
  }

  auto streamXml = [](const void* data, size_t size, size_t count, void* user_data) -> size_t {
    auto user = (StreamTocUserData*)user_data;
    size_t toRead = size * count;
    user->position += toRead;
    bool isFinal = user->position == user->totalSize;
    auto parseResult = XML_Parse(user->parser, (const char*)data, static_cast<int>(toRead), isFinal);
    // bail if something went wrong during parse
    if (parseResult == XML_STATUS_ERROR) {
      std::longjmp(user->jumpBuf, -1);
    }
    // bail at user request
    auto streamCtx = (StreamContext*)XML_GetUserData(user->parser);
    if (streamCtx && streamCtx->bail) {
      std::longjmp(user->jumpBuf, 1);
    }
    return toRead;
  };

  auto result = zip::streamFileEntry(
    fp, fileEntry,
    streamXml, &streamTocUser,
    allocator);
  // XML_ParserFree(parser); // no-op

  if (!result.has_value()) {
    return Error::InvalidFormat;
  }
  return {};
}

}
