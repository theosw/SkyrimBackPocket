#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace back_pocket::item_menu_footer {
enum class menu_kind : std::uint8_t {
  inventory,
  container,
  barter,
  gift,
};

enum class input_family : std::uint8_t {
  keyboard_mouse,
  gamepad,
};

struct presentation {
  bool visible = false;
  std::string_view text;
};

using presentation_provider = presentation (*)(RE::GFxMovie&, menu_kind, bool footer_selected);

[[nodiscard]] bool install(std::uint32_t keyboard_scan_code,
                           std::optional<std::uint32_t> gamepad_key_code,
                           presentation_provider provider);
[[nodiscard]] bool set_input_family(input_family input);
[[nodiscard]] bool refresh(RE::GFxMovieView& view, bool footer_selected);
} // namespace back_pocket::item_menu_footer
