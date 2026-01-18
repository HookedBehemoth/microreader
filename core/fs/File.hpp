#pragma once

#ifndef ARDUINO
#include <cstdio>
#else
#include <Arduino.h>
#endif

namespace fs {

class File {
 private:
#ifndef ARDUINO
  FILE* fp;
#else

#endif
};

}
