#include "Settings.h"
#include <cstring>

#include <Arduino.h>

namespace {
  const char* SettingsPath = "/microreader/settings.cfg";

  // Integer settings storage
  constexpr int INT_SETTING_COUNT = static_cast<int>(IntSetting::Count);
  constexpr int PATH_SETTING_COUNT = static_cast<int>(PathSetting::Count);
  
  std::optional<int> intSettings[INT_SETTING_COUNT];
  
  // String settings storage (paths limited to 255 chars) - using optional with static array
  std::optional<std::array<char, 256>> stringSettings[PATH_SETTING_COUNT];
  
  // Mapping tables (single source of truth)
  constexpr const char* INT_SETTING_KEYS[INT_SETTING_COUNT] = {
    "settings.margin",
    "settings.lineHeight",
    "settings.alignment",
    "settings.showChapterNumbers",
    "settings.fontFamily",
    "settings.fontSize",
    "settings.uiFontSize",
    "ui.screen",
    "ui.previousScreen",
  };
  
  constexpr const char* PATH_SETTING_KEYS[PATH_SETTING_COUNT] = {
    "filebrowser.selected",
    "textviewer.lastPath",
    "pkpass.lastPath",
  };

  std::optional<IntSetting> IntKeyFromString(const char* str) {
    for (int i = 0; i < INT_SETTING_COUNT; i++) {
      if (strcmp(str, INT_SETTING_KEYS[i]) == 0) {
        return static_cast<IntSetting>(i);
      }
    }
    return std::nullopt;
  }

  std::optional<PathSetting> PathKeyFromString(const char* str) {
    for (int i = 0; i < PATH_SETTING_COUNT; i++) {
      if (strcmp(str, PATH_SETTING_KEYS[i]) == 0) {
        return static_cast<PathSetting>(i);
      }
    }
    return std::nullopt;
  }

  const char* IntKeyToString(IntSetting key) {
    int idx = static_cast<int>(key);
    if (idx >= 0 && idx < INT_SETTING_COUNT) {
      return INT_SETTING_KEYS[idx];
    }
    return "";
  }

  const char* PathKeyToString(PathSetting key) {
    int idx = static_cast<int>(key);
    if (idx >= 0 && idx < PATH_SETTING_COUNT) {
      return PATH_SETTING_KEYS[idx];
    }
    return "";
  }
}

Settings::Settings(SDCardManager& sdManager) : sd(sdManager) {}

bool Settings::load() {
  if (!sd.ready())
    return false;

  char buf[2048];
  size_t r = sd.readFileToBuffer(SettingsPath, buf, sizeof(buf));
  if (r == 0) {
    // Reset all settings
    for (int i = 0; i < INT_SETTING_COUNT; i++) {
      intSettings[i].reset();
    }
    for (int i = 0; i < PATH_SETTING_COUNT; i++) {
      stringSettings[i].reset();
    }
    return false;
  }

  // Ensure null-termination
  buf[sizeof(buf) - 1] = '\0';
  parseSettingsBuffer(buf);
  return true;
}

bool Settings::save() {
  if (!sd.ready())
    return false;
  
  char buf[2048];
  char* p = buf;
  char* end = buf + sizeof(buf) - 1;
  
  // Dump all integer settings that have values
  for (int i = 0; i < INT_SETTING_COUNT && p < end; i++) {
    if (intSettings[i].has_value()) {
      const char* key = IntKeyToString(static_cast<IntSetting>(i));
      int written = snprintf(p, end - p, "%s=%d\n", key, intSettings[i].value());
      if (written > 0 && p + written < end) {
        p += written;
      }
    }
  }
  
  // Dump all string settings that have values
  for (int i = 0; i < PATH_SETTING_COUNT && p < end; i++) {
    if (stringSettings[i].has_value()) {
      const char* key = PathKeyToString(static_cast<PathSetting>(i));
      int written = snprintf(p, end - p, "%s=%s\n", key, stringSettings[i].value().data());
      if (written > 0 && p + written < end) {
        p += written;
      }
    }
  }
  
  *p = '\0';
  return sd.writeFile(SettingsPath, std::string_view(buf, p - buf));
}

bool Settings::getInt(IntSetting key, int& out) const {
  int idx = (int)key;
  if (idx >= 0 && idx < (int)IntSetting::Count && intSettings[idx].has_value()) {
    out = intSettings[idx].value();
    return true;
  }
  return false;
}

void Settings::setInt(IntSetting key, int v) {
  int idx = (int)key;
  if (idx >= 0 && idx < (int)IntSetting::Count) {
    intSettings[idx] = v;
  }
}

std::string_view Settings::getString(PathSetting key) const {
  int idx = (int)key;
  if (idx >= 0 && idx < (int)PathSetting::Count && stringSettings[idx].has_value()) {
    return std::string_view(stringSettings[idx].value().data());
  }
  return std::string_view();
}

void Settings::setString(PathSetting key, const String& value) {
  int idx = (int)key;
  if (idx >= 0 && idx < (int)PathSetting::Count) {
    std::array<char, 256> arr;
    strncpy(arr.data(), value.c_str(), 255);
    arr[255] = '\0';
    stringSettings[idx] = arr;
  }
}

void Settings::parseSettingsBuffer(const char* buf) {
  // Reset all settings
  for (int i = 0; i < INT_SETTING_COUNT; i++) {
    intSettings[i].reset();
  }
  for (int i = 0; i < PATH_SETTING_COUNT; i++) {
    stringSettings[i].reset();
  }
  
  const char* p = buf;
  while (*p) {
    // Read line
    const char* eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    if (len > 0 && len < 512) {
      char line[512];
      strncpy(line, p, len);
      line[len] = '\0';
      
      // Trim leading whitespace
      char* start = line;
      while (*start && (*start == ' ' || *start == '\t'))
        start++;
      
      // Trim trailing whitespace
      char* end = start + strlen(start) - 1;
      while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
        *end-- = '\0';
      
      // Skip empty lines and comments
      if (*start == '\0' || *start == '#') continue;

      char* eq = strchr(start, '=');
      if (!eq) continue;

      *eq = '\0';
      const char* key = start;
      const char* val = eq + 1;
      
      // Try to parse as integer setting
      auto intKey = IntKeyFromString(key);
      if (intKey) {
        intSettings[(int)*intKey] = atoi(val);
        continue;
      }

      // Try to parse as path setting
      auto pathKey = PathKeyFromString(key);
      if (pathKey) {
        std::array<char, 256> arr;
        strncpy(arr.data(), val, 255);
        arr[255] = '\0';
        stringSettings[(int)*pathKey] = arr;
      }
    }
    if (!eol)
      break;
    p = eol + 1;
  }
}
