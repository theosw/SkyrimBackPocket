#pragma once

#include <cstdint>

namespace back_pocket::disenchant_policy {
[[nodiscard]] constexpr bool should_protect(const bool original_protected,
                                            const bool enchanted,
                                            const bool pocketed) noexcept {
  return original_protected || (enchanted && pocketed);
}
} // namespace back_pocket::disenchant_policy
