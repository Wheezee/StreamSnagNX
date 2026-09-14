#include "search_tab.hpp"

#include "audio/now_playing.hpp"
#include "audio/player.hpp"
#include "audio/queue_manager.hpp"
#include "library/download_manager.hpp"
#include "library/library_store.hpp"
#include "settings/settings_store.hpp"
#include "yt/innertube.hpp"
#include "yt/track.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

#include <string>

namespace ssnx
{
namespace
{

std::string HomePath()
{
#ifdef __SWITCH__
    return "sdmc:/switch/StreamSnagNX";
#else
    return ".";
#endif
}

std::string ThumbPath(const std::string& video_id)
{
    return library::LibraryStore::ThumbPath(video_id);
}


std::string PromptSearch()
{
#ifdef __SWITCH__
    SwkbdConfig keyboard {};
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
        return {};
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "Search music");
    swkbdConfigSetOkButtonText(&keyboard, "Search");
    swkbdConfigSetStringLenMax(&keyboard, 64);
    char output[128] = {};
    const Result rc = swkbdShow(&keyboard, output, sizeof(output));
    swkbdClose(&keyboard);
    if (R_FAILED(rc))
        return {};
    return output;
#else
    return "starboy";
#endif
}

} // namespace

SearchTab::~SearchTab()
{
    alive_->store(false);
}

SearchTab::SearchTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(18, 28, 18, 28);
    setBackgroundColor(nvgRGB(12, 13, 16));

    auto* header = new brls::Header();
    header->setTitle("Search");
    header->setSubtitle("A: Play  |  Y: Download (Get)  |  X: Add to Playlist");
    addView(header);

    search_button_ = new brls::Button();
    search_button_->setText("Search");
    search_button_->setHeight(46);
    search_button_->setCornerRadius(10);
    search_button_->setMarginBottom(10);
    search_button_->registerClickAction([this](brls::View*) {
        const std::string query = PromptSearch();
        if (!query.empty())
            RunSearch(query);
        return true;
    });
    addView(search_button_);

    status_ = new brls::Label();
    status_->setText("Tap Search to begin");
    status_->setFontSize(14);
    status_->setTextColor(nvgRGB(112, 119, 130));
    status_->setMarginBottom(12);
    addView(status_);

    auto* frame = new brls::ScrollingFrame();
    frame->setGrow(1.0f);
    results_ = new brls::Box(brls::Axis::COLUMN);
    results_->setGrow(1.0f);
    frame->setContentView(results_);
    addView(frame);

#ifdef __SWITCH__
    mkdir((HomePath() + "/thumbs").c_str(), 0777);
#endif
}

void SearchTab::RunSearch(const std::string& query)
{
    status_->setText("Searching...");
    results_->clearViews(true);
    thumb_views_.clear();
    ++search_gen_;

    auto alive = alive_;
    brls::async([this, alive, query]() {
        yt::InnertubeClient client;
        std::vector<Track> tracks = client.Search(query);
        const std::string error = client.last_error();
        brls::sync([this, alive, tracks = std::move(tracks), error]() {
            if (!alive->load())
                return;
            if (tracks.empty())
            {
                status_->setText("Search failed: " + (error.empty() ? "unknown" : error));
                return;
            }
            status_->setText(std::to_string(tracks.size()) + " results");
            last_tracks_ = tracks;
            ShowResults();
            for (const auto& track : tracks)
                DownloadThumb(track.video_id, track.thumb_url);
        });
    });
}

