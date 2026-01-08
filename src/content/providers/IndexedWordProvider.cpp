#include "IndexedWordProvider.h"

#include <algorithm>
#include <cstring>

// ESC-based format constants (same as FileWordProvider)
// Format: ESC + command byte (2 bytes total, fixed length)
static bool tryGetAlignmentStart(char cmd, TextAlign* out) {
  switch (cmd) {
    case 'L':
      if (out) *out = TextAlign::Left;
      return true;
    case 'R':
      if (out) *out = TextAlign::Right;
      return true;
    case 'C':
      if (out) *out = TextAlign::Center;
      return true;
    case 'J':
      if (out) *out = TextAlign::Justify;
      return true;
  }
  return false;
}

static bool tryGetAlignmentEnd(char cmd, TextAlign* out) {
  switch (cmd) {
    case 'l':
      if (out) *out = TextAlign::Left;
      return true;
    case 'r':
      if (out) *out = TextAlign::Right;
      return true;
    case 'c':
      if (out) *out = TextAlign::Center;
      return true;
    case 'j':
      if (out) *out = TextAlign::Justify;
      return true;
  }
  return false;
}

static bool tryGetStyleForward(char cmd, FontStyle* out) {
  switch (cmd) {
    case 'B':
      if (out) *out = FontStyle::BOLD;
      return true;
    case 'b':
      if (out) *out = FontStyle::REGULAR;
      return true;
    case 'I':
      if (out) *out = FontStyle::ITALIC;
      return true;
    case 'i':
      if (out) *out = FontStyle::REGULAR;
      return true;
    case 'X':
      if (out) *out = FontStyle::BOLD_ITALIC;
      return true;
    case 'x':
      if (out) *out = FontStyle::REGULAR;
      return true;
    case 'H':
      if (out) *out = FontStyle::HIDDEN;
      return true;
    case 'h':
      if (out) *out = FontStyle::REGULAR;
      return true;
  }
  return false;
}

static bool isEscCommandChar(char cmd) {
  return tryGetAlignmentStart(cmd, nullptr) || tryGetAlignmentEnd(cmd, nullptr) || tryGetStyleForward(cmd, nullptr);
}

IndexedWordProvider::IndexedWordProvider(const String& content) {
  bufferSize_ = content.length();
  if (bufferSize_ > 0) {
    buffer_ = new char[bufferSize_ + 1];
    std::memcpy(buffer_, content.c_str(), bufferSize_);
    buffer_[bufferSize_] = '\0';
  }
  try {
    skipUtf8Bom();
    indexWords();
  } catch (...) {
    delete[] buffer_;
    buffer_ = nullptr;
    throw;
  }
}

IndexedWordProvider::IndexedWordProvider(const char* data, size_t length) {
  bufferSize_ = length;
  if (bufferSize_ > 0 && data != nullptr) {
    buffer_ = new char[bufferSize_ + 1];
    std::memcpy(buffer_, data, bufferSize_);
    buffer_[bufferSize_] = '\0';
  }
  try {
    skipUtf8Bom();
    indexWords();
  } catch (...) {
    delete[] buffer_;
    buffer_ = nullptr;
    throw;
  }
}

IndexedWordProvider::~IndexedWordProvider() {
  delete[] buffer_;
  buffer_ = nullptr;
}

void IndexedWordProvider::skipUtf8Bom() {
  // Check for UTF-8 BOM at start (0xEF 0xBB 0xBF)
  if (bufferSize_ >= 3 && buffer_ != nullptr) {
    if ((unsigned char)buffer_[0] == 0xEF && (unsigned char)buffer_[1] == 0xBB && (unsigned char)buffer_[2] == 0xBF) {
      // Skip BOM by adjusting start position
      // We don't modify the buffer, just start indexing after BOM
      bytePosition_ = 3;
    }
  }
}

bool IndexedWordProvider::isWordBoundary(char c) const {
  return (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == ESC_CHAR);
}

bool IndexedWordProvider::isEscToken(size_t pos) const {
  if (pos + 1 >= bufferSize_) return false;
  if (buffer_[pos] != ESC_CHAR) return false;
  return isEscCommandChar(buffer_[pos + 1]);
}

size_t IndexedWordProvider::parseEscToken(size_t pos, TextAlign* outAlign, FontStyle* outStyle) const {
  if (pos + 1 >= bufferSize_) return 0;
  if (buffer_[pos] != ESC_CHAR) return 0;

  char cmd = buffer_[pos + 1];

  TextAlign align;
  if (tryGetAlignmentStart(cmd, &align)) {
    if (outAlign) *outAlign = align;
    return 2;
  }
  if (tryGetAlignmentEnd(cmd, &align)) {
    if (outAlign) *outAlign = TextAlign::None;
    return 2;
  }

  FontStyle style;
  if (tryGetStyleForward(cmd, &style)) {
    if (outStyle) *outStyle = style;
    return 2;
  }

  return 0;
}

