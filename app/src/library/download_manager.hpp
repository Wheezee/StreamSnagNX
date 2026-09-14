#pragma once

#include "library_store.hpp"
#include "../yt/track.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace ssnx::library
{

enum class DownloadState
{
    IDLE,
    RESOLVING,
    DOWNLOADING,
    SAVING_LYRICS,
    SAVING_THUMB,
    COMPLETED,
    FAILED
};

struct DownloadProgress
{
    DownloadState state = DownloadState::IDLE;
    double progress = 0.0;
    std::string message;
};

class DownloadManager
{
  public:
    static DownloadManager& Instance();

    void StartDownload(const Track& track, std::function<void(const DownloadProgress&)> on_progress = nullptr);
    DownloadProgress GetProgress(const std::string& video_id);
    bool IsBusy(const std::string& video_id);
    void Shutdown();

  private:
    DownloadManager() = default;
    ~DownloadManager();

    void Worker(Track track, std::function<void(const DownloadProgress&)> on_progress);

    std::recursive_mutex mutex_;
    std::map<std::string, DownloadProgress> progress_map_;
    std::atomic<bool> stop_ {false};
};

} // namespace ssnx::library