void SearchTab::ShowResults()
{
    // Rows are rebuilt from last_tracks_ once thumbs land.
    results_->clearViews(true);
    for (const auto& track : last_tracks_)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setFocusable(true);
        row->setHeight(76);
        row->setCornerRadius(10);
        row->setBackgroundColor(nvgRGB(21, 21, 24));
        row->setMarginBottom(8);
        row->setPadding(0, 14, 0, 14);
        row->setAlignItems(brls::AlignItems::CENTER);

        auto* thumb = new brls::Image();
        thumb->setWidth(52);
        thumb->setHeight(52);
        thumb->setCornerRadius(8);
        thumb->setScalingType(brls::ImageScalingType::FILL);
        thumb->setMarginRight(14);
        if (FILE* probe = fopen(ThumbPath(track.video_id).c_str(), "rb"))
        {
            fclose(probe);
            thumb->setImageFromFile(ThumbPath(track.video_id));
        }
        thumb_views_[track.video_id] = thumb;
        row->addView(thumb);

        auto* meta = new brls::Box(brls::Axis::COLUMN);
        meta->setGrow(1.0f);
        auto* title = new brls::Label();
        title->setText(track.title);
        title->setFontSize(17);
        title->setTextColor(nvgRGB(236, 236, 239));
        meta->addView(title);
        auto* artist = new brls::Label();
        artist->setText(track.subtitle);
        artist->setFontSize(13);
        artist->setTextColor(nvgRGB(151, 159, 170));
        meta->addView(artist);
        row->addView(meta);

        auto* badge = new brls::Label();
        badge->setFontSize(14);
        const bool is_saved = library::LibraryStore::Instance().IsSaved(track.video_id);
        const bool is_busy = library::DownloadManager::Instance().IsBusy(track.video_id);

        if (is_saved)
        {
            badge->setText("[Saved]");
            badge->setTextColor(nvgRGB(100, 200, 120));
        }
        else if (is_busy)
        {
            badge->setText("[Downloading]");
            badge->setTextColor(nvgRGB(255, 180, 80));
        }
        else
        {
            badge->setText("[Get: Y]");
            badge->setTextColor(nvgRGB(77, 159, 255));
        }
        badge->setMarginRight(8);
        row->addView(badge);

        // Click / A: Play track
        row->registerClickAction([this, track](brls::View*) {
            PlayTrack(track);
            return true;
        });

        // Y Button: Download (Get)
        row->registerAction(
            "Download (Get)", brls::BUTTON_Y,
            [this, track, badge](brls::View*) {
                PromptDownload(track, badge);
                return true;
            },
            false, false);

        // X Button: Add to Playlist
        row->registerAction(
            "Add to Playlist", brls::BUTTON_X,
            [this, track](brls::View*) {
                ShowAddToPlaylistDialog(track);
                return true;
            },
            false, false);

        // B Button: Return to Search button
        row->registerAction(
            "Back to Search", brls::BUTTON_B,
            [this](brls::View*) {
                if (search_button_)
                    brls::Application::giveFocus(search_button_);
                return true;
            },
            true, false);

        // First row links directly to Search button via D-pad UP/DOWN
        if (results_->getChildren().empty() && search_button_)
        {
            row->setCustomNavigationRoute(brls::FocusDirection::UP, search_button_);
            search_button_->setCustomNavigationRoute(brls::FocusDirection::DOWN, row);
        }

        results_->addView(row);
    }
}

void SearchTab::PromptDownload(const Track& track, brls::Label* badge)
{
    if (library::LibraryStore::Instance().IsSaved(track.video_id))
    {
        status_->setText("Already saved in Library: " + track.title);
        return;
    }

    if (library::DownloadManager::Instance().IsBusy(track.video_id))
    {
        status_->setText("Already downloading: " + track.title);
        return;
    }

    brls::Dialog* dialog = new brls::Dialog("Download '" + track.title + "' to SD card?");
    dialog->addButton("Download", [this, track, badge]() {
        DownloadTrack(track, badge);
    });
    dialog->addButton("Cancel", []() {});
    dialog->open();
}

