#pragma once

#include "../yt/track.hpp"

#include <mutex>
#include <string>

namespace ssnx::audio
{

struct NowPlayingInfo
{
    Track track;
    std::string via;
    std::string error;
};

// Shared now-playing state: SearchTab writes on play, NowPlayingTab reads.
class NowPlaying
{
  public:
    static NowPlaying& Instance()
    {
        static NowPlaying state;
        return state;
    }

    void Set(const Track& track, const std::string& via, const std::string& error = "")
    {
        std::lock_guard<std::mutex> lock(mutex_);
        info_.track = track;
        info_.via = via;
        info_.error = error;
    }

    NowPlayingInfo Get()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return info_;
    }

  private:
    std::mutex mutex_;
    NowPlayingInfo info_;
};

} // namespace ssnx::audio
