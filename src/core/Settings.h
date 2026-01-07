#pragma once

#include <string_view>

// Predefined setting keys
enum class IntSetting {
  MARGIN,
  LINE_HEIGHT,
  ALIGNMENT,
  SHOW_CHAPTER_NUMBERS,
  FONT_FAMILY,
  FONT_SIZE,
  UI_FONT_SIZE,
  UI_SCREEN,
  UI_PREVIOUS_SCREEN,
  Count
};

enum class PathSetting {
  FILEBROWSER_SELECTED,
  TEXTVIEWER_LAST_PATH,
  PKPASS_LAST_PATH,
  Count
};

namespace Settings {
  // Load from /microreader/settings.cfg or import legacy settings
  bool load();
  // Persist current settings to SD
  bool save();

  // Get/Set integer values
  bool getInt(IntSetting key, int& out);
  void setInt(IntSetting key, int v);

  // Get/Set string values (paths up to 255 chars)
  std::string_view getString(PathSetting key);
  void setString(PathSetting key, std::string_view value);
};
