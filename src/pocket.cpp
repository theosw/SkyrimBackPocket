#include "back_pocket/pocket.h"

#include <algorithm>

namespace back_pocket {
bool pocket::contains(const form_id form) const noexcept {
  return form != 0 && forms_.contains(form);
}

bool pocket::toggle(const form_id form) {
  if (form == 0) {
    return false;
  }

  const auto [iterator, inserted] = forms_.insert(form);
  if (!inserted) {
    forms_.erase(iterator);
  }
  return inserted;
}

bool pocket::erase(const form_id form) noexcept {
  return forms_.erase(form) != 0;
}

void pocket::clear() noexcept {
  forms_.clear();
}

std::size_t pocket::size() const noexcept {
  return forms_.size();
}

std::vector<form_id> pocket::snapshot() const {
  std::vector<form_id> forms(forms_.begin(), forms_.end());
  std::ranges::sort(forms);
  return forms;
}

void pocket::restore(const std::span<const form_id> forms) {
  forms_.clear();
  forms_.reserve(forms.size());
  for (const form_id form : forms) {
    if (form != 0) {
      forms_.insert(form);
    }
  }
}

bool pocket::visible(const form_id form, const inventory_view view) const noexcept {
  if (form == 0) {
    return view == inventory_view::regular;
  }
  const bool hidden = contains(form);
  return view == inventory_view::regular ? !hidden : hidden;
}
} // namespace back_pocket
