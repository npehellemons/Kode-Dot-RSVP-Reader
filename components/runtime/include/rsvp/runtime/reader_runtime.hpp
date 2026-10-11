#pragma once

#include "rsvp/controller/reader_controller.hpp"
#include "rsvp/lifecycle/reader_lifecycle.hpp"
#include "rsvp/platform/input_contract.hpp"

#include <cstdint>
#include <vector>

namespace rsvp::runtime {

class ReaderRuntime {
public:
    ReaderRuntime(rsvp::controller::ReaderController controller,
                  rsvp::lifecycle::ReaderStatePaths paths,
                  rsvp::lifecycle::DocumentIdentity document);

    static ReaderRuntime restore(
        const std::vector<rsvp::text::DisplayFrame>& frames,
        const std::vector<rsvp::text::Chapter>& chapters,
        rsvp::lifecycle::ReaderStatePaths paths,
        rsvp::lifecycle::DocumentIdentity document,
        rsvp::playback::PlaybackSettings fallback_settings = {},
        std::uint64_t monotonic_ms = 0);

    rsvp::controller::ReaderView view() const;
    const rsvp::controller::ReaderController& controller() const;

    bool handle_control_event(const rsvp::platform::ControlEvent& event,
                              std::uint64_t updated_at);
    bool handle_lifecycle_event(const rsvp::platform::LifecycleEvent& event,
                                std::uint64_t updated_at);
    bool tick(std::uint64_t monotonic_ms);
    bool persist(std::uint64_t updated_at) const;

private:
    rsvp::controller::ReaderController controller_;
    rsvp::lifecycle::ReaderStatePaths paths_;
    rsvp::lifecycle::DocumentIdentity document_;
};

} // namespace rsvp::runtime
