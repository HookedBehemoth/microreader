#pragma once

#include <cstddef>
#include <cstring>
#include <concepts>

/// Non-owning slice of an array
template<typename T>
class Slice {
 private:
  const T* data_;
  size_t size_;

 public:
  constexpr Slice() : data_(nullptr), size_(0) { /* ... */ }
  constexpr Slice(const T* data, size_t size) : data_(data), size_(size) { /* ... */ }
  
  template<size_t N>
  constexpr Slice(const T (&data)[N]) : data_(data), size_(N - 1) { /* ... */ }

  static Slice<char> fromCStr(const char* str) requires std::same_as<T, char> {
    size_t len = std::strlen(str);
    return Slice<char>(str, len);
  }

  constexpr const T* begin() const {
    return data_;
  }

  constexpr const T* end() const {
    return data_ + size_;
  }

  constexpr Slice<T> subString(size_t start, size_t length) const {
    if (start + length > size_) {
      length = size_ - start;  // Adjust length to fit
    }
    return Slice<T>(data_ + start, length);
  }

  constexpr Slice<T> operator[](size_t start, size_t length) const {
    return subString(start, length);
  }

  constexpr const T& operator[](size_t index) const {
    // Add bounds checking as needed
    return data_[index];
  }

  /// Slice up to (but not including) the first occurrence of character c
  /// If c is not found, returns the entire slice
  constexpr Slice<T> sliceUntil(T c) const {
    for (size_t i = 0; i < size_; i++) {
      if (data_[i] == c) {
        return Slice<T>(data_, i);
      }
    }
    return *this;
  }

  /// Slice up to (but not including) the first occurrence of any character in chars
  /// If none are found, returns the entire slice
  constexpr Slice<T> sliceUntilAny(Slice<T> chars) const {
    for (size_t i = 0; i < size_; i++) {
      for (T c : chars) {
        if (data_[i] == c) {
          return Slice<T>(data_, i);
        }
      }
    }
    return *this;
  }

  constexpr Slice<T> skip(size_t n) const {
    if (n >= size_) {
      return Slice<T>(data_ + size_, 0);
    }
    return Slice<T>(data_ + n, size_ - n);
  }

  constexpr Slice<T> skipUntil(T c) const {
    for (size_t i = 0; i < size_; i++) {
      if (data_[i] == c) {
        return Slice<T>(data_ + i, size_ - i);
      }
    }
    return Slice<T>(data_ + size_, 0);
  }

  constexpr Slice<T> skipAll(Slice<T> chars) const {
    const T* it = begin();
    while (it != end()) {
      if (chars.contains(*it)) {
        it++;
      } else {
        return Slice<T>(it, end() - it);
      }
    }
    return Slice<T>(end(), 0);
  }

  constexpr Slice<T> trimEnd(Slice<T> chars) const {
    size_t newSize = size_;
    while (newSize > 0 && chars.contains(data_[newSize - 1])) {
      newSize--;
    }
    return Slice<T>(data_, newSize);
  }

  constexpr size_t find(T c) const {
    for (size_t i = 0; i < size_; i++) {
      if (data_[i] == c) {
        return i;
      }
    }
    return size_;  // Not found
  }

  constexpr size_t find(Slice<T> str) const {
    size_t limit = size_ >= str.size() ? size_ - str.size() + 1 : 0;
    for (size_t i = 0; i < limit; i++) {
      if (this->subString(i, str.size()) == str) {
        return i;
      }
    }

    return size_;
  }

  constexpr size_t findAny(Slice<T> chars) const {
    for (size_t i = 0; i < size_; i++) {
      for (T c : chars) {
        if (data_[i] == c) {
          return i;
        }
      }
    }
    return size_;  // Not found
  }

  constexpr bool contains(T c) const {
    return find(c) != size_;
  }

  constexpr bool containsAny(Slice<T> chars) const {
    return findAny(chars) != size_;
  }

  constexpr const T* data() const {
    return data_;
  }

  constexpr size_t size() const {
    return size_;
  }

  constexpr bool operator==(const Slice<T> rhs) const {
    if (size_ != rhs.size_) {
      return false;
    }
    for (size_t i = 0; i < size_; i++) {
      if (data_[i] != rhs.data_[i]) {
        return false;
      }
    }
    return true;
  }

  constexpr bool startsWith(const Slice<T> prefix) const {
    if (prefix.size() > size()) {
      return false;
    }
    return this->subString(0, prefix.size()) == prefix;
  }
};

using StringView = Slice<char>;

constexpr bool caseInsensitiveEquals(const StringView lhs, const StringView rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); i++) {
    char c1 = lhs[i];
    char c2 = rhs[i];
    if (c1 >= 'A' && c1 <= 'Z') {
      c1 += 'a' - 'A';
    }
    if (c2 >= 'A' && c2 <= 'Z') {
      c2 += 'a' - 'A';
    }
    if (c1 != c2) {
      return false;
    }
  }
  return true;
}

constexpr Slice<char> ExampleString = "asdfghjkl";

static_assert(ExampleString[3] == 'f');
static_assert(ExampleString.size() == 9);
static_assert(ExampleString[2, 4] == "dfgh");
static_assert(ExampleString[0, 4] == "asdf");
static_assert(ExampleString[5, 100] == "hjkl");
static_assert(caseInsensitiveEquals("AbCdEf", "aBcDeF"));
static_assert(!caseInsensitiveEquals("AbCdEf", "aBcDeG"));
static_assert(ExampleString.sliceUntil('g') == "asdf");
static_assert(ExampleString.sliceUntil('z') == "asdfghjkl");
static_assert(ExampleString.sliceUntilAny("xz") == "asdfghjkl");
static_assert(ExampleString.skip(4) == "ghjkl");
static_assert(ExampleString.skipUntil('g') == "ghjkl");
static_assert(ExampleString.find('f') == 3);
static_assert(ExampleString.find('z') == 9);
static_assert(ExampleString.findAny("zl") == 8);
static_assert(ExampleString.findAny("xy") == 9);
static_assert(ExampleString.find("xy") == 9);
static_assert(ExampleString.find("asd") == 0);
static_assert(ExampleString.find("ghjk") == 4);
