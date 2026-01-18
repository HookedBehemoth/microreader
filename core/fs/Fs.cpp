#include "Fs.hpp"

#include <sys/stat.h>
#include <cstddef>
#include <cstring>
#include <cerrno>

namespace fs {

bool ensurePath(StringView fullPath) {
  constexpr size_t MaxPathLength = 512;
  if (fullPath.size() >= MaxPathLength) {
    return false;
  }
  char pathBuffer[MaxPathLength] = {};
  size_t lastSlash = 0;
  while (lastSlash < fullPath.size()) {
    size_t segmentLength = fullPath.find('/', lastSlash + 1);
    if (segmentLength == fullPath.size()) {
      break;
    }
    std::memcpy(pathBuffer, fullPath.data(), segmentLength);
    pathBuffer[segmentLength] = '\0';
    int rc = mkdir(pathBuffer, 0755);
    if (rc != 0 && errno != EEXIST) {
      return false;
    }
    if (segmentLength + 1 >= fullPath.size()) {
      break;
    }
    lastSlash = segmentLength;
  }
  return true;
}

}