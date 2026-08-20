#pragma once

#include "back_pocket/pocket.h"
#include "config.h"

namespace back_pocket::inventory_filter {
[[nodiscard]] bool install(pocket& state, const config::settings& settings);
} // namespace back_pocket::inventory_filter