void IndexedWordProvider::indexWords() {
  wordIndices_.clear();
  styleRanges_.clear();
  alignmentRanges_.clear();

  if (buffer_ == nullptr || bufferSize_ == 0) return;

  size_t pos = bytePosition_;  // Start after BOM if present
  FontStyle currentStyle = FontStyle::REGULAR;
  TextAlign currentAlignment = TextAlign::None;
  uint32_t styleStartWord = 0;
  uint32_t alignmentStartOffset = 0;
  bool inStyle = false;
  bool inAlignment = false;

  while (pos < bufferSize_) {
    // Skip ESC tokens and track style/alignment changes
    while (pos < bufferSize_ && isEscToken(pos)) {
      TextAlign newAlign = currentAlignment;
      FontStyle newStyle = currentStyle;
      size_t tokenLen = parseEscToken(pos, &newAlign, &newStyle);

      // Check for alignment change
      if (newAlign != currentAlignment) {
        // Close previous alignment range if any
        if (inAlignment && alignmentStartOffset < pos) {
          alignmentRanges_.push_back({alignmentStartOffset, (uint32_t)pos, currentAlignment});
        }
        currentAlignment = newAlign;
        alignmentStartOffset = (uint32_t)pos + (uint32_t)tokenLen;
        inAlignment = (currentAlignment != TextAlign::None);
      }

      // Check for style change
      if (newStyle != currentStyle) {
        // Close previous style range if any
        if (inStyle && styleStartWord < wordIndices_.size()) {
          styleRanges_.push_back({styleStartWord, (uint32_t)wordIndices_.size(), currentStyle});
        }
        currentStyle = newStyle;
        styleStartWord = (uint32_t)wordIndices_.size();
        inStyle = (currentStyle != FontStyle::REGULAR);
      }

      pos += tokenLen;
    }

    if (pos >= bufferSize_) break;

    // Skip carriage returns
    while (pos < bufferSize_ && buffer_[pos] == '\r') {
      pos++;
    }

    if (pos >= bufferSize_) break;

    // Check for ESC token again after skipping CR
    if (isEscToken(pos)) continue;

    char c = buffer_[pos];

    // Record word start
    WordIndex wordIdx;
    wordIdx.start = (uint32_t)pos;
    wordIndices_.push_back(wordIdx);

    // Handle single-character tokens (newline, tab)
    if (c == '\n' || c == '\t') {
      pos++;
      // Close alignment at newline
      if (c == '\n' && inAlignment) {
        alignmentRanges_.push_back({alignmentStartOffset, (uint32_t)pos, currentAlignment});
        currentAlignment = TextAlign::None;
        inAlignment = false;
      }
      continue;
    }

    // Handle space sequences
    if (c == ' ') {
      while (pos < bufferSize_ && buffer_[pos] == ' ') {
        pos++;
      }
      continue;
    }

    // Handle regular word
    while (pos < bufferSize_) {
      if (isEscToken(pos)) break;
      char cc = buffer_[pos];
      if (cc == '\r') {
        pos++;
        continue;
      }
      if (cc == ' ' || cc == '\n' || cc == '\t') break;
      pos++;
    }
  }

  // Close any remaining open ranges
  if (inStyle && styleStartWord < wordIndices_.size()) {
    styleRanges_.push_back({styleStartWord, (uint32_t)wordIndices_.size(), currentStyle});
  }
  if (inAlignment && alignmentStartOffset < bufferSize_) {
    alignmentRanges_.push_back({alignmentStartOffset, (uint32_t)bufferSize_, currentAlignment});
  }
}

void IndexedWordProvider::updateStyleContext(size_t pos) {
  // Reset to default
  currentStyle_ = FontStyle::REGULAR;
  currentAlignment_ = TextAlign::None;

  if (buffer_ == nullptr || pos == 0) return;

  // Find paragraph start (newline boundary)
  size_t paraStart = 0;
  for (size_t i = pos; i > 0; --i) {
    if (buffer_[i - 1] == '\n') {
      paraStart = i;
      break;
    }
  }

  // Scan forward from paragraph start to current position, processing style tokens
  size_t scanPos = paraStart;
  while (scanPos < pos) {
    if (buffer_[scanPos] == ESC_CHAR && scanPos + 1 < bufferSize_) {
      FontStyle style;
      TextAlign align;
      if (parseEscToken(scanPos, &align, &style)) {
        if (align != TextAlign::None) {
          currentAlignment_ = align;
        }
        currentStyle_ = style;
      }
      scanPos += 2;
    } else {
      scanPos++;
    }
  }
}

