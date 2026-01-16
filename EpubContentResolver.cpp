#include "EpubContentResolver.hpp"
#include "ZipParser.hpp"

namespace epub {

// Resolve files relative to content base path and returns zip entry index
zip::Result<uint16_t> ContentResolver::resolve(StringView href) const {
  // Strip any anchor (#...) from the src path
  href = href.sliceUntil('#');
  
  // Build full path relative to content base
  char pathBuffer[512];
  StringView fullPath;
  if (basePath.size() > 0) {
    fullPath = join(pathBuffer, sizeof(pathBuffer),
      basePath, "/", href);
  } else {
    fullPath = href;
  }

  return zip::findFileEntryIndex(entries, fullPath);
}

}
