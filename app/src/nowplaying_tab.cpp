#include "nowplaying_tab.hpp"

#include "audio/now_playing.hpp"
#include "audio/player.hpp"
#include "audio/queue_manager.hpp"
#include "settings/settings_store.hpp"
#include "shell/player_icon_view.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ssnx
{

NowPlayingTab::NowPlayingTab()
    : brls::Box(brls::Axis::ROW)
{
    setPadding(16, 28, 16, 28);
    setBackgroundColor(nvgRGB(12, 13, 16));

    const auto accent = settings::SettingsStore::Instance().GetAccentColor();

    // Left column: Now Playing info & playback controls (~440px)
    auto* left_panel = new brls::Box(brls::Axis::COLUMN);
    left_panel->setWidth(440);
    left_panel->setMarginRight(28);
    left_panel->setAlignItems(brls::AlignItems::CENTER);
    addView(left_panel);

    art_ = new brls::Image();
    art_->setWidth(220);
    art_->setHeight(220);
    art_->setCornerRadius(14);
    art_->setScalingType(brls::ImageScalingType::FILL);
    art_->setMarginTop(8);
    art_->setMarginBottom(12);
    left_panel->addView(art_);

    title_ = new brls::Label();
    title_->setText("Nothing playing yet");
    title_->setFontSize(22);
    title_->setTextColor(nvgRGB(245, 248, 250));
    title_->setSingleLine(true);
    title_->setMarginBottom(4);
    left_panel->addView(title_);

    artist_ = new brls::Label();
    artist_->setText("Search for a track and press A");
    artist_->setFontSize(16);
    artist_->setTextColor(accent);
    artist_->setSingleLine(true);
    artist_->setMarginBottom(4);
    left_panel->addView(artist_);

    status_ = new brls::Label();
    status_->setText("");
    status_->setFontSize(12);
    status_->setTextColor(nvgRGB(151, 159, 170));
    status_->setSingleLine(true);
    status_->setMarginBottom(12);
    left_panel->addView(status_);

    // Media Controls Row (Rewind, Play/Pause, Fast-Forward)
    controls_row_ = new brls::Box(brls::Axis::ROW);
    controls_row_->setAlignItems(brls::AlignItems::CENTER);
    controls_row_->setJustifyContent(brls::JustifyContent::CENTER);
    controls_row_->setMarginBottom(12);

    rw_box_ = new brls::Box(brls::Axis::ROW);
    rw_box_->setWidth(42);
    rw_box_->setHeight(42);
    rw_box_->setCornerRadius(21);
    rw_box_->setAlignItems(brls::AlignItems::CENTER);
    rw_box_->setJustifyContent(brls::JustifyContent::CENTER);
    rw_box_->setBackgroundColor(nvgRGB(35, 35, 44));
    rw_box_->setFocusable(true);
    rw_icon_ = new shell::PlayerIconView(shell::PlayerIconType::Prev, nvgRGB(240, 240, 245), 16.0f);
    rw_box_->addView(rw_icon_);
    rw_box_->registerClickAction([](brls::View*) {
        audio::QueueManager::Instance().Previous();
        return true;
    });
    controls_row_->addView(rw_box_);

    play_box_ = new brls::Box(brls::Axis::ROW);
    play_box_->setWidth(50);
    play_box_->setHeight(50);
    play_box_->setCornerRadius(25);
    play_box_->setMarginLeft(14);
    play_box_->setMarginRight(14);
    play_box_->setAlignItems(brls::AlignItems::CENTER);
    play_box_->setJustifyContent(brls::JustifyContent::CENTER);
    play_box_->setBackgroundColor(accent);
    play_box_->setFocusable(true);

    play_icon_ = new shell::PlayerIconView(shell::PlayerIconType::Play, nvgRGB(10, 26, 51), 18.0f);
    play_box_->addView(play_icon_);

    play_box_->registerClickAction([](brls::View*) {
        auto& player = audio::Player::Instance();
        if (player.IsPlaying())
            player.SetPaused(!player.IsPaused());
        return true;
    });
    controls_row_->addView(play_box_);

    ff_box_ = new brls::Box(brls::Axis::ROW);
    ff_box_->setWidth(42);
    ff_box_->setHeight(42);
    ff_box_->setCornerRadius(21);
    ff_box_->setAlignItems(brls::AlignItems::CENTER);
    ff_box_->setJustifyContent(brls::JustifyContent::CENTER);
    ff_box_->setBackgroundColor(nvgRGB(35, 35, 44));
    ff_box_->setFocusable(true);
    ff_icon_ = new shell::PlayerIconView(shell::PlayerIconType::Next, nvgRGB(240, 240, 245), 16.0f);
    ff_box_->addView(ff_icon_);
    ff_box_->registerClickAction([](brls::View*) {
        audio::QueueManager::Instance().Next();
        return true;
    });
    controls_row_->addView(ff_box_);

    // Register ZL / ZR seeking (Borealis LT / RT)
    registerAction("Seek -3s", brls::BUTTON_LT, [](brls::View*) {
        audio::Player::Instance().SeekRelative(-3.0);
        return true;
    });
    registerAction("Seek +3s", brls::BUTTON_RT, [](brls::View*) {
        audio::Player::Instance().SeekRelative(3.0);
        return true;
    });

    left_panel->addView(controls_row_);

    // Seek bar
    seek_bar_ = new brls::Box(brls::Axis::ROW);
    seek_bar_->setWidth(340);
    seek_bar_->setHeight(8);
    seek_bar_->setCornerRadius(4);
    seek_bar_->setBackgroundColor(nvgRGB(38, 42, 48));

    seek_fill_ = new brls::Box();
    seek_fill_->setWidth(0);
    seek_fill_->setHeight(8);
    seek_fill_->setCornerRadius(4);
    seek_fill_->setBackgroundColor(accent);
    seek_bar_->addView(seek_fill_);
    left_panel->addView(seek_bar_);

    // Timestamps
    auto* times_box = new brls::Box(brls::Axis::ROW);
    times_box->setWidth(340);
    times_box->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    times_box->setMarginTop(4);

    time_cur_label_ = new brls::Label();
    time_cur_label_->setText("0:00");
    time_cur_label_->setFontSize(12);
    time_cur_label_->setTextColor(nvgRGB(151, 159, 170));
    times_box->addView(time_cur_label_);

    time_dur_label_ = new brls::Label();
    time_dur_label_->setText("0:00");
    time_dur_label_->setFontSize(12);
    time_dur_label_->setTextColor(nvgRGB(151, 159, 170));
    times_box->addView(time_dur_label_);
    left_panel->addView(times_box);

    // LRC Offset Adjuster Row (-0.5s / +0.5s)
    auto* lrc_offset_row = new brls::Box(brls::Axis::ROW);
    lrc_offset_row->setAlignItems(brls::AlignItems::CENTER);
    lrc_offset_row->setMarginTop(10);

    auto* lrc_label = new brls::Label();
    lrc_label->setText("LRC Sync: ");
    lrc_label->setFontSize(12);
    lrc_label->setTextColor(nvgRGB(151, 159, 170));
    lrc_offset_row->addView(lrc_label);

    auto* minus_btn = new brls::Box(brls::Axis::ROW);
    minus_btn->setPadding(4, 10, 4, 10);
    minus_btn->setCornerRadius(6);
    minus_btn->setBackgroundColor(nvgRGB(35, 35, 44));
    minus_btn->setFocusable(true);
    auto* minus_label = new brls::Label();
    minus_label->setText("-0.5s");
    minus_label->setFontSize(12);
    minus_label->setTextColor(nvgRGB(220, 220, 225));
    minus_btn->addView(minus_label);
    minus_btn->registerClickAction([this](brls::View*) {
        lrc_offset_ -= 0.5;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%.1fs", lrc_offset_ >= 0.0 ? "+" : "", lrc_offset_);
        lrc_offset_val_->setText(buf);
        if (!current_lyrics_vid_.empty())
            settings::SettingsStore::Instance().SetLrcOffset(current_lyrics_vid_, lrc_offset_);
        return true;
    });
    lrc_offset_row->addView(minus_btn);

    lrc_offset_val_ = new brls::Label();
    lrc_offset_val_->setText("+0.0s");
    lrc_offset_val_->setFontSize(12);
    lrc_offset_val_->setTextColor(accent);
    lrc_offset_val_->setMarginLeft(8);
    lrc_offset_val_->setMarginRight(8);
    lrc_offset_row->addView(lrc_offset_val_);

    auto* plus_btn = new brls::Box(brls::Axis::ROW);
    plus_btn->setPadding(4, 10, 4, 10);
    plus_btn->setCornerRadius(6);
    plus_btn->setBackgroundColor(nvgRGB(35, 35, 44));
    plus_btn->setFocusable(true);
    auto* plus_label = new brls::Label();
    plus_label->setText("+0.5s");
    plus_label->setFontSize(12);
    plus_label->setTextColor(nvgRGB(220, 220, 225));
    plus_btn->addView(plus_label);
    plus_btn->registerClickAction([this](brls::View*) {
        lrc_offset_ += 0.5;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s%.1fs", lrc_offset_ >= 0.0 ? "+" : "", lrc_offset_);
        lrc_offset_val_->setText(buf);
        if (!current_lyrics_vid_.empty())
            settings::SettingsStore::Instance().SetLrcOffset(current_lyrics_vid_, lrc_offset_);
        return true;
    });
    lrc_offset_row->addView(plus_btn);

    left_panel->addView(lrc_offset_row);

    // Right column: Lyrics scroll view
    auto* right_panel = new brls::Box(brls::Axis::COLUMN);
    right_panel->setGrow(1.0f);
    addView(right_panel);

    auto* lyrics_header_row = new brls::Box(brls::Axis::ROW);
    lyrics_header_row->setAlignItems(brls::AlignItems::CENTER);
    lyrics_header_row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    lyrics_header_row->setMarginBottom(4);

    lyrics_header_ = new brls::Header();
    lyrics_header_->setTitle("Lyrics");
    lyrics_header_row->addView(lyrics_header_);

    browse_lrc_btn_ = new brls::Box(brls::Axis::ROW);
    browse_lrc_btn_->setPadding(6, 12, 6, 12);
    browse_lrc_btn_->setCornerRadius(8);
    browse_lrc_btn_->setBackgroundColor(nvgRGB(35, 35, 44));
    browse_lrc_btn_->setFocusable(true);
    browse_lrc_lbl_ = new brls::Label();
    browse_lrc_lbl_->setText("Browse LRC (Y)");
    browse_lrc_lbl_->setFontSize(13);
    browse_lrc_lbl_->setTextColor(accent);
    browse_lrc_btn_->addView(browse_lrc_lbl_);
    browse_lrc_btn_->registerClickAction([this](brls::View*) {
        PromptBrowseLyrics();
        return true;
    });
    lyrics_header_row->addView(browse_lrc_btn_);
    right_panel->addView(lyrics_header_row);

    registerAction("Browse LRC", brls::BUTTON_Y, [this](brls::View*) {
        PromptBrowseLyrics();
        return true;
    });

    lyrics_scroll_ = new brls::ScrollingFrame();
    lyrics_scroll_->setGrow(1.0f);
    lyrics_scroll_->setMarginTop(6);
    right_panel->addView(lyrics_scroll_);

    lyrics_box_ = new brls::Box(brls::Axis::COLUMN);
    lyrics_box_->setPadding(16, 16, 360, 16);
    lyrics_scroll_->setContentView(lyrics_box_);

    lyrics_status_label_ = new brls::Label();
    lyrics_status_label_->setText("Play a track to view lyrics…");
    lyrics_status_label_->setFontSize(17);
    lyrics_status_label_->setTextColor(nvgRGB(120, 128, 140));
    lyrics_box_->addView(lyrics_status_label_);
}

NowPlayingTab::~NowPlayingTab()
{
    alive_->store(false);
    lyrics_gen_++;
}

std::string NowPlayingTab::FormatTime(double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0.0)
        return "--:--";
    const int total = static_cast<int>(seconds);
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", total / 60, total % 60);
    return buffer;
}