String IndexedWordProvider::getWordAt(size_t wordIndex) const {
  if (wordIndex >= wordIndices_.size()) return String();

  uint32_t start = wordIndices_[wordIndex].start;
  uint32_t end;

  // Find end of word (start of next word or end of buffer)
  if (wordIndex + 1 < wordIndices_.size()) {
    end = wordIndices_[wordIndex + 1].start;
    // Walk backwards to skip ESC tokens between words
    while (end > start && end >= 2) {
      if (buffer_[end - 2] == ESC_CHAR && isEscCommandChar(buffer_[end - 1])) {
        end -= 2;
      } else {
        break;
      }
    }
    // Skip trailing carriage returns
    while (end > start && buffer_[end - 1] == '\r') {
      end--;
    }
  } else {
    end = (uint32_t)bufferSize_;
    // Skip trailing ESC tokens and CR
    while (end > start) {
      if (end >= 2 && buffer_[end - 2] == ESC_CHAR && isEscCommandChar(buffer_[end - 1])) {
        end -= 2;
      } else if (buffer_[end - 1] == '\r') {
        end--;
      } else {
        break;
      }
    }
  }

  if (end <= start) return String();

  // Build string without carriage returns
  String result;
  for (uint32_t i = start; i < end; i++) {
    if (buffer_[i] != '\r') {
      result += buffer_[i];
    }
  }
  return result;
}

size_t IndexedWordProvider::getWordLength(size_t wordIndex) const {
  if (wordIndex >= wordIndices_.size()) return 0;

  uint32_t start = wordIndices_[wordIndex].start;
  uint32_t end;

  if (wordIndex + 1 < wordIndices_.size()) {
    end = wordIndices_[wordIndex + 1].start;
    // Skip ESC tokens
    while (end > start && end >= 2) {
      if (buffer_[end - 2] == ESC_CHAR && isEscCommandChar(buffer_[end - 1])) {
        end -= 2;
      } else {
        break;
      }
    }
    // Skip CR
    while (end > start && buffer_[end - 1] == '\r') {
      end--;
    }
  } else {
    end = (uint32_t)bufferSize_;
    while (end > start) {
      if (end >= 2 && buffer_[end - 2] == ESC_CHAR && isEscCommandChar(buffer_[end - 1])) {
        end -= 2;
      } else if (buffer_[end - 1] == '\r') {
        end--;
      } else {
        break;
      }
    }
  }

  // Count non-CR characters
  size_t len = 0;
  for (uint32_t i = start; i < end; i++) {
    if (buffer_[i] != '\r') len++;
  }
  return len;
}

uint32_t IndexedWordProvider::getWordOffset(size_t wordIndex) const {
  if (wordIndex >= wordIndices_.size()) return (uint32_t)bufferSize_;
  return wordIndices_[wordIndex].start;
}

FontStyle IndexedWordProvider::getStyleForWord(size_t wordIndex) const {
  // Find the style range that contains this word
  for (const auto& range : styleRanges_) {
    if (wordIndex >= range.startWordIndex && wordIndex < range.endWordIndex) {
      return range.style;
    }
  }
  return FontStyle::REGULAR;
}

size_t IndexedWordProvider::findWordIndexForOffset(uint32_t offset) const {
  if (wordIndices_.empty()) return SIZE_MAX;

  // If offset is before first word, return first word index (0)
  if (offset < wordIndices_[0].start) return 0;

  // Binary search for the word containing this offset
  size_t left = 0;
  size_t right = wordIndices_.size();

  while (left < right) {
    size_t mid = left + (right - left) / 2;
    if (wordIndices_[mid].start <= offset) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }

  // left is now one past the word that starts at or before offset
  return (left > 0) ? left - 1 : 0;
}

void IndexedWordProvider::setWordIndex(size_t wordIndex) {
  if (wordIndex >= wordIndices_.size()) {
    currentWordIndex_ = wordIndices_.size();
    bytePosition_ = bufferSize_;
  } else {
    currentWordIndex_ = wordIndex;
    bytePosition_ = wordIndices_[wordIndex].start;
  }
  prevWordIndex_ = currentWordIndex_;
  updateStyleContext(bytePosition_);
}

bool IndexedWordProvider::hasNextWord() {
  return currentWordIndex_ < wordIndices_.size();
}

