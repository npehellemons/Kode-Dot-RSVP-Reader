#include "kode_dot/reader_lifecycle.hpp"

namespace kode_dot {

std::optional<rsvp::platform::LifecycleSignal> lifecycle_signal_from_native(
    ReaderNativeLifecycleSignal signal) {
    switch (signal) {
    case ReaderNativeLifecycleSignal::sleep:
        return rsvp::platform::LifecycleSignal::sleep;
    case ReaderNativeLifecycleSignal::wake:
        return rsvp::platform::LifecycleSignal::wake;
    case ReaderNativeLifecycleSignal::suspend:
        return rsvp::platform::LifecycleSignal::suspend;
    case ReaderNativeLifecycleSignal::resume:
        return rsvp::platform::LifecycleSignal::resume;
    case ReaderNativeLifecycleSignal::exit:
        return rsvp::platform::LifecycleSignal::exit;
    case ReaderNativeLifecycleSignal::unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

std::optional<rsvp::platform::LifecycleEvent> lifecycle_event_from_native(
    const ReaderNativeLifecycleEvent& event,
    std::uint64_t monotonic_ms) {
    const auto signal = lifecycle_signal_from_native(event.signal);
    if (!signal) {
        return std::nullopt;
    }

    return rsvp::platform::LifecycleEvent{*signal, monotonic_ms};
}

} // namespace kode_dot