void NowPlayingTab::FetchLyricsAsync(const std::string& title, const std::string& artist,
                                    const std::string& video_id, double duration)
{
    const uint64_t gen = ++lyrics_gen_;

    lyrics_box_->clearViews();
    lyric_labels_.clear();
    current_active_line_ = -1;

    // Check if local sidecar exists
    const std::string local_lrc = "sdmc:/switch/StreamSnagNX/music/" + video_id + ".lrc";
    FILE* fp = fopen(local_lrc.c_str(), "r");
    if (fp)
    {
        std::string content;
        char buf[256];
        while (fgets(buf, sizeof(buf), fp))
            content += buf;
        fclose(fp);

        if (!content.empty())
        {
            lyrics::LyricsResult res;
            res.synced = lyrics::LrclibClient::ParseLRC(content);
            res.is_synced = !res.synced.empty();
            if (!res.is_synced)
            {
                std::stringstream ss(content);
                std::string line;
                while (std::getline(ss, line))
                    res.plain.push_back(line);
                res.is_plain = !res.plain.empty();
            }
            lyrics_result_ = res;
            current_lyrics_vid_ = video_id;
            PopulateLyricsView();
            return;
        }
    }

    lyrics_status_label_ = new brls::Label();
    lyrics_status_label_->setText("Fetching lyrics from LRCLIB…");
    lyrics_status_label_->setFontSize(17);
    lyrics_status_label_->setTextColor(nvgRGB(120, 128, 140));
    lyrics_box_->addView(lyrics_status_label_);
    lyrics_scroll_->setContentOffsetY(0.0f, false);

    auto alive = alive_;
    brls::async([this, alive, title, artist, video_id, duration, gen]() {
        const auto res = lyrics::LrclibClient::FetchLyrics(title, artist, duration);
        if (!alive->load() || lyrics_gen_.load() != gen)
            return;

        std::lock_guard<std::mutex> lock(lyrics_mutex_);
        lyrics_result_ = res;
        current_lyrics_vid_ = video_id;
        lyrics_need_populate_ = true;
    });
}

