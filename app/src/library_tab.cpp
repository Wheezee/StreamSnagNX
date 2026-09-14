#include "library_tab.hpp"

#include "audio/now_playing.hpp"
#include "audio/player.hpp"
#include "audio/queue_manager.hpp"
#include "library/library_store.hpp"
#include "settings/settings_store.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace ssnx
{
namespace
{

std::string PromptPlaylistName()
{
#ifdef __SWITCH__
    SwkbdConfig keyboard {};
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
        return {};
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "New Playlist Name");
    swkbdConfigSetOkButtonText(&keyboard, "Create");
    swkbdConfigSetStringLenMax(&keyboard, 40);
    char output[128] = {};
    const Result rc = swkbdShow(&keyboard, output, sizeof(output));
    swkbdClose(&keyboard);
    if (R_FAILED(rc))
        return {};
    return output;
#else
    return "My Favorites";
#endif
}

} // namespace

LibraryTab::LibraryTab()
    : brls::Box(brls::Axis::ROW)
{
    setPadding(16, 28, 16, 28);
    setBackgroundColor(nvgRGB(12, 13, 16));

    library::LibraryStore::Instance().Init();

    // Sidenav on the left (width 230px matching mockup)
    auto* sidenav_wrapper = new brls::Box(brls::Axis::COLUMN);
    sidenav_wrapper->setWidth(230);
    sidenav_wrapper->setMarginRight(24);
    addView(sidenav_wrapper);

    auto* sidenav_header = new brls::Header();
    sidenav_header->setTitle("Playlists");
    sidenav_header->setSubtitle("Offline SD");
    sidenav_wrapper->addView(sidenav_header);

    auto* sidenav_scroll = new brls::ScrollingFrame();
    sidenav_scroll->setGrow(1.0f);
    sidenav_box_ = new brls::Box(brls::Axis::COLUMN);
    sidenav_scroll->setContentView(sidenav_box_);
    sidenav_wrapper->addView(sidenav_scroll);

    // Track list on the right (takes remaining width)
    auto* main_panel = new brls::Box(brls::Axis::COLUMN);
    main_panel->setGrow(1.0f);
    addView(main_panel);

    auto* header_row = new brls::Box(brls::Axis::ROW);
    header_row->setAlignItems(brls::AlignItems::CENTER);
    header_row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);

    tracklist_header_ = new brls::Header();
    tracklist_header_->setTitle("All Tracks");
    tracklist_header_->setSubtitle("SD Storage");
    header_row->addView(tracklist_header_);

    sort_btn_ = new brls::Box(brls::Axis::ROW);
    sort_btn_->setPadding(6, 12, 6, 12);
    sort_btn_->setCornerRadius(8);
    sort_btn_->setBackgroundColor(nvgRGB(35, 35, 44));
    sort_btn_->setFocusable(true);
    sort_lbl_ = new brls::Label();
    sort_lbl_->setText("Sort: " + settings::SettingsStore::Instance().GetLibrarySortString() + " (+/-)");
    sort_lbl_->setFontSize(13);
    sort_lbl_->setTextColor(settings::SettingsStore::Instance().GetAccentColor());
    sort_btn_->addView(sort_lbl_);
    sort_btn_->registerClickAction([this](brls::View*) {
        settings::SettingsStore::Instance().CycleLibrarySort(true);
        RefreshTrackList();
        return true;
    });
    header_row->addView(sort_btn_);
    main_panel->addView(header_row);

    tracklist_status_ = new brls::Label();
    tracklist_status_->setText("A: Play · X: Add to playlist · Y: Delete · +/-: Sort");
    tracklist_status_->setFontSize(14);
    tracklist_status_->setTextColor(nvgRGB(120, 128, 140));
    tracklist_status_->setMarginBottom(12);
    main_panel->addView(tracklist_status_);

    registerAction("Sort (+)", brls::BUTTON_START, [this](brls::View*) {
        settings::SettingsStore::Instance().CycleLibrarySort(true);
        RefreshTrackList();
        return true;
    });

    registerAction("Sort (-)", brls::BUTTON_BACK, [this](brls::View*) {
        settings::SettingsStore::Instance().CycleLibrarySort(false);
        RefreshTrackList();
        return true;
    });

    auto* main_scroll = new brls::ScrollingFrame();
    main_scroll->setGrow(1.0f);
    tracklist_box_ = new brls::Box(brls::Axis::COLUMN);
    tracklist_box_->setGrow(1.0f);
    main_scroll->setContentView(tracklist_box_);
    main_panel->addView(main_scroll);

    RefreshSidenav();
    RefreshTrackList();
}

