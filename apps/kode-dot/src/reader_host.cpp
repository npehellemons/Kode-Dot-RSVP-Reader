#include "kode_dot/reader_host.hpp"

#include <utility>

namespace kode_dot {

ReaderHost::ReaderHost(ReaderApp app, ReaderClock& clock, ReaderRenderer& renderer)
    : app_(std::move(app)),
      clock_(clock),
      renderer_(renderer) {}

void ReaderHost::render() {
    renderer_.render(make_reader_screen_frame(app_.view_model()));
}

bool ReaderHost::tick() {
    return render_if_handled(app_.tick(clock_.monotonic_ms()));
}

bool ReaderHost::dispatch_control(rsvp::platform::PhysicalControl control,
                                  rsvp::platform::ControlPhase phase) {
    const auto now = clock_.monotonic_ms();
    return render_if_handled(app_.handle_control_event(
        rsvp::platform::ControlEvent{control, phase, now},
        clock_.state_timestamp_ms()));
}

bool ReaderHost::dispatch_lifecycle(rsvp::platform::LifecycleSignal signal) {
    const auto now = clock_.monotonic_ms();
    return render_if_handled(app_.handle_lifecycle_event(
        rsvp::platform::LifecycleEvent{signal, now},
        clock_.state_timestamp_ms()));
}

bool ReaderHost::persist() const {
    return app_.persist(clock_.state_timestamp_ms());
}

const ReaderApp& ReaderHost::app() const {
    return app_;
}

ReaderApp& ReaderHost::app() {
    return app_;
}

bool ReaderHost::render_if_handled(bool handled) {
    if (handled) {
        render();
    }
    return handled;
}

} // namespace kode_dot
