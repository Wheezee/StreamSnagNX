#pragma once

#include "../yt/track.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ssnx::audio
{

enum class QueueSource
{
    NONE,
    SEARCH,
    LIBRARY
};

class QueueManager
{
  public:
    static QueueManager& Instance();

    void Init();

    // Register track playing from SearchTab
    void SetSearchTrack(const Track& track);

    // Set active library queue (All Tracks or Playlist) and start playback
    void SetLibraryQueue(const std::vector<Track>& tracks, size_t index, const std::string& playlist_id);

    // Next / Previous actions
    void Next();
    void Previous();

    // Callback when track finishes naturally (EOF)
    void OnTrackEnded();

    bool HasNext() const;
    bool HasPrev() const;
    QueueSource GetSource() const;
    size_t GetCurrentIndex() const;
    size_t GetQueueSize() const;
    Track GetCurrentTrack() const;
    std::string GetPlaylistId() const;

    void SetOnTrackChange(std::function<void(const Track& track)> cb);

  private:
    QueueManager();

    void PlayCurrentLocked();

    mutable std::recursive_mutex mutex_;
    std::vector<Track> queue_;
    size_t current_index_ = 0;
    QueueSource source_ = QueueSource::NONE;
    std::string playlist_id_;
    std::function<void(const Track& track)> on_track_change_;
    bool initialized_ = false;
};

} // namespace ssnx::audio