void SearchTab::DownloadTrack(const Track& track, brls::Label* badge)
{
    try
    {
        if (library::LibraryStore::Instance().IsSaved(track.video_id))
        {
            status_->setText("Already saved in Library: " + track.title);
            return;
        }

        if (library::DownloadManager::Instance().IsBusy(track.video_id))
        {
            status_->setText("Already downloading: " + track.title);
            return;
        }

        if (badge)
        {
            badge->setText("[0%]");
            badge->setTextColor(nvgRGB(255, 180, 80));
        }
        status_->setText("Downloading: " + track.title + " (Resolving stream...)");

        auto alive = alive_;
        library::DownloadManager::Instance().StartDownload(
            track, [this, badge, alive, track](const library::DownloadProgress& p) {
                brls::sync([this, badge, alive, p, track]() {
                    if (!alive->load())
                        return;

                    if (p.state == library::DownloadState::COMPLETED)
                    {
                        if (badge)
                        {
                            badge->setText("[Saved]");
                            badge->setTextColor(nvgRGB(100, 200, 120));
                        }
                        status_->setText("Saved to Library: " + track.title);
                    }
                    else if (p.state == library::DownloadState::FAILED)
                    {
                        if (badge)
                        {
                            badge->setText("[Failed]");
                            badge->setTextColor(nvgRGB(255, 100, 100));
                        }
                        status_->setText("Download failed: " + p.message);
                        brls::Dialog* err_dlg = new brls::Dialog("Download failed for:\n" + track.title + "\n\nReason: " + p.message);
                        err_dlg->addButton("OK", []() {});
                        err_dlg->open();
                    }
                    else
                    {
                        const int pct = static_cast<int>(p.progress * 100.0);
                        if (badge)
                        {
                            badge->setText("[" + std::to_string(pct) + "%]");
                            badge->setTextColor(nvgRGB(255, 180, 80));
                        }
                        status_->setText(track.title + " - " + p.message);
                    }
                });
            });
    }
    catch (const std::exception& e)
    {
        brls::Dialog* err_dlg = new brls::Dialog(std::string("Exception: ") + e.what());
        err_dlg->addButton("OK", []() {});
        err_dlg->open();
    }
    catch (...)
    {
        brls::Dialog* err_dlg = new brls::Dialog("Unknown exception occurred");
        err_dlg->addButton("OK", []() {});
        err_dlg->open();
    }
}

