#pragma once

#include <cstdint>
#include <optional>

namespace back_pocket::tab_transition_policy {

inline constexpr std::uint32_t external_segment = 0;
inline constexpr std::uint32_t player_segment = 1;

struct plan {
  bool update_pending = false;
  bool pending_value = false;
  bool normalize_external = false;
  bool restore_pocket = false;
};

[[nodiscard]] constexpr plan after_tab_press(
    const std::uint32_t target_segment, const std::optional<std::uint32_t> before_segment,
    const std::optional<std::uint32_t> after_segment, const bool pocket_selected_before,
    const bool pending_before) noexcept {
  if (!before_segment.has_value() || !after_segment.has_value() ||
      *before_segment == *after_segment || *after_segment != target_segment) {
    return {};
  }

  if (target_segment == external_segment && *before_segment == player_segment) {
    return {
        .update_pending = true,
        .pending_value = pocket_selected_before,
        .normalize_external = pocket_selected_before,
    };
  }

  if (target_segment == player_segment && *before_segment == external_segment) {
    return {
        .update_pending = true,
        .pending_value = false,
        .restore_pocket = pending_before,
    };
  }

  return {};
}

} // namespace back_pocket::tab_transition_policy
