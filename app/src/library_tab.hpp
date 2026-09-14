#pragma once

#include "library/library_store.hpp"

#include <borealis.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ssnx
{

class LibraryTab : public brls::Box
{
  public:
    LibraryTab();
    ~LibraryTab() override = default;

    void SetOnPlayStarted(std::function<void()> callback) { on_play_started_ = std::move(callback); }
    void Refresh();

  private:
    void RefreshSidenav();
    void RefreshTrackList();
    void PlayLibraryTrack(const library::LibraryTrack& track);
    void PromptNewPlaylist();
    void ShowAddToPlaylistDialog(const library::LibraryTrack& track);
    void ShowDeleteDialog(const library::LibraryTrack& track);

    std::string current_playlist_id_ = "all"; // "all" or playlist ID
    std::function<void()> on_play_started_;

    brls::Box* sidenav_box_ = nullptr;
    brls::Box* tracklist_box_ = nullptr;
    brls::Header* tracklist_header_ = nullptr;
    brls::Label* tracklist_status_ = nullptr;
    brls::Box* sort_btn_ = nullptr;
    brls::Label* sort_lbl_ = nullptr;
};

} // namespace ssnx

