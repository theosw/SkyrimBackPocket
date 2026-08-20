#include "pch.h"

#include "config.h"

namespace back_pocket::config {
namespace {
constexpr auto config_path = L"Data/SKSE/Plugins/BackPocket.ini";

std::uint32_t read_scan_code(const wchar_t* key, const std::string_view label,
                             const std::uint32_t fallback, const bool allow_disabled = false) {
  const auto configured = static_cast<std::uint32_t>(
      ::GetPrivateProfileIntW(L"input", key, static_cast<int>(fallback), config_path));
  if (configured == disabled_scan_code && allow_disabled) {
    return disabled_scan_code;
  }
  if (configured == disabled_scan_code || configured > 255) {
    logger::warn("invalid {} scan code {}; using {}", label, configured, fallback);
    return fallback;
  }
  return configured;
}

bool read_bool(const wchar_t* section, const wchar_t* key, const bool fallback) {
  std::array<wchar_t, 32> buffer{};
  ::GetPrivateProfileStringW(section, key, fallback ? L"true" : L"false", buffer.data(),
                             static_cast<DWORD>(buffer.size()), config_path);

  std::wstring normalized;
  for (const wchar_t character : std::wstring_view{buffer.data()}) {
    if (!std::iswspace(character)) {
      normalized.push_back(static_cast<wchar_t>(std::towlower(character)));
    }
  }

  if (normalized == L"true" || normalized == L"1" || normalized == L"yes" || normalized == L"on") {
    return true;
  }
  if (normalized == L"false" || normalized == L"0" || normalized == L"no" || normalized == L"off") {
    return false;
  }
  logger::warn("invalid boolean setting; using {}", fallback);
  return fallback;
}
} // namespace

settings load() {
  settings result{
      .toggle_item_scan_code =
          read_scan_code(L"toggle_item_scan_code", "toggle_item", default_toggle_item_scan_code),
      .toggle_view_scan_code =
          read_scan_code(L"toggle_view_scan_code", "toggle_view", default_toggle_view_scan_code,
                         true),
      .show_notifications = read_bool(L"display", L"show_notifications", true),
  };
  if (result.toggle_view_scan_code != disabled_scan_code &&
      result.toggle_item_scan_code == result.toggle_view_scan_code) {
    logger::warn("item and view actions share scan code {}; disabling the view shortcut",
                 result.toggle_view_scan_code);
    result.toggle_view_scan_code = disabled_scan_code;
  }
  logger::info("configuration loaded: toggle_item={}, toggle_view={}, notifications={}",
               result.toggle_item_scan_code, result.toggle_view_scan_code,
               result.show_notifications);
  return result;
}
} // namespace back_pocket::config
