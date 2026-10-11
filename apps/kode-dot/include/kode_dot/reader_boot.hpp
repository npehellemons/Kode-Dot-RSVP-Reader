#pragma once

#include "kode_dot/reader_app.hpp"
#include "kode_dot/reader_host.hpp"
#include "kode_dot/reader_storage.hpp"
#include "rsvp/lifecycle/reader_lifecycle.hpp"
#include "rsvp/playback/playback_engine.hpp"
#include "rsvp/text/chapter.hpp"
#include "rsvp/text/display_frame.hpp"

#include <vector>

namespace kode_dot {

struct ReaderHostBoot {
    rsvp::lifecycle::DocumentIdentity document;
    rsvp::playback::PlaybackSettings fallback_settings;
};

ReaderAppBoot make_reader_app_boot(
    const ReaderStorageProvider& storage,
    ReaderHostBoot boot);

ReaderHost make_reader_host(
    const std::vector<rsvp::text::DisplayFrame>& frames,
    const std::vector<rsvp::text::Chapter>& chapters,
    ReaderHostBoot boot,
    const ReaderStorageProvider& storage,
    ReaderClock& clock,
    ReaderRenderer& renderer);

} // namespace kode_dot
