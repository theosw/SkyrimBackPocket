#include "pch.h"

#include "item_menu_footer.h"

namespace back_pocket::item_menu_footer {
namespace {
constexpr std::string_view inventory_prototype = "_global.InventoryMenu.prototype";
constexpr std::string_view container_prototype = "_global.ContainerMenu.prototype";
constexpr std::string_view barter_prototype = "_global.BarterMenu.prototype";
constexpr std::string_view gift_prototype = "_global.GiftMenu.prototype";
constexpr std::string_view update_bottom_bar = "updateBottomBar";
constexpr std::string_view hook_marker = "backPocketBottomBarHooked";
constexpr std::string_view menu_root = "_root.Menu_mc";

struct state {
  std::uint32_t keyboard_scan_code = 0;
  std::optional<std::uint32_t> gamepad_key_code;
  presentation_provider provider = nullptr;
  input_family input = input_family::keyboard_mouse;
  bool installed = false;
  bool append_failure_logged = false;
  bool refresh_failure_logged = false;
};

state& get_state() {
  static state instance;
  return instance;
}

[[nodiscard]] std::string_view prototype_path(const menu_kind kind) noexcept {
  switch (kind) {
  case menu_kind::inventory:
    return inventory_prototype;
  case menu_kind::container:
    return container_prototype;
  case menu_kind::barter:
    return barter_prototype;
  case menu_kind::gift:
    return gift_prototype;
  }
  return {};
}

[[nodiscard]] std::string_view menu_name(const menu_kind kind) noexcept {
  switch (kind) {
  case menu_kind::inventory:
    return "inventory";
  case menu_kind::container:
    return "container";
  case menu_kind::barter:
    return "barter";
  case menu_kind::gift:
    return "gift";
  }
  return "unknown";
}

[[nodiscard]] bool selected_argument(const RE::GFxValue& value) noexcept {
  if (value.IsBool()) {
    return value.GetBool();
  }
  return value.IsNumber() && value.GetNumber() != 0.0;
}

[[nodiscard]] std::optional<std::uint32_t> active_key_code(const state& current) noexcept {
  if (current.input == input_family::gamepad) {
    return current.gamepad_key_code;
  }
  return current.keyboard_scan_code;
}

[[nodiscard]] bool append_action(RE::GFxFunctionHandler::Params& params, const menu_kind kind) {
  state& current = get_state();
  if (params.movie == nullptr || params.argCount < 1 || params.argsWithThisRef == nullptr ||
      !params.argsWithThisRef[0].IsObject() || current.provider == nullptr) {
    return false;
  }

  const presentation action =
      current.provider(*params.movie, kind, selected_argument(params.args[0]));
  if (!action.visible) {
    return true;
  }
  const std::optional<std::uint32_t> key_code = active_key_code(current);
  if (!key_code.has_value() || action.text.empty()) {
    return true;
  }

  RE::GFxValue nav_panel;
  if (!params.argsWithThisRef[0].GetMember("navPanel", &nav_panel) || !nav_panel.IsObject()) {
    return false;
  }

  RE::GFxValue controls;
  RE::GFxValue button_data;
  RE::GFxValue label;
  params.movie->CreateObject(&controls);
  params.movie->CreateObject(&button_data);
  params.movie->CreateString(&label, action.text.data());
  if (!controls.SetMember("keyCode", RE::GFxValue(static_cast<double>(*key_code))) ||
      !button_data.SetMember("text", label) || !button_data.SetMember("controls", controls)) {
    return false;
  }

  RE::GFxValue button;
  if (!nav_panel.Invoke("addButton", &button, &button_data, 1) || button.IsUndefined()) {
    return false;
  }

  const RE::GFxValue instant(true);
  return nav_panel.Invoke("updateButtons", nullptr, &instant, 1);
}

[[nodiscard]] bool add_footer_capacity(RE::GFxMovieView& view, const menu_kind kind) {
  RE::GFxValue menu;
  RE::GFxValue nav_panel;
  RE::GFxValue buttons;
  if (!view.GetVariable(&menu, menu_root.data()) || !menu.IsObject() ||
      !menu.GetMember("navPanel", &nav_panel) || !nav_panel.IsObject() ||
      !nav_panel.GetMember("buttons", &buttons) || !buttons.IsArray()) {
    return false;
  }

  RE::GFxValue renderer_name;
  RE::GFxValue initializer;
  RE::GFxValue depth;
  if (!nav_panel.GetMember("buttonRenderer", &renderer_name) || !renderer_name.IsString() ||
      !nav_panel.Invoke("getNextHighestDepth", &depth) || !depth.IsNumber()) {
    return false;
  }
  static_cast<void>(nav_panel.GetMember("buttonInitializer", &initializer));

  const std::uint32_t previous_capacity = buttons.GetArraySize();
  const std::string instance_name = std::format("backPocketButton{}", previous_capacity);
  RE::GFxValue instance_name_value;
  view.CreateString(&instance_name_value, instance_name.c_str());
  std::array<RE::GFxValue, 4> arguments{
      renderer_name,
      instance_name_value,
      depth,
      initializer,
  };

  RE::GFxValue button;
  if (!nav_panel.Invoke("attachMovie", &button, arguments.data(), arguments.size()) ||
      !button.IsObject() || !button.SetMember("_visible", RE::GFxValue(false)) ||
      !buttons.PushBack(button) ||
      !nav_panel.SetMember("maxButtons",
                           RE::GFxValue(static_cast<double>(previous_capacity + 1)))) {
    return false;
  }

  logger::info("{} footer capacity expanded from {} to {}", menu_name(kind), previous_capacity,
               previous_capacity + 1);
  return true;
}

class update_bottom_bar_handler final : public RE::GFxFunctionHandler {
public:
  update_bottom_bar_handler(RE::GFxValue old_function, const menu_kind kind)
      : old_function_(std::move(old_function)), kind_(kind) {}

