#include "rsvp/ui/menu_model.hpp"

#include <algorithm>
#include <string>

namespace rsvp::ui {
namespace {

std::string fixation_label(rsvp::storage::FixationMode mode) {
    switch (mode) {
    case rsvp::storage::FixationMode::center:
        return "Center";
    case rsvp::storage::FixationMode::orp:
        return "ORP";
    }
    return "Center";
}

std::string punctuation_label(rsvp::storage::PunctuationMode mode) {
    switch (mode) {
    case rsvp::storage::PunctuationMode::off:
        return "Off";
    case rsvp::storage::PunctuationMode::gentle:
        return "Gentle";
    }
    return "Gentle";
}

std::size_t clamp_selection(std::size_t selected_index, std::size_t count) {
    if (count == 0) {
        return 0;
    }
    return std::min(selected_index, count - 1);
}

} // namespace

MainMenuModel make_main_menu_model(
    const rsvp::library::LibraryCatalog& catalog,
    const rsvp::library::BookRecord* current_book,
    bool storage_ready,
    std::size_t selected_index) {
    MainMenuModel model;
    model.entries.push_back(
        {MainMenuItem::library,
         "Library",
         std::to_string(catalog.books.size()) + " books",
         !catalog.books.empty()});
    model.entries.push_back(
        {MainMenuItem::current_book,
         "Current book",
         current_book == nullptr ? "None" : current_book->title,
         current_book != nullptr});
    model.entries.push_back(
        {MainMenuItem::settings,
         "Settings",
         "Reader",
         true});
    model.entries.push_back(
        {MainMenuItem::device_status,
         "Device",
         storage_ready ? "Storage ready" : "Storage unavailable",
         true});
    model.selected_index = clamp_selection(selected_index, model.entries.size());
    return model;
}

SettingsMenuModel make_settings_menu_model(
    const rsvp::storage::SettingsV1& settings,
    std::size_t selected_index) {
    SettingsMenuModel model;
    model.entries.push_back(
        {SettingsMenuItem::wpm,
         "Speed",
         std::to_string(settings.wpm) + " WPM",
         true});
    model.entries.push_back(
        {SettingsMenuItem::font_scale,
         "Font",
         std::to_string(settings.font_scale) + "%",
         true});
    model.entries.push_back(
        {SettingsMenuItem::fixation,
         "Fixation",
         fixation_label(settings.fixation_mode),
         true});
    model.entries.push_back(
        {SettingsMenuItem::punctuation,
         "Punctuation",
         punctuation_label(settings.punctuation_mode),
         true});
    model.selected_index = clamp_selection(selected_index, model.entries.size());
    return model;
}

} // namespace rsvp::ui
