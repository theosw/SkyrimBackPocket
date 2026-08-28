#pragma once

#include <cstdint>
#include <optional>

namespace back_pocket::config {
inline constexpr std::uint32_t disabled_scan_code = 0;
inline constexpr std::uint32_t default_toggle_item_scan_code = 48; // B
inline constexpr std::uint32_t default_toggle_view_scan_code = disabled_scan_code;
inline constexpr std::uint32_t default_controller_toggle_item_key_code = 280; // LT

struct settings {
  std::uint32_t toggle_item_scan_code = default_toggle_item_scan_code;
  std::uint32_t toggle_view_scan_code = default_toggle_view_scan_code;
  std::optional<std::uint32_t> controller_toggle_item_key_code =
      default_controller_toggle_item_key_code;
  bool show_notifications = true;
};

[[nodiscard]] settings load();
} // namespace back_pocket::config