void NowPlayingTab::PopulateLyricsView()
{
    lyrics_box_->clearViews();
    lyric_labels_.clear();
    current_active_line_ = -1;

    if (lyrics_result_.is_instrumental)
    {
        lyrics_status_label_ = new brls::Label();
        lyrics_status_label_->setText("Instrumental");
        lyrics_status_label_->setFontSize(20);
        lyrics_status_label_->setTextColor(nvgRGB(77, 159, 255));
        lyrics_box_->addView(lyrics_status_label_);
    }
    else if (lyrics_result_.is_synced)
    {
        for (size_t i = 0; i < lyrics_result_.synced.size(); ++i)
        {
            auto* label = new brls::Label();
            label->setText(lyrics_result_.synced[i].text);
            label->setFontSize(17);
            label->setTextColor(nvgRGB(100, 108, 120));
            label->setMarginBottom(10);
            lyrics_box_->addView(label);
            lyric_labels_.push_back(label);
        }
    }
    else if (lyrics_result_.is_plain)
    {
        for (const auto& line : lyrics_result_.plain)
        {
            auto* label = new brls::Label();
            label->setText(line.empty() ? " " : line);
            label->setFontSize(16);
            label->setTextColor(nvgRGB(160, 168, 180));
            label->setMarginBottom(8);
            lyrics_box_->addView(label);
            lyric_labels_.push_back(label);
        }
    }
    else
    {
        lyrics_status_label_ = new brls::Label();
        lyrics_status_label_->setText(lyrics_result_.error.empty() ? "No lyrics found"
                                                                  : lyrics_result_.error);
        lyrics_status_label_->setFontSize(16);
        lyrics_status_label_->setTextColor(nvgRGB(120, 128, 140));
        lyrics_box_->addView(lyrics_status_label_);
    }

    lyrics_scroll_->setContentOffsetY(0.0f, false);
}

