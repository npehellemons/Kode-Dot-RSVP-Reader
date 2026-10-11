#include "rsvp/platform/input_contract.hpp"

namespace rsvp::platform {

std::optional<rsvp::controller::ReaderInput> reader_input_for_control_event(
    const ControlEvent& event) {
    using rsvp::controller::ReaderInput;

    if (event.phase == ControlPhase::hold_end) {
        if (event.control == PhysicalControl::dpad_left ||
            event.control == PhysicalControl::dpad_right) {
            return ReaderInput::hold_end;
        }
        return std::nullopt;
    }

    if (event.phase == ControlPhase::hold_start ||
        event.phase == ControlPhase::hold_repeat) {
        switch (event.control) {
        case PhysicalControl::dpad_left:
            return ReaderInput::hold_left;
        case PhysicalControl::dpad_right:
            return ReaderInput::hold_right;
        case PhysicalControl::red_button:
        case PhysicalControl::blue_button:
        case PhysicalControl::dpad_up:
        case PhysicalControl::dpad_down:
        case PhysicalControl::touch_primary:
            return std::nullopt;
        }
    }

    if (event.phase != ControlPhase::release) {
        return std::nullopt;
    }

    switch (event.control) {
    case PhysicalControl::red_button:
    case PhysicalControl::touch_primary:
        return ReaderInput::red;
    case PhysicalControl::blue_button:
        return ReaderInput::blue;
    case PhysicalControl::dpad_left:
        return ReaderInput::dpad_left;
    case PhysicalControl::dpad_right:
        return ReaderInput::dpad_right;
    case PhysicalControl::dpad_up:
        return ReaderInput::dpad_up;
    case PhysicalControl::dpad_down:
        return ReaderInput::dpad_down;
    }

    return std::nullopt;
}

std::optional<rsvp::controller::ReaderInput> reader_input_for_lifecycle_event(
    const LifecycleEvent& event) {
    using rsvp::controller::ReaderInput;

    switch (event.signal) {
    case LifecycleSignal::sleep:
    case LifecycleSignal::suspend:
    case LifecycleSignal::exit:
        return ReaderInput::sleep;
    case LifecycleSignal::wake:
        return ReaderInput::red;
    case LifecycleSignal::resume:
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace rsvp::platform
