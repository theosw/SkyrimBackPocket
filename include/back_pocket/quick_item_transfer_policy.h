#pragma once

#include <cstdint>
#include <optional>

namespace back_pocket::quick_item_transfer_policy {

inline constexpr std::uint32_t no_action = 0;

struct plan {
  bool call_original = true;
  std::uint32_t replacement_action = no_action;
};

[[nodiscard]] constexpr bool has_defined_action(const std::uint32_t action) noexcept {
  return (action >= 1 && action <= 9) || (action >= 12 && action <= 20);
}

[[nodiscard]] constexpr bool is_qit_no_action(const std::uint32_t action) noexcept {
  return action == 0 || action == 11;
}

[[nodiscard]] constexpr plan
add_button(const bool back_pocket_selected,
           const std::optional<std::uint32_t> selected_action = std::nullopt) noexcept {
  const bool undefined_action = selected_action.has_value() &&
                                !is_qit_no_action(*selected_action) &&
                                !has_defined_action(*selected_action);
  if (back_pocket_selected || undefined_action) {
    return {.call_original = false, .replacement_action = no_action};
  }
  return {};
}

} // namespace back_pocket::quick_item_transfer_policy
