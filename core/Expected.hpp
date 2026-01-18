#pragma once

#include <variant>
#include <optional>

namespace util {

template<typename T, typename E>
class Expected {
 private:
  std::variant<T, E> storage;
  bool hasValue;
 public:
  Expected(const T& value) : storage(value), hasValue(true) {}
  Expected(const E& error) : storage(error), hasValue(false) {}
  Expected(T&& value) : storage(std::move(value)), hasValue(true) {}
  Expected(E&& error) : storage(std::move(error)), hasValue(false) {}
  bool has_value() const {
    return hasValue;
  }
  T& value() {
    return std::get<T>(storage);
  }
  const T& value() const {
    return std::get<T>(storage);
  }
  E& error() {
    return std::get<E>(storage);
  }
  const E& error() const {
    return std::get<E>(storage);
  }
  std::optional<T> to_optional() {
    if (hasValue) {
      return std::get<T>(storage);
    } else {
      return std::nullopt;
    }
  }
  operator bool() const {
    return hasValue;
  }
  T& operator*() {
    return std::get<T>(storage);
  }
  const T& operator*() const {
    return std::get<T>(storage);
  }
  T* operator->() {
    return &std::get<T>(storage);
  }
  const T* operator->() const {
    return &std::get<T>(storage);
  }
};

template< class T, class E >
    requires std::is_void_v<T>
class Expected<T, E> {
 private:
  std::optional<E> storage;
 public:
  Expected() : storage() {}
  Expected(E&& error) : storage(std::move(error)) {}
  bool has_value() const {
    return !storage.has_value();
  }
  E& error() {
    return *storage;
  }
  const E& error() const {
    return *storage;
  }
  operator bool() const {
    return has_value();
  }
};

}
