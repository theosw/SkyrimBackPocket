#include "pch.h"

#include "disenchant_filter.h"

#include "back_pocket/disenchant_policy.h"

namespace back_pocket::disenchant_filter {
namespace {
constexpr std::uint8_t relative_call_opcode = 0xE8;
constexpr std::size_t trampoline_bytes = 14;

struct runtime_state {
  pocket* pocket_state = nullptr;
  bool installed = false;
};

runtime_state& state() {
  static runtime_state instance;
  return instance;
}

class is_quest_object_hook {
public:
  static bool install() {
    REL::Relocation<std::uintptr_t> function{RELOCATION_ID(50454, 51359)};
    const std::uintptr_t call_site =
        function.address() + REL::Relocate<std::uintptr_t>(0x133, 0x140);
    const std::uint8_t opcode = *reinterpret_cast<const std::uint8_t*>(call_site);
    if (opcode != relative_call_opcode) {
      logger::error("native disenchant hook call site is invalid: address={:016X}, opcode={:02X}",
                    call_site, opcode);
      return false;
    }

    SKSE::AllocTrampoline(trampoline_bytes);
    original_ = SKSE::GetTrampoline().write_call<5>(call_site, &thunk);
    if (original_.address() == 0) {
      logger::error("native disenchant hook did not capture the existing call target");
      return false;
    }

    logger::info(
        "native disenchant protection hook installed: call_site={:016X}, chained_target={:016X}",
        call_site, original_.address());
    return true;
  }

private:
  static bool thunk(RE::InventoryEntryData* item) {
    const bool original_protected = original_(item);
    runtime_state& current = state();
    if (original_protected || current.pocket_state == nullptr || item == nullptr ||
        item->object == nullptr) {
      return original_protected;
    }

    const form_id form = item->object->GetFormID();
    const bool pocketed = current.pocket_state->contains(form);
    const bool protect =
        disenchant_policy::should_protect(original_protected, item->IsEnchanted(), pocketed);
    if (protect) {
      logger::info("DISENCHANT_ENTRY_PROTECTED form={:08X}", form);
    }
    return protect;
  }

  inline static REL::Relocation<decltype(thunk)> original_;
};
} // namespace

bool install(pocket& pocket_state, const bool hide_pocketed_items) {
  runtime_state& current = state();
  if (current.installed) {
    return true;
  }

  current.pocket_state = &pocket_state;
  if (!hide_pocketed_items) {
    current.installed = true;
    logger::info("native disenchant protection disabled by configuration");
    return true;
  }

  if (!is_quest_object_hook::install()) {
    return false;
  }

  current.installed = true;
  return true;
}
} // namespace back_pocket::disenchant_filter