void LibraryTab::Refresh()
{
    RefreshSidenav();
    RefreshTrackList();
}

void LibraryTab::PromptNewPlaylist()
{
    const std::string name = PromptPlaylistName();
    if (!name.empty())
    {
        const std::string id = library::LibraryStore::Instance().CreatePlaylist(name);
        if (!id.empty())
        {
            current_playlist_id_ = id;
            RefreshSidenav();
            RefreshTrackList();
        }
    }
}

void LibraryTab::RefreshSidenav()
{
    sidenav_box_->clearViews();
    auto& store = library::LibraryStore::Instance();
    const auto all_tracks = store.GetTracks();
    const auto playlists = store.GetPlaylists();

    // 1. "All Tracks" button
    auto* all_btn = new brls::Box(brls::Axis::ROW);
    all_btn->setFocusable(true);
    all_btn->setHeight(50);
    all_btn->setCornerRadius(8);
    all_btn->setPadding(0, 14, 0, 14);
    all_btn->setAlignItems(brls::AlignItems::CENTER);
    all_btn->setMarginBottom(6);

    if (current_playlist_id_ == "all")
        all_btn->setBackgroundColor(nvgRGB(32, 40, 52));
    else
        all_btn->setBackgroundColor(nvgRGB(20, 21, 25));

    auto* all_label = new brls::Label();
    all_label->setText("All Tracks (" + std::to_string(all_tracks.size()) + ")");
    all_label->setFontSize(16);
    all_label->setTextColor(current_playlist_id_ == "all" ? nvgRGB(77, 159, 255) : nvgRGB(220, 225, 230));
    all_btn->addView(all_label);

    all_btn->registerClickAction([this](brls::View*) {
        current_playlist_id_ = "all";
        RefreshSidenav();
        RefreshTrackList();
        return true;
    });
    sidenav_box_->addView(all_btn);

    // 2. Visual Separator Line
    auto* divider = new brls::Box();
    divider->setHeight(1);
    divider->setMarginTop(4);
    divider->setMarginBottom(8);
    divider->setBackgroundColor(nvgRGB(40, 42, 50));
    sidenav_box_->addView(divider);

    // 3. Custom playlists
    for (const auto& pl : playlists)
    {
        auto* pl_row = new brls::Box(brls::Axis::ROW);
        pl_row->setFocusable(true);
        pl_row->setHeight(50);
        pl_row->setCornerRadius(8);
        pl_row->setPadding(0, 14, 0, 14);
        pl_row->setAlignItems(brls::AlignItems::CENTER);
        pl_row->setMarginBottom(6);

        if (current_playlist_id_ == pl.id)
            pl_row->setBackgroundColor(nvgRGB(32, 40, 52));
        else
            pl_row->setBackgroundColor(nvgRGB(20, 21, 25));

        auto* pl_label = new brls::Label();
        pl_label->setText(pl.name + " (" + std::to_string(pl.video_ids.size()) + ")");
        pl_label->setFontSize(15);
        pl_label->setTextColor(current_playlist_id_ == pl.id ? nvgRGB(77, 159, 255) : nvgRGB(200, 205, 210));
        pl_label->setGrow(1.0f);
        pl_row->addView(pl_label);

        // Click: select playlist
        pl_row->registerClickAction([this, pl_id = pl.id](brls::View*) {
            current_playlist_id_ = pl_id;
            RefreshSidenav();
            RefreshTrackList();
            return true;
        });

        // Press Y: Delete playlist with confirmation dialog
        pl_row->registerAction("Delete Playlist", brls::BUTTON_Y, [this, pl_id = pl.id, pl_name = pl.name](brls::View*) {
            brls::Dialog* dialog = new brls::Dialog("Delete playlist '" + pl_name + "'?");
            dialog->addButton("Delete", [this, pl_id]() {
                library::LibraryStore::Instance().DeletePlaylist(pl_id);
                if (current_playlist_id_ == pl_id)
                    current_playlist_id_ = "all";
                RefreshSidenav();
                RefreshTrackList();
            });
            dialog->addButton("Cancel", []() {});
            dialog->open();
            return true;
        }, false, false);

        sidenav_box_->addView(pl_row);
    }

    // 4. "+ New Playlist" button appended at end of list
    auto* new_pl_btn = new brls::Box(brls::Axis::ROW);
    new_pl_btn->setFocusable(true);
    new_pl_btn->setHeight(46);
    new_pl_btn->setCornerRadius(8);
    new_pl_btn->setPadding(0, 14, 0, 14);
    new_pl_btn->setAlignItems(brls::AlignItems::CENTER);
    new_pl_btn->setMarginTop(8);
    new_pl_btn->setMarginBottom(16);
    new_pl_btn->setBackgroundColor(nvgRGB(26, 28, 34));

    auto* new_pl_label = new brls::Label();
    new_pl_label->setText("+ New Playlist");
    new_pl_label->setFontSize(14);
    new_pl_label->setTextColor(nvgRGB(150, 160, 175));
    new_pl_btn->addView(new_pl_label);

    new_pl_btn->registerClickAction([this](brls::View*) {
        PromptNewPlaylist();
        return true;
    });
    sidenav_box_->addView(new_pl_btn);
}

