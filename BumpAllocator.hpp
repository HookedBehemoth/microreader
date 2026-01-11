#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>

#include "stringview.h"

namespace mem {

template<size_t BufferSize>
class FrontBumpScope;
template<size_t BufferSize>
class BackBumpScope;

/// A double ended bump allocator
/// Semi-permanent allocations should be made from the back.
template<size_t BufferSize>
class BumpAllocator {
 private:
  std::byte buffer[BufferSize];
  std::size_t bumpFront = 0;
  std::size_t bumpBack = BufferSize;
  friend class FrontBumpScope<BufferSize>;
  friend class BackBumpScope<BufferSize>;

  BumpAllocator(const BumpAllocator&) = delete;
  BumpAllocator& operator=(const BumpAllocator&) = delete;
  BumpAllocator(BumpAllocator&&) = delete;
  BumpAllocator& operator=(BumpAllocator&&) = delete;

 public:
  BumpAllocator() { /* ... */ }

  /// get available memory between front and back bumps
  [[nodiscard]]
  std::size_t availableMemory() {
    return bumpBack - bumpFront;
  }

  /// reset the allocator to empty state
  void reset() {
    std::memset(buffer, 0, BufferSize);
    bumpFront = 0;
    bumpBack = BufferSize;
  }

  /// aligned temporary bump allocator
  template<typename T>
  [[nodiscard]]
  inline T* bumpAlloc(size_t count = 1) {
    size_t size = sizeof(T) * count;
    size_t space = availableMemory();
    void* ptr = buffer + bumpFront;
    void* aligned = std::align(alignof(T), size, ptr, space);
    if (!aligned) {
      return nullptr;
    }
    if ((std::byte*)aligned + size > (std::byte*)buffer + bumpBack) {
      return nullptr;
    }
    bumpFront = (std::byte*)aligned + size - buffer;
    // printf("bumpAlloc: requested %zu bytes, new bump front: %zu\n", size, bumpFront);
    return (T*)aligned;
  }

  /// semi-permanent sub-allocator which goes downwards
  template<typename T>
  [[nodiscard]]
  inline T* subAlloc(size_t count) {
    size_t size = sizeof(T) * count;
    // For back allocation, we need to align downwards
    std::size_t newBumpBack = bumpBack - size;
    // Align downwards by masking off lower bits (works for power-of-2 alignments)
    std::size_t aligned_offset = newBumpBack & ~(alignof(T) - 1);
    void* aligned = buffer + aligned_offset;
    
    if (aligned_offset < bumpFront) {
      return nullptr;
    }
    if ((std::byte*)aligned + size > (std::byte*)buffer + bumpBack) {
      return nullptr;
    }
    bumpBack = aligned_offset;
    // printf("subAlloc: requested %zu bytes, new bump back: %zu\n", size, bumpBack);
    return (T*)aligned;
  }

  /// fill unused memory with zeros for sanity checking
  void sanityCheck() {
    std::memset(buffer + bumpFront, 0x00, availableMemory());
  }

  /// retain the given string in the back bump area
  [[nodiscard]]
  std::optional<StringView> retain(StringView str) {
    char* mem = subAlloc<char>(str.size());
    if (!mem) {
      return std::nullopt;
    }
    std::memcpy(mem, str.data(), str.size());
    return StringView { mem, str.size() };
  }

  template<typename... Args>
  std::optional<StringView> joinTemp(Args&&... args) {
    size_t totalSize = 0;
    ((totalSize += StringView{args}.size()), ...);

    char* buffer = bumpAlloc<char>(totalSize);
    if (buffer) {
      return std::nullopt;
    }

    char* ptr = buffer;
    ((std::memcpy(ptr, StringView{args}.data(), StringView{args}.size()), ptr += StringView{args}.size()), ...);
    return StringView { buffer, totalSize };
  }

  template<typename... Args>
  std::optional<StringView> join(Args&&... args) {
    size_t totalSize = 0;
    ((totalSize += StringView{args}.size()), ...);

    char* buffer = subAlloc<char>(totalSize);
    if (!buffer) {
      return std::nullopt;
    }

    char* ptr = buffer;
    ((std::memcpy(ptr, StringView{args}.data(), StringView{args}.size()), ptr += StringView{args}.size()), ...);
    return StringView { buffer, totalSize };
  }

  /// returns an object that will roll back front bump on destruction
  [[nodiscard]]
  FrontBumpScope<BufferSize> beginFrontScope() {
    return FrontBumpScope<BufferSize>(*this);
  }

  /// returns an object that will roll back back bump on destruction
  [[nodiscard]]
  BackBumpScope<BufferSize> beginBackScope() {
    return BackBumpScope<BufferSize>(*this);
  }

  void dumpState() {
    printf("BumpAllocator state:\n");
    printf("  Buffer size: %zu bytes\n", BufferSize);
    printf("  Free memory: %zu bytes\n", availableMemory());
    printf("  Used memory: %zu bytes\n", BufferSize - availableMemory());
    printf("  Used front bump: %zu bytes\n", bumpFront);
    printf("  Used back bump: %zu bytes\n", BufferSize - bumpBack);
  }
};

template<size_t BufferSize>
class FrontBumpScope {
 public:
  FrontBumpScope(BumpAllocator<BufferSize>& alloc_) : alloc(alloc_), m_oldBumpFront(alloc.bumpFront) {}
  ~FrontBumpScope() { if(enabled) alloc.bumpFront = m_oldBumpFront; }
  void disable() { enabled = false; }
  void reset() { alloc.bumpFront = m_oldBumpFront; disable(); }
 private:
  BumpAllocator<BufferSize>& alloc;
  std::size_t m_oldBumpFront;
  bool enabled = true;
};

template<size_t BufferSize>
class BackBumpScope {
 public:
  BackBumpScope(BumpAllocator<BufferSize>& alloc_) : alloc(alloc_), m_oldBumpBack(alloc.bumpBack) {}
  ~BackBumpScope() { if(enabled) alloc.bumpBack = m_oldBumpBack; }
  void disable() { enabled = false; }
  void reset() { alloc.bumpBack = m_oldBumpBack; disable(); }
 private:
  BumpAllocator<BufferSize>& alloc;
  std::size_t m_oldBumpBack;
  bool enabled = true;
};

} // namespace mem
