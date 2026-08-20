# Architecture notes

## Invariants

- Back Pocket changes presentation and membership metadata, never physical inventory ownership.
- Only player-owned inventory views may be filtered. External container, merchant, and recipient
  inventories remain unchanged.
- An unidentified Scaleform row stays visible in regular view and stays hidden in Back Pocket view.
  This makes integration failure conservative: normal inventory does not lose entries.
- Category selection is session UI state and is not serialized. Membership is per-save state and is
  serialized.
- Base form `0` is invalid and is never persisted.

## SkyUI integration

SkyUI constructs a `FilteredEnumeration` for `itemList.entryList`, then adds its type, name, and sort
filters. Back Pocket reserves `0x00100000`, an unused item-menu filter bit, and injects a player-side
category using that flag in Inventory, Container, Barter, and Gift menus.

Back Pocket installs two native filters around SkyUI's chain:

1. A membership filter is prepended. It strips stale Back Pocket metadata and reapplies the reserved
   bit to pocketed player rows before SkyUI's type filter runs. SkyUI bits 0-9 identify player rows;
   its separate external-inventory bits are never marked.
2. A visibility filter is appended through the public `addFilter` method. It removes pocketed rows
   from every regular category and removes regular rows from the Back Pocket category.

The Back Pocket category requests the mixed `All` column layout, since it can contain weapons,
armor, books, and miscellaneous items together.

Refreshing uses `itemList.requestInvalidate()`. That calls SkyUI's enumeration invalidation,
recalculates the maximum scroll position, and repairs selection through SkyUI's own list code.

SKSE's inventory-entry callback annotates item-menu rows with `backPocketFormId`. The filter also
accepts SkyUI's `formId` field as a compatibility fallback.

Container and Barter menus divide their category list into an external segment and a player segment.
Back Pocket is appended only to the player segment. Gift menus show one inventory at a time, so the
category is omitted when the displayed inventory is not the player's. Any unfamiliar category
layout fails open before filters are installed.

## Category icon

`CategoryListEntry.initialize` normally loads every icon from the active SkyUI category art file.
Back Pocket wraps that function and temporarily changes `iconSource` only for its injected entry.
The renderer then loads `Interface/BackPocket/category_icon.swf` and selects its `back_pocket` frame.
If the hook is unavailable, the category safely falls back to the active icon pack's Misc icon.

The SWF is generated from repository-owned vector geometry and does not replace or patch any item
menu SWF or another mod's category icon pack.

## Persistence

The SKSE co-save record contains a count followed by resolved 32-bit form IDs. Loading rejects
oversized, truncated, zero-form, and version-mismatched payloads. SKSE `ResolveFormID` handles
ordinary load-order movement; unresolved forms are dropped.

## MVP limitations

- Skyrim AE Steam runtime 1.6.1170 only.
- Keyboard bindings only.
- Player views in Inventory, Container, Barter, and Gift menus only; crafting menus are unchanged.
- Membership is base-form-wide, not per-instance.
- No MCM or footer prompt yet; configuration is an INI and feedback uses notifications.
- The optional category shortcut is disabled by default and keyboard-only; standard category
  navigation remains controller-aware.
