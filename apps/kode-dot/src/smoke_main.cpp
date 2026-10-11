#include "kode_dot/reader_boot.hpp"
#include "kode_dot/reader_document.hpp"
#include "kode_dot/reader_host.hpp"
#include "kode_dot/reader_screen.hpp"
#include "kode_dot/reader_storage.hpp"
#include "rsvp/platform/input_contract.hpp"
#include "rsvp/playback/playback_engine.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

class SmokeClock : public kode_dot::ReaderClock {
public:
    std::uint64_t monotonic = 0;
    std::uint64_t state_timestamp = 0;

    std::uint64_t monotonic_ms() const override {
        return monotonic;
    }

    std::uint64_t state_timestamp_ms() const override {
        return state_timestamp;
    }
};

std::string_view mode_name(kode_dot::ReaderScreenMode mode) {
    switch (mode) {
    case kode_dot::ReaderScreenMode::title:
        return "title";
    case kode_dot::ReaderScreenMode::rsvp:
        return "rsvp";
    case kode_dot::ReaderScreenMode::context:
        return "context";
    case kode_dot::ReaderScreenMode::sleep:
        return "sleep";
    }
    return "rsvp";
}

class SmokeRenderer : public kode_dot::ReaderRenderer {
public:
    void render(const kode_dot::ReaderScreenFrame& frame) override {
        std::cout << "mode=" << mode_name(frame.mode)
                  << " text=\"" << frame.primary_text << "\""
                  << " chapter=" << frame.chapter_label
                  << " pos=" << frame.position_label
                  << " wpm=" << frame.wpm_label
                  << " remaining=" << frame.remaining_label
                  << " progress=" << frame.progress_percent
                  << " playing=" << (frame.playing ? "yes" : "no")
                  << " sleeping=" << (frame.sleeping ? "yes" : "no")
                  << '\n';
    }
};

} // namespace

int main() {
    const auto bundle = kode_dot::make_bundled_sample_document();
    kode_dot::DirectoryReaderStorageProvider storage(
        std::filesystem::temp_directory_path() / "kode-dot-smoke-state");

    SmokeClock clock;
    SmokeRenderer renderer;
    kode_dot::ReaderHostBoot boot;
    boot.document = bundle.identity;
    boot.fallback_settings = rsvp::playback::PlaybackSettings{.wpm = 275};

    auto host = kode_dot::make_reader_host(
        bundle.frames,
        bundle.chapters,
        boot,
        storage,
        clock,
        renderer);

    host.render();

    clock.monotonic = 1;
    clock.state_timestamp = 1;
    host.dispatch_control(rsvp::platform::PhysicalControl::red_button,
                          rsvp::platform::ControlPhase::release);

    clock.monotonic = 400;
    clock.state_timestamp = 400;
    host.tick();

    host.dispatch_lifecycle(rsvp::platform::LifecycleSignal::sleep);
    return 0;
}
