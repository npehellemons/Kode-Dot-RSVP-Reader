#pragma once

#include "rsvp/library/book_catalog.hpp"
#include "rsvp/storage/state_store.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rsvp::ui {

enum class MainMenuItem {
    library,
    current_book,
    settings,
    device_status,
};

struct MainMenuEntry {
    MainMenuItem item = MainMenuItem::library;
    std::string label;
    std::string value;
    bool enabled = true;
};

struct MainMenuModel {
    std::vector<MainMenuEntry> entries;
    std::size_t selected_index = 0;
};

enum class SettingsMenuItem {
    wpm,
    font_scale,
    fixation,
    punctuation,
};

struct SettingsMenuEntry {
    SettingsMenuItem item = SettingsMenuItem::wpm;
    std::string label;
    std::string value;
    bool adjustable = true;
};

struct SettingsMenuModel {
    std::vector<SettingsMenuEntry> entries;
    std::size_t selected_index = 0;
};

MainMenuModel make_main_menu_model(
    const rsvp::library::LibraryCatalog& catalog,
    const rsvp::library::BookRecord* current_book,
    bool storage_ready,
    std::size_t selected_index = 0);

SettingsMenuModel make_settings_menu_model(
    const rsvp::storage::SettingsV1& settings,
    std::size_t selected_index = 0);

} // namespace rsvp::ui
