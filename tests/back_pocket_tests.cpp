#include "back_pocket/category_policy.h"
#include "back_pocket/form_id_policy.h"
#include "back_pocket/input_binding.h"
#include "back_pocket/pocket.h"
#include "back_pocket/quick_item_transfer_policy.h"
#include "back_pocket/saved_state.h"
#include "back_pocket/tab_transition_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <limits>

TEST_CASE("a pocket toggles forms and snapshots them deterministically") {
  back_pocket::pocket pocket;

  CHECK(pocket.toggle(0x02000003));
  CHECK(pocket.toggle(0x01000002));
  CHECK_FALSE(pocket.toggle(0x02000003));
  CHECK_FALSE(pocket.toggle(0));

  CHECK(pocket.snapshot() == std::vector<back_pocket::form_id>{0x01000002});
}

TEST_CASE("regular and pocket views are complementary") {
  back_pocket::pocket pocket;
  REQUIRE(pocket.toggle(0x01000001));

  CHECK_FALSE(pocket.visible(0x01000001, back_pocket::inventory_view::regular));
  CHECK(pocket.visible(0x01000002, back_pocket::inventory_view::regular));
  CHECK(pocket.visible(0, back_pocket::inventory_view::regular));

  CHECK(pocket.visible(0x01000001, back_pocket::inventory_view::pocket));
  CHECK_FALSE(pocket.visible(0x01000002, back_pocket::inventory_view::pocket));
  CHECK_FALSE(pocket.visible(0, back_pocket::inventory_view::pocket));
}

TEST_CASE("GFx form IDs accept unsigned and signed 32-bit encodings") {
  using back_pocket::form_id_policy::from_gfx_number;

  constexpr std::uint32_t esl_form = 0xFE001234u;
  constexpr std::int32_t signed_esl = std::bit_cast<std::int32_t>(esl_form);

  CHECK(from_gfx_number(static_cast<double>(esl_form)) == esl_form);
  CHECK(from_gfx_number(static_cast<double>(signed_esl)) == esl_form);
  CHECK(from_gfx_number(-1.0) == std::numeric_limits<std::uint32_t>::max());
  CHECK_FALSE(from_gfx_number(1.5).has_value());
  CHECK_FALSE(from_gfx_number(-2147483649.0).has_value());
  CHECK_FALSE(from_gfx_number(4294967296.0).has_value());
}

TEST_CASE("restoring a pocket removes duplicates and invalid forms") {
  back_pocket::pocket pocket;
  const std::array forms{0x01000001u, 0x01000001u, 0u, 0x02000002u};

  pocket.restore(forms);

  CHECK(pocket.snapshot() == std::vector<back_pocket::form_id>{0x01000001u, 0x02000002u});
}

TEST_CASE("category policy adds and removes only the Back Pocket bit") {
  constexpr std::uint32_t weapon_and_favorite = 0x00000003u;
  const std::uint32_t pocketed =
      back_pocket::category_policy::row_filter_flag(weapon_and_favorite, true);

  CHECK((pocketed & back_pocket::category_policy::pocket_filter_flag) != 0);
  CHECK((pocketed & weapon_and_favorite) == weapon_and_favorite);
  CHECK(back_pocket::category_policy::row_filter_flag(pocketed, false) == weapon_and_favorite);
}

TEST_CASE("category policy distinguishes player and external inventory flags") {
  using namespace back_pocket::category_policy;

  CHECK(is_player_inventory_filter(0x00000001u));
  CHECK(is_player_inventory_filter(player_inventory_filter_mask));
  CHECK_FALSE(is_player_inventory_filter(0));
  CHECK_FALSE(is_player_inventory_filter(0x00000400u));
  CHECK_FALSE(is_player_inventory_filter(0x000FFC00u));
  CHECK_FALSE(is_player_inventory_filter(pocket_filter_flag));

  CHECK(is_pocket_category(pocket_filter_flag));
  CHECK_FALSE(is_pocket_category(player_inventory_filter_mask));
}

TEST_CASE("saved state round trips and rejects malformed payloads") {
  const std::array forms{0x01000001u, 0xFE001234u};
  const auto encoded = back_pocket::saved_state::encode(forms);
  REQUIRE(encoded.error == back_pocket::saved_state::failure::none);

  const auto decoded = back_pocket::saved_state::decode(encoded.bytes);
  CHECK(decoded.error == back_pocket::saved_state::failure::none);
  CHECK(decoded.forms == std::vector<back_pocket::form_id>(forms.begin(), forms.end()));

  const auto truncated = std::span<const std::byte>{encoded.bytes}.first(encoded.bytes.size() - 1);
  CHECK(back_pocket::saved_state::decode(truncated).error ==
        back_pocket::saved_state::failure::invalid_size);
}

