#include "back_pocket/saved_state.h"

#include <limits>

namespace back_pocket::saved_state {
namespace {
void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
  for (std::uint32_t shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xFFu));
  }
}

std::uint32_t read_u32(const std::span<const std::byte> bytes, const std::size_t offset) {
  std::uint32_t result = 0;
  for (std::uint32_t shift = 0; shift < 32; shift += 8) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + shift / 8]) << shift;
  }
  return result;
}
} // namespace

encode_result encode(const std::span<const form_id> forms) {
  if (forms.size() > maximum_entries || forms.size() > std::numeric_limits<std::uint32_t>::max()) {
    return {.error = failure::too_many_entries};
  }

  encode_result result;
  result.bytes.reserve(sizeof(std::uint32_t) + forms.size() * sizeof(form_id));
  append_u32(result.bytes, static_cast<std::uint32_t>(forms.size()));
  for (const form_id form : forms) {
    if (form == 0) {
      return {.error = failure::invalid_form};
    }
    append_u32(result.bytes, form);
  }
  return result;
}

decode_result decode(const std::span<const std::byte> bytes) {
  if (bytes.size() < sizeof(std::uint32_t)) {
    return {.error = failure::invalid_size};
  }

  const std::uint32_t count = read_u32(bytes, 0);
  if (count > maximum_entries) {
    return {.error = failure::too_many_entries};
  }
  const std::size_t expected =
      sizeof(std::uint32_t) + static_cast<std::size_t>(count) * sizeof(form_id);
  if (bytes.size() != expected) {
    return {.error = failure::invalid_size};
  }

  decode_result result;
  result.forms.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    const form_id form =
        read_u32(bytes, sizeof(std::uint32_t) + static_cast<std::size_t>(index) * sizeof(form_id));
    if (form == 0) {
      return {.error = failure::invalid_form};
    }
    result.forms.push_back(form);
  }
  return result;
}
} // namespace back_pocket::saved_state
