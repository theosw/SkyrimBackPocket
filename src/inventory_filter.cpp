#include "pch.h"

#include "inventory_filter.h"

#include "back_pocket/category_policy.h"
#include "back_pocket/form_id_policy.h"
#include "back_pocket/input_binding.h"
#include "back_pocket/quick_item_transfer_policy.h"
#include "back_pocket/tab_transition_policy.h"
#include "item_menu_footer.h"

namespace back_pocket::inventory_filter {
namespace {
constexpr std::string_view inventory_lists_path = "_root.Menu_mc.inventoryLists";
constexpr std::string_view category_list_path = "_root.Menu_mc.inventoryLists.categoryList";
constexpr std::string_view item_list_path = "_root.Menu_mc.inventoryLists.itemList";
constexpr std::string_view item_enumeration_path =
    "_root.Menu_mc.inventoryLists.itemList.listEnumeration";
constexpr std::string_view category_entry_prototype_path = "_global.CategoryListEntry.prototype";
constexpr std::string_view inventory_lists_prototype_path = "_global.InventoryLists.prototype";
constexpr std::string_view quick_item_transfer_prototype_path =
    "_global.QuickItemTransfer.prototype";

constexpr std::string_view form_id_member = "backPocketFormId";
constexpr std::string_view fallback_form_id_member = "formId";
constexpr std::string_view filter_flag_member = "filterFlag";
constexpr std::string_view membership_filter_marker = "backPocketMembershipFilterInstalled";
constexpr std::string_view visibility_filter_marker = "backPocketVisibilityFilterInstalled";
constexpr std::string_view category_marker = "backPocketCategory";
constexpr std::string_view icon_hook_marker = "backPocketIconHookInstalled";
constexpr std::string_view tab_press_hook_marker = "backPocketTabPressHookInstalled";
constexpr std::string_view quick_item_transfer_hook_marker =
    "backPocketCompatibilityHookInstalled";
constexpr std::string_view quick_item_transfer_dispatch_hook_marker =
    "backPocketCompatibilityDispatchHookInstalled";
constexpr std::string_view restore_on_player_tab_member = "backPocketRestoreOnPlayerTab";
constexpr std::string_view category_icon_source = "BackPocket/category_icon.swf";
constexpr std::string_view category_icon_label = "back_pocket";
constexpr std::string_view fallback_icon_label = "inv_misc";
constexpr std::string_view category_text = "Back Pocket";

constexpr std::uint32_t default_regular_category_index = 1;
constexpr std::uint32_t invalid_category_index = std::numeric_limits<std::uint32_t>::max();

enum class menu_kind : std::uint8_t {
  none,
  inventory,
  container,
  barter,
  gift,
};

enum class category_installation : std::uint8_t {
  installed,
  not_applicable,
  failed,
};

enum class integration_result : std::uint8_t {
  ready,
  not_applicable,
  cancelled,
  failed,
};

enum class optional_hook_installation : std::uint8_t {
  installed_now,
  already_installed,
  unavailable,
  failed,
};

enum class queued_action : std::uint8_t {
  none,
  toggle_item,
  toggle_category,
};

enum class action_device : std::uint8_t {
  keyboard,
  gamepad,
};

struct runtime_state {
  pocket* pocket_state = nullptr;
  config::settings configuration;
  bool installed = false;
  std::atomic_bool menu_open = false;
  std::atomic<menu_kind> active_menu = menu_kind::none;
  bool filter_installed = false;
  bool category_installed = false;
  bool icon_hook_installed = false;
  bool tab_press_hook_installed = false;
  std::uint32_t category_index = invalid_category_index;
  std::uint32_t last_regular_category_index = default_regular_category_index;
  std::atomic_bool integration_queued = false;
  std::atomic_bool footer_refresh_queued = false;
  std::atomic_bool quick_item_transfer_failure_logged = false;
  std::atomic<queued_action> pending_action = queued_action::none;
  std::optional<input_binding::gamepad_button_id> controller_toggle_item_button;
  input_binding::controller_press controller_press;
  std::vector<input_binding::gamepad_button_id> gamepad_buttons_down;
  bool has_last_toggle_action = false;
  std::chrono::steady_clock::time_point last_toggle_action_at{};
  action_device last_toggle_action_device = action_device::keyboard;
};

runtime_state& state() {
  static runtime_state instance;
  return instance;
}

void queue_footer_refresh();
void request_invalidate(RE::GFxMovie& movie);

[[nodiscard]] menu_kind classify_menu(const std::string_view name) noexcept {
  if (name == RE::InventoryMenu::MENU_NAME) {
    return menu_kind::inventory;
  }
  if (name == RE::ContainerMenu::MENU_NAME) {
    return menu_kind::container;
  }
  if (name == RE::BarterMenu::MENU_NAME) {
    return menu_kind::barter;
  }
  if (name == RE::GiftMenu::MENU_NAME) {
    return menu_kind::gift;
  }
  return menu_kind::none;
}

[[nodiscard]] std::string_view menu_name(const menu_kind menu) noexcept {
  switch (menu) {
  case menu_kind::inventory:
    return RE::InventoryMenu::MENU_NAME;
  case menu_kind::container:
    return RE::ContainerMenu::MENU_NAME;
  case menu_kind::barter:
    return RE::BarterMenu::MENU_NAME;
  case menu_kind::gift:
    return RE::GiftMenu::MENU_NAME;
  case menu_kind::none:
    break;
  }
  return {};
}

void notify(const std::string_view message) {
  if (state().configuration.show_notifications) {
    RE::DebugNotification(message.data());
  }
}

[[nodiscard]] std::optional<std::uint32_t> number_as_u32(const RE::GFxValue& value) {
  if (!value.IsNumber()) {
    return std::nullopt;
  }
  const double number = value.GetNumber();
  if (!std::isfinite(number) || number < 0.0 ||
      number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(number);
}

[[nodiscard]] std::optional<form_id> read_form_id(RE::GFxValue& row) {
  RE::GFxValue value;
  if (row.GetMember(form_id_member.data(), &value)) {
    if (value.IsNumber()) {
      const auto form = form_id_policy::from_gfx_number(value.GetNumber());
      if (form.has_value()) {
        return *form;
      }
    }
  }
  if (row.GetMember(fallback_form_id_member.data(), &value) && value.IsNumber()) {
    if (const auto form = form_id_policy::from_gfx_number(value.GetNumber()); form.has_value()) {
      return *form;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::uint32_t read_filter_flag(RE::GFxValue& row) {
  RE::GFxValue value;
  if (!row.GetMember(filter_flag_member.data(), &value)) {
    return 0;
  }
  return number_as_u32(value).value_or(0);
}

bool set_row_membership(RE::GFxValue& row) {
  runtime_state& current = state();
  if (current.pocket_state == nullptr) {
    return false;
  }

  const std::uint32_t original = read_filter_flag(row);
  const std::optional<form_id> form = read_form_id(row);
  const bool pocketed = category_policy::is_player_inventory_filter(original) && form.has_value() &&
                        current.pocket_state->contains(*form);
  const std::uint32_t updated = category_policy::row_filter_flag(original, pocketed);
  return row.SetMember(filter_flag_member.data(), RE::GFxValue(static_cast<double>(updated)));
}

[[nodiscard]] bool is_player_category_flag(const std::uint32_t flag) noexcept {
  return category_policy::is_pocket_category(flag) ||
         category_policy::is_player_inventory_filter(flag);
}

[[nodiscard]] std::optional<std::uint32_t> selected_category_flag(RE::GFxMovie& movie) {
  RE::GFxValue category_list;
  RE::GFxValue selected_entry;
  RE::GFxValue flag;
  if (!movie.GetVariable(&category_list, category_list_path.data()) || !category_list.IsObject() ||
      !category_list.GetMember("selectedEntry", &selected_entry) || !selected_entry.IsObject() ||
      !selected_entry.GetMember("flag", &flag)) {
    return std::nullopt;
  }
  return number_as_u32(flag);
}

[[nodiscard]] std::optional<std::uint32_t> selected_category_index(RE::GFxMovie& movie) {
  RE::GFxValue category_list;
  RE::GFxValue selected_index;
  if (!movie.GetVariable(&category_list, category_list_path.data()) || !category_list.IsObject() ||
      !category_list.GetMember("selectedIndex", &selected_index)) {
    return std::nullopt;
  }
  return number_as_u32(selected_index);
}

[[nodiscard]] bool back_pocket_category_selected(RE::GFxMovie& movie) {
  RE::GFxValue category_list;
  RE::GFxValue selected_entry;
  RE::GFxValue marker;
  return movie.GetVariable(&category_list, category_list_path.data()) &&
         category_list.IsObject() && category_list.GetMember("selectedEntry", &selected_entry) &&
         selected_entry.IsObject() &&
         selected_entry.GetMember(category_marker.data(), &marker) && marker.IsBool() &&
         marker.GetBool();
}

void use_mixed_inventory_layout(RE::GFxMovie& movie) {
  RE::GFxValue layout;
  if (!movie.GetVariable(&layout, "_root.Menu_mc.inventoryLists.itemList.layout") ||
      !layout.IsObject()) {
    return;
  }
  const RE::GFxValue flag(static_cast<double>(category_policy::mixed_inventory_filter_flag));
  static_cast<void>(layout.Invoke("changeFilterFlag", nullptr, &flag, 1));
}

class membership_filter_handler final : public RE::GFxFunctionHandler {
public:
  void Call(Params& params) override {
    if (params.argCount < 1 || !params.args[0].IsArray()) {
      return;
    }

    RE::GFxValue& rows = params.args[0];
    const std::uint32_t count = rows.GetArraySize();
    for (std::uint32_t index = 0; index < count; ++index) {
      RE::GFxValue row;
      if (rows.GetElement(index, &row) && row.IsObject()) {
        static_cast<void>(set_row_membership(row));
      }
    }
  }
};

class visibility_filter_handler final : public RE::GFxFunctionHandler {
public:
  void Call(Params& params) override {
    if (params.argCount < 1 || !params.args[0].IsArray() || params.movie == nullptr) {
      return;
    }

    runtime_state& current = state();
    if (current.pocket_state == nullptr) {
      return;
    }

    const std::optional<std::uint32_t> selected_flag = selected_category_flag(*params.movie);
    if (!selected_flag.has_value() || !is_player_category_flag(*selected_flag)) {
      return;
    }

    const bool pocket_view = category_policy::is_pocket_category(*selected_flag);
    if (pocket_view) {
      use_mixed_inventory_layout(*params.movie);
    } else if (const auto index = selected_category_index(*params.movie);
               index.has_value() && *index != current.category_index) {
      current.last_regular_category_index = *index;
    }

    RE::GFxValue& rows = params.args[0];
    for (std::uint32_t index = rows.GetArraySize(); index > 0;) {
      --index;
      RE::GFxValue row;
      bool visible = !pocket_view;
      if (rows.GetElement(index, &row) && row.IsObject()) {
        const bool player_row = category_policy::is_player_inventory_filter(read_filter_flag(row));
        const std::optional<form_id> form = read_form_id(row);
        if (player_row && form.has_value()) {
          const inventory_view view =
              pocket_view ? inventory_view::pocket : inventory_view::regular;
          visible = current.pocket_state->visible(*form, view);
        }
      }
      if (!visible && !rows.RemoveElement(index)) {
        logger::warn("Back Pocket could not remove filtered row {}", index);
      }
    }
  }
};

[[nodiscard]] bool initializes_back_pocket_renderer(const RE::GFxFunctionHandler::Params& params) {
  if (params.argCount < 2 || !params.args[1].IsObject()) {
    return false;
  }

  const std::optional<std::uint32_t> clip_index = number_as_u32(params.args[0]);
  RE::GFxValue category_list;
  RE::GFxValue entries;
  if (!clip_index.has_value() || !params.args[1].GetMember("list", &category_list) ||
      !category_list.IsObject() || !category_list.GetMember("entryList", &entries) ||
      !entries.IsArray()) {
    return false;
  }

  std::uint32_t segment_offset = 0;
  RE::GFxValue offset;
  if (category_list.GetMember("_segmentOffset", &offset)) {
    const std::optional<std::uint32_t> parsed = number_as_u32(offset);
    if (!parsed.has_value()) {
      return false;
    }
    segment_offset = *parsed;
  }
  if (*clip_index > std::numeric_limits<std::uint32_t>::max() - segment_offset) {
    return false;
  }

  const std::uint32_t entry_index = *clip_index + segment_offset;
  RE::GFxValue entry;
  RE::GFxValue marker;
  return entry_index < entries.GetArraySize() && entries.GetElement(entry_index, &entry) &&
         entry.IsObject() && entry.GetMember(category_marker.data(), &marker) && marker.IsBool() &&
         marker.GetBool();
}

class category_icon_initialize_handler final : public RE::GFxFunctionHandler {
public:
  explicit category_icon_initialize_handler(RE::GFxValue original)
      : original_(std::move(original)) {}

  void Call(Params& params) override {
    bool replace_source = false;
    bool had_source = false;
    RE::GFxValue original_source;

    if (initializes_back_pocket_renderer(params)) {
      had_source = params.args[1].GetMember("iconSource", &original_source);
      replace_source = params.args[1].SetMember("iconSource", RE::GFxValue(category_icon_source));
    }

    if (original_.IsObject()) {
      original_.Invoke("call", params.retVal, params.argsWithThisRef,
                       static_cast<std::size_t>(params.argCount) + 1);
    }

    if (replace_source) {
      if (had_source) {
        static_cast<void>(params.args[1].SetMember("iconSource", original_source));
      } else {
        static_cast<void>(params.args[1].DeleteMember("iconSource"));
      }
    }
  }

private:
  RE::GFxValue original_;
};

bool install_category_icon_hook(RE::GFxMovie& movie) {
  RE::GFxValue prototype;
  if (!movie.GetVariable(&prototype, category_entry_prototype_path.data()) ||
      !prototype.IsObject()) {
    logger::warn("Back Pocket could not find CategoryListEntry.prototype; using the "
                 "active icon pack's Misc icon");
    return false;
  }

  RE::GFxValue installed;
  if (prototype.GetMember(icon_hook_marker.data(), &installed) && installed.IsBool() &&
      installed.GetBool()) {
    return true;
  }

  RE::GFxValue original;
  if (!prototype.GetMember("initialize", &original) || !original.IsObject()) {
    logger::warn("Back Pocket could not hook CategoryListEntry.initialize; using "
                 "the active icon pack's Misc icon");
    return false;
  }

  const auto handler = RE::make_gptr<category_icon_initialize_handler>(std::move(original));
  RE::GFxValue replacement;
  movie.CreateFunction(&replacement, handler.get());
  return prototype.SetMember("initialize", replacement) &&
         prototype.SetMember(icon_hook_marker.data(), RE::GFxValue(true));
}

bool set_icon_label(RE::GFxValue& icon_art, const std::uint32_t index, const bool custom_icon) {
  if (!icon_art.IsArray()) {
    return false;
  }
  if (icon_art.GetArraySize() <= index && !icon_art.SetArraySize(index + 1)) {
    return false;
  }
  return icon_art.SetElement(index,
                             RE::GFxValue(custom_icon ? category_icon_label : fallback_icon_label));
}

category_installation inject_category(RE::GFxMovie& movie, const bool custom_icon) {
  RE::GFxValue category_list;
  RE::GFxValue entries;
  RE::GFxValue icon_art;
  if (!movie.GetVariable(&category_list, category_list_path.data()) || !category_list.IsObject() ||
      !category_list.GetMember("entryList", &entries) || !entries.IsArray() ||
      !category_list.GetMember("iconArt", &icon_art) || !icon_art.IsArray()) {
    return category_installation::failed;
  }

  runtime_state& current = state();
  const std::uint32_t entry_count = entries.GetArraySize();
  std::optional<std::uint32_t> first_player_index;
  std::optional<std::uint32_t> last_player_index;
  std::optional<std::uint32_t> existing_pocket_index;
  for (std::uint32_t index = 0; index < entry_count; ++index) {
    RE::GFxValue entry;
    RE::GFxValue flag;
    if (!entries.GetElement(index, &entry) || !entry.IsObject() ||
        !entry.GetMember("flag", &flag)) {
      continue;
    }
    const std::optional<std::uint32_t> parsed_flag = number_as_u32(flag);
    if (!parsed_flag.has_value()) {
      continue;
    }
    if (category_policy::is_pocket_category(*parsed_flag)) {
      existing_pocket_index = index;
    } else if (category_policy::is_player_inventory_filter(*parsed_flag)) {
      if (!first_player_index.has_value()) {
        first_player_index = index;
      }
      last_player_index = index;
    }
  }

  if (!first_player_index.has_value() || !last_player_index.has_value()) {
    return entry_count == 0 ? category_installation::failed : category_installation::not_applicable;
  }

  if (existing_pocket_index.has_value()) {
    if (*existing_pocket_index != entry_count - 1 ||
        *last_player_index + 1 != *existing_pocket_index) {
      return category_installation::failed;
    }

    RE::GFxValue entry;
    if (!entries.GetElement(*existing_pocket_index, &entry) || !entry.IsObject() ||
        !entry.SetMember("bDontHide", RE::GFxValue(true)) ||
        !entry.SetMember("filterFlag", RE::GFxValue(1)) ||
        !entry.SetMember(category_marker.data(), RE::GFxValue(true)) ||
        !set_icon_label(icon_art, *existing_pocket_index - *first_player_index, custom_icon)) {
      return category_installation::failed;
    }
    current.category_index = *existing_pocket_index;
    current.last_regular_category_index =
        *first_player_index < *last_player_index ? *first_player_index + 1 : *first_player_index;
    return category_installation::installed;
  }

  // Supported SkyUI item menus put the player segment last. Refuse to inject into an
  // unfamiliar layout so filtering always fails open.
  if (*last_player_index != entry_count - 1) {
    return category_installation::failed;
  }

  RE::GFxValue entry;
  movie.CreateObject(&entry);
  if (!entry.SetMember("text", RE::GFxValue(category_text)) ||
      !entry.SetMember("flag",
                       RE::GFxValue(static_cast<double>(category_policy::pocket_filter_flag))) ||
      !entry.SetMember("bDontHide", RE::GFxValue(true)) ||
      !entry.SetMember("savedItemIndex", RE::GFxValue(0)) ||
      !entry.SetMember("filterFlag", RE::GFxValue(1)) ||
      !entry.SetMember(category_marker.data(), RE::GFxValue(true))) {
    return category_installation::failed;
  }

  const std::uint32_t index = entries.GetArraySize();
  if (!entries.SetArraySize(index + 1) || !entries.SetElement(index, entry) ||
      !set_icon_label(icon_art, index - *first_player_index, custom_icon)) {
    return category_installation::failed;
  }

  current.category_index = index;
  current.last_regular_category_index =
      *first_player_index < *last_player_index ? *first_player_index + 1 : *first_player_index;
  static_cast<void>(category_list.Invoke("InvalidateData", nullptr, nullptr, 0));
  return category_installation::installed;
}

template <class Handler> bool create_filter(RE::GFxMovie& movie, RE::GFxValue& filter) {
  movie.CreateObject(&filter);
  const auto handler = RE::make_gptr<Handler>();
  RE::GFxValue apply_filter;
  movie.CreateFunction(&apply_filter, handler.get());
  return filter.SetMember("applyFilter", apply_filter);
}

bool prepend_filter(RE::GFxValue& enumeration, const RE::GFxValue& filter) {
  RE::GFxValue chain;
  if (!enumeration.GetMember("_filterChain", &chain) || !chain.IsArray()) {
    return false;
  }

  const std::uint32_t count = chain.GetArraySize();
  if (!chain.SetArraySize(count + 1)) {
    return false;
  }
  for (std::uint32_t index = count; index > 0; --index) {
    RE::GFxValue previous;
    if (!chain.GetElement(index - 1, &previous) || !chain.SetElement(index, previous)) {
      return false;
    }
  }
  return chain.SetElement(0, filter);
}

bool install_filters(RE::GFxMovie& movie) {
  RE::GFxValue enumeration;
  if (!movie.GetVariable(&enumeration, item_enumeration_path.data()) || !enumeration.IsObject()) {
    return false;
  }

  RE::GFxValue marker;
  const bool membership_installed =
      enumeration.GetMember(membership_filter_marker.data(), &marker) && marker.IsBool() &&
      marker.GetBool();
  if (!membership_installed) {
    RE::GFxValue filter;
    if (!create_filter<membership_filter_handler>(movie, filter) ||
        !prepend_filter(enumeration, filter) ||
        !enumeration.SetMember(membership_filter_marker.data(), RE::GFxValue(true))) {
      return false;
    }
  }

  marker.SetUndefined();
  const bool visibility_installed =
      enumeration.GetMember(visibility_filter_marker.data(), &marker) && marker.IsBool() &&
      marker.GetBool();
  if (!visibility_installed) {
    RE::GFxValue filter;
    if (!create_filter<visibility_filter_handler>(movie, filter) ||
        !enumeration.Invoke("addFilter", nullptr, &filter, 1) ||
        !enumeration.SetMember(visibility_filter_marker.data(), RE::GFxValue(true))) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] std::optional<std::uint32_t>
active_category_segment(RE::GFxValue& inventory_lists) {
  RE::GFxValue category_list;
  RE::GFxValue active_segment;
  if (!inventory_lists.GetMember("categoryList", &category_list) ||
      !category_list.IsObject() ||
      !category_list.GetMember("activeSegment", &active_segment)) {
    return std::nullopt;
  }
  return number_as_u32(active_segment);
}

[[nodiscard]] bool restore_on_player_tab(RE::GFxValue& inventory_lists) {
  RE::GFxValue restore;
  return inventory_lists.GetMember(restore_on_player_tab_member.data(), &restore) &&
         restore.IsBool() && restore.GetBool();
}

[[nodiscard]] std::optional<std::uint32_t>
find_pocket_category_index(RE::GFxValue& category_list) {
  RE::GFxValue entries;
  if (!category_list.GetMember("entryList", &entries) || !entries.IsArray()) {
    return std::nullopt;
  }

  const std::uint32_t count = entries.GetArraySize();
  for (std::uint32_t index = 0; index < count; ++index) {
    RE::GFxValue entry;
    RE::GFxValue marker;
    RE::GFxValue flag;
    if (!entries.GetElement(index, &entry) || !entry.IsObject() ||
        !entry.GetMember(category_marker.data(), &marker) || !marker.IsBool() ||
        !marker.GetBool() || !entry.GetMember("flag", &flag)) {
      continue;
    }
    if (const std::optional<std::uint32_t> parsed = number_as_u32(flag);
        parsed.has_value() && category_policy::is_pocket_category(*parsed)) {
      return index;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t>
find_external_fallback_category_index(RE::GFxValue& category_list) {
  RE::GFxValue entries;
  if (!category_list.GetMember("entryList", &entries) || !entries.IsArray()) {
    return std::nullopt;
  }

  std::optional<std::uint32_t> fallback;
  const std::uint32_t count = entries.GetArraySize();
  for (std::uint32_t index = 0; index < count; ++index) {
    RE::GFxValue entry;
    RE::GFxValue flag;
    if (!entries.GetElement(index, &entry) || !entry.IsObject() ||
        !entry.GetMember("flag", &flag)) {
      continue;
    }
    const std::optional<std::uint32_t> parsed = number_as_u32(flag);
    if (!parsed.has_value() || *parsed == 0 ||
        category_policy::is_player_inventory_filter(*parsed) ||
        category_policy::is_pocket_category(*parsed)) {
      continue;
    }
    fallback = index;
  }
  return fallback;
}

bool select_category(RE::GFxValue& inventory_lists, RE::GFxValue& category_list,
                     const std::uint32_t category_index) {
  return category_list.SetMember(
             "selectedIndex", RE::GFxValue(static_cast<double>(category_index))) &&
         category_list.Invoke("UpdateList", nullptr, nullptr, 0) &&
         inventory_lists.Invoke("showItemsList", nullptr, nullptr, 0);
}

bool normalize_external_category(RE::GFxMovie& movie, RE::GFxValue& inventory_lists,
                                 std::uint32_t& normalized_index) {
  RE::GFxValue category_list;
  if (!inventory_lists.GetMember("categoryList", &category_list) ||
      !category_list.IsObject()) {
    return false;
  }

  const std::optional<std::uint32_t> fallback_index =
      find_external_fallback_category_index(category_list);
  if (!fallback_index.has_value() ||
      !select_category(inventory_lists, category_list, *fallback_index)) {
    return false;
  }

  normalized_index = *fallback_index;
  request_invalidate(movie);
  const std::optional<std::uint32_t> selected_index = selected_category_index(movie);
  const std::optional<std::uint32_t> selected_flag = selected_category_flag(movie);
  return selected_index == fallback_index && selected_flag.has_value() && *selected_flag != 0 &&
         !category_policy::is_player_inventory_filter(*selected_flag) &&
         !category_policy::is_pocket_category(*selected_flag);
}

bool restore_pocket_category(RE::GFxMovie& movie, RE::GFxValue& inventory_lists,
                             std::uint32_t& restored_index) {
  RE::GFxValue category_list;
  if (!inventory_lists.GetMember("categoryList", &category_list) ||
      !category_list.IsObject()) {
    return false;
  }

  const std::optional<std::uint32_t> category_index =
      find_pocket_category_index(category_list);
  if (!category_index.has_value() ||
      !select_category(inventory_lists, category_list, *category_index)) {
    return false;
  }

  state().category_index = *category_index;
  restored_index = *category_index;
  use_mixed_inventory_layout(movie);
  request_invalidate(movie);
  const std::optional<std::uint32_t> restored_flag = selected_category_flag(movie);
  return restored_flag.has_value() && category_policy::is_pocket_category(*restored_flag);
}

class tab_press_handler final : public RE::GFxFunctionHandler {
public:
  explicit tab_press_handler(RE::GFxValue original) : original_(std::move(original)) {}

  void Call(Params& params) override {
    const auto call_original = [&]() {
      if (original_.IsObject()) {
        original_.Invoke("call", params.retVal, params.argsWithThisRef,
                         static_cast<std::size_t>(params.argCount) + 1);
      }
    };

    const menu_kind menu = state().active_menu.load();
    if (params.movie == nullptr || params.thisPtr == nullptr || !params.thisPtr->IsObject() ||
        (menu != menu_kind::container && menu != menu_kind::barter)) {
      call_original();
      return;
    }

    RE::GFxValue index;
    if (params.argCount < 1 || !params.args[0].IsObject() ||
        !params.args[0].GetMember("index", &index)) {
      call_original();
      return;
    }

    const std::optional<std::uint32_t> target_segment = number_as_u32(index);
    if (!target_segment.has_value()) {
      call_original();
      return;
    }

    RE::GFxValue inventory_lists = *params.thisPtr;
    const std::optional<std::uint32_t> before_segment =
        active_category_segment(inventory_lists);
    const std::optional<std::uint32_t> before_index = selected_category_index(*params.movie);
    const std::optional<std::uint32_t> before_flag = selected_category_flag(*params.movie);
    const bool pocket_selected_before =
        before_flag.has_value() && category_policy::is_pocket_category(*before_flag);
    const bool pending_before = restore_on_player_tab(inventory_lists);

    call_original();

    const std::optional<std::uint32_t> after_segment =
        active_category_segment(inventory_lists);
    const tab_transition_policy::plan transition = tab_transition_policy::after_tab_press(
        *target_segment, before_segment, after_segment, pocket_selected_before, pending_before);
    if (!transition.update_pending) {
      return;
    }

    if (!inventory_lists.SetMember(restore_on_player_tab_member.data(),
                                   RE::GFxValue(transition.pending_value))) {
      logger::warn("{} Back Pocket could not preserve category intent across a tab transition",
                   menu_name(menu));
      return;
    }

    if (*target_segment == tab_transition_policy::external_segment) {
      if (transition.normalize_external) {
        const std::optional<std::uint32_t> displaced_index =
            selected_category_index(*params.movie);
        const std::optional<std::uint32_t> displaced_flag =
            selected_category_flag(*params.movie);
        std::uint32_t normalized_index = invalid_category_index;
        if (!normalize_external_category(*params.movie, inventory_lists, normalized_index)) {
          logger::warn(
              "{} Back Pocket could not select a real external category after leaving the "
              "player tab",
              menu_name(menu));
        } else {
          logger::info(
              "EXTERNAL_CATEGORY_NORMALIZED menu={} displaced_index={} displaced_flag={:08X} "
              "category_index={}",
              menu_name(menu), displaced_index.value_or(invalid_category_index),
              displaced_flag.value_or(0), normalized_index);
        }
      }
      logger::info(
          "PLAYER_TAB_LEFT menu={} selected_index={} selected_flag={:08X} remember_pocket={}",
          menu_name(menu), before_index.value_or(invalid_category_index),
          before_flag.value_or(0), transition.pending_value);
      return;
    }

    if (!transition.restore_pocket) {
      return;
    }

    const std::optional<std::uint32_t> displaced_index = selected_category_index(*params.movie);
    const std::optional<std::uint32_t> displaced_flag = selected_category_flag(*params.movie);
    std::uint32_t restored_index = invalid_category_index;
    if (!restore_pocket_category(*params.movie, inventory_lists, restored_index)) {
      logger::warn(
          "{} Back Pocket could not restore its category after returning to the player tab",
          menu_name(menu));
      return;
    }
    logger::info(
        "POCKET_CATEGORY_RESTORED menu={} displaced_index={} displaced_flag={:08X} "
        "category_index={}",
        menu_name(menu), displaced_index.value_or(invalid_category_index),
        displaced_flag.value_or(0), restored_index);
  }

private:
  RE::GFxValue original_;
};

bool install_tab_press_hook(RE::GFxMovie& movie) {
  RE::GFxValue prototype;
  if (!movie.GetVariable(&prototype, inventory_lists_prototype_path.data()) ||
      !prototype.IsObject()) {
    return false;
  }

  RE::GFxValue installed;
  if (prototype.GetMember(tab_press_hook_marker.data(), &installed) && installed.IsBool() &&
      installed.GetBool()) {
    return true;
  }

  RE::GFxValue original;
  if (!prototype.GetMember("onTabPress", &original) || !original.IsObject()) {
    return false;
  }

  const auto handler = RE::make_gptr<tab_press_handler>(std::move(original));
  RE::GFxValue replacement;
  movie.CreateFunction(&replacement, handler.get());
  return prototype.SetMember("onTabPress", replacement) &&
         prototype.SetMember(tab_press_hook_marker.data(), RE::GFxValue(true));
}

class quick_item_transfer_add_button_handler final : public RE::GFxFunctionHandler {
public:
  explicit quick_item_transfer_add_button_handler(RE::GFxValue original)
      : original_(std::move(original)) {}

  void Call(Params& params) override {
    const std::optional<std::uint32_t> selected_action =
        params.movie != nullptr ? selected_category_index(*params.movie) : std::nullopt;
    const bool pocket_selected =
        params.movie != nullptr && back_pocket_category_selected(*params.movie);
    const quick_item_transfer_policy::plan action =
        quick_item_transfer_policy::add_button(pocket_selected, selected_action);
    if (action.call_original) {
      if (original_.IsObject()) {
        original_.Invoke("call", params.retVal, params.argsWithThisRef,
                         static_cast<std::size_t>(params.argCount) + 1);
      }
      return;
    }

    const bool reset = params.thisPtr != nullptr && params.thisPtr->IsObject() &&
                       params.thisPtr->SetMember(
                           "currentAction",
                           RE::GFxValue(static_cast<double>(action.replacement_action)));
    bool& suppression_logged = pocket_selected ? back_pocket_suppression_logged_
                                               : undefined_action_suppression_logged_;
    if (reset && !suppression_logged) {
      suppression_logged = true;
      logger::info(
          "QUICK_ITEM_TRANSFER_SUPPRESSED reason={} selected_action={} current_action=0",
          pocket_selected ? "back_pocket" : "undefined_action",
          selected_action.value_or(invalid_category_index));
    }
    if (!reset && !reset_failure_logged_) {
      reset_failure_logged_ = true;
      logger::error(
          "Back Pocket suppressed Quick Item Transfer but could not reset its current action");
    }
  }

private:
  RE::GFxValue original_;
  bool back_pocket_suppression_logged_ = false;
  bool undefined_action_suppression_logged_ = false;
  bool reset_failure_logged_ = false;
};

optional_hook_installation install_quick_item_transfer_compatibility_hook(RE::GFxMovie& movie) {
  RE::GFxValue prototype;
  if (!movie.GetVariable(&prototype, quick_item_transfer_prototype_path.data()) ||
      !prototype.IsObject()) {
    return optional_hook_installation::unavailable;
  }

  RE::GFxValue installed;
  if (prototype.GetMember(quick_item_transfer_hook_marker.data(), &installed) &&
      installed.IsBool() && installed.GetBool()) {
    return optional_hook_installation::already_installed;
  }

  RE::GFxValue original;
  if (!prototype.GetMember("addButton", &original) || !original.IsObject()) {
    return optional_hook_installation::failed;
  }

  const auto handler =
      RE::make_gptr<quick_item_transfer_add_button_handler>(std::move(original));
  RE::GFxValue replacement;
  movie.CreateFunction(&replacement, handler.get());
  if (!prototype.SetMember("addButton", replacement) ||
      !prototype.SetMember(quick_item_transfer_hook_marker.data(), RE::GFxValue(true))) {
    return optional_hook_installation::failed;
  }
  return optional_hook_installation::installed_now;
}

optional_hook_installation ensure_quick_item_transfer_compatibility(RE::GFxMovie& movie) {
  const optional_hook_installation result =
      install_quick_item_transfer_compatibility_hook(movie);
  if (result == optional_hook_installation::installed_now) {
    logger::info("Quick Item Transfer compatibility hook installed");
  } else if (result == optional_hook_installation::failed &&
             !state().quick_item_transfer_failure_logged.exchange(true)) {
    logger::warn("Quick Item Transfer was detected but its addButton API was incompatible");
  }
  return result;
}

class quick_item_transfer_event_dispatch_handler final : public RE::GFxFunctionHandler {
public:
  explicit quick_item_transfer_event_dispatch_handler(RE::GFxValue original)
      : original_(std::move(original)) {}

  void Call(Params& params) override {
    if (!compatibility_resolved_ && params.movie != nullptr) {
      const optional_hook_installation result =
          ensure_quick_item_transfer_compatibility(*params.movie);
      compatibility_resolved_ = result != optional_hook_installation::unavailable;
    }

    if (original_.IsObject()) {
      original_.Invoke("call", params.retVal, params.argsWithThisRef,
                       static_cast<std::size_t>(params.argCount) + 1);
    }
  }

private:
  RE::GFxValue original_;
  bool compatibility_resolved_ = false;
};

bool install_quick_item_transfer_dispatch_hook(RE::GFxMovie& movie) {
  RE::GFxValue inventory_lists;
  if (!movie.GetVariable(&inventory_lists, inventory_lists_path.data()) ||
      !inventory_lists.IsObject()) {
    return false;
  }

  RE::GFxValue installed;
  if (inventory_lists.GetMember(quick_item_transfer_dispatch_hook_marker.data(), &installed) &&
      installed.IsBool() && installed.GetBool()) {
    return true;
  }

  RE::GFxValue original;
  if (!inventory_lists.GetMember("dispatchEvent", &original) || !original.IsObject()) {
    return false;
  }

  const auto handler =
      RE::make_gptr<quick_item_transfer_event_dispatch_handler>(std::move(original));
  RE::GFxValue replacement;
  movie.CreateFunction(&replacement, handler.get());
  return inventory_lists.SetMember("dispatchEvent", replacement) &&
         inventory_lists.SetMember(quick_item_transfer_dispatch_hook_marker.data(),
                                   RE::GFxValue(true));
}

void request_invalidate(RE::GFxMovie& movie) {
  RE::GFxValue item_list;
  if (movie.GetVariable(&item_list, item_list_path.data()) && item_list.IsObject()) {
    static_cast<void>(item_list.Invoke("requestInvalidate", nullptr, nullptr, 0));
  }
}

[[nodiscard]] RE::GFxMovieView* active_movie() {
  RE::UI* ui = RE::UI::GetSingleton();
  runtime_state& current = state();
  if (!current.menu_open.load()) {
    return nullptr;
  }
  const menu_kind menu = current.active_menu.load();
  const std::string_view name = menu_name(menu);
  if (ui == nullptr || menu == menu_kind::none || !ui->IsMenuOpen(name)) {
    return nullptr;
  }
  const RE::GPtr<RE::GFxMovieView> movie = ui->GetMovieView(name);
  return movie.get();
}

[[nodiscard]] bool console_open() {
  RE::UI* ui = RE::UI::GetSingleton();
  return ui != nullptr &&
         (ui->IsMenuOpen(RE::Console::MENU_NAME) ||
          ui->IsMenuOpen(RE::ConsoleNativeUIMenu::MENU_NAME));
}

[[nodiscard]] bool editable_text_focused(RE::GFxMovie& movie) {
  RE::GFxValue selection;
  RE::GFxValue focus_path;
  bool queried = false;
  if (movie.GetVariable(&selection, "_global.Selection") && selection.IsObject()) {
    queried = selection.Invoke("getFocus", &focus_path, nullptr, 0);
  }
  if (!queried && movie.GetVariable(&selection, "Selection") && selection.IsObject()) {
    queried = selection.Invoke("getFocus", &focus_path, nullptr, 0);
  }
  if (!queried) {
    queried = movie.Invoke("Selection.getFocus", &focus_path, nullptr, 0) ||
              movie.Invoke("_global.Selection.getFocus", &focus_path, nullptr, 0);
  }
  if (!queried || !focus_path.IsString() || focus_path.GetString() == nullptr) {
    return false;
  }

  RE::GFxValue focused;
  RE::GFxValue text_type;
  return movie.GetVariable(&focused, focus_path.GetString()) && focused.IsDisplayObject() &&
         focused.GetMember("type", &text_type) && text_type.IsString() &&
         text_type.GetString() != nullptr && std::string_view{text_type.GetString()} == "input";
}

[[nodiscard]] bool item_list_input_disabled(RE::GFxMovie& movie) {
  RE::GFxValue item_list;
  if (!movie.GetVariable(&item_list, item_list_path.data()) || !item_list.IsObject()) {
    return false;
  }

  for (const std::string_view member : {std::string_view{"disableInput"},
                                        std::string_view{"disableSelection"}}) {
    RE::GFxValue value;
    if (item_list.GetMember(member.data(), &value) && value.IsBool() && value.GetBool()) {
      return true;
    }
  }
  return false;
}

integration_result install_menu_integration_now() {
  RE::GFxMovieView* movie = active_movie();
  if (movie == nullptr) {
    logger::debug("ITEM_MENU_INTEGRATION_CANCELLED reason=menu_unavailable");
    return integration_result::cancelled;
  }

  runtime_state& current = state();
  const menu_kind menu = current.active_menu.load();
  current.icon_hook_installed = install_category_icon_hook(*movie);
  const category_installation category = inject_category(*movie, current.icon_hook_installed);
  if (category == category_installation::not_applicable) {
    logger::info("ITEM_MENU_INTEGRATION_SKIPPED menu={} reason=no_player_inventory",
                 menu_name(menu));
    return integration_result::not_applicable;
  }
  current.category_installed = category == category_installation::installed;
  if (!current.category_installed) {
    logger::warn("item menu integration failed open: menu={}, category=false, filters=false, "
                 "icon={}",
                 menu_name(menu), current.icon_hook_installed);
    return integration_result::failed;
  }

  current.filter_installed = install_filters(*movie);
  if (!current.filter_installed) {
    logger::warn("item menu integration incomplete: menu={}, category=true, filters=false, icon={}",
                 menu_name(menu), current.icon_hook_installed);
    return integration_result::failed;
  }

  current.tab_press_hook_installed = install_tab_press_hook(*movie);
  if (!current.tab_press_hook_installed) {
    logger::warn("{} Back Pocket could not hook SkyUI's player-tab transition; tab changes may "
                 "require reselecting the category",
                  menu_name(menu));
  }

  const bool quick_item_transfer_dispatch_hook_installed =
      menu == menu_kind::container && install_quick_item_transfer_dispatch_hook(*movie);
  if (menu == menu_kind::container && !quick_item_transfer_dispatch_hook_installed) {
    logger::warn("Container Menu did not expose the event API needed for Quick Item Transfer "
                 "compatibility");
  }

  request_invalidate(*movie);
  queue_footer_refresh();
  logger::info(
      "ITEM_MENU_INTEGRATION_READY menu={} category_index={} custom_icon={} tab_hook={} "
      "qit_event_hook={}",
      menu_name(menu), current.category_index, current.icon_hook_installed,
      current.tab_press_hook_installed, quick_item_transfer_dispatch_hook_installed);
  return integration_result::ready;
}

void queue_menu_integration() {
  runtime_state& current = state();
  if (current.integration_queued.exchange(true)) {
    return;
  }

  SKSE::GetTaskInterface()->AddUITask([]() {
    runtime_state& queued = state();
    queued.integration_queued.store(false);
    if (!queued.menu_open.load()) {
      return;
    }
    const integration_result result = install_menu_integration_now();
    if (result == integration_result::failed) {
      logger::error("Back Pocket could not attach its SkyUI category");
      notify("Back Pocket category failed to initialize; check BackPocket.log");
    }
  });
}

[[nodiscard]] std::optional<form_id> resolve_inventory_target(RE::GFxMovie& movie) {
  RE::GFxValue item_list;
  RE::GFxValue selected_entry;
  if (!movie.GetVariable(&item_list, item_list_path.data()) || !item_list.IsObject() ||
      !item_list.GetMember("selectedEntry", &selected_entry) || !selected_entry.IsObject()) {
    return std::nullopt;
  }
  return read_form_id(selected_entry);
}

[[nodiscard]] menu_kind footer_menu_kind(const item_menu_footer::menu_kind menu) noexcept {
  switch (menu) {
  case item_menu_footer::menu_kind::inventory:
    return menu_kind::inventory;
  case item_menu_footer::menu_kind::container:
    return menu_kind::container;
  case item_menu_footer::menu_kind::barter:
    return menu_kind::barter;
  case item_menu_footer::menu_kind::gift:
    return menu_kind::gift;
  }
  return menu_kind::none;
}

[[nodiscard]] item_menu_footer::presentation
footer_presentation(RE::GFxMovie& movie, const item_menu_footer::menu_kind menu,
                    const bool footer_selected) {
  runtime_state& current = state();
  if (!current.menu_open.load() || current.active_menu.load() != footer_menu_kind(menu) ||
      editable_text_focused(movie) ||
      (menu != item_menu_footer::menu_kind::barter && !footer_selected)) {
    return {};
  }

  const std::optional<std::uint32_t> selected_flag = selected_category_flag(movie);
  const std::optional<form_id> target = resolve_inventory_target(movie);
  if (!selected_flag.has_value() || !is_player_category_flag(*selected_flag) ||
      !target.has_value() || current.pocket_state == nullptr) {
    return {};
  }

  const bool pocketed = current.pocket_state->contains(*target);
  return {
      .visible = true,
      .text = pocketed ? std::string_view{"Unpocket"} : std::string_view{"Pocket"},
  };
}

void refresh_footer_now() {
  runtime_state& current = state();
  current.footer_refresh_queued.store(false);
  RE::GFxMovieView* movie = active_movie();
  if (movie == nullptr) {
    return;
  }
  if (current.active_menu.load() == menu_kind::container) {
    static_cast<void>(ensure_quick_item_transfer_compatibility(*movie));
  }
  static_cast<void>(
      item_menu_footer::refresh(*movie, resolve_inventory_target(*movie).has_value()));
}

void queue_footer_refresh() {
  runtime_state& current = state();
  if (!current.menu_open.load() || current.footer_refresh_queued.exchange(true)) {
    return;
  }
  SKSE::GetTaskInterface()->AddUITask(&refresh_footer_now);
}

void toggle_selected_item(RE::GFxMovie& movie) {
  runtime_state& current = state();
  if (current.pocket_state == nullptr) {
    return;
  }
  const std::optional<std::uint32_t> selected_flag = selected_category_flag(movie);
  if (!selected_flag.has_value() || !is_player_category_flag(*selected_flag)) {
    return;
  }
  const std::optional<form_id> target = resolve_inventory_target(movie);
  if (!target.has_value()) {
    notify("Back Pocket: no item selected");
    return;
  }

  const bool now_pocketed = current.pocket_state->toggle(*target);
  request_invalidate(movie);
  // SkyUI repairs the selection and refreshes its footer during invalidation. Refreshing here
  // would briefly render the toggled state against the row that is about to disappear.
  notify(now_pocketed ? "Item moved to Back Pocket" : "Item restored to inventory");
  logger::info("ITEM_TOGGLED form={:08X}, pocketed={}, total={}", *target, now_pocketed,
               current.pocket_state->size());
}

void switch_category(RE::GFxMovie& movie) {
  runtime_state& current = state();
  if (!current.category_installed || current.category_index == invalid_category_index) {
    notify("Back Pocket category is unavailable");
    return;
  }

  const std::optional<std::uint32_t> selected_index = selected_category_index(movie);
  const std::optional<std::uint32_t> selected_flag = selected_category_flag(movie);
  if (!selected_flag.has_value() || !is_player_category_flag(*selected_flag)) {
    return;
  }
  const bool pocket_selected = category_policy::is_pocket_category(*selected_flag);
  std::uint32_t destination = current.category_index;
  if (pocket_selected) {
    destination = current.last_regular_category_index;
  } else if (selected_index.has_value()) {
    current.last_regular_category_index = *selected_index;
  }

  RE::GFxValue inventory_lists;
  RE::GFxValue category_list;
  if (!movie.GetVariable(&inventory_lists, inventory_lists_path.data()) ||
      !inventory_lists.IsObject() ||
      !movie.GetVariable(&category_list, category_list_path.data()) || !category_list.IsObject() ||
      !select_category(inventory_lists, category_list, destination)) {
    notify("Back Pocket category could not be selected");
    return;
  }

  if (!pocket_selected) {
    use_mixed_inventory_layout(movie);
  }
  request_invalidate(movie);
  queue_footer_refresh();
  notify(pocket_selected ? "Inventory" : "Back Pocket");
}

void run_pending_action() {
  runtime_state& current = state();
  const queued_action action = current.pending_action.exchange(queued_action::none);
  if (action == queued_action::none) {
    return;
  }

  const std::string_view action_name =
      action == queued_action::toggle_item ? "TOGGLE_ITEM" : "TOGGLE_CATEGORY";
  RE::GFxMovieView* movie = active_movie();
  if (movie == nullptr || !current.menu_open.load()) {
    logger::info("{}_SUPPRESSED reason=menu_unavailable", action_name);
    return;
  }
  if (console_open()) {
    logger::info("{}_SUPPRESSED reason=console", action_name);
    return;
  }
  if (editable_text_focused(*movie)) {
    logger::info("{}_SUPPRESSED reason=editable_text", action_name);
    return;
  }
  if (item_list_input_disabled(*movie)) {
    logger::info("{}_SUPPRESSED reason=item_list_disabled", action_name);
    return;
  }

  switch (action) {
  case queued_action::toggle_item:
    toggle_selected_item(*movie);
    break;
  case queued_action::toggle_category:
    switch_category(*movie);
    break;
  case queued_action::none:
    break;
  }
}

void queue_action(const queued_action action) {
  runtime_state& current = state();
  queued_action expected = queued_action::none;
  if (!current.pending_action.compare_exchange_strong(expected, action)) {
    return;
  }
  SKSE::GetTaskInterface()->AddUITask(&run_pending_action);
}

[[nodiscard]] std::optional<input_binding::button_phase>
button_phase_of(const RE::ButtonEvent& button) noexcept {
  if (button.IsDown()) {
    return input_binding::button_phase::down;
  }
  if (button.IsHeld()) {
    return input_binding::button_phase::held;
  }
  if (button.IsUp()) {
    return input_binding::button_phase::up;
  }
  return std::nullopt;
}

void set_footer_input_family(const item_menu_footer::input_family family) {
  if (item_menu_footer::set_input_family(family)) {
    queue_footer_refresh();
  }
}

[[nodiscard]] bool another_gamepad_button_down(const input_binding::gamepad_button_id button) {
  return std::ranges::any_of(
      state().gamepad_buttons_down,
      [button](const input_binding::gamepad_button_id candidate) { return candidate != button; });
}

void record_gamepad_button(const input_binding::gamepad_button_id button,
                           const input_binding::button_phase phase) {
  std::vector<input_binding::gamepad_button_id>& buttons = state().gamepad_buttons_down;
  const auto found = std::ranges::find(buttons, button);
  if (phase == input_binding::button_phase::down) {
    if (found == buttons.end()) {
      buttons.push_back(button);
    }
    return;
  }
  if (phase == input_binding::button_phase::up && found != buttons.end()) {
    buttons.erase(found);
  }
}

void dispatch_toggle_action(const action_device device, const std::uint32_t code) {
  constexpr std::chrono::milliseconds duplicate_window{250};
  runtime_state& current = state();
  const auto now = std::chrono::steady_clock::now();
  if (current.has_last_toggle_action && device != current.last_toggle_action_device &&
      now - current.last_toggle_action_at <= duplicate_window) {
    logger::debug("TOGGLE_ITEM_DUPLICATE_SUPPRESSED device={} code={}",
                  device == action_device::gamepad ? "gamepad" : "keyboard", code);
    return;
  }

  current.has_last_toggle_action = true;
  current.last_toggle_action_at = now;
  current.last_toggle_action_device = device;
  logger::info("TOGGLE_ITEM_PRESSED device={} code={}",
               device == action_device::gamepad ? "gamepad" : "keyboard", code);
  queue_action(queued_action::toggle_item);
}

class input_event_sink final : public RE::BSTEventSink<RE::InputEvent*> {
public:
  RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
                                        RE::BSTEventSource<RE::InputEvent*>*) override {
    if (events == nullptr || !state().menu_open.load()) {
      return RE::BSEventNotifyControl::kContinue;
    }

    for (RE::InputEvent* event = *events; event != nullptr; event = event->next) {
      const RE::ButtonEvent* button = event->AsButtonEvent();
      const bool released = button != nullptr && button->IsUp();
      if (!released && (event->GetDevice() == RE::INPUT_DEVICE::kGamepad ||
                        event->AsThumbstickEvent() != nullptr)) {
        set_footer_input_family(item_menu_footer::input_family::gamepad);
      } else if (!released && (event->GetDevice() == RE::INPUT_DEVICE::kKeyboard ||
                               event->GetDevice() == RE::INPUT_DEVICE::kMouse ||
                               event->AsMouseMoveEvent() != nullptr)) {
        set_footer_input_family(item_menu_footer::input_family::keyboard_mouse);
      }

      if (button == nullptr) {
        continue;
      }

      runtime_state& current = state();
      if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard && button->IsDown()) {
        const std::uint32_t scan_code = button->GetIDCode();
        if (scan_code == current.configuration.toggle_item_scan_code) {
          input_binding::note_keyboard_action(current.controller_press);
          dispatch_toggle_action(action_device::keyboard, scan_code);
          continue;
        }
        if (current.configuration.toggle_view_scan_code != config::disabled_scan_code &&
            scan_code == current.configuration.toggle_view_scan_code) {
          queue_action(queued_action::toggle_category);
        }
        continue;
      }

      if (button->GetDevice() != RE::INPUT_DEVICE::kGamepad) {
        continue;
      }
      const std::optional<input_binding::button_phase> phase = button_phase_of(*button);
      if (!phase.has_value()) {
        continue;
      }

      const input_binding::gamepad_button_id observed = button->GetIDCode();
      input_binding::controller_result result = input_binding::controller_result::none;
      if (current.controller_toggle_item_button.has_value()) {
        result = input_binding::observe_controller(current.controller_press,
                                                   *current.controller_toggle_item_button, observed,
                                                   *phase, another_gamepad_button_down(observed));
      }
      record_gamepad_button(observed, *phase);
      if (result == input_binding::controller_result::invoke) {
        dispatch_toggle_action(action_device::gamepad, observed);
      }
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

input_event_sink& input_sink() {
  static input_event_sink instance;
  return instance;
}

class menu_event_sink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
  RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
    if (event == nullptr) {
      return RE::BSEventNotifyControl::kContinue;
    }
    const menu_kind menu = classify_menu(event->menuName.c_str());
    if (menu == menu_kind::none) {
      return RE::BSEventNotifyControl::kContinue;
    }

    runtime_state& current = state();
    if (event->opening) {
      current.menu_open.store(false);
      current.active_menu.store(menu);
      current.controller_press = {};
      current.gamepad_buttons_down.clear();
      current.has_last_toggle_action = false;
      current.footer_refresh_queued.store(false);
      current.quick_item_transfer_failure_logged.store(false);
      current.filter_installed = false;
      current.category_installed = false;
      current.icon_hook_installed = false;
      current.tab_press_hook_installed = false;
      current.category_index = invalid_category_index;
      current.last_regular_category_index = default_regular_category_index;
      current.menu_open.store(true);
      logger::info("ITEM_MENU_OPENED menu={} integration_queued=true", menu_name(menu));
      queue_menu_integration();
    } else if (current.active_menu.load() == menu) {
      current.menu_open.store(false);
      current.active_menu.store(menu_kind::none);
      current.integration_queued.store(false);
      current.footer_refresh_queued.store(false);
      current.pending_action.store(queued_action::none);
      current.controller_press = {};
      current.gamepad_buttons_down.clear();
      current.has_last_toggle_action = false;
      logger::info("ITEM_MENU_CLOSED menu={}", menu_name(menu));
    }
    return RE::BSEventNotifyControl::kContinue;
  }
};

menu_event_sink& menu_sink() {
  static menu_event_sink instance;
  return instance;
}

void on_inventory_item(RE::GFxMovieView*, RE::GFxValue* object, RE::InventoryEntryData* item) {
  if (object == nullptr || item == nullptr || item->object == nullptr) {
    return;
  }
  static_cast<void>(object->SetMember(
      form_id_member.data(), RE::GFxValue(static_cast<double>(item->object->GetFormID()))));
}
} // namespace

bool install(pocket& pocket_state, const config::settings& settings) {
  runtime_state& current = state();
  if (current.installed) {
    return true;
  }

  const SKSE::ScaleformInterface* scaleform = SKSE::GetScaleformInterface();
  RE::UI* ui = RE::UI::GetSingleton();
  RE::BSInputDeviceManager* input = RE::BSInputDeviceManager::GetSingleton();
  if (scaleform == nullptr || ui == nullptr || input == nullptr ||
      SKSE::GetTaskInterface() == nullptr) {
    logger::error("Back Pocket inventory integration prerequisites are unavailable");
    return false;
  }

  current.pocket_state = &pocket_state;
  current.configuration = settings;
  current.controller_toggle_item_button =
      settings.controller_toggle_item_key_code.has_value()
          ? input_binding::gamepad_button(*settings.controller_toggle_item_key_code)
          : std::nullopt;
  if (settings.controller_toggle_item_key_code.has_value() &&
      !current.controller_toggle_item_button.has_value()) {
    logger::warn("controller toggle_item key code {} has no raw gamepad mapping; disabling it",
                 *settings.controller_toggle_item_key_code);
  }
  const bool footer_installed =
      item_menu_footer::install(settings.toggle_item_scan_code,
                                settings.controller_toggle_item_key_code, &footer_presentation);
  if (!footer_installed) {
    logger::warn("Back Pocket will continue without one or more native item-menu footers");
  }
  scaleform->Register(&on_inventory_item);
  ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&menu_sink());
  input->AddEventSink(&input_sink());
  current.installed = true;
  logger::info(
      "player item-menu categories and filters registered; item_key={}, controller_item_key={}, "
      "category_shortcut={}, footer={}",
      settings.toggle_item_scan_code,
      settings.controller_toggle_item_key_code.has_value()
          ? static_cast<int>(*settings.controller_toggle_item_key_code)
          : -1,
      settings.toggle_view_scan_code, footer_installed);
  return true;
}
} // namespace back_pocket::inventory_filter