void LibraryTab::RefreshTrackList()
{
    tracklist_box_->clearViews();
    auto& store = library::LibraryStore::Instance();
    std::vector<library::LibraryTrack> display_tracks;

    if (current_playlist_id_ == "all")
    {
        display_tracks = store.GetTracks();
        tracklist_header_->setTitle("All Tracks (" + std::to_string(display_tracks.size()) + ")");
        tracklist_header_->setSubtitle("Offline SD music");
    }
    else
    {
        const auto pl = store.GetPlaylist(current_playlist_id_);
        if (pl.has_value())
        {
            tracklist_header_->setTitle(pl->name + " (" + std::to_string(pl->video_ids.size()) + ")");
            tracklist_header_->setSubtitle("Custom Playlist");
            for (const auto& vid : pl->video_ids)
            {
                const auto t = store.GetTrack(vid);
                if (t.has_value())
                    display_tracks.push_back(*t);
            }
        }
        else
        {
            current_playlist_id_ = "all";
            RefreshSidenav();
            RefreshTrackList();
            return;
        }
    }

    const auto sort_mode = settings::SettingsStore::Instance().GetLibrarySort();
    if (sort_lbl_)
    {
        sort_lbl_->setText("Sort: " + settings::SettingsStore::Instance().GetLibrarySortString() + " (+/-)");
        sort_lbl_->setTextColor(settings::SettingsStore::Instance().GetAccentColor());
    }

    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };

    switch (sort_mode)
    {
        case settings::LibrarySort::TITLE_AZ:
            std::sort(display_tracks.begin(), display_tracks.end(), [&](const auto& a, const auto& b) {
                return to_lower(a.title) < to_lower(b.title);
            });
            break;
        case settings::LibrarySort::ARTIST_AZ:
            std::sort(display_tracks.begin(), display_tracks.end(), [&](const auto& a, const auto& b) {
                std::string art_a = to_lower(a.artist.empty() ? a.subtitle : a.artist);
                std::string art_b = to_lower(b.artist.empty() ? b.subtitle : b.artist);
                if (art_a != art_b)
                    return art_a < art_b;
                return to_lower(a.title) < to_lower(b.title);
            });
            break;
        case settings::LibrarySort::RECENTLY_ADDED:
            std::sort(display_tracks.begin(), display_tracks.end(), [](const auto& a, const auto& b) {
                return a.added_at > b.added_at;
            });
            break;
        case settings::LibrarySort::OLDEST_ADDED:
            std::sort(display_tracks.begin(), display_tracks.end(), [](const auto& a, const auto& b) {
                return a.added_at < b.added_at;
            });
            break;
    }

    if (display_tracks.empty())
    {
        auto* empty_label = new brls::Label();
        empty_label->setText("No tracks found. Search songs and press Y to download them offline!");
        empty_label->setFontSize(16);
        empty_label->setTextColor(nvgRGB(120, 128, 140));
        empty_label->setMarginTop(24);
        tracklist_box_->addView(empty_label);
        return;
    }

    for (size_t track_idx = 0; track_idx < display_tracks.size(); ++track_idx)
    {
        const auto& track = display_tracks[track_idx];
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

        if (!track.local_thumb_path.empty())
        {
            FILE* probe = fopen(track.local_thumb_path.c_str(), "rb");
            if (probe)
            {
                fclose(probe);
                thumb->setImageFromFile(track.local_thumb_path);
            }
        }
        row->addView(thumb);

        auto* meta = new brls::Box(brls::Axis::COLUMN);
        meta->setGrow(1.0f);
        auto* title = new brls::Label();
        title->setText(track.title);
        title->setFontSize(17);
        title->setTextColor(nvgRGB(236, 236, 239));
        meta->addView(title);
        auto* artist = new brls::Label();
        artist->setText(track.artist);
        artist->setFontSize(13);
        artist->setTextColor(nvgRGB(151, 159, 170));
        meta->addView(artist);
        row->addView(meta);

        // Format Badge
        auto* badge = new brls::Label();
        badge->setText(track.ext == "opus" ? "OPUS" : "M4A");
        badge->setFontSize(12);
        badge->setTextColor(nvgRGB(77, 159, 255));
        badge->setMarginRight(12);
        row->addView(badge);

        // Click / A action: Play offline with full queue
        row->registerClickAction([this, track_idx, display_tracks](brls::View*) {
            std::vector<Track> queue;
            queue.reserve(display_tracks.size());
            for (const auto& lt : display_tracks)
            {
                Track t;
                t.video_id = lt.video_id;
                t.title = lt.title;
                t.artist = lt.artist;
                t.subtitle = lt.subtitle;
                t.thumb_url = lt.thumb_url;
                queue.push_back(t);
            }
            audio::QueueManager::Instance().SetLibraryQueue(queue, track_idx, current_playlist_id_);
            if (on_play_started_)
                on_play_started_();
            return true;
        });

        // X Button: Add to playlist
        row->registerAction(
            "Add to Playlist", brls::BUTTON_X,
            [this, track](brls::View*) {
                ShowAddToPlaylistDialog(track);
                return true;
            },
            false, false);

        // Y Button: Delete from library
        row->registerAction(
            "Delete", brls::BUTTON_Y,
            [this, track](brls::View*) {
                ShowDeleteDialog(track);
                return true;
            },
            false, false);

        tracklist_box_->addView(row);
    }
}

