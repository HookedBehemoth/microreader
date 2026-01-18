#pragma once

#include <StringView.hpp>
#include <cstdio>
#include <optional>

#ifndef ARDUINO
#include <cstdio>
#define PrintF(...) std::printf(__VA_ARGS__)
#define Print(...) std::printf("%s", __VA_ARGS__)
#else
#include <Arduino.h>
#define PrintF(...) Serial.printf(__VA_ARGS__)
#define Print(...) Serial.print(__VA_ARGS__)
#endif

static inline void put(StringView str) {
#ifndef ARDUINO
  fwrite(str.data(), 1, str.size(), stdout);
#else
  Serial.write((const uint8_t*)str.data(), str.size());
#endif
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
