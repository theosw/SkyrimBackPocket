#include "pch.h"

#include "inventory_filter.h"

#include "back_pocket/category_policy.h"

namespace back_pocket::inventory_filter {
namespace {
constexpr std::string_view inventory_lists_path = "_root.Menu_mc.inventoryLists";
constexpr std::string_view category_list_path = "_root.Menu_mc.inventoryLists.categoryList";
constexpr std::string_view item_list_path = "_root.Menu_mc.inventoryLists.itemList";
constexpr std::string_view item_enumeration_path =
    "_root.Menu_mc.inventoryLists.itemList.listEnumeration";
constexpr std::string_view category_entry_prototype_path = "_global.CategoryListEntry.prototype";

constexpr std::string_view form_id_member = "backPocketFormId";
constexpr std::string_view fallback_form_id_member = "formId";
constexpr std::string_view filter_flag_member = "filterFlag";
constexpr std::string_view membership_filter_marker = "backPocketMembershipFilterInstalled";
constexpr std::string_view visibility_filter_marker = "backPocketVisibilityFilterInstalled";
constexpr std::string_view category_marker = "backPocketCategory";
constexpr std::string_view icon_hook_marker = "backPocketIconHookInstalled";
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
  failed,
};

enum class queued_action : std::uint8_t {
  none,
  toggle_item,
  toggle_category,
};

struct runtime_state {
  pocket* pocket_state = nullptr;
  config::settings configuration;
  bool installed = false;
  bool menu_open = false;
  menu_kind active_menu = menu_kind::none;
  bool filter_installed = false;
  bool category_installed = false;
  bool icon_hook_installed = false;
  std::uint32_t category_index = invalid_category_index;
  std::uint32_t last_regular_category_index = default_regular_category_index;
  std::atomic_bool integration_queued = false;
  std::atomic<queued_action> pending_action = queued_action::none;
};

runtime_state& state() {
  static runtime_state instance;
  return instance;
}

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
    if (const auto form = number_as_u32(value); form.has_value()) {
      return *form;
    }
  }
  if (row.GetMember(fallback_form_id_member.data(), &value)) {
    return number_as_u32(value);
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
  const bool pocketed = category_policy::is_player_inventory_filter(original) &&
                        form.has_value() && current.pocket_state->contains(*form);
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
               index.has_value() && *index != state().category_index) {
      current.last_regular_category_index = *index;
    }

    RE::GFxValue& rows = params.args[0];
    for (std::uint32_t index = 0; index < rows.GetArraySize();) {
      RE::GFxValue row;
      if (!rows.GetElement(index, &row) || !row.IsObject()) {
        if (pocket_view) {
          static_cast<void>(rows.RemoveElement(index));
        } else {
          ++index;
        }
        continue;
      }

      const bool player_row =
          category_policy::is_player_inventory_filter(read_filter_flag(row));
      const std::optional<form_id> form = read_form_id(row);
      const bool pocketed =
          player_row && form.has_value() && current.pocket_state->contains(*form);
      const bool visible = pocket_view ? pocketed : !pocketed;
      if (visible) {
        ++index;
      } else {
        static_cast<void>(rows.RemoveElement(index));
      }
    }
  }
};