bool IndexedWordProvider::hasPrevWord() {
  return currentWordIndex_ > 0;
}

StyledWord IndexedWordProvider::getNextWord() {
  if (currentWordIndex_ >= wordIndices_.size()) {
    return StyledWord();
  }

  prevWordIndex_ = currentWordIndex_;
  String text = getWordAt(currentWordIndex_);
  FontStyle style = getStyleForWord(currentWordIndex_);

  // Advance to next word
  currentWordIndex_++;
  if (currentWordIndex_ < wordIndices_.size()) {
    bytePosition_ = wordIndices_[currentWordIndex_].start;
  } else {
    bytePosition_ = bufferSize_;
  }

  return StyledWord(text, style);
}

StyledWord IndexedWordProvider::getPrevWord() {
  if (currentWordIndex_ == 0) {
    return StyledWord();
  }

  prevWordIndex_ = currentWordIndex_;
  currentWordIndex_--;

  String text = getWordAt(currentWordIndex_);
  FontStyle style = getStyleForWord(currentWordIndex_);
  bytePosition_ = wordIndices_[currentWordIndex_].start;

  return StyledWord(text, style);
}

float IndexedWordProvider::getPercentage() {
  if (bufferSize_ == 0) return 1.0f;
  return static_cast<float>(bytePosition_) / static_cast<float>(bufferSize_);
}

float IndexedWordProvider::getPercentage(int index) {
  if (bufferSize_ == 0) return 1.0f;
  return static_cast<float>(index) / static_cast<float>(bufferSize_);
}

void IndexedWordProvider::setPosition(int index) {
  if (index < 0) index = 0;
  if ((size_t)index >= bufferSize_) {
    bytePosition_ = bufferSize_;
    currentWordIndex_ = wordIndices_.size();
  } else {
    bytePosition_ = (size_t)index;
    currentWordIndex_ = findWordIndexForOffset((uint32_t)index);
  }
  prevWordIndex_ = currentWordIndex_;
  updateStyleContext(bytePosition_);
}

int IndexedWordProvider::getCurrentIndex() {
  return (int)bytePosition_;
}

char IndexedWordProvider::peekChar(int offset) {
  int pos = (int)bytePosition_ + offset;
  if (pos < 0 || (size_t)pos >= bufferSize_) {
    return '\0';
  }
  return buffer_[pos];
}

int IndexedWordProvider::consumeChars(int n) {
  if (n <= 0) return 0;

  int consumed = 0;
  while (consumed < n && bytePosition_ < bufferSize_) {
    // Skip ESC tokens
    if (isEscToken(bytePosition_)) {
      bytePosition_ += 2;
      continue;
    }

    char c = buffer_[bytePosition_];
    bytePosition_++;
    // Skip carriage returns
    if (c != '\r') {
      consumed++;
    }
  }

  // Update word index to match new byte position
  currentWordIndex_ = findWordIndexForOffset((uint32_t)bytePosition_);
  return consumed;
}

bool IndexedWordProvider::isInsideWord() {
  if (bytePosition_ <= 0 || bytePosition_ >= bufferSize_) {
    return false;
  }

  auto isWordChar = [](char c) { return c != '\0' && c != ' ' && c != '\n' && c != '\t' && c != '\r'; };

  char prevChar = buffer_[bytePosition_ - 1];
  char currentChar = buffer_[bytePosition_];

  return isWordChar(prevChar) && isWordChar(currentChar);
}

void IndexedWordProvider::ungetWord() {
  currentWordIndex_ = prevWordIndex_;
  if (currentWordIndex_ < wordIndices_.size()) {
    bytePosition_ = wordIndices_[currentWordIndex_].start;
  } else {
    bytePosition_ = bufferSize_;
  }
}

void IndexedWordProvider::reset() {
  currentWordIndex_ = 0;
  prevWordIndex_ = 0;
  bytePosition_ = 0;
  // Skip BOM if present
  if (bufferSize_ >= 3 && buffer_ != nullptr) {
    if ((unsigned char)buffer_[0] == 0xEF && (unsigned char)buffer_[1] == 0xBB && (unsigned char)buffer_[2] == 0xBF) {
      bytePosition_ = 3;
    }
  }
  updateStyleContext(bytePosition_);
}

TextAlign IndexedWordProvider::getParagraphAlignment() {
  // Find alignment for current byte position
  for (const auto& range : alignmentRanges_) {
    if (bytePosition_ >= range.startOffset && bytePosition_ < range.endOffset) {
      return range.alignment;
    }
  }
  return TextAlign::None;
}