void LibraryTab::PlayLibraryTrack(const library::LibraryTrack& track)
{
    Track t;
    t.video_id = track.video_id;
    t.title = track.title;
    t.artist = track.artist;
    t.subtitle = track.subtitle;
    t.thumb_url = track.thumb_url;

    audio::NowPlaying::Instance().Set(t, t.subtitle);
    if (audio::Player::Instance().Play(track.local_audio_path, track.video_id, track.local_audio_path, ""))
    {
        if (on_play_started_)
            on_play_started_();
    }
}

void LibraryTab::ShowAddToPlaylistDialog(const library::LibraryTrack& track)
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
    if (!track.artist.empty())
        track_display += " • " + track.artist;
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
            row->registerClickAction([this, pl_id = pl.id, vid = track.video_id, dialog](brls::View*) {
                dialog->dismiss([this, pl_id, vid]() {
                    library::LibraryStore::Instance().AddTrackToPlaylist(pl_id, vid);
                    RefreshSidenav();
                    RefreshTrackList();
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
                        library::LibraryStore::Instance().AddTrackToPlaylist(pl_id, track.video_id);
                        RefreshSidenav();
                        RefreshTrackList();
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

void LibraryTab::ShowDeleteDialog(const library::LibraryTrack& track)
{
    if (current_playlist_id_ != "all")
    {
        // Remove from current playlist
        brls::Dialog* dialog = new brls::Dialog("Remove '" + track.title + "' from this playlist?");
        dialog->addButton("Remove", [this, vid = track.video_id]() {
            library::LibraryStore::Instance().RemoveTrackFromPlaylist(current_playlist_id_, vid);
            RefreshSidenav();
            RefreshTrackList();
        });
        dialog->addButton("Cancel", []() {});
        dialog->open();
    }
    else
    {
        // Delete completely from SD
        brls::Dialog* dialog = new brls::Dialog("Delete '" + track.title + "' from SD card and Library?");
        dialog->addButton("Delete", [this, vid = track.video_id]() {
            library::LibraryStore::Instance().RemoveTrack(vid);
            RefreshSidenav();
            RefreshTrackList();
        });
        dialog->addButton("Cancel", []() {});
        dialog->open();
    }
}

} // namespace ssnx
