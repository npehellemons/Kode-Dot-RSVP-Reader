#include "kode_dot/reader_app.hpp"

#include <utility>

namespace kode_dot {

ReaderApp::ReaderApp(rsvp::runtime::ReaderRuntime runtime)
    : runtime_(std::move(runtime)) {}

ReaderApp ReaderApp::restore(
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    ReaderAppBoot boot,
    std::uint64_t monotonic_ms) {
    return ReaderApp(rsvp::runtime::ReaderRuntime::restore(
        frames,
        chapters,
        std::move(boot.state_paths),
        std::move(boot.document),
        boot.fallback_settings,
        monotonic_ms));
}

bool ReaderApp::handle_control_event(const rsvp::platform::ControlEvent& event,
                                     std::uint64_t updated_at) {
    return runtime_.handle_control_event(event, updated_at);
}

bool ReaderApp::handle_lifecycle_event(const rsvp::platform::LifecycleEvent& event,
                                       std::uint64_t updated_at) {
    return runtime_.handle_lifecycle_event(event, updated_at);
}

bool ReaderApp::tick(std::uint64_t monotonic_ms) {
    return runtime_.tick(monotonic_ms);
}

bool ReaderApp::persist(std::uint64_t updated_at) const {
    return runtime_.persist(updated_at);
}

rsvp::ui::ReaderViewModel ReaderApp::view_model() const {
    return rsvp::ui::make_reader_view_model(runtime_.view());
}

rsvp::controller::ReaderView ReaderApp::view() const {
    return runtime_.view();
}

} // namespace kode_dot
