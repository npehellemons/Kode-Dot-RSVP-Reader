#pragma once

#include "kode_dot/reader_app.hpp"
#include "rsvp/platform/input_contract.hpp"
#include "rsvp/ui/reader_view_model.hpp"

#include <cstdint>

namespace kode_dot {

class ReaderClock {
public:
    virtual ~ReaderClock() = default;
    virtual std::uint64_t monotonic_ms() const = 0;
    virtual std::uint64_t state_timestamp_ms() const = 0;
};

class ReaderRenderer {
public:
    virtual ~ReaderRenderer() = default;
    virtual void render(const rsvp::ui::ReaderViewModel& model) = 0;
};

class ReaderHost {
public:
    ReaderHost(ReaderApp app, ReaderClock& clock, ReaderRenderer& renderer);

    void render();
    bool tick();
    bool dispatch_control(rsvp::platform::PhysicalControl control,
                          rsvp::platform::ControlPhase phase);
    bool dispatch_lifecycle(rsvp::platform::LifecycleSignal signal);
    bool persist() const;

    const ReaderApp& app() const;
    ReaderApp& app();

private:
    bool render_if_handled(bool handled);

    ReaderApp app_;
    ReaderClock& clock_;
    ReaderRenderer& renderer_;
};

} // namespace kode_dot