void SearchTab::ShowAddToPlaylistDialog(const Track& track)
{
    auto playlists = library::LibraryStore::Instance().GetPlaylists();
    const NVGcolor accent = settings::SettingsStore::Instance().GetAccentColor();

    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setWidth(720);
    content->setPadding(20, 24, 20, 24);

    // 1. Top message header
    auto* title_lbl = new brls::Label();
    title_lbl->setText("Add to Playlist");
    title_lbl->setFontSize(20);
    title_lbl->setTextColor(nvgRGB(240, 240, 245));
    title_lbl->setMarginBottom(4);
    content->addView(title_lbl);

    auto* sub_lbl = new brls::Label();
    std::string track_display = track.title;
    if (!track.subtitle.empty())
        track_display += " • " + track.subtitle;
    if (track_display.size() > 60)
        track_display = track_display.substr(0, 57) + "...";
    sub_lbl->setText(track_display);
    sub_lbl->setFontSize(13);
    sub_lbl->setTextColor(nvgRGB(140, 148, 160));
    sub_lbl->setMarginBottom(14);
    content->addView(sub_lbl);

    // 2. Playlists list in a ScrollingFrame
    auto* scroll = new brls::ScrollingFrame();
    const float list_h = playlists.empty() ? 64.0f : std::min(320.0f, static_cast<float>(playlists.size() * 64));
    scroll->setHeight(list_h);
    scroll->setWidth(672);
    scroll->setMarginBottom(12);

    auto* list_box = new brls::Box(brls::Axis::COLUMN);
    list_box->setWidth(672);
    scroll->setContentView(list_box);
    content->addView(scroll);

    // 3. Bottom button: + Create New Playlist (full width)
    auto* new_pl_btn = new brls::Box(brls::Axis::ROW);
    new_pl_btn->setFocusable(true);
    new_pl_btn->setHeight(48);
    new_pl_btn->setWidth(672);
    new_pl_btn->setCornerRadius(8);
    new_pl_btn->setBackgroundColor(nvgRGB(36, 38, 46));
    new_pl_btn->setAlignItems(brls::AlignItems::CENTER);
    new_pl_btn->setJustifyContent(brls::JustifyContent::CENTER);

    auto* new_pl_lbl = new brls::Label();
    new_pl_lbl->setText("+  Create New Playlist");
    new_pl_lbl->setFontSize(15);
    new_pl_lbl->setTextColor(accent);
    new_pl_btn->addView(new_pl_lbl);
    content->addView(new_pl_btn);

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

    // Dialog
    brls::Dialog* dialog = new brls::Dialog(content);

    // Direct B button dismissal callbacks registered on all layers
    auto dismiss_cb = [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    };

    brls::Box* first_row = nullptr;

    if (playlists.empty())
    {
        auto* empty_lbl = new brls::Label();
        empty_lbl->setText("No playlists created yet. Create one below!");
        empty_lbl->setFontSize(14);
        empty_lbl->setTextColor(nvgRGB(150, 155, 165));
        empty_lbl->setMarginTop(8);
        list_box->addView(empty_lbl);
    }
    else
    {
        for (size_t i = 0; i < playlists.size(); ++i)
        {
            const auto& pl = playlists[i];
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

            auto* name_lbl = new brls::Label();
            name_lbl->setText(pl.name);
            name_lbl->setFontSize(16);
            name_lbl->setTextColor(nvgRGB(240, 240, 245));
            meta->addView(name_lbl);

            auto* count_lbl = new brls::Label();
            count_lbl->setText(std::to_string(pl.video_ids.size()) + " track" + (pl.video_ids.size() == 1 ? "" : "s"));
            count_lbl->setFontSize(12);
            count_lbl->setTextColor(nvgRGB(145, 153, 165));
            meta->addView(count_lbl);

            row->addView(meta);

            // Check if track is already in this playlist
            const bool already_in = std::find(pl.video_ids.begin(), pl.video_ids.end(), track.video_id) != pl.video_ids.end();
            auto* badge = new brls::Label();
            badge->setFontSize(13);
            if (already_in)
            {
                badge->setText("[Added ✓]");
                badge->setTextColor(nvgRGB(100, 200, 120));
            }
            else
            {
                badge->setText("[Add]");
                badge->setTextColor(accent);
            }
            row->addView(badge);

            row->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
            row->registerClickAction([this, pl_id = pl.id, pl_name = pl.name, track, dialog](brls::View*) {
                dialog->dismiss([this, pl_id, pl_name, track]() {
                    if (!library::LibraryStore::Instance().IsSaved(track.video_id))
                    {
                        library::DownloadManager::Instance().StartDownload(track);
                    }
                    library::LibraryStore::Instance().AddTrackToPlaylist(pl_id, track.video_id);
                    status_->setText("Added to playlist '" + pl_name + "'");
                });
                return true;
            });

            if (i == 0)
                first_row = row;

            list_box->addView(row);
        }
    }

    dialog->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
    content->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
    new_pl_btn->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
    close_btn->registerAction("Back", brls::BUTTON_B, dismiss_cb, false, false, brls::SOUND_BACK);
    close_btn->registerClickAction(dismiss_cb);
    dialog->registerClickAction(dismiss_cb);

    if (playlists.empty())
    {
        dialog->setLastFocusedView(close_btn);
        content->setLastFocusedView(close_btn);
    }
    else if (first_row)
    {
        dialog->setLastFocusedView(first_row);
        content->setLastFocusedView(first_row);
    }

    new_pl_btn->registerClickAction([this, track, dialog](brls::View*) {
        dialog->dismiss([this, track]() {
#ifdef __SWITCH__
            SwkbdConfig keyboard {};
            if (R_SUCCEEDED(swkbdCreate(&keyboard, 0)))
            {
                swkbdConfigMakePresetDefault(&keyboard);
                swkbdConfigSetHeaderText(&keyboard, "New Playlist Name");
                swkbdConfigSetOkButtonText(&keyboard, "Create");
                swkbdConfigSetStringLenMax(&keyboard, 40);
                char output[128] = {};
                if (R_SUCCEEDED(swkbdShow(&keyboard, output, sizeof(output))) && output[0] != '\0')
                {
                    const std::string pl_id = library::LibraryStore::Instance().CreatePlaylist(output);
                    if (!pl_id.empty())
                    {
                        if (!library::LibraryStore::Instance().IsSaved(track.video_id))
                            library::DownloadManager::Instance().StartDownload(track);
                        library::LibraryStore::Instance().AddTrackToPlaylist(pl_id, track.video_id);
                        status_->setText("Created playlist '" + std::string(output) + "' & added track");
                    }
                }
                swkbdClose(&keyboard);
            }
#endif
        });
        return true;
    });

    dialog->open();
}

