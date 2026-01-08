/**
 * IndexedLayoutTest.cpp - Test suite for indexed layout functionality
 *
 * Tests the memory-efficient indexed layout where words are stored
 * by index rather than copying the word text.
 */

#include <iostream>
#include <string>
#include <vector>

#include "content/providers/IndexedWordProvider.h"
#include "rendering/TextRenderer.h"
#include "text/hyphenation/HyphenationStrategy.h"  // For Language enum
#include "text/layout/GreedyLayoutStrategy.h"
#include "text/layout/IndexedLayout.h"
#include "test_utils.h"

namespace IndexedLayoutTests {

// ESC sequence constants for style testing
constexpr char ESC_BOLD_ON[] = "\x1B" "B";
constexpr char ESC_BOLD_OFF[] = "\x1B" "b";

/**
 * Test: Basic indexed layout
 */
void testBasicIndexedLayout(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Basic Indexed Layout ===\n";

  String content = "Hello world this is a test of indexed layout functionality";
  IndexedWordProvider provider(content);

  // Create renderer and layout strategy
  TextRenderer renderer;
  GreedyLayoutStrategy layout;

  // Configure layout
  LayoutStrategy::LayoutConfig config;
  config.marginLeft = 10;
  config.marginRight = 10;
  config.marginTop = 10;
  config.marginBottom = 10;
  config.lineHeight = 20;
  config.minSpaceWidth = 4;
  config.pageWidth = 200;
  config.pageHeight = 100;
  config.alignment = LayoutStrategy::ALIGN_LEFT;
  config.language = Language::NONE;

  // Perform indexed layout
  IndexedPageLayout pageLayout = layout.layoutTextIndexed(provider, renderer, config);

  std::cout << "  Lines in layout: " << pageLayout.lines.size() << "\n";
  std::cout << "  Start position: " << pageLayout.startPosition << "\n";
  std::cout << "  End position: " << pageLayout.endPosition << "\n";

  // Print words in each line
  for (size_t i = 0; i < pageLayout.lines.size(); i++) {
    const auto& line = pageLayout.lines[i];
    std::cout << "  Line " << i << ": " << line.words.size() << " words\n";
    for (const auto& word : line.words) {
      String text = provider.getWordAt(word.wordIndex);
      std::cout << "    [" << word.wordIndex << "] '" << text.c_str()
                << "' x=" << word.x << " y=" << word.y << " w=" << word.width << "\n";
    }
  }

  runner.expectTrue(!pageLayout.lines.empty(), "Indexed layout produces lines");
  runner.expectTrue(pageLayout.startPosition == 0, "Start position is 0");
  runner.expectTrue(pageLayout.endPosition > 0, "End position is > 0");
}

/**
 * Test: Indexed layout preserves word indices
 */
void testWordIndicesPreserved(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Word Indices Preserved ===\n";

  String content = "One two three";
  IndexedWordProvider provider(content);

  TextRenderer renderer;
  GreedyLayoutStrategy layout;

  LayoutStrategy::LayoutConfig config;
  config.marginLeft = 0;
  config.marginRight = 0;
  config.marginTop = 0;
  config.marginBottom = 0;
  config.lineHeight = 20;
  config.minSpaceWidth = 4;
  config.pageWidth = 1000;  // Wide enough for one line
  config.pageHeight = 100;
  config.alignment = LayoutStrategy::ALIGN_LEFT;
  config.language = Language::NONE;

  IndexedPageLayout pageLayout = layout.layoutTextIndexed(provider, renderer, config);

  // Should have one line with all words
  runner.expectTrue(pageLayout.lines.size() >= 1, "At least one line");

  if (!pageLayout.lines.empty()) {
    const auto& line = pageLayout.lines[0];
    // Verify we can get correct words from indices
    bool allCorrect = true;
    for (const auto& word : line.words) {
      String text = provider.getWordAt(word.wordIndex);
      if (text.isEmpty() && word.wordIndex < provider.getWordCount()) {
        allCorrect = false;
        std::cout << "  ERROR: Empty word at index " << word.wordIndex << "\n";
      }
    }
    runner.expectTrue(allCorrect, "All word indices map to correct words");
  }
}

/**
 * Test: Indexed layout style handling
 */
void testIndexedLayoutStyles(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Indexed Layout Styles ===\n";

  // Content with bold text using named constants
  String content = String("Normal ") + ESC_BOLD_ON + "Bold" + ESC_BOLD_OFF + " normal";
  IndexedWordProvider provider(content);

  TextRenderer renderer;
  GreedyLayoutStrategy layout;

  LayoutStrategy::LayoutConfig config;
  config.marginLeft = 0;
  config.marginRight = 0;
  config.marginTop = 0;
  config.marginBottom = 0;
  config.lineHeight = 20;
  config.minSpaceWidth = 4;
  config.pageWidth = 1000;
  config.pageHeight = 100;
  config.alignment = LayoutStrategy::ALIGN_LEFT;
  config.language = Language::NONE;

  IndexedPageLayout pageLayout = layout.layoutTextIndexed(provider, renderer, config);

  // Check that styles are preserved in the layout
  bool foundBold = false;
  for (const auto& line : pageLayout.lines) {
    for (const auto& word : line.words) {
      String text = provider.getWordAt(word.wordIndex);
      std::cout << "  Word '" << text.c_str() << "' style=" << (int)word.style << "\n";
      if (text == "Bold" && word.style == FontStyle::BOLD) {
        foundBold = true;
      }
    }
  }

  runner.expectTrue(foundBold, "Bold style preserved in indexed layout");
}

/**
 * Test: Empty content
 */
void testEmptyContent(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Empty Content Layout ===\n";

  String content = "";
  IndexedWordProvider provider(content);

  TextRenderer renderer;
  GreedyLayoutStrategy layout;

  LayoutStrategy::LayoutConfig config;
  config.marginLeft = 10;
  config.marginRight = 10;
  config.marginTop = 10;
  config.marginBottom = 10;
  config.lineHeight = 20;
  config.minSpaceWidth = 4;
  config.pageWidth = 200;
  config.pageHeight = 100;
  config.alignment = LayoutStrategy::ALIGN_LEFT;
  config.language = Language::NONE;

  IndexedPageLayout pageLayout = layout.layoutTextIndexed(provider, renderer, config);

  std::cout << "  Lines: " << pageLayout.lines.size() << "\n";

  runner.expectTrue(pageLayout.isEmpty(), "Empty content produces empty layout");
}

/**
 * Run all tests
 */
void runAllTests(TestUtils::TestRunner& runner) {
  testBasicIndexedLayout(runner);
  testWordIndicesPreserved(runner);
  testIndexedLayoutStyles(runner);
  testEmptyContent(runner);
}

}  // namespace IndexedLayoutTests

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("Indexed Layout Test Suite");

  IndexedLayoutTests::runAllTests(runner);

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
