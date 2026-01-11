#pragma once

#include "stringview.h"

namespace fs {

/// Ensures that the full directory path exists
bool ensurePath(StringView fullPath);

}