void NowPlayingTab::UpdateAccentColors()
{
    const auto& settings = settings::SettingsStore::Instance();
    const uint32_t current_theme = static_cast<uint32_t>(settings.GetTheme());
    if (current_theme == last_theme_idx_)
        return;
    last_theme_idx_ = current_theme;

    const NVGcolor accent = settings.GetAccentColor();
    artist_->setTextColor(accent);
    play_box_->setBackgroundColor(accent);
    seek_fill_->setBackgroundColor(accent);
    lrc_offset_val_->setTextColor(accent);
    if (browse_lrc_lbl_)
        browse_lrc_lbl_->setTextColor(accent);
}

void NowPlayingTab::PromptBrowseLyrics(const std::string& override_query)
{
    const audio::NowPlayingInfo info = audio::NowPlaying::Instance().Get();
    if (info.track.video_id.empty())
        return;

    std::string query = override_query;
    if (query.empty())
    {
        const std::string clean_title = lyrics::LrclibClient::CleanTitle(info.track.title);
        const std::string clean_artist = lyrics::LrclibClient::CleanArtist(info.track.artist);
        query = clean_title;
        if (!clean_artist.empty())
            query += " " + clean_artist;
    }

    auto alive = alive_;
    brls::async([this, alive, query, track = info.track]() {
        auto candidates = lyrics::LrclibClient::SearchCandidates(query);
        brls::sync([this, alive, candidates, query, track]() {
            if (!alive->load())
                return;

            const NVGcolor accent = settings::SettingsStore::Instance().GetAccentColor();

            auto* content = new brls::Box(brls::Axis::COLUMN);
            content->setWidth(720);
            content->setPadding(20, 24, 20, 24);

            // 1. Top message header
            auto* title_lbl = new brls::Label();
            title_lbl->setText("Select Lyrics Match");
            title_lbl->setFontSize(20);
            title_lbl->setTextColor(nvgRGB(240, 240, 245));
            title_lbl->setMarginBottom(4);
            content->addView(title_lbl);

            auto* sub_lbl = new brls::Label();
            char sub_buf[256];
            snprintf(sub_buf, sizeof(sub_buf), "Search: \"%s\"  •  %zu match%s",
                     query.c_str(), candidates.size(), candidates.size() == 1 ? "" : "es");
            sub_lbl->setText(sub_buf);
            sub_lbl->setFontSize(13);
            sub_lbl->setTextColor(nvgRGB(140, 148, 160));
            sub_lbl->setMarginBottom(14);
            content->addView(sub_lbl);

            // Dialog
            brls::Dialog* dialog = new brls::Dialog(content);

            // Direct B button dismissal callbacks registered on all layers
            auto dismiss_cb = [dialog](brls::View*) {
                dialog->dismiss();
                return true;
            };

            // 2. Candidate list in a ScrollingFrame (only if candidates exist)
            brls::Box* first_row = nullptr;
            if (candidates.empty())
            {
                auto* empty_lbl = new brls::Label();
                empty_lbl->setText("No lyrics matches found for this query.\nTry Custom Search below, or press B to return.");
                empty_lbl->setFontSize(15);
                empty_lbl->setTextColor(nvgRGB(155, 162, 175));
                empty_lbl->setMarginTop(8);
                empty_lbl->setMarginBottom(18);
                empty_lbl->setHorizontalAlign(brls::HorizontalAlign::CENTER);
                content->addView(empty_lbl);
            }
            else
            {
                auto* scroll = new brls::ScrollingFrame();
                const float list_h = std::min(320.0f, static_cast<float>(candidates.size() * 64));
                scroll->setHeight(list_h);
                scroll->setWidth(672);
                scroll->setMarginBottom(12);

                auto* list_box = new brls::Box(brls::Axis::COLUMN);
                list_box->setWidth(672);
                scroll->setContentView(list_box);
                content->addView(scroll);

                for (size_t i = 0; i < candidates.size(); ++i)
                {
                    const auto& c = candidates[i];
                    auto* row = new brls::Box(brls::Axis::ROW);
                    row->setFocusable(true);
                    row->setHeight(56);
                    row->setWidth(672);
                    row->setCornerRadius(8);
                    row->setBackgroundColor(nvgRGB(26, 26, 32));
                    row->setMarginBottom(8);
                    row->setPadding(0, 14, 0, 14);
                    row->setAlignItems(brls::AlignItems::CENTER);
                    row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

                    auto* meta = new brls::Box(brls::Axis::COLUMN);
                    meta->setGrow(1.0f);
                    meta->setMarginRight(12);

                    auto* t_lbl = new brls::Label();
                    t_lbl->setText(c.track_name);
                    t_lbl->setFontSize(16);
                    t_lbl->setTextColor(nvgRGB(240, 240, 245));
                    meta->addView(t_lbl);

                    auto* a_lbl = new brls::Label();
                    std::string artist_str = c.artist_name;
                    if (!c.album_name.empty())
                        artist_str += " • " + c.album_name;
                    a_lbl->setText(artist_str);
                    a_lbl->setFontSize(12);
                    a_lbl->setTextColor(nvgRGB(145, 153, 165));
                    meta->addView(a_lbl);

                    row->addView(meta);

                    auto* badge = new brls::Label();
                    badge->setFontSize(13);
                    if (c.is_synced)
                    {
                        badge->setText("[Synced]");
                        badge->setTextColor(accent);
                    }
                    else if (c.is_plain)
                    {
                        badge->setText("[Plain]");
                        badge->setTextColor(nvgRGB(180, 190, 205));
                    }
                    else if (c.is_instrumental)
                    {
                        badge->setText("[Instrumental]");
                        badge->setTextColor(nvgRGB(130, 140, 155));
                    }
                    else
                    {
                        badge->setText("[LRC]");
                        badge->setTextColor(nvgRGB(140, 150, 160));
                    }
                    row->addView(badge);

                    row->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
                    row->registerClickAction([this, c, track, dialog](brls::View*) {
                        dialog->dismiss([this, c, track]() {
                            ApplyLyricCandidate(c, track.video_id);
                        });
                        return true;
                    });

                    if (i == 0)
                        first_row = row;

                    list_box->addView(row);
                }
            }

            // 3. Custom Search button (full width)
            auto* custom_btn = new brls::Box(brls::Axis::ROW);
            custom_btn->setFocusable(true);
            custom_btn->setHeight(48);
            custom_btn->setWidth(672);
            custom_btn->setCornerRadius(8);
            custom_btn->setBackgroundColor(nvgRGB(36, 38, 46));
            custom_btn->setAlignItems(brls::AlignItems::CENTER);
            custom_btn->setJustifyContent(brls::JustifyContent::CENTER);
            custom_btn->setMarginBottom(8);

            auto* custom_lbl = new brls::Label();
            custom_lbl->setText("🔍  Custom Search (Keyboard)...");
            custom_lbl->setFontSize(15);
            custom_lbl->setTextColor(accent);
            custom_btn->addView(custom_lbl);
            content->addView(custom_btn);

            // 4. Back / Close button (full width, always clickable/focusable)
            auto* close_btn = new brls::Box(brls::Axis::ROW);
            close_btn->setFocusable(true);
            close_btn->setHeight(44);
            close_btn->setWidth(672);
            close_btn->setCornerRadius(8);
            close_btn->setBackgroundColor(nvgRGB(24, 24, 28));
            close_btn->setAlignItems(brls::AlignItems::CENTER);
            close_btn->setJustifyContent(brls::JustifyContent::CENTER);

            auto* close_lbl = new brls::Label();
            close_lbl->setText("Back / Close  (B)");
            close_lbl->setFontSize(14);
            close_lbl->setTextColor(nvgRGB(160, 168, 178));
            close_btn->addView(close_lbl);
            content->addView(close_btn);

            dialog->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
            content->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
            custom_btn->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
            close_btn->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
            close_btn->registerClickAction(dismiss_cb);
            dialog->registerClickAction(dismiss_cb);

            if (first_row)
            {
                dialog->setLastFocusedView(first_row);
                content->setLastFocusedView(first_row);
            }
            else
            {
                dialog->setLastFocusedView(close_btn);
                content->setLastFocusedView(close_btn);
            }

            custom_btn->registerClickAction([this, track, dialog](brls::View*) {
                dialog->dismiss([this, track]() {
#ifdef __SWITCH__
                    SwkbdConfig keyboard {};
                    if (R_SUCCEEDED(swkbdCreate(&keyboard, 0)))
                    {
                        swkbdConfigMakePresetDefault(&keyboard);
                        swkbdConfigSetHeaderText(&keyboard, "Search Lyrics (Title Artist)");
                        swkbdConfigSetOkButtonText(&keyboard, "Search");
                        swkbdConfigSetStringLenMax(&keyboard, 64);
                        char output[128] = {};
                        if (R_SUCCEEDED(swkbdShow(&keyboard, output, sizeof(output))) && output[0] != '\0')
                        {
                            PromptBrowseLyrics(output);
                        }
                        swkbdClose(&keyboard);
                    }
#endif
                });
                return true;
            });

            dialog->open();
        });
    });
}

