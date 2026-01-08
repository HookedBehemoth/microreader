#ifndef INDEXED_WORD_PROVIDER_H
#define INDEXED_WORD_PROVIDER_H

#include <cstdint>
#include <vector>

#include "../css/CssStyle.h"
#include "WString.h"
#include "WordProvider.h"
#include "rendering/SimpleFont.h"

/**
 * WordIndex - Represents a word's position in the chapter buffer
 *
 * Words are identified by their start byte offset in the chapter buffer.
 * The end of a word is the start of the next word (or end of buffer).
 */
struct WordIndex {
  uint32_t start;  // Byte offset of word start in chapter buffer
};

/**
 * StyleRange - Represents a style applied to a range of words
 *
 * Styles are stored as word ranges to minimize memory usage.
 * Multiple styles can overlap (e.g., bold within italic).
 */
struct StyleRange {
  uint32_t startWordIndex;  // First word this style applies to
  uint32_t endWordIndex;    // One past the last word (exclusive)
  FontStyle style;          // Style to apply (BOLD, ITALIC, BOLD_ITALIC)
};

/**
 * AlignmentRange - Represents alignment for a range of text
 *
 * Paragraph alignment stored as byte ranges in the chapter buffer.
 */
struct AlignmentRange {
  uint32_t startOffset;  // Byte offset where alignment begins
  uint32_t endOffset;    // Byte offset where alignment ends (exclusive)
  TextAlign alignment;   // Alignment type
};

/**
 * IndexedWordProvider - Memory-efficient word provider
 *
 * This provider loads the entire chapter as one contiguous byte buffer
 * and indexes words by their start positions. This reduces dynamic memory
 * allocation during text layout and navigation.
 *
 * Key features:
 * - Chapter content stored as a single byte buffer
 * - Words indexed by start position (uint32_t array)
 * - Styles stored as word ranges
 * - No string allocation when reading words (returns views into buffer)
 *
 * Usage:
 *   1. Create with chapter content (String or const char*)
 *   2. Words are automatically indexed on construction
 *   3. Use getNextWord()/getPrevWord() which return StyledWord
 *      (the text field is constructed on demand from the buffer)
 */
class IndexedWordProvider : public WordProvider {
 public:
  /**
   * Construct from a String (content is copied)
   */
  explicit IndexedWordProvider(const String& content);

  /**
   * Construct from raw buffer (content is copied)
   */
  IndexedWordProvider(const char* data, size_t length);

  ~IndexedWordProvider() override;

  // WordProvider interface implementation
  bool hasNextWord() override;
  bool hasPrevWord() override;
  StyledWord getNextWord() override;
  StyledWord getPrevWord() override;

  float getPercentage() override;
  float getPercentage(int index) override;
  void setPosition(int index) override;
  int getCurrentIndex() override;
  char peekChar(int offset = 0) override;
  int consumeChars(int n) override;
  bool isInsideWord() override;
  void ungetWord() override;
  void reset() override;

  TextAlign getParagraphAlignment() override;

  // Direct buffer access for layout optimization
  const char* getBufferData() const {
    return buffer_;
  }
  size_t getBufferSize() const {
    return bufferSize_;
  }

  // Word index access for layout optimization
  size_t getWordCount() const {
    return wordIndices_.size();
  }

  /**
   * Get word by index without modifying position
   *
   * @param wordIndex Index into the word indices array
   * @return The word text at that index
   */
  String getWordAt(size_t wordIndex) const;

  /**
   * Get word length at specified word index
   */
  size_t getWordLength(size_t wordIndex) const;

  /**
   * Get the buffer offset for a word index
   */
  uint32_t getWordOffset(size_t wordIndex) const;

  /**
   * Get style for word at specified index
   */
  FontStyle getStyleForWord(size_t wordIndex) const;

  /**
   * Find which word index contains a given byte offset
   * Returns SIZE_MAX if offset is past end
   */
  size_t findWordIndexForOffset(uint32_t offset) const;

  /**
   * Get the current word index (not byte position)
   */
  size_t getCurrentWordIndex() const {
    return currentWordIndex_;
  }

  /**
   * Set position by word index (not byte position)
   */
  void setWordIndex(size_t wordIndex);

 private:
  // Index all words in the buffer
  void indexWords();

  // Parse ESC tokens for style/alignment
  bool isEscToken(size_t pos) const;
  size_t parseEscToken(size_t pos, TextAlign* outAlign = nullptr, FontStyle* outStyle = nullptr) const;
  void updateStyleContext(size_t pos);

  // Word boundary detection
  bool isWordBoundary(char c) const;

  // Skip UTF-8 BOM at start
  void skipUtf8Bom();

  // Buffer holding chapter content
  char* buffer_ = nullptr;
  size_t bufferSize_ = 0;

  // Word indices (start positions of each word)
  std::vector<WordIndex> wordIndices_;

  // Style ranges (sorted by startWordIndex)
  std::vector<StyleRange> styleRanges_;

  // Alignment ranges (sorted by startOffset)
  std::vector<AlignmentRange> alignmentRanges_;

  // Current position state
  size_t currentWordIndex_ = 0;
  size_t prevWordIndex_ = 0;
  size_t bytePosition_ = 0;  // Current byte position for compatibility

  // Current style/alignment state
  FontStyle currentStyle_ = FontStyle::REGULAR;
  TextAlign currentAlignment_ = TextAlign::None;

  // ESC token constants
  static constexpr char ESC_CHAR = '\x1B';
};

#endif  // INDEXED_WORD_PROVIDER_H
