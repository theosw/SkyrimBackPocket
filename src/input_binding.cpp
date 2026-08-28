#include "back_pocket/input_binding.h"

namespace back_pocket::input_binding {
controller_result observe_controller(controller_press& state, const gamepad_button_id trigger,
                                     const gamepad_button_id observed, const button_phase phase,
                                     const bool another_button_down) noexcept {
  if (observed != trigger) {
    if (state.trigger_down && phase != button_phase::up) {
      state.modified = true;
    }
    return controller_result::none;
  }
  if (phase == button_phase::down) {
    state = {
        .trigger_down = true,
        .modified = another_button_down,
        .keyboard_action_observed = false,
    };
    return controller_result::none;
  }
  if (phase != button_phase::up || !state.trigger_down) {
    return controller_result::none;
  }

  const bool invoke = !state.modified && !state.keyboard_action_observed;
  state = {};
  return invoke ? controller_result::invoke : controller_result::none;
}

void note_keyboard_action(controller_press& state) noexcept {
  if (state.trigger_down) {
    state.keyboard_action_observed = true;
  }
}
} // namespace back_pocket::input_binding
