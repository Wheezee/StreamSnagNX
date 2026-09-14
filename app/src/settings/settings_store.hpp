#pragma once

#include <borealis.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ssnx::settings
{

enum class StreamQuality
{
    AUTO = 0,      // Prefer Opus 251 -> AAC 140
    OPUS_HIGH = 1, // Prefer Opus (itag 251 ~160kbps)
    M4A_MED = 2,   // Prefer AAC (itag 140 ~128kbps)
    LOW = 3        // Save data (itag 249/250 ~50-70kbps)
};

enum class DownloadFormat
{
    AUTO = 0, // Match resolved stream
    OPUS = 1, // .opus container
    M4A = 2   // .m4a container
};

enum class AppTheme
{
    BLUE = 0,   // #4D9FFF (Default)
    GREEN = 1,  // #34D399
    RED = 2,    // #F87171
    PURPLE = 3  // #A78BFA
};

enum class LibrarySort
{
    RECENTLY_ADDED = 0,
    OLDEST_ADDED   = 1,
    TITLE_AZ       = 2,
    ARTIST_AZ      = 3,
};

class SettingsStore
{
  public:
    static SettingsStore& Instance();

    void Init();
    void Save();

    // Stream Quality
    StreamQuality GetStreamQuality() const { return stream_quality_; }
    std::string GetStreamQualityString() const;
    void SetStreamQuality(StreamQuality q);
    void CycleStreamQuality();

    // Download Format
    DownloadFormat GetDownloadFormat() const { return download_format_; }
    std::string GetDownloadFormatString() const;
    void SetDownloadFormat(DownloadFormat f);
    void CycleDownloadFormat();

    // Lyrics
    bool GetLyricsEnabled() const { return lyrics_enabled_; }
    std::string GetLyricsString() const { return lyrics_enabled_ ? "Enabled (LRCLIB)" : "Disabled"; }
    void ToggleLyrics();

    // Theme
    AppTheme GetTheme() const { return theme_; }
    std::string GetThemeString() const;
    NVGcolor GetAccentColor() const;
    void SetTheme(AppTheme t);
    void CycleTheme();

    // Sleep Timer
    int GetSleepTimerMinutes() const { return sleep_timer_minutes_; }
    std::string GetSleepTimerString() const;
    int GetRemainingSleepSeconds() const;
    void SetSleepTimerMinutes(int mins);
    void CycleSleepTimer();
    void CheckSleepTimer();

    struct CleanResult
    {
        size_t thumbs_removed = 0;
        size_t buffers_removed = 0;
        size_t total() const { return thumbs_removed + buffers_removed; }
    };

    // Clean orphaned .part files and unreferenced search thumbnails
    CleanResult CleanTempFiles();

    // Callbacks on change
    void RegisterListener(std::function<void()> listener);

    // LRC sync offset per track
    double GetLrcOffset(const std::string& video_id) const;
    void SetLrcOffset(const std::string& video_id, double offset);

    // Library Sort
    LibrarySort GetLibrarySort() const { return library_sort_; }
    std::string GetLibrarySortString() const;
    void SetLibrarySort(LibrarySort sort);
    void CycleLibrarySort(bool forward = true);

    // Shutdown
    void Shutdown() {}

  private:
    SettingsStore();
    ~SettingsStore();

    static std::string ConfigPath();

    mutable std::recursive_mutex mutex_;
    StreamQuality stream_quality_ = StreamQuality::AUTO;
    DownloadFormat download_format_ = DownloadFormat::AUTO;
    bool lyrics_enabled_ = true;
    AppTheme theme_ = AppTheme::BLUE;
    int sleep_timer_minutes_ = 0; // 0 = Off
    std::chrono::steady_clock::time_point sleep_deadline_ {};
    std::unordered_map<std::string, double> lrc_offsets_;
    LibrarySort library_sort_ = LibrarySort::RECENTLY_ADDED;

    std::vector<std::function<void()>> listeners_;
    bool initialized_ = false;
};

} // namespace ssnx::settings

