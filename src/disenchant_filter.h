#pragma once

#include "back_pocket/pocket.h"

namespace back_pocket::disenchant_filter {
[[nodiscard]] bool install(pocket& state, bool hide_pocketed_items);
} // namespace back_pocket::disenchant_filter
