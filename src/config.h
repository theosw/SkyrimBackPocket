#pragma once

#include <cstdint>

namespace back_pocket::config {
inline constexpr std::uint32_t disabled_scan_code = 0;
inline constexpr std::uint32_t default_toggle_item_scan_code = 48; // B
inline constexpr std::uint32_t default_toggle_view_scan_code = disabled_scan_code;

struct settings {
  std::uint32_t toggle_item_scan_code = default_toggle_item_scan_code;
  std::uint32_t toggle_view_scan_code = default_toggle_view_scan_code;
  bool show_notifications = true;
};

[[nodiscard]] settings load();
} // namespace back_pocket::config
