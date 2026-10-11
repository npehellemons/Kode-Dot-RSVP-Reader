#pragma once

#include "rsvp/lifecycle/reader_lifecycle.hpp"
#include "rsvp/library/document_text.hpp"
#include "rsvp/text/chapter.hpp"
#include "rsvp/text/display_frame.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace kode_dot {

struct ReaderDocumentBundle {
    rsvp::library::DocumentText document;
    std::vector<rsvp::text::DisplayFrame> frames;
    std::vector<rsvp::text::Chapter> chapters;
    rsvp::lifecycle::DocumentIdentity identity;
};

ReaderDocumentBundle make_reader_document_bundle(
    std::string_view source,
    std::string_view filename,
    const rsvp::library::TxtImportOptions& import_options = {},
    const rsvp::text::DisplayFrameOptions& frame_options = {});

const char* bundled_sample_text();
std::string_view bundled_sample_filename();
ReaderDocumentBundle make_bundled_sample_document();

} // namespace kode_dot