TEST_CASE("SkyUI gamepad key codes map to raw controller buttons") {
  using back_pocket::input_binding::gamepad_button;

  CHECK(gamepad_button(266) == 0x0001u);
  CHECK(gamepad_button(280) == 0x0009u);
  CHECK(gamepad_button(281) == 0x000Au);
  CHECK_FALSE(gamepad_button(265).has_value());
  CHECK_FALSE(gamepad_button(282).has_value());
}

TEST_CASE("a standalone controller action commits on release") {
  using namespace back_pocket::input_binding;

  controller_press press;
  CHECK(observe_controller(press, 0x0009u, 0x0009u, button_phase::down, false) ==
        controller_result::none);
  CHECK(observe_controller(press, 0x0009u, 0x0009u, button_phase::held, false) ==
        controller_result::none);
  CHECK(observe_controller(press, 0x0009u, 0x0009u, button_phase::up, false) ==
        controller_result::invoke);
}

TEST_CASE("controller chords and matching keyboard events do not double toggle") {
  using namespace back_pocket::input_binding;

  controller_press modified;
  CHECK(observe_controller(modified, 0x0009u, 0x0009u, button_phase::down, false) ==
        controller_result::none);
  CHECK(observe_controller(modified, 0x0009u, 0x1000u, button_phase::down, true) ==
        controller_result::none);
  CHECK(observe_controller(modified, 0x0009u, 0x0009u, button_phase::up, false) ==
        controller_result::none);

  controller_press mirrored;
  CHECK(observe_controller(mirrored, 0x0009u, 0x0009u, button_phase::down, false) ==
        controller_result::none);
  note_keyboard_action(mirrored);
  CHECK(observe_controller(mirrored, 0x0009u, 0x0009u, button_phase::up, false) ==
        controller_result::none);
}

TEST_CASE("leaving the player tab remembers only Back Pocket") {
  using namespace back_pocket::tab_transition_policy;

  const plan pocket = after_tab_press(external_segment, player_segment, external_segment, true,
                                      false);
  CHECK(pocket.update_pending);
  CHECK(pocket.pending_value);
  CHECK(pocket.normalize_external);
  CHECK_FALSE(pocket.restore_pocket);

  const plan regular = after_tab_press(external_segment, player_segment, external_segment, false,
                                       true);
  CHECK(regular.update_pending);
  CHECK_FALSE(regular.pending_value);
  CHECK_FALSE(regular.normalize_external);
  CHECK_FALSE(regular.restore_pocket);
}

TEST_CASE("returning to the player tab restores and consumes a pending pocket view") {
  using namespace back_pocket::tab_transition_policy;

  const plan restore = after_tab_press(player_segment, external_segment, player_segment, false,
                                       true);
  CHECK(restore.update_pending);
  CHECK_FALSE(restore.pending_value);
  CHECK_FALSE(restore.normalize_external);
  CHECK(restore.restore_pocket);

  const plan regular = after_tab_press(player_segment, external_segment, player_segment, false,
                                       false);
  CHECK(regular.update_pending);
  CHECK_FALSE(regular.pending_value);
  CHECK_FALSE(regular.normalize_external);
  CHECK_FALSE(regular.restore_pocket);
}

TEST_CASE("ignored and unrelated tab presses preserve pending state") {
  using namespace back_pocket::tab_transition_policy;

  const plan ignored = after_tab_press(player_segment, external_segment, external_segment, false,
                                       true);
  CHECK_FALSE(ignored.update_pending);
  CHECK_FALSE(ignored.normalize_external);
  CHECK_FALSE(ignored.restore_pocket);

  const plan same_segment =
      after_tab_press(player_segment, player_segment, player_segment, true, true);
  CHECK_FALSE(same_segment.update_pending);
  CHECK_FALSE(same_segment.normalize_external);
  CHECK_FALSE(same_segment.restore_pocket);

  const plan unknown = after_tab_press(2, player_segment, 2, true, true);
  CHECK_FALSE(unknown.update_pending);
  CHECK_FALSE(unknown.restore_pocket);

  const plan missing = after_tab_press(player_segment, std::nullopt, player_segment, false, true);
  CHECK_FALSE(missing.update_pending);
  CHECK_FALSE(missing.restore_pocket);
}

TEST_CASE("Quick Item Transfer is neutralized for Back Pocket and undefined actions") {
  using namespace back_pocket::quick_item_transfer_policy;

  const plan pocket = add_button(true, 21);
  CHECK_FALSE(pocket.call_original);
  CHECK(pocket.replacement_action == no_action);

  for (const std::uint32_t action : {1u, 9u, 12u, 20u}) {
    const plan regular = add_button(false, action);
    CHECK(regular.call_original);
    CHECK(regular.replacement_action == no_action);
  }

  for (const std::uint32_t action : {10u, 21u, 99u}) {
    const plan undefined = add_button(false, action);
    CHECK_FALSE(undefined.call_original);
    CHECK(undefined.replacement_action == no_action);
  }

  CHECK(add_button(false, 0).call_original);
  CHECK(add_button(false, 11).call_original);
  CHECK(add_button(false, std::nullopt).call_original);
}