[[nodiscard]] bool initializes_back_pocket_renderer(
    const RE::GFxFunctionHandler::Params& params) {
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
    return entry_count == 0 ? category_installation::failed
                            : category_installation::not_applicable;
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

void request_invalidate(RE::GFxMovie& movie) {
  RE::GFxValue item_list;
  if (movie.GetVariable(&item_list, item_list_path.data()) && item_list.IsObject()) {
    static_cast<void>(item_list.Invoke("requestInvalidate", nullptr, nullptr, 0));
  }
}

[[nodiscard]] RE::GFxMovie* active_movie() {
  RE::UI* ui = RE::UI::GetSingleton();
  const std::string_view name = menu_name(state().active_menu);
  if (ui == nullptr || name.empty() || !ui->IsMenuOpen(name)) {
    return nullptr;
  }
  const RE::GPtr<RE::GFxMovieView> movie = ui->GetMovieView(name);
  return movie.get();
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
  if (!queried || !focus_path.IsString() || focus_path.GetString() == nullptr) {
    return false;
  }

  RE::GFxValue focused;
  RE::GFxValue text_type;
  return movie.GetVariable(&focused, focus_path.GetString()) && focused.IsDisplayObject() &&
         focused.GetMember("type", &text_type) && text_type.IsString() &&
         text_type.GetString() != nullptr && std::string_view{text_type.GetString()} == "input";
}

integration_result install_menu_integration_now() {
  RE::GFxMovie* movie = active_movie();
  if (movie == nullptr) {
    return integration_result::failed;
  }

  runtime_state& current = state();
  current.icon_hook_installed = install_category_icon_hook(*movie);
  const category_installation category = inject_category(*movie, current.icon_hook_installed);
  if (category == category_installation::not_applicable) {
    logger::info("ITEM_MENU_INTEGRATION_SKIPPED menu={} reason=no_player_inventory",
                 menu_name(current.active_menu));
    return integration_result::not_applicable;
  }
  current.category_installed = category == category_installation::installed;
  if (!current.category_installed) {
    logger::warn("item menu integration failed open: menu={}, category=false, filters=false, "
                 "icon={}",
                 menu_name(current.active_menu), current.icon_hook_installed);
    return integration_result::failed;
  }

  current.filter_installed = install_filters(*movie);
  if (!current.filter_installed) {
    logger::warn("item menu integration incomplete: menu={}, category=true, filters=false, icon={}",
                 menu_name(current.active_menu), current.icon_hook_installed);
    return integration_result::failed;
  }

  request_invalidate(*movie);
  logger::info("ITEM_MENU_INTEGRATION_READY menu={} category_index={} custom_icon={}",
               menu_name(current.active_menu), current.category_index,
               current.icon_hook_installed);
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
    if (!queued.menu_open) {
      return;
    }
    if (install_menu_integration_now() == integration_result::failed) {
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
      !category_list.SetMember("selectedIndex", RE::GFxValue(static_cast<double>(destination)))) {
    notify("Back Pocket category could not be selected");
    return;
  }

  static_cast<void>(inventory_lists.Invoke("showItemsList", nullptr, nullptr, 0));
  if (!pocket_selected) {
    use_mixed_inventory_layout(movie);
  }
  notify(pocket_selected ? "Inventory" : "Back Pocket");
}

void run_pending_action() {
  runtime_state& current = state();
  const queued_action action = current.pending_action.exchange(queued_action::none);
  RE::GFxMovie* movie = active_movie();
  if (movie == nullptr || !current.menu_open || editable_text_focused(*movie)) {
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

class input_event_sink final : public RE::BSTEventSink<RE::InputEvent*> {
public:
  RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
                                        RE::BSTEventSource<RE::InputEvent*>*) override {
    if (events == nullptr || !state().menu_open) {
      return RE::BSEventNotifyControl::kContinue;
    }

    for (RE::InputEvent* event = *events; event != nullptr; event = event->next) {
      const RE::ButtonEvent* button = event->AsButtonEvent();
      if (button == nullptr || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard ||
          !button->IsDown()) {
        continue;
      }

      const std::uint32_t scan_code = button->GetIDCode();
      if (scan_code == state().configuration.toggle_item_scan_code) {
        queue_action(queued_action::toggle_item);
        break;
      }
      if (state().configuration.toggle_view_scan_code != config::disabled_scan_code &&
          scan_code == state().configuration.toggle_view_scan_code) {
        queue_action(queued_action::toggle_category);
        break;
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
      current.menu_open = true;
      current.active_menu = menu;
      current.filter_installed = false;
      current.category_installed = false;
      current.icon_hook_installed = false;
      current.category_index = invalid_category_index;
      current.last_regular_category_index = default_regular_category_index;
      logger::info("ITEM_MENU_OPENED menu={} integration_queued=true", menu_name(menu));
      queue_menu_integration();
    } else if (current.active_menu == menu) {
      current.menu_open = false;
      current.active_menu = menu_kind::none;
      current.integration_queued.store(false);
      current.pending_action.store(queued_action::none);
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
  scaleform->Register(&on_inventory_item);
  ui->GetEventSource<RE::MenuOpenCloseEvent>()->AddEventSink(&menu_sink());
  input->AddEventSink(&input_sink());
  current.installed = true;
  logger::info("player item-menu categories and filters registered; item_key={}, "
               "category_shortcut={}",
               settings.toggle_item_scan_code, settings.toggle_view_scan_code);
  return true;
}
} // namespace back_pocket::inventory_filter
