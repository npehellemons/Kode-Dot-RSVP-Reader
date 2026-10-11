#include "kode_dot/reader_document.hpp"

#include "rsvp/storage/state_store.hpp"
#include "rsvp/text/tokenizer.hpp"

namespace kode_dot {
namespace {

constexpr char kBundledSampleFilename[] = "Kode Dot - Bundled Sample.txt";
constexpr char kBundledSampleText[] =
    "Title: Bundled Sample\n"
    "Author: Kode Dot\n"
    "\n"
    "# Chapter One\n"
    "This bundled text proves the native reader boot path without file import.\n"
    "It includes comfortable pacing, context view, and supercalifragilisticexpialidocious.\n"
    "\n"
    "# Chapter Two\n"
    "The second chapter keeps progress and title-card behavior testable offline.\n";

} // namespace

ReaderDocumentBundle make_reader_document_bundle(
    std::string_view source,
    std::string_view filename,
    const rsvp::library::TxtImportOptions& import_options,
    const rsvp::text::DisplayFrameOptions& frame_options) {
    ReaderDocumentBundle bundle;
    bundle.document =
        rsvp::library::import_txt_document(source, filename, import_options);
    bundle.frames = rsvp::text::make_display_frames(
        rsvp::text::tokenize(bundle.document.reading_text_utf8),
        frame_options);
    bundle.chapters = rsvp::library::make_frame_chapters(
        bundle.document,
        bundle.frames);
    bundle.identity = rsvp::lifecycle::DocumentIdentity{
        bundle.document.title.empty() ? std::string(filename) : bundle.document.title,
        rsvp::storage::content_hash(bundle.document.reading_text_utf8),
    };
    return bundle;
}

const char* bundled_sample_text() {
    return kBundledSampleText;
}

std::string_view bundled_sample_filename() {
    return kBundledSampleFilename;
}

ReaderDocumentBundle make_bundled_sample_document() {
    return make_reader_document_bundle(kBundledSampleText, kBundledSampleFilename);
}

} // namespace kode_dot
