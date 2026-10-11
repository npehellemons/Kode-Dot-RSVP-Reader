#pragma once

#include "rsvp/controller/reader_controller.hpp"

#include <cstdint>
#include <optional>

namespace rsvp::platform {

enum class PhysicalControl {
    red_button,
    blue_button,
    dpad_left,
    dpad_right,
    dpad_up,
    dpad_down,
    touch_primary,
};

enum class ControlPhase {
    press,
    release,
    hold_start,
    hold_repeat,
    hold_end,
};

struct ControlEvent {
    PhysicalControl control = PhysicalControl::red_button;
    ControlPhase phase = ControlPhase::press;
    std::uint64_t monotonic_ms = 0;
};

enum class LifecycleSignal {
    sleep,
    wake,
    suspend,
    resume,
    exit,
};

struct LifecycleEvent {
    LifecycleSignal signal = LifecycleSignal::sleep;
    std::uint64_t monotonic_ms = 0;
};

std::optional<rsvp::controller::ReaderInput> reader_input_for_control_event(
    const ControlEvent& event);

std::optional<rsvp::controller::ReaderInput> reader_input_for_lifecycle_event(
    const LifecycleEvent& event);

} // namespace rsvp::platform
