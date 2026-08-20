#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace back_pocket {
using form_id = std::uint32_t;

enum class inventory_view {
  regular,
  pocket,
};

class pocket {
public:
  [[nodiscard]] bool contains(form_id form) const noexcept;
  [[nodiscard]] bool toggle(form_id form);
  [[nodiscard]] bool erase(form_id form) noexcept;
  void clear() noexcept;

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::vector<form_id> snapshot() const;
  void restore(std::span<const form_id> forms);

  [[nodiscard]] bool visible(form_id form, inventory_view view) const noexcept;

private:
  std::unordered_set<form_id> forms_;
};
} // namespace back_pocket
