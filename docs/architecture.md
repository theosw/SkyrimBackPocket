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

Back Pocket wraps SkyUI's existing `onTabPress` function without replacing its behavior. SkyUI's
two category segments do not have equal lengths once Back Pocket is appended, so its normal
right-to-left index mapping can land on the divider and cannot map back to Back Pocket. The wrapper
records whether Back Pocket was selected on a real transition to the external tab. Since Back
Pocket has no external counterpart, the wrapper selects the last real external category instead of
letting SkyUI land on its divider. On the matching return transition, it synchronously restores
Back Pocket by locating the marked category entry, refreshing the category renderer, rerunning
`showItemsList`, and invalidating the item enumeration. Ignored tab presses and regular-category
transitions retain SkyUI's native behavior.

## Native footer and input

Inventory, Container, Barter, and Gift menu `PostCreate` hooks add one renderer to SkyUI's existing
`navPanel` and wrap `updateBottomBar`. The wrapper first runs SkyUI's original function, then appends
a `Pocket` or `Unpocket` action only for an eligible player-owned row. It reads the configured
keyboard scan code or SkyUI gamepad key code according to the last active input family. Replacement
menus that do not expose this API fail open without affecting Back Pocket filtering or bindings.

The controller action maps SkyUI key codes 266-281 to Skyrim's raw gamepad button IDs. It commits on
release only when no other controller button participated in the press. Keyboard events emitted by
controller overhauls are coalesced with the matching physical press, and a short cross-device guard
prevents delayed synthetic events from toggling twice. Input remains observational: Back Pocket
does not suppress native actions assigned to the same button.

Queued actions are validated against the item menu itself instead of inferring focus from Skyrim's
UI stack. Back Pocket suppresses an action when SkyUI has focused editable text, when SkyUI marks
the item list disabled for an internal quantity/submenu interaction, or while either component of
the Console is open. The highlighted player-owned row is validated again immediately before it is
toggled.

Quick Item Transfer derives its Container Menu footer action directly from SkyUI's category index.
QIT loads its Scaleform class asynchronously through Papyrus, so Back Pocket wraps the active
`InventoryLists.dispatchEvent` instance instead of polling for it. Immediately before SkyUI
dispatches an inventory event, Back Pocket detects QIT and wraps its `addButton` method. This occurs
before QIT's registered event listener can rebuild the footer. While the marked Back Pocket
category is selected, the wrapper omits QIT's unsupported footer action and resets `currentAction`
to zero so QIT's Papyrus hotkey safely no-ops. It also neutralizes undefined QIT indices such as 10,
which SkyUI derives when switching from Back Pocket index 21 to the external container segment.
QIT's defined category actions call its original implementation unchanged, and the hook is absent
when QIT is not installed. Back Pocket also performs the same detection before its own direct
footer refreshes.

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

- One CommonLibSSE-NG binary targets Skyrim SE 1.5.97 and Skyrim AE 1.6.1170. Runtime loading is
  explicitly restricted to those two versions; 1.5.97 still needs beta in-game validation.
- Keyboard and separately configurable controller item actions; the optional category shortcut is
  keyboard-only.
- Player views in Inventory, Container, Barter, and Gift menus only; crafting menus are unchanged.
- Membership is base-form-wide, not per-instance.
- No MCM; configuration is an INI and feedback uses notifications plus SkyUI's native item-menu
  footer where available.
- The optional category shortcut is disabled by default and keyboard-only; standard category
  navigation remains controller-aware.
