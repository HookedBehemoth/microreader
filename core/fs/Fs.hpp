#pragma once

#include <StringView.hpp>

namespace fs {

/// Ensures that the full directory path exists
bool ensurePath(StringView fullPath);

}