  void Call(Params& params) override {
    if (old_function_.IsObject()) {
      old_function_.Invoke("call", params.retVal, params.argsWithThisRef,
                           static_cast<std::size_t>(params.argCount) + 1);
    }

    state& current = get_state();
    if (!append_action(params, kind_) && !current.append_failure_logged) {
      current.append_failure_logged = true;
      logger::error("{} Back Pocket footer could not append a SkyUI button", menu_name(kind_));
    }
  }

private:
  RE::GFxValue old_function_;
  menu_kind kind_;
};

[[nodiscard]] bool setup(RE::IMenu& menu, const menu_kind kind) {
  RE::GFxMovieView* view = menu.uiMovie.get();
  if (view == nullptr) {
    return false;
  }

  RE::GFxValue prototype;
  const std::string_view path = prototype_path(kind);
  if (!view->GetVariable(&prototype, path.data()) || !prototype.IsObject()) {
    return false;
  }

  RE::GFxValue installed;
  if (prototype.GetMember(hook_marker.data(), &installed) && installed.IsBool() &&
      installed.GetBool()) {
    return true;
  }
  if (!add_footer_capacity(*view, kind)) {
    return false;
  }

  RE::GFxValue old_function;
  if (!prototype.GetMember(update_bottom_bar.data(), &old_function) || !old_function.IsObject()) {
    return false;
  }

  const auto handler = RE::make_gptr<update_bottom_bar_handler>(std::move(old_function), kind);
  RE::GFxValue new_function;
  view->CreateFunction(&new_function, handler.get());
  return prototype.SetMember(update_bottom_bar.data(), new_function) &&
         prototype.SetMember(hook_marker.data(), RE::GFxValue(true));
}

void note_setup_result(RE::IMenu& menu, const menu_kind kind) {
  if (setup(menu, kind)) {
    logger::info("{} Back Pocket footer hook installed", menu_name(kind));
  } else {
    logger::warn("{} menu does not expose SkyUI's native footer API", menu_name(kind));
  }
}

class inventory_menu_hook {
public:
  [[nodiscard]] static bool install() {
    REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_InventoryMenu[0]};
    original_post_create_ = vtable.write_vfunc(0x2, &post_create);
    return original_post_create_.address() != 0;
  }

private:
  static void post_create(RE::IMenu* menu) {
    if (menu != nullptr) {
      note_setup_result(*menu, menu_kind::inventory);
    }
    original_post_create_(menu);
  }

