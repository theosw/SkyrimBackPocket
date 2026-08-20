#pragma once

#include <cstdint>

namespace back_pocket::category_policy {
// SkyUI reserves bits 0-9 for player inventory and 10-19 for container
// inventory. Bit 20 is unused by the item menus and remains exactly
// representable by ActionScript's 32-bit bitwise operators.
inline constexpr std::uint32_t player_inventory_filter_mask = 0x000003FFu;
inline constexpr std::uint32_t pocket_filter_flag = 0x00100000u;
inline constexpr std::uint32_t mixed_inventory_filter_flag = player_inventory_filter_mask;

[[nodiscard]] constexpr bool is_player_inventory_filter(
    const std::uint32_t filter_flag) noexcept {
  return (filter_flag & player_inventory_filter_mask) != 0;
}

[[nodiscard]] constexpr bool is_pocket_category(const std::uint32_t filter_flag) noexcept {
  return filter_flag == pocket_filter_flag;
}

[[nodiscard]] constexpr std::uint32_t row_filter_flag(const std::uint32_t original,
                                                      const bool pocketed) noexcept {
  return pocketed ? original | pocket_filter_flag : original & ~pocket_filter_flag;
}
} // namespace back_pocket::category_policy
