# Back Pocket

Back Pocket keeps permanent utility items out of Skyrim's regular inventory list without
removing them from the player. Pocketed items remain owned, usable by other systems, and available
through a **Back Pocket** category beside SkyUI's Weapons, Armor, and other inventory categories.
The category follows the player into the player side of container, barter, and gift menus; external
inventories never receive or inherit it.

This repository contains a Skyrim AE Steam 1.6.1170 beta for SkyUI/SKSE, developed and validated in
LoreRim.

## Current controls

- `B`: move the highlighted player-owned base form into or out of Back Pocket.
- Mouse, keyboard, and controller category navigation can select Back Pocket normally.

Back Pocket appears only in player-owned inventory views. Chest contents, merchant stock, and a
gift recipient's inventory retain their ordinary categories and rows.

The pocket key is a DirectInput scan code in `Data/SKSE/Plugins/BackPocket.ini`. An optional shortcut
that jumps between Back Pocket and the last regular category is disabled by default; set
`toggle_view_scan_code=47` to bind it to `V`. A value of `0` disables that shortcut. Typing in
SkyUI's search field suppresses hotkey actions.

## Design

Back Pocket reserves an unused item-menu filter bit and injects a player-side category for it. Two
native `applyFilter(array)` objects join SkyUI's existing `FilteredEnumeration`: the first marks
player-owned rows before SkyUI's type filter, and the last excludes pocketed rows from the player's
regular categories. External rows are identified by SkyUI's separate container flag range and are
never marked or hidden. Category filtering, search, sorting, row layout, selection, and scrollbar
bounds therefore remain owned by SkyUI.

The pouch is a standalone `Interface/BackPocket/category_icon.swf`. Back Pocket redirects only its
own category renderer to that file, leaving Aura, Dear Diary, and other category icon packs intact.

Membership is per save through SKSE serialization. Form IDs are resolved on load, so ordinary
plugin load-order changes are supported. The MVP tracks base forms: all instances represented by a
row share the same Back Pocket state.

## Build

Prerequisites:

- Visual Studio 2022 with the x64 C++ toolchain
- CMake 3.24+
- vcpkg through `VCPKG_ROOT`
- CommonLibSSE-NG through `CommonLibSSEPath` or `BACK_POCKET_COMMONLIBSSE_NG_DIR`

```powershell
cmake --preset ae
cmake --build --preset dev
ctest --preset dev
cmake --build --preset release
```

The `stage` target produces an MO2-ready folder under `package/BackPocket`.

For local development, configure an MO2 mod folder once and use the `deploy` target:

```powershell
cmake -S . -B build -DBACK_POCKET_DEPLOY_DIR="D:/Lorerim/mods/BackPocket"
cmake --build build --config RelWithDebInfo --target deploy
```

The deploy target updates `SKSE/Plugins/BackPocket.dll`, `BackPocket.ini`, and the standalone
category icon; MO2 metadata is left untouched.

## Planned validation and expansion

1. Validate player-side container, barter, and gift behavior in SkyUI 6.11 with Aura's Inventory
   Tweaks.
2. Add an inventory footer prompt and a configurable controller shortcut.
3. Add optional DIII row-icon metadata and global presets.
4. Evaluate per-instance identity for enchanted, tempered, renamed, or otherwise distinct items.
