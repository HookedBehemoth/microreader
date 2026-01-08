#ifndef INDEXED_LAYOUT_H
#define INDEXED_LAYOUT_H

#include <cstdint>
#include <vector>

#include "../content/css/CssStyle.h"
#include "rendering/SimpleFont.h"

/**
 * IndexedWord - Represents a word in layout by its indices into the chapter buffer
 *
 * Instead of storing the word text, we store:
 * - wordIndex: Index into the IndexedWordProvider's word array
 * - Additional layout information (position, width, split status)
 */
struct IndexedWord {
  uint32_t wordIndex;   // Index into provider's word array
  int16_t width;        // Rendered width in pixels
  int16_t x;            // X position on screen
  int16_t y;            // Y position on screen
  bool wasSplit;        // True if word was hyphenated
  FontStyle style;      // Font style for rendering

  IndexedWord()
      : wordIndex(0), width(0), x(0), y(0), wasSplit(false), style(FontStyle::REGULAR) {}

  IndexedWord(uint32_t idx, int16_t w, int16_t xPos, int16_t yPos, bool split, FontStyle s = FontStyle::REGULAR)
      : wordIndex(idx), width(w), x(xPos), y(yPos), wasSplit(split), style(s) {}
};

/**
 * LineRange - Represents a line as a range of word indices
 *
 * This is a more compact representation that stores just the
 * start and end word indices, with additional metadata about
 * split words at line boundaries.
 */
struct LineRange {
  uint32_t startWordIndex;  // First word index in this line
  uint32_t endWordIndex;    // One past the last word index (exclusive)
  int16_t y;                // Y position of this line
  TextAlign alignment;      // Alignment for this line

  // Split word information
  bool hasLeadingSplit;   // True if line starts with continuation of split word
  bool hasTrailingSplit;  // True if line ends with hyphenated word
  uint32_t splitCharStart;  // If hasLeadingSplit, char index where this part starts
  uint32_t splitCharEnd;    // If hasTrailingSplit, char index where hyphenation occurs

  LineRange()
      : startWordIndex(0),
        endWordIndex(0),
        y(0),
        alignment(TextAlign::Left),
        hasLeadingSplit(false),
        hasTrailingSplit(false),
        splitCharStart(0),
        splitCharEnd(0) {}
};

/**
 * IndexedLine - A line with its words stored as indices
 *
 * This is an intermediate representation used during layout.
 * Contains the list of IndexedWord items plus alignment.
 */
struct IndexedLine {
  std::vector<IndexedWord> words;
  TextAlign alignment;

  IndexedLine() : alignment(TextAlign::Left) {}
};

/**
 * IndexedPageLayout - Page layout using indexed word representation
 *
 * This layout stores words by their indices rather than duplicating
 * the word text. This significantly reduces memory usage when dealing
 * with large chapters.
 */
struct IndexedPageLayout {
  std::vector<IndexedLine> lines;
  int startPosition;  // Provider byte position at page start
  int endPosition;    // Provider byte position at page end

  // For efficient rendering, we can also store compact line ranges
  std::vector<LineRange> lineRanges;

  IndexedPageLayout() : startPosition(0), endPosition(0) {}

  void clear() {
    lines.clear();
    lineRanges.clear();
    startPosition = 0;
    endPosition = 0;
  }

  bool isEmpty() const {
    return lines.empty() && lineRanges.empty();
  }
};

/**
 * ChapterLayoutCache - Caches layout for an entire chapter
 *
 * When a chapter is loaded, we can pre-compute and cache the
 * line breaks for the entire chapter. This enables instant
 * page navigation without re-computing layout.
 */
struct ChapterLayoutCache {
  // All line ranges for the entire chapter
  std::vector<LineRange> allLineRanges;

  // Page boundaries (indices into allLineRanges)
  // pageBreaks[i] is the index of the first line of page i+1
  // pageBreaks[0] = lines per page (start of page 1)
  std::vector<uint32_t> pageBreaks;

  // Layout configuration used to generate this cache
  int16_t pageWidth;
  int16_t pageHeight;
  int16_t marginLeft;
  int16_t marginRight;
  int16_t marginTop;
  int16_t marginBottom;
  int16_t lineHeight;

  // Total number of pages
  uint32_t totalPages() const {
    return pageBreaks.size() + 1;
  }

  // Get the line range for a specific page
  void getLinesForPage(uint32_t pageIndex, uint32_t& startLine, uint32_t& endLine) const {
    if (pageIndex == 0) {
      startLine = 0;
      endLine = pageBreaks.empty() ? (uint32_t)allLineRanges.size() : pageBreaks[0];
    } else if (pageIndex <= pageBreaks.size()) {
      startLine = pageBreaks[pageIndex - 1];
      endLine = (pageIndex < pageBreaks.size()) ? pageBreaks[pageIndex] : (uint32_t)allLineRanges.size();
    } else {
      startLine = 0;
      endLine = 0;
    }
  }

  bool isValid() const {
    return !allLineRanges.empty();
  }

  void clear() {
    allLineRanges.clear();
    pageBreaks.clear();
  }
};

#endif  // INDEXED_LAYOUT_H
