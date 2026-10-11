#pragma once

#include "rsvp/lifecycle/reader_lifecycle.hpp"
#include "rsvp/platform/input_contract.hpp"
#include "rsvp/playback/playback_engine.hpp"
#include "rsvp/runtime/reader_runtime.hpp"
#include "rsvp/text/chapter.hpp"
#include "rsvp/text/display_frame.hpp"
#include "rsvp/ui/reader_view_model.hpp"

#include <cstdint>
#include <vector>

namespace kode_dot {

struct ReaderAppBoot {
    rsvp::lifecycle::ReaderStatePaths state_paths;
    rsvp::lifecycle::DocumentIdentity document;
    rsvp::playback::PlaybackSettings fallback_settings;
};

class ReaderApp {
public:
    explicit ReaderApp(rsvp::runtime::ReaderRuntime runtime);

    static ReaderApp restore(
        const std::vector<rsvp::text::DisplayFrame>& frames,
        const std::vector<rsvp::text::Chapter>& chapters,
        ReaderAppBoot boot,
        std::uint64_t monotonic_ms = 0);

    bool handle_control_event(const rsvp::platform::ControlEvent& event,
                              std::uint64_t updated_at);
    bool handle_lifecycle_event(const rsvp::platform::LifecycleEvent& event,
                                std::uint64_t updated_at);
    bool tick(std::uint64_t monotonic_ms);
    bool persist(std::uint64_t updated_at) const;

    rsvp::ui::ReaderViewModel view_model() const;
    rsvp::controller::ReaderView view() const;

private:
    rsvp::runtime::ReaderRuntime runtime_;
};

} // namespace kode_dot
