#pragma once

#include "stringview.h"
#include <cstdio>
#include <optional>

// #ifndef ARDUINO
// #include <cstdio>
// #define LogF(...) std::printf(__VA_ARGS__)
// #define Log(...) std::printf("%s", __VA_ARGS__)
// #else
// #include <Arduino.h>
// #define LogF(...) Serial.printf(__VA_ARGS__)
// #define Log(...) Serial.print(__VA_ARGS__)
// #endif

static inline void put(StringView str) {
  fwrite(str.data(), 1, str.size(), stdout);
}

template <typename... Args>
static inline void print(const Args&... args) {
    (put(args), ...);
}

template <typename... Args>
static inline void println(const Args&... args) {
    print(args...);
    print("\n");
}

template <typename... Args>
static inline void iprintln(int indent, const Args&... args) {
    for (int i = 0; i < indent; i++) {
        print("  ");
    }
    println(args...);
}
