# Back Pocket

Back Pocket keeps permanent utility items out of Skyrim's regular inventory list without
removing them from the player. Pocketed items remain owned, usable by other systems, and available
through a **Back Pocket** category beside SkyUI's Weapons, Armor, and other inventory categories.
The category follows the player into the player side of container, barter, and gift menus; external
inventories never receive or inherit it.

This repository contains a universal Skyrim SE/AE beta for Steam runtimes 1.5.97 and 1.6.1170.
Development and primary in-game validation use LoreRim on 1.6.1170; the 1.5.97 runtime remains a
beta validation target.

## Current controls

- `B`: move the highlighted player-owned base form into or out of Back Pocket.
- `Left Trigger`: the default controller action in full item menus.
- Mouse, keyboard, and controller category navigation can select Back Pocket normally.

Back Pocket appears only in player-owned inventory views. Chest contents, merchant stock, and a
gift recipient's inventory retain their ordinary categories and rows.

Inventory, container, barter, and gift menus append a contextual `Pocket` or `Unpocket` action to
SkyUI's native footer. The displayed glyph follows the last-used keyboard/mouse or controller input.

Quick Item Transfer is detected dynamically in the Container Menu. Back Pocket neutralizes QIT
while its category is selected and for the undefined index SkyUI produces when switching from Back
Pocket to the container side. Ordinary QIT categories continue through the mod's original
implementation.

The keyboard pocket key is a DirectInput scan code in `Data/SKSE/Plugins/BackPocket.ini`. The
controller action uses SkyUI gamepad key codes; `280` is Left Trigger and `-1` disables it. LT is
unused in LoreRim's Keyboard Friendly and Complete Controller item-menu layouts. Pick Up As Junk
uses the same controller default, and Gamepad++ maps LT to Left Equip, so users combining those
bindings must rebind or disable one of them. Back Pocket observes its action without consuming
Skyrim's native input.

An optional shortcut that jumps between Back Pocket and the last regular category is disabled by
default; set `toggle_view_scan_code=47` to bind it to `V`. A value of `0` disables that shortcut.
Hotkey actions are suppressed while typing in SkyUI's search field, using an internal SkyUI item
dialog, or viewing the Console over an item menu.

Pocketed enchanted items are hidden from the enchanter's Disenchant list by default. Pocketed
unenchanted items remain available in the Item list when applying a new enchantment. Set
`hide_pocketed_from_disenchanting=false` under `[menus]` in `BackPocket.ini` to show them in the
Disenchant list again. Back Pocket reads this setting when Skyrim starts.

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

The 0.2.2 build uses CommonLibSSE-NG commit
`b93280e832f263dbef44e44cbe2936622a02f91a`. Use that revision to reproduce the release build.

The resulting DLL supports Skyrim 1.5.97 with SKSE64 2.0.20 and Skyrim 1.6.1170 with SKSE64 2.2.6.
Users need the Address Library package matching their game runtime.

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

1. Complete in-game validation on Skyrim 1.5.97.
2. Add optional DIII row-icon metadata and global presets.
3. Evaluate per-instance identity for enchanted, tempered, renamed, or otherwise distinct items.