void NowPlayingTab::ApplyLyricCandidate(const lyrics::LyricCandidate& c, const std::string& video_id)
{
    lyrics::LyricsResult res;
    if (c.is_synced)
    {
        res.synced = lyrics::LrclibClient::ParseLRC(c.synced_lyrics);
        res.is_synced = !res.synced.empty();
    }
    if (!res.is_synced && c.is_plain)
    {
        std::stringstream ss(c.plain_lyrics);
        std::string line;
        while (std::getline(ss, line))
            res.plain.push_back(line);
        res.is_plain = !res.plain.empty();
    }
    res.is_instrumental = c.is_instrumental;

    {
        std::lock_guard<std::mutex> lock(lyrics_mutex_);
        lyrics_result_ = res;
        current_lyrics_vid_ = video_id;
    }

    PopulateLyricsView();

    // Persist as local .lrc sidecar file
    const std::string local_lrc = "sdmc:/switch/StreamSnagNX/music/" + video_id + ".lrc";
    std::ofstream lrc_file(local_lrc);
    if (lrc_file.is_open())
    {
        if (c.is_synced)
            lrc_file << c.synced_lyrics << "\n";
        else if (c.is_plain)
            lrc_file << c.plain_lyrics << "\n";
        lrc_file.flush();
        lrc_file.close();
#ifdef __SWITCH__
        fsdevCommitDevice("sdmc");
#endif
    }
}

