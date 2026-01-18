#pragma once

namespace tiny {

/// An wrapper for types with a specific sentinel value that indicates "no value"
template<typename T, T sentinel = T{}>
class optional {
 private:
  T value_ = sentinel;
 public:
  constexpr optional() { /* ... */ };
  constexpr optional(const optional&) = default;
  constexpr optional(optional&&) = default;
  constexpr optional& operator=(const optional&) = default;
  constexpr optional& operator=(optional&&) = default;
  constexpr ~optional() = default;
  constexpr optional(const T& value) : value_(value) { /* ... */ }

  constexpr bool has_value() const {
    return value_ != sentinel;
  }

  constexpr T& value() {
    return value_;
  }

  constexpr const T& value() const {
    return value_;
  }

  constexpr void reset() {
    value_ = sentinel;
  }

  constexpr T& operator*() {
    return value_;
  }

  constexpr const T& operator*() const {
    return value_;
  }

  constexpr bool operator==(const optional& other) const {
    if (has_value() != other.has_value()) {
      return false;
    }
    if (!has_value()) {
      return true;
    }
    return value_ == other.value_;
  }
};

} // namespace tiny
