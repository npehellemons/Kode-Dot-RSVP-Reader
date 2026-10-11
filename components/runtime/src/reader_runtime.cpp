#include "rsvp/runtime/reader_runtime.hpp"

#include <utility>

namespace rsvp::runtime {

ReaderRuntime::ReaderRuntime(rsvp::controller::ReaderController controller,
                             rsvp::lifecycle::ReaderStatePaths paths,
                             rsvp::lifecycle::DocumentIdentity document)
    : controller_(std::move(controller)),
      paths_(std::move(paths)),
      document_(std::move(document)) {}

ReaderRuntime ReaderRuntime::restore(
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    rsvp::lifecycle::ReaderStatePaths paths,
    rsvp::lifecycle::DocumentIdentity document,
    rsvp::playback::PlaybackSettings fallback_settings,
    std::uint64_t monotonic_ms) {
    auto controller = rsvp::lifecycle::restore_reader_controller(
        frames, chapters, paths, document, fallback_settings, monotonic_ms);
    return ReaderRuntime(std::move(controller), std::move(paths), std::move(document));
}

rsvp::controller::ReaderView ReaderRuntime::view() const {
    return controller_.view();
}

const rsvp::controller::ReaderController& ReaderRuntime::controller() const {
    return controller_;
}

bool ReaderRuntime::handle_control_event(const rsvp::platform::ControlEvent& event,
                                         std::uint64_t updated_at) {
    const auto input = rsvp::platform::reader_input_for_control_event(event);
    if (!input) {
        return false;
    }

    const auto handled = controller_.handle_input(*input, event.monotonic_ms);
    if (handled) {
        persist(updated_at);
    }
    return handled;
}

bool ReaderRuntime::handle_lifecycle_event(const rsvp::platform::LifecycleEvent& event,
                                           std::uint64_t updated_at) {
    switch (event.signal) {
    case rsvp::platform::LifecycleSignal::sleep:
    case rsvp::platform::LifecycleSignal::suspend:
    case rsvp::platform::LifecycleSignal::exit:
        return rsvp::lifecycle::pause_and_persist_for_sleep(controller_,
                                                            paths_,
                                                            document_,
                                                            updated_at);
    case rsvp::platform::LifecycleSignal::wake:
    case rsvp::platform::LifecycleSignal::resume:
        break;
    }

    const auto input = rsvp::platform::reader_input_for_lifecycle_event(event);
    if (!input) {
        return false;
    }

    const auto handled = controller_.handle_input(*input, event.monotonic_ms);
    if (handled) {
        persist(updated_at);
    }
    return handled;
}

bool ReaderRuntime::tick(std::uint64_t monotonic_ms) {
    return controller_.tick(monotonic_ms);
}

bool ReaderRuntime::persist(std::uint64_t updated_at) const {
    return rsvp::lifecycle::persist_reader_state(
        paths_,
        rsvp::lifecycle::capture_reader_state(document_, controller_, updated_at));
}

} // namespace rsvp::runtime
