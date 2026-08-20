#include "back_pocket/category_policy.h"
#include "back_pocket/pocket.h"
#include "back_pocket/saved_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

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
