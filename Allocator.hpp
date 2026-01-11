#pragma once

#include "BumpAllocator.hpp"

namespace mem {

  constexpr size_t AllocatorSize = 200 * 1024;
  using Allocator = mem::BumpAllocator<AllocatorSize>;

}
