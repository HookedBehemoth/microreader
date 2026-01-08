/**
 * IndexedWordProviderTest.cpp - Test suite for IndexedWordProvider
 *
 * Tests the memory-efficient IndexedWordProvider implementation which
 * loads entire chapters as one byte string and indexes words by position.
 */

#include <iostream>
#include <string>
#include <vector>

#include "content/providers/IndexedWordProvider.h"
#include "test_utils.h"

namespace IndexedWordProviderTests {

constexpr int MAX_FAILURES_TO_REPORT = 10;

// ESC sequence constants for style testing
// Format: ESC + command byte (2 bytes total)
constexpr char ESC_CHAR = '\x1B';
constexpr char ESC_BOLD_ON[] = "\x1B" "B";
constexpr char ESC_BOLD_OFF[] = "\x1B" "b";
constexpr char ESC_ITALIC_ON[] = "\x1B" "I";
constexpr char ESC_ITALIC_OFF[] = "\x1B" "i";

// Helper to escape a string for output
std::string escapeForOutput(const String& word) {
  std::string result;
  for (unsigned int i = 0; i < word.length(); i++) {
    char c = word.charAt(i);
    if (c == '\n')
      result += "\\n";
    else if (c == '\r')
      result += "\\r";
    else if (c == '\t')
      result += "\\t";
    else if (c == ESC_CHAR)
      result += "\\e";
    else
      result += c;
  }
  return result;
}

/**
 * Test: Basic construction and word counting
 */
void testBasicConstruction(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Basic Construction ===\n";

  String content = "Hello world this is a test";
  IndexedWordProvider provider(content);

  size_t wordCount = provider.getWordCount();
  std::cout << "  Word count: " << wordCount << "\n";

  // "Hello" " " "world" " " "this" " " "is" " " "a" " " "test" = 11 tokens
  runner.expectTrue(wordCount == 11, "Word count matches expected",
                    "Expected 11, got " + std::to_string(wordCount));
}

/**
 * Test: Forward word iteration
 */
void testForwardIteration(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Forward Iteration ===\n";

  String content = "Hello world test";
  IndexedWordProvider provider(content);

  std::string rebuilt;
  int count = 0;
  while (provider.hasNextWord()) {
    StyledWord word = provider.getNextWord();
    if (word.text.isEmpty()) break;
    rebuilt += word.text.c_str();
    count++;
    std::cout << "  Word " << count << ": '" << escapeForOutput(word.text) << "'\n";
  }

  std::cout << "  Rebuilt: '" << rebuilt << "'\n";
  runner.expectTrue(rebuilt == content.c_str(), "Forward iteration reconstructs text",
                    "Expected '" + std::string(content.c_str()) + "', got '" + rebuilt + "'");
}

/**
 * Test: Backward word iteration
 */
void testBackwardIteration(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Backward Iteration ===\n";

  String content = "Hello world test";
  IndexedWordProvider provider(content);

  // Go to end
  while (provider.hasNextWord()) {
    provider.getNextWord();
  }

  std::string rebuilt;
  while (provider.hasPrevWord()) {
    StyledWord word = provider.getPrevWord();
    if (word.text.isEmpty()) break;
    rebuilt.insert(0, word.text.c_str());
  }

  std::cout << "  Rebuilt: '" << rebuilt << "'\n";
  runner.expectTrue(rebuilt == content.c_str(), "Backward iteration reconstructs text",
                    "Expected '" + std::string(content.c_str()) + "', got '" + rebuilt + "'");
}

/**
 * Test: Bidirectional word match
 */
void testBidirectionalMatch(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Bidirectional Word Match ===\n";

  String content = "One two three four five";
  IndexedWordProvider provider(content);

  // Collect words forward
  std::vector<std::string> forwardWords;
  while (provider.hasNextWord()) {
    StyledWord word = provider.getNextWord();
    if (word.text.isEmpty()) break;
    forwardWords.push_back(word.text.c_str());
  }

  // Collect words backward
  std::vector<std::string> backwardWords;
  while (provider.hasPrevWord()) {
    StyledWord word = provider.getPrevWord();
    if (word.text.isEmpty()) break;
    backwardWords.insert(backwardWords.begin(), word.text.c_str());
  }

  std::cout << "  Forward count: " << forwardWords.size() << "\n";
  std::cout << "  Backward count: " << backwardWords.size() << "\n";

  bool match = (forwardWords.size() == backwardWords.size());
  if (match) {
    for (size_t i = 0; i < forwardWords.size(); i++) {
      if (forwardWords[i] != backwardWords[i]) {
        match = false;
        std::cout << "  Mismatch at " << i << ": forward='" << forwardWords[i]
                  << "' backward='" << backwardWords[i] << "'\n";
      }
    }
  }

  runner.expectTrue(match, "Forward and backward words match");
}

/**
 * Test: Word index access
 */
void testWordIndexAccess(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Word Index Access ===\n";

  String content = "Alpha Beta Gamma";
  IndexedWordProvider provider(content);

  // Test getWordAt
  String word0 = provider.getWordAt(0);
  String word1 = provider.getWordAt(1);
  String word2 = provider.getWordAt(2);

  std::cout << "  Word 0: '" << word0.c_str() << "'\n";
  std::cout << "  Word 1: '" << word1.c_str() << "'\n";
  std::cout << "  Word 2: '" << word2.c_str() << "'\n";

  runner.expectTrue(word0 == "Alpha", "Word 0 is 'Alpha'", "Got: '" + std::string(word0.c_str()) + "'");
  runner.expectTrue(word1 == " ", "Word 1 is space", "Got: '" + std::string(word1.c_str()) + "'");
  runner.expectTrue(word2 == "Beta", "Word 2 is 'Beta'", "Got: '" + std::string(word2.c_str()) + "'");
}

/**
 * Test: Position setting and getting
 */
void testPositionOperations(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Position Operations ===\n";

  String content = "Hello world";
  IndexedWordProvider provider(content);

  // Get initial position
  int initial = provider.getCurrentIndex();
  std::cout << "  Initial position: " << initial << "\n";

  // Read a word and check position changed
  provider.getNextWord();
  int afterFirst = provider.getCurrentIndex();
  std::cout << "  After first word: " << afterFirst << "\n";

  // Set position back
  provider.setPosition(initial);
  int reset = provider.getCurrentIndex();
  std::cout << "  After reset: " << reset << "\n";

  runner.expectTrue(reset == initial, "Position reset works",
                    "Expected " + std::to_string(initial) + ", got " + std::to_string(reset));
}

/**
 * Test: Style detection (ESC tokens)
 */
void testStyleDetection(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Style Detection ===\n";

  // Create content with ESC style tokens using named constants
  String content = String("Normal ") + ESC_BOLD_ON + "Bold" + ESC_BOLD_OFF + " normal";
  IndexedWordProvider provider(content);

  std::vector<std::pair<std::string, FontStyle>> words;
  while (provider.hasNextWord()) {
    StyledWord word = provider.getNextWord();
    if (word.text.isEmpty()) break;
    words.push_back({word.text.c_str(), word.style});
    std::cout << "  Word: '" << escapeForOutput(word.text)
              << "' style=" << (int)word.style << "\n";
  }

  // Check that "Bold" has bold style
  bool foundBold = false;
  for (const auto& w : words) {
    if (w.first == "Bold" && w.second == FontStyle::BOLD) {
      foundBold = true;
      break;
    }
  }

  runner.expectTrue(foundBold, "Bold style detected for 'Bold' word");
}

/**
 * Test: Newline handling
 */
void testNewlineHandling(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Newline Handling ===\n";

  String content = "Line one\nLine two";
  IndexedWordProvider provider(content);

  std::vector<std::string> words;
  while (provider.hasNextWord()) {
    StyledWord word = provider.getNextWord();
    if (word.text.isEmpty()) break;
    words.push_back(word.text.c_str());
    std::cout << "  Word: '" << escapeForOutput(String(word.text.c_str())) << "'\n";
  }

  // Should have: "Line" " " "one" "\n" "Line" " " "two"
  bool hasNewline = false;
  for (const auto& w : words) {
    if (w == "\n") {
      hasNewline = true;
      break;
    }
  }

  runner.expectTrue(hasNewline, "Newline is detected as separate word");
}

/**
 * Test: Buffer access methods
 */
void testBufferAccess(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Buffer Access ===\n";

  String content = "Test buffer access";
  IndexedWordProvider provider(content);

  const char* buffer = provider.getBufferData();
  size_t size = provider.getBufferSize();

  std::cout << "  Buffer size: " << size << "\n";
  std::cout << "  Content length: " << content.length() << "\n";

  runner.expectTrue(buffer != nullptr, "Buffer is not null");
  runner.expectTrue(size == content.length(), "Buffer size matches content length",
                    "Expected " + std::to_string(content.length()) + ", got " + std::to_string(size));
}

/**
 * Test: Unget word functionality
 */
void testUngetWord(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Unget Word ===\n";

  String content = "First second third";
  IndexedWordProvider provider(content);

  // Get first word
  StyledWord word1 = provider.getNextWord();
  std::cout << "  First read: '" << word1.text.c_str() << "'\n";

  // Unget
  provider.ungetWord();

  // Get again
  StyledWord word2 = provider.getNextWord();
  std::cout << "  After unget: '" << word2.text.c_str() << "'\n";

  runner.expectTrue(word1.text == word2.text, "Unget restores previous word",
                    "First: '" + std::string(word1.text.c_str()) + "', Second: '" + std::string(word2.text.c_str()) + "'");
}

/**
 * Test: findWordIndexForOffset
 */
void testFindWordIndex(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Find Word Index For Offset ===\n";

  String content = "Alpha Beta Gamma";
  IndexedWordProvider provider(content);

  // Offset 0 should be word 0 ("Alpha")
  size_t idx0 = provider.findWordIndexForOffset(0);
  std::cout << "  Offset 0 -> word index " << idx0 << "\n";

  // Offset 6 should be word 2 ("Beta")
  size_t idx6 = provider.findWordIndexForOffset(6);
  std::cout << "  Offset 6 -> word index " << idx6 << "\n";

  runner.expectTrue(idx0 == 0, "Offset 0 maps to word 0");
  runner.expectTrue(idx6 == 2, "Offset 6 maps to word 2 (Beta)",
                    "Expected 2, got " + std::to_string(idx6));
}

/**
 * Test: Percentage calculation
 */
void testPercentage(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Percentage Calculation ===\n";

  String content = "0123456789";
  IndexedWordProvider provider(content);

  float initial = provider.getPercentage();
  std::cout << "  Initial percentage: " << initial << "\n";

  // Move to middle
  provider.setPosition(5);
  float middle = provider.getPercentage();
  std::cout << "  Middle percentage: " << middle << "\n";

  runner.expectTrue(initial < 0.1f, "Initial percentage is near 0",
                    "Got: " + std::to_string(initial));
  runner.expectTrue(middle > 0.4f && middle < 0.6f, "Middle percentage is around 50%",
                    "Got: " + std::to_string(middle));
}

/**
 * Test: Empty content handling
 */
void testEmptyContent(TestUtils::TestRunner& runner) {
  std::cout << "\n=== Test: Empty Content ===\n";

  String empty = "";
  IndexedWordProvider provider(empty);

  runner.expectTrue(provider.getWordCount() == 0, "Empty content has 0 words");
  runner.expectTrue(!provider.hasNextWord(), "hasNextWord returns false for empty");
  runner.expectTrue(!provider.hasPrevWord(), "hasPrevWord returns false for empty");
}

/**
 * Run all tests
 */
void runAllTests(TestUtils::TestRunner& runner) {
  testBasicConstruction(runner);
  testForwardIteration(runner);
  testBackwardIteration(runner);
  testBidirectionalMatch(runner);
  testWordIndexAccess(runner);
  testPositionOperations(runner);
  testStyleDetection(runner);
  testNewlineHandling(runner);
  testBufferAccess(runner);
  testUngetWord(runner);
  testFindWordIndex(runner);
  testPercentage(runner);
  testEmptyContent(runner);
}

}  // namespace IndexedWordProviderTests

int main(int argc, char** argv) {
  TestUtils::TestRunner runner("IndexedWordProvider Test Suite");

  IndexedWordProviderTests::runAllTests(runner);

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