void SearchTab::PlayTrack(const Track& track)
{
    audio::QueueManager::Instance().SetSearchTrack(track);

    // Check if saved offline in library
    const auto saved_track = library::LibraryStore::Instance().GetTrack(track.video_id);
    if (saved_track.has_value() && !saved_track->local_audio_path.empty())
    {
        FILE* fp = fopen(saved_track->local_audio_path.c_str(), "rb");
        if (fp)
        {
            fclose(fp);
            status_->setText("Playing offline…");
            audio::NowPlaying::Instance().Set(track, track.subtitle);
            if (audio::Player::Instance().Play(saved_track->local_audio_path, track.video_id,
                                               saved_track->local_audio_path, ""))
            {
                if (on_play_started_)
                    on_play_started_();
                return;
            }
        }
    }

    // Check if cached on disk from previous playback
    for (const char* ext : {"m4a", "opus"})
    {
        const std::string existing = library::LibraryStore::AudioPath(track.video_id, ext);
        FILE* fp = fopen(existing.c_str(), "rb");
        if (fp)
        {
            fclose(fp);
            status_->setText("Playing offline…");
            audio::NowPlaying::Instance().Set(track, track.subtitle);
            if (audio::Player::Instance().Play(existing, track.video_id, existing, ""))
            {
                if (on_play_started_)
                    on_play_started_();
                return;
            }
        }
    }

    status_->setText("Resolving audio…");
    auto alive = alive_;
    brls::async([this, alive, track]() {
        yt::InnertubeClient client;
        yt::ResolvedAudio audio = client.ResolveAudio(track.video_id);
        brls::sync([this, alive, track, audio]() {
            if (!alive->load())
                return;
            if (audio.url.empty())
            {
                status_->setText("No audio stream");
                audio::NowPlaying::Instance().Set(track, "", "No audio stream");
                return;
            }
            audio::NowPlaying::Instance().Set(track, track.subtitle);
            status_->setText("Playing " + track.title);
#ifdef __SWITCH__
            mkdir(library::LibraryStore::MusicDir().c_str(), 0777);
#endif
            const std::string local_path =
                library::LibraryStore::AudioPath(track.video_id, audio.ext);
            if (!audio::Player::Instance().Play(
                    audio.url, track.video_id, local_path,
                    audio.user_agent))
            {
                status_->setText("Playback failed");
                audio::NowPlaying::Instance().Set(track, track.subtitle, "Playback failed");
                return;
            }
            if (on_play_started_)
                on_play_started_();
        });
    });
}

void SearchTab::DownloadThumb(const std::string& video_id, const std::string& url)
{
    if (video_id.empty() || url.empty())
        return;
    if (FILE* probe = fopen(ThumbPath(video_id).c_str(), "rb"))
    {
        fclose(probe);
        return; // cached
    }
    const int gen = search_gen_;
    auto alive = alive_;
    brls::async([this, alive, video_id, url, gen]() {
        if (!yt::InnertubeClient::DownloadFile(url, ThumbPath(video_id)))
            return;
        brls::sync([this, alive, video_id, gen]() {
            // Stale search or view gone: never touch dead views.
            if (!alive->load() || gen != search_gen_)
                return;
            const auto it = thumb_views_.find(video_id);
            if (it != thumb_views_.end() && it->second)
                it->second->setImageFromFile(ThumbPath(video_id));
        });
    });
}

} // namespace ssnx
