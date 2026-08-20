#pragma once

#include "back_pocket/pocket.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace back_pocket::saved_state {
inline constexpr std::uint32_t current_version = 1;
inline constexpr std::size_t maximum_entries = 16'384;

enum class failure {
  none,
  too_many_entries,
  invalid_size,
  invalid_form,
};

struct encode_result {
  std::vector<std::byte> bytes;
  failure error = failure::none;
};

struct decode_result {
  std::vector<form_id> forms;
  failure error = failure::none;
};

[[nodiscard]] encode_result encode(std::span<const form_id> forms);
[[nodiscard]] decode_result decode(std::span<const std::byte> bytes);
} // namespace back_pocket::saved_state
