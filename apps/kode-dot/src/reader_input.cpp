#include "kode_dot/reader_input.hpp"

namespace kode_dot {

std::optional<rsvp::platform::PhysicalControl> physical_control_from_native(
    ReaderNativeControl control) {
    switch (control) {
    case ReaderNativeControl::red_button:
        return rsvp::platform::PhysicalControl::red_button;
    case ReaderNativeControl::blue_button:
        return rsvp::platform::PhysicalControl::blue_button;
    case ReaderNativeControl::dpad_left:
        return rsvp::platform::PhysicalControl::dpad_left;
    case ReaderNativeControl::dpad_right:
        return rsvp::platform::PhysicalControl::dpad_right;
    case ReaderNativeControl::dpad_up:
        return rsvp::platform::PhysicalControl::dpad_up;
    case ReaderNativeControl::dpad_down:
        return rsvp::platform::PhysicalControl::dpad_down;
    case ReaderNativeControl::touch_primary:
        return rsvp::platform::PhysicalControl::touch_primary;
    case ReaderNativeControl::unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<rsvp::platform::ControlPhase> control_phase_from_native(
    ReaderNativePhase phase) {
    switch (phase) {
    case ReaderNativePhase::press:
        return rsvp::platform::ControlPhase::press;
    case ReaderNativePhase::release:
        return rsvp::platform::ControlPhase::release;
    case ReaderNativePhase::hold_start:
        return rsvp::platform::ControlPhase::hold_start;
    case ReaderNativePhase::hold_repeat:
        return rsvp::platform::ControlPhase::hold_repeat;
    case ReaderNativePhase::hold_end:
        return rsvp::platform::ControlPhase::hold_end;
    case ReaderNativePhase::unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<rsvp::platform::ControlEvent> control_event_from_native(
    const ReaderNativeInputEvent& event,
    std::uint64_t monotonic_ms) {
    const auto control = physical_control_from_native(event.control);
    const auto phase = control_phase_from_native(event.phase);
    if (!control || !phase) {
        return std::nullopt;
    }

    return rsvp::platform::ControlEvent{*control, *phase, monotonic_ms};
}

} // namespace kode_dot
