#include "kode_dot/reader_boot.hpp"

#include <utility>

namespace kode_dot {

ReaderAppBoot make_reader_app_boot(
    const ReaderStorageProvider& storage,
    ReaderHostBoot boot) {
    return ReaderAppBoot{
        storage.reader_state_paths(),
        std::move(boot.document),
        boot.fallback_settings,
    };
}

ReaderHost make_reader_host(
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    ReaderHostBoot boot,
    const ReaderStorageProvider& storage,
    ReaderClock& clock,
    ReaderRenderer& renderer) {
    auto app = ReaderApp::restore(
        frames,
        chapters,
        make_reader_app_boot(storage, std::move(boot)),
        clock.monotonic_ms());
    return ReaderHost(std::move(app), clock, renderer);
}

} // namespace kode_dot
