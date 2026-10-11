#pragma once

#include "rsvp/platform/input_contract.hpp"

#include <cstdint>
#include <optional>

namespace kode_dot {

enum class ReaderNativeLifecycleSignal {
    sleep,
    wake,
    suspend,
    resume,
    exit,
    unknown,
};

struct ReaderNativeLifecycleEvent {
    ReaderNativeLifecycleSignal signal = ReaderNativeLifecycleSignal::unknown;
};

std::optional<rsvp::platform::LifecycleSignal> lifecycle_signal_from_native(
    ReaderNativeLifecycleSignal signal);

std::optional<rsvp::platform::LifecycleEvent> lifecycle_event_from_native(
    const ReaderNativeLifecycleEvent& event,
    std::uint64_t monotonic_ms);

} // namespace kode_dot
