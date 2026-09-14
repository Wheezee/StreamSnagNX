#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ssnx::audio
{

// Streaming audio player, newpipe-style: curl downloads the https stream
// to a local cache file (FFmpeg on Switch can't do TLS itself); FFmpeg
// decodes the growing file, resampled to 48kHz stereo s16, out via audout.
class Player
{
  public:
    Player();
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Plays a remote stream URL, caching to final_path (+".part" while
    // downloading). Starts once enough bytes are buffered. False if the
    // download or open fails. Download mirrors switch-newpipe: 1MiB
    // `range=` URL chunks (not HTTP Range), keep-alive handle, matching
    // client UA — plain full GETs get 403/throttled.
    bool Play(const std::string& url, const std::string& video_id,
              const std::string& final_path,
              const std::string& user_agent = "");
    void Stop();
    void SetPaused(bool paused);
    bool IsPaused() const { return paused_.load(); }
    bool IsPlaying() const { return playing_.load(); }

    // Seconds. Duration may be 0 when the container hides it.
    double Position() const;
    double Duration() const { return duration_.load(); }
    // 0.0-1.0 download progress of the current track.
    double DownloadProgress() const;

    // Seeking
    void SeekRelative(double delta_seconds);
    void SeekTo(double seconds);

    void SetOnTrackEnded(std::function<void()> cb) { on_track_ended_ = std::move(cb); }

    static Player& Instance();

  private:
    void Worker(std::string url, std::string video_id, std::string final_path,
                std::string user_agent);

    std::thread worker_;
    std::atomic<bool> playing_ {false};
    std::atomic<bool> paused_ {false};
    std::atomic<bool> stop_requested_ {false};
    std::atomic<double> seek_requested_seconds_ {-1.0};
    std::atomic<double> position_ {0.0};
    std::atomic<double> duration_ {0.0};
    std::atomic<uint64_t> downloaded_bytes_ {0};
    std::atomic<uint64_t> total_bytes_ {0};
    std::atomic<bool> download_done_ {false};
    std::atomic<bool> download_ok_ {false};
    std::function<void()> on_track_ended_;
};

} // namespace ssnx::audio
