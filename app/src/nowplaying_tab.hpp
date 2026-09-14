#pragma once

#include "lyrics/lrclib.hpp"

#include <borealis.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ssnx::shell
{
class PlayerIconView;
}

namespace ssnx
{

class NowPlayingTab : public brls::Box
{
  public:
    NowPlayingTab();
    ~NowPlayingTab() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

  private:
    static std::string FormatTime(double seconds);
    void FetchLyricsAsync(const std::string& title, const std::string& artist,
                          const std::string& video_id, double duration);
    void PopulateLyricsView();
    void UpdateAccentColors();
    void PromptBrowseLyrics(const std::string& override_query = "");
    void ApplyLyricCandidate(const lyrics::LyricCandidate& candidate, const std::string& video_id);

    // Left column: Now Playing info & playback controls (~440px)
    brls::Image* art_ = nullptr;
    brls::Label* title_ = nullptr;
    brls::Label* artist_ = nullptr;
    brls::Label* status_ = nullptr;

    // Controls
    brls::Box* controls_row_ = nullptr;
    brls::Box* rw_box_ = nullptr;
    shell::PlayerIconView* rw_icon_ = nullptr;
    brls::Box* play_box_ = nullptr;
    shell::PlayerIconView* play_icon_ = nullptr;
    brls::Box* ff_box_ = nullptr;
    shell::PlayerIconView* ff_icon_ = nullptr;

    // Seek bar & timestamps
    brls::Box* seek_bar_ = nullptr;
    brls::Box* seek_fill_ = nullptr;
    brls::Label* time_cur_label_ = nullptr;
    brls::Label* time_dur_label_ = nullptr;

    // LRC sync offset
    brls::Label* lrc_offset_val_ = nullptr;
    double lrc_offset_ = 0.0;

    // Right column: Lyrics scroll view
    brls::Header* lyrics_header_ = nullptr;
    brls::Box* browse_lrc_btn_ = nullptr;
    brls::Label* browse_lrc_lbl_ = nullptr;
    brls::ScrollingFrame* lyrics_scroll_ = nullptr;
    brls::Box* lyrics_box_ = nullptr;
    brls::Label* lyrics_status_label_ = nullptr;
    std::vector<brls::Label*> lyric_labels_;

    std::string last_key_;
    std::string current_lyrics_vid_;

    std::mutex lyrics_mutex_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    std::atomic<uint64_t> lyrics_gen_ {0};
    lyrics::LyricsResult lyrics_result_;
    bool lyrics_need_populate_ = false;
    int current_active_line_ = -1;
    uint32_t last_theme_idx_ = 999;
};

} // namespace ssnx
