// SPDX-License-Identifier: GPL-3.0-or-later
// What the launcher screens ask of the app, answered from the console: the games folder,
// the settings file, Eden's metadata reader.
#pragma once

#include "pe/ui/services.hpp"

#include <mutex>
#include <string>
#include <vector>

class EdenServices final : public pe::ui::Services {
public:
    // launch_error: why the game chosen last time did not start (empty: nothing to report).
    explicit EdenServices(std::string launch_error);

    pe::ui::Home home() override;
    std::string clock() override;
    unsigned controllers() override;
    std::string version() override;

    std::vector<pe::ui::Game> games() override;
    std::string game_path(const std::string& file) override;
    bool game_exists(const std::string& file) override;
    bool take_update(pe::ui::UpdateOffer* offer) override;
    bool start_update() override;
    pe::ui::UpdateStatus update_status() override;
    void cancel_update() override;
    bool apply_update() override;
    void finish_update() override;
    bool docked(std::uint64_t title_id) override;
    bool set_docked(std::uint64_t title_id, bool docked) override;
    pe::ui::GameSettings game_settings(std::uint64_t title_id) override;
    bool set_game_settings(std::uint64_t title_id, const pe::ui::GameSettings& settings) override;

    pe::ui::Preferences preferences() override;
    bool set_preferences(const pe::ui::Preferences& preferences) override;
    const std::vector<std::string>& resolution_labels() override;
    const std::vector<std::string>& resolution_keys() override;
    const std::vector<std::string>& filter_labels() override;
    const std::vector<std::string>& language_labels() override;
    std::string language_region(int language) override;
    std::string setup_details() override;

    bool folders(const std::string& directory, std::vector<std::string>* names) override;
    pe::ui::FolderInfo folder_info(const std::string& directory) override;
    std::string files_folder() override;
    std::string saved_files_folder() override;
    std::string default_files_folder() override;
    bool set_files_folder(const std::string& directory) override;
    int filesystem_access() override;

    std::vector<pe::ui::Profile> profiles() override;
    bool choose_profile(int index) override;
    int add_profile() override;
    bool rename_profile(int index, int step) override;
    bool remove_profile(int index) override;

    bool save_transfer_available() override;
    pe::ui::SaveSource save_import_source(std::uint64_t title_id) override;
    bool save_import(std::uint64_t title_id, std::string* message) override;
    bool save_export(std::uint64_t title_id, std::string* message) override;

    std::vector<pe::ui::Mod> mods(std::uint64_t title_id) override;
    bool set_mod_enabled(std::uint64_t title_id, const std::string& name, bool enabled) override;
    bool set_cheat_enabled(std::uint64_t title_id, const std::string& mod, const std::string& cheat,
                           bool enabled) override;
    bool mods_enabled(std::uint64_t title_id) override;
    bool set_mods_enabled(std::uint64_t title_id, bool enabled) override;
    std::string mods_folder(std::uint64_t title_id) override;
    bool make_mods_folder(std::uint64_t title_id) override;

    bool load_image(const std::string& path, pe::gfx::Image* image) override;

private:
    int user_ = -1; // the PS5 user in front when the menu opened (negative: unknown)
    std::string launch_error_;
    std::string setup_; // what is missing from keys and firmware; empty when ready
    std::mutex bridge_; // Eden's metadata reader keeps state between calls: one caller at a time
};
