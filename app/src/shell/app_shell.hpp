#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

namespace ssnx
{
class SearchTab;
class LibraryTab;
class NowPlayingTab;
class SettingsTab;
} // namespace ssnx

namespace ssnx::shell
{

class PlayerIconView;

class AppShell : public brls::Box
{
  public:
    AppShell();
    ~AppShell() override;

    void SelectTab(int index);
    void FocusTab(int index) { SelectTab(index); }

    void onFocusGained() override;
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    void BuildHeader();
    void BuildMiniPlayer();
    void UpdateMiniPlayer();
    void UpdateAccentColors();

    // Top Header
    brls::Box* header_container_ = nullptr;
    brls::Rectangle* brand_dot_ = nullptr;
    brls::Label* brand_title_ = nullptr;
    brls::Label* brand_badge_ = nullptr;
    brls::Box* tabs_container_ = nullptr;
    brls::Box* chips_container_ = nullptr;
    brls::Box* lib_chip_box_ = nullptr;
    brls::Label* lib_chip_label_ = nullptr;

    struct TabItem
    {
        std::string name;
        brls::Box* box = nullptr;
        brls::Label* label = nullptr;
        brls::Rectangle* underline = nullptr;
        brls::View* content = nullptr;
    };
    std::vector<TabItem> tabs_;
    int active_tab_index_ = 0;

    // Content container
    brls::Box* content_container_ = nullptr;
    brls::View* active_content_ = nullptr;

    // Persistent Bottom Mini-Player
    brls::Box* mini_player_container_ = nullptr;
    brls::Image* mini_thumb_ = nullptr;
    brls::Label* mini_title_ = nullptr;
    brls::Label* mini_artist_ = nullptr;
    brls::Box* mini_rw_box_ = nullptr;
    PlayerIconView* mini_rw_icon_ = nullptr;
    brls::Box* mini_pp_box_ = nullptr;
    PlayerIconView* mini_pp_icon_ = nullptr;
    brls::Box* mini_ff_box_ = nullptr;
    PlayerIconView* mini_ff_icon_ = nullptr;
    brls::Box* mini_progress_bar_ = nullptr;
    brls::Box* mini_progress_fill_ = nullptr;
    brls::Label* mini_time_label_ = nullptr;

    std::string last_mini_vid_;
    std::string last_mini_title_;
    bool last_mini_paused_ = false;
    bool last_mini_playing_ = false;
    int last_lib_count_ = -1;
    uint32_t last_theme_idx_ = 999;

    // Retained tab instances
    SearchTab* search_tab_ = nullptr;
    LibraryTab* library_tab_ = nullptr;
    NowPlayingTab* nowplaying_tab_ = nullptr;
    SettingsTab* settings_tab_ = nullptr;
};

} // namespace ssnx::shell
