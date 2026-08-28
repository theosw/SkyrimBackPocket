#pragma once

#include "back_pocket/pocket.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace back_pocket::form_id_policy {
[[nodiscard]] inline std::optional<form_id> from_gfx_number(const double number) noexcept {
  if (!std::isfinite(number) || std::trunc(number) != number) {
    return std::nullopt;
  }
  if (number >= 0.0 && number <= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return static_cast<form_id>(number);
  }
  if (number >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) && number < 0.0) {
    return static_cast<form_id>(static_cast<std::int32_t>(number));
  }
  return std::nullopt;
}
} // namespace back_pocket::form_id_policy