void NowPlayingTab::draw(NVGcontext* vg, float x, float y, float width, float height,
                         brls::Style style, brls::FrameContext* ctx)
{
    UpdateAccentColors();

    const audio::NowPlayingInfo info = audio::NowPlaying::Instance().Get();
    auto& player = audio::Player::Instance();

    const std::string key = info.track.video_id + "|" + info.via + "|" + info.error;
    if (key != last_key_)
    {
        last_key_ = key;
        if (!info.track.video_id.empty())
        {
            title_->setText(info.track.title);
            artist_->setText(info.track.artist.empty() ? info.track.subtitle
                                                       : info.track.artist);
            if (!info.error.empty())
                status_->setText(info.error);
            else if (!info.track.subtitle.empty() && info.track.subtitle != info.track.artist)
                status_->setText(info.track.subtitle);
            else
                status_->setText("");

            const std::string thumb_file =
                "sdmc:/switch/StreamSnagNX/thumbs/" + info.track.video_id + ".jpg";
            FILE* probe = fopen(thumb_file.c_str(), "rb");
            if (probe)
            {
                fclose(probe);
                art_->setImageFromFile(thumb_file);
            }

            if (info.track.video_id != current_lyrics_vid_)
            {
                current_lyrics_vid_ = info.track.video_id;
                lrc_offset_ = settings::SettingsStore::Instance().GetLrcOffset(info.track.video_id);
                char buf[32];
                snprintf(buf, sizeof(buf), "%s%.1fs", lrc_offset_ >= 0.0 ? "+" : "", lrc_offset_);
                lrc_offset_val_->setText(buf);

                FetchLyricsAsync(info.track.title, info.track.artist, info.track.video_id,
                                 player.Duration());
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(lyrics_mutex_);
        if (lyrics_need_populate_)
        {
            lyrics_need_populate_ = false;
            PopulateLyricsView();
        }
    }

    const NVGcolor accent = settings::SettingsStore::Instance().GetAccentColor();

    // Synced lyrics highlight & auto-scroll (with user LRC offset)
    if (lyrics_result_.is_synced && !lyric_labels_.empty())
    {
        const double pos = player.Position() + lrc_offset_;
        int active_idx = -1;
        for (int i = 0; i < static_cast<int>(lyrics_result_.synced.size()); ++i)
        {
            if (pos >= lyrics_result_.synced[i].timestamp)
                active_idx = i;
            else
                break;
        }

        if (active_idx != current_active_line_)
        {
            if (current_active_line_ >= 0 &&
                current_active_line_ < static_cast<int>(lyric_labels_.size()))
            {
                lyric_labels_[current_active_line_]->setTextColor(nvgRGB(100, 108, 120));
                lyric_labels_[current_active_line_]->setFontSize(17);
            }

            current_active_line_ = active_idx;
            if (current_active_line_ >= 0 &&
                current_active_line_ < static_cast<int>(lyric_labels_.size()))
            {
                auto* active_label = lyric_labels_[current_active_line_];
                active_label->setTextColor(accent);
                active_label->setFontSize(20);

                // Use the exact layout Y coordinate of the label within the scroll content
                const float label_y = active_label->getLocalY();
                const float label_h = active_label->getHeight();
                float view_h = lyrics_scroll_->getHeight();
                if (view_h <= 0.0f)
                    view_h = 420.0f;

                // Center active line in the visible view (around 40% from the top)
                const float target_y = std::max(0.0f, label_y + (label_h / 2.0f) - (view_h * 0.4f));
                lyrics_scroll_->setContentOffsetY(target_y, true);
            }
        }
    }

    // Play/Pause icon
    if (play_icon_)
        play_icon_->setIconType(player.IsPaused() || !player.IsPlaying() ? shell::PlayerIconType::Play : shell::PlayerIconType::Pause);

    // Seekbar and time update
    const double pos = player.Position();
    const double dur = player.Duration();
    time_cur_label_->setText(FormatTime(pos));
    time_dur_label_->setText(FormatTime(dur));

    if (dur > 0.0)
    {
        const float pct = std::clamp(static_cast<float>(pos / dur), 0.0f, 1.0f);
        seek_fill_->setWidth(pct * 340.0f);
    }
    else
    {
        seek_fill_->setWidth(0.0f);
    }

    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

} // namespace ssnx
