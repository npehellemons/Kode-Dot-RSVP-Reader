#pragma once

#include "rsvp/platform/input_contract.hpp"

#include <cstdint>
#include <optional>

namespace kode_dot {

enum class ReaderNativeControl {
    red_button,
    blue_button,
    dpad_left,
    dpad_right,
    dpad_up,
    dpad_down,
    touch_primary,
    unknown,
};

enum class ReaderNativePhase {
    press,
    release,
    hold_start,
    hold_repeat,
    hold_end,
    unknown,
};

struct ReaderNativeInputEvent {
    ReaderNativeControl control = ReaderNativeControl::unknown;
    ReaderNativePhase phase = ReaderNativePhase::unknown;
};

std::optional<rsvp::platform::PhysicalControl> physical_control_from_native(
    ReaderNativeControl control);

std::optional<rsvp::platform::ControlPhase> control_phase_from_native(
    ReaderNativePhase phase);

std::optional<rsvp::platform::ControlEvent> control_event_from_native(
    const ReaderNativeInputEvent& event,
    std::uint64_t monotonic_ms);

} // namespace kode_dot
