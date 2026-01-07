#pragma once

#include "rendering/SimpleFont.h"

// Font family
const FontFamily* getCurrentFontFamily();
void setCurrentFontFamily(const FontFamily* family);

// Simple fonts
const SimpleGFXfont* getMainFont();
void setMainFont(const SimpleGFXfont* font);

const SimpleGFXfont* getTitleFont();
void setTitleFont(const SimpleGFXfont* font);