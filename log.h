#pragma once

#ifndef ARDUINO
#include <cstdio>
#define LogF(...) std::printf(__VA_ARGS__)
#define Log(...) std::printf("%s", __VA_ARGS__)
#else
#include <Arduino.h>
#define LogF(...) Serial.printf(__VA_ARGS__)
#define Log(...) Serial.print(__VA_ARGS__)
#endif