  inline static REL::Relocation<decltype(post_create)> original_post_create_;
};

class container_menu_hook {
public:
  [[nodiscard]] static bool install() {
    REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_ContainerMenu[0]};
    original_post_create_ = vtable.write_vfunc(0x2, &post_create);
    return original_post_create_.address() != 0;
  }

private:
  static void post_create(RE::IMenu* menu) {
    if (menu != nullptr) {
      note_setup_result(*menu, menu_kind::container);
    }
    original_post_create_(menu);
  }

  inline static REL::Relocation<decltype(post_create)> original_post_create_;
};

class barter_menu_hook {
public:
  [[nodiscard]] static bool install() {
    REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_BarterMenu[0]};
    original_post_create_ = vtable.write_vfunc(0x2, &post_create);
    return original_post_create_.address() != 0;
  }

private:
  static void post_create(RE::IMenu* menu) {
    if (menu != nullptr) {
      note_setup_result(*menu, menu_kind::barter);
    }
    original_post_create_(menu);
  }

  inline static REL::Relocation<decltype(post_create)> original_post_create_;
};

class gift_menu_hook {
public:
  [[nodiscard]] static bool install() {
    REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_GiftMenu[0]};
    original_post_create_ = vtable.write_vfunc(0x2, &post_create);
    return original_post_create_.address() != 0;
  }

private:
  static void post_create(RE::IMenu* menu) {
    if (menu != nullptr) {
      note_setup_result(*menu, menu_kind::gift);
    }
    original_post_create_(menu);
  }

  inline static REL::Relocation<decltype(post_create)> original_post_create_;
};
} // namespace

bool install(const std::uint32_t keyboard_scan_code,
             const std::optional<std::uint32_t> gamepad_key_code,
             const presentation_provider provider) {
  state& current = get_state();
  current.keyboard_scan_code = keyboard_scan_code;
  current.gamepad_key_code = gamepad_key_code;
  current.provider = provider;
  if (current.installed) {
    return true;
  }

  const bool inventory_installed = inventory_menu_hook::install();
  const bool container_installed = container_menu_hook::install();
  const bool barter_installed = barter_menu_hook::install();
  const bool gift_installed = gift_menu_hook::install();
  current.installed =
      inventory_installed && container_installed && barter_installed && gift_installed;
  if (current.installed) {
    logger::info(
        "native Inventory, Container, Barter, and Gift Back Pocket footer hooks registered");
  } else {
    logger::warn("one or more native Back Pocket footer hooks could not be registered");
  }
  return current.installed;
}

bool set_input_family(const input_family input) {
  state& current = get_state();
  if (current.input == input) {
    return false;
  }
  current.input = input;
  logger::debug("Back Pocket footer input family changed: {}",
                input == input_family::gamepad ? "gamepad" : "keyboard-mouse");
  return true;
}

bool refresh(RE::GFxMovieView& view, const bool footer_selected) {
  const RE::GFxValue selected_value(footer_selected);
  if (view.Invoke("_root.Menu_mc.updateBottomBar", nullptr, &selected_value, 1)) {
    return true;
  }

  state& current = get_state();
  if (!current.refresh_failure_logged) {
    current.refresh_failure_logged = true;
    logger::warn("Back Pocket footer could not request a menu refresh");
  }
  return false;
}
} // namespace back_pocket::item_menu_footer
