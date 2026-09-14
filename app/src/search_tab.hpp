#pragma once

#include "yt/track.hpp"

#include <borealis.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ssnx
{

class SearchTab : public brls::Box
{
  public:
    SearchTab();
    ~SearchTab() override;

    // Called on the UI thread when a track starts playing.
    void SetOnPlayStarted(std::function<void()> callback) { on_play_started_ = std::move(callback); }

  private:
    void RunSearch(const std::string& query);
    void ShowResults();
    void DownloadThumb(const std::string& video_id, const std::string& url);
    void PlayTrack(const Track& track);
    void PromptDownload(const Track& track, brls::Label* badge);
    void DownloadTrack(const Track& track, brls::Label* badge);
    void ShowAddToPlaylistDialog(const Track& track);

    brls::Button* search_button_ = nullptr;
    brls::Box* results_ = nullptr;
    brls::Label* status_ = nullptr;
    std::vector<Track> last_tracks_;
    std::function<void()> on_play_started_;
    // TabFrame destroys tabs on switch; async continuations must never
    // touch members after that. The flag lives on the heap: check it
    // first inside every UI-thread continuation.
    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
    // Thumb views by video id for in-place updates (never rebuild while focused).
    std::map<std::string, brls::Image*> thumb_views_;
    int search_gen_ = 0;
};

} // namespace ssnx
