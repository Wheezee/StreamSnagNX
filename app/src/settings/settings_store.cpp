#include "settings_store.hpp"

#include "../audio/player.hpp"
#include "../library/library_store.hpp"
#include "../util/log.hpp"

#include <jansson.h>

#ifdef __SWITCH__
#include <dirent.h>
#include <switch.h>
#include <sys/stat.h>
#endif

#include <chrono>
#include <cstdio>
#include <thread>
#include <unordered_set>

namespace ssnx::settings
{
namespace
{

struct JsonDeleter
{
    void operator()(json_t* j) const
    {
        if (j)
            json_decref(j);
    }
};

using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

} // namespace

SettingsStore& SettingsStore::Instance()
{
    static SettingsStore instance;
    return instance;
}

SettingsStore::SettingsStore() = default;
SettingsStore::~SettingsStore() = default;

std::string SettingsStore::ConfigPath()
{
    return library::LibraryStore::StorageDir() + "/settings.json";
}

void SettingsStore::Init()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_)
        return;

    const std::string path = ConfigPath();
    const std::string tmp_path = path + ".tmp";
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
    {
        FILE* tmp_fp = fopen(tmp_path.c_str(), "rb");
        if (tmp_fp)
        {
            fclose(tmp_fp);
            std::rename(tmp_path.c_str(), path.c_str());
            fp = fopen(path.c_str(), "rb");
        }
    }

    if (fp)
    {
        fclose(fp);
        json_error_t err {};
        json_t* raw = json_load_file(path.c_str(), 0, &err);
        if (raw)
        {
            JsonPtr root(raw);
            if (json_is_object(root.get()))
            {
                json_t* sq = json_object_get(root.get(), "stream_quality");
                if (json_is_integer(sq))
                    stream_quality_ = static_cast<StreamQuality>(json_integer_value(sq));

                json_t* df = json_object_get(root.get(), "download_format");
                if (json_is_integer(df))
                    download_format_ = static_cast<DownloadFormat>(json_integer_value(df));

                json_t* ly = json_object_get(root.get(), "lyrics_enabled");
                if (json_is_boolean(ly))
                    lyrics_enabled_ = json_is_true(ly);

                json_t* th = json_object_get(root.get(), "theme");
                if (json_is_integer(th))
                    theme_ = static_cast<AppTheme>(json_integer_value(th));

                json_t* ls = json_object_get(root.get(), "library_sort");
                if (json_is_integer(ls))
                    library_sort_ = static_cast<LibrarySort>(json_integer_value(ls));

                json_t* offsets = json_object_get(root.get(), "lrc_offsets");
                if (json_is_object(offsets))
                {
                    const char* key = nullptr;
                    json_t* val = nullptr;
                    json_object_foreach(offsets, key, val)
                    {
                        if (json_is_number(val))
                            lrc_offsets_[key] = json_number_value(val);
                    }
                }
            }
            Log("settings: loaded settings from " + path);
        }
    }
    else
    {
        Log("settings: settings.json does not exist, using defaults");
    }

    initialized_ = true;
}

void SettingsStore::Save()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string path = ConfigPath();
    const std::string tmp_path = path + ".tmp";

    JsonPtr root(json_object());
    json_object_set_new(root.get(), "stream_quality", json_integer(static_cast<int>(stream_quality_)));
    json_object_set_new(root.get(), "download_format", json_integer(static_cast<int>(download_format_)));
    json_object_set_new(root.get(), "lyrics_enabled", lyrics_enabled_ ? json_true() : json_false());
    json_object_set_new(root.get(), "theme", json_integer(static_cast<int>(theme_)));
    json_object_set_new(root.get(), "library_sort", json_integer(static_cast<int>(library_sort_)));

    json_t* offsets_obj = json_object();
    for (const auto& [vid, off] : lrc_offsets_)
    {
        json_object_set_new(offsets_obj, vid.c_str(), json_real(off));
    }
    json_object_set_new(root.get(), "lrc_offsets", offsets_obj);

    FILE* fp = fopen(tmp_path.c_str(), "wb");
    if (fp)
    {
        char* dump = json_dumps(root.get(), JSON_INDENT(2));
        if (dump)
        {
            fputs(dump, fp);
            free(dump);
        }
        fflush(fp);
        fclose(fp);
        std::remove(path.c_str());
        std::rename(tmp_path.c_str(), path.c_str());
#ifdef __SWITCH__
        fsdevCommitDevice("sdmc");
#endif
        Log("settings: saved settings to " + path);
    }
}

double SettingsStore::GetLrcOffset(const std::string& video_id) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = lrc_offsets_.find(video_id);
    if (it != lrc_offsets_.end())
        return it->second;
    return 0.0;
}

void SettingsStore::SetLrcOffset(const std::string& video_id, double offset)
{
    if (video_id.empty())
        return;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        lrc_offsets_[video_id] = offset;
    }
    Save();
}

std::string SettingsStore::GetStreamQualityString() const
{
    switch (stream_quality_)
    {
        case StreamQuality::AUTO:
            return "Auto (Opus 251 -> AAC 140)";
        case StreamQuality::OPUS_HIGH:
            return "Opus High (itag 251 ~160k)";
        case StreamQuality::M4A_MED:
            return "AAC Medium (itag 140 ~128k)";
        case StreamQuality::LOW:
            return "Low (itag 249/250 ~50-70k)";
    }
    return "Auto";
}

void SettingsStore::SetStreamQuality(StreamQuality q)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        stream_quality_ = q;
    }
    Save();
    for (auto& l : listeners_)
        l();
}

void SettingsStore::CycleStreamQuality()
{
    int next = (static_cast<int>(stream_quality_) + 1) % 4;
    SetStreamQuality(static_cast<StreamQuality>(next));
}

std::string SettingsStore::GetDownloadFormatString() const
{
    switch (download_format_)
    {
        case DownloadFormat::AUTO:
            return "Auto (match stream)";
        case DownloadFormat::OPUS:
            return "Opus (.opus)";
        case DownloadFormat::M4A:
            return "AAC (.m4a)";
    }
    return "Auto";
}

void SettingsStore::SetDownloadFormat(DownloadFormat f)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        download_format_ = f;
    }
    Save();
    for (auto& l : listeners_)
        l();
}

void SettingsStore::CycleDownloadFormat()
{
    int next = (static_cast<int>(download_format_) + 1) % 3;
    SetDownloadFormat(static_cast<DownloadFormat>(next));
}

void SettingsStore::ToggleLyrics()
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        lyrics_enabled_ = !lyrics_enabled_;
    }
    Save();
    for (auto& l : listeners_)
        l();
}

std::string SettingsStore::GetThemeString() const
{
    switch (theme_)
    {
        case AppTheme::BLUE:
            return "Neon Blue";
        case AppTheme::GREEN:
            return "Emerald Green";
        case AppTheme::RED:
            return "Crimson Red";
        case AppTheme::PURPLE:
            return "Cyber Purple";
    }
    return "Neon Blue";
}

NVGcolor SettingsStore::GetAccentColor() const
{
    switch (theme_)
    {
        case AppTheme::BLUE:
            return nvgRGB(77, 159, 255);
        case AppTheme::GREEN:
            return nvgRGB(52, 211, 153);
        case AppTheme::RED:
            return nvgRGB(248, 113, 113);
        case AppTheme::PURPLE:
            return nvgRGB(167, 139, 250);
    }
    return nvgRGB(77, 159, 255);
}

void SettingsStore::SetTheme(AppTheme t)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        theme_ = t;
    }
    Save();
    for (auto& l : listeners_)
        l();
}

void SettingsStore::CycleTheme()
{
    int next = (static_cast<int>(theme_) + 1) % 4;
    SetTheme(static_cast<AppTheme>(next));
}

int SettingsStore::GetRemainingSleepSeconds() const
{
    if (sleep_timer_minutes_ == 0)
        return 0;
    const auto now = std::chrono::steady_clock::now();
    if (now >= sleep_deadline_)
        return 0;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(sleep_deadline_ - now).count());
}

std::string SettingsStore::GetSleepTimerString() const
{
    const int rem = GetRemainingSleepSeconds();
    if (sleep_timer_minutes_ == 0 || rem <= 0)
        return "Off";

    const int m = rem / 60;
    const int s = rem % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d min (%02d:%02d left)", sleep_timer_minutes_, m, s);
    return buf;
}

void SettingsStore::SetSleepTimerMinutes(int mins)
{
    sleep_timer_minutes_ = mins;
    if (mins > 0)
        sleep_deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(mins * 60);
    else
        sleep_deadline_ = {};

    Log("settings: sleep timer set to " + std::to_string(mins) + " minutes");
    for (auto& l : listeners_)
        l();
}

void SettingsStore::CycleSleepTimer()
{
    static const int kTimerOptions[] = {0, 15, 30, 45, 60, 90};
    int current_idx = 0;
    for (size_t i = 0; i < sizeof(kTimerOptions) / sizeof(kTimerOptions[0]); ++i)
    {
        if (kTimerOptions[i] == sleep_timer_minutes_)
        {
            current_idx = static_cast<int>(i);
            break;
        }
    }
    int next_idx = (current_idx + 1) % (sizeof(kTimerOptions) / sizeof(kTimerOptions[0]));
    SetSleepTimerMinutes(kTimerOptions[next_idx]);
}

void SettingsStore::CheckSleepTimer()
{
    if (sleep_timer_minutes_ <= 0)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now >= sleep_deadline_)
    {
        Log("settings: sleep timer expired! Stopping audio playback.");
        audio::Player::Instance().Stop();
        sleep_timer_minutes_ = 0;
        sleep_deadline_ = {};
        for (auto& l : listeners_)
            l();
    }
}

std::string SettingsStore::GetLibrarySortString() const
{
    switch (library_sort_)
    {
        case LibrarySort::RECENTLY_ADDED:
            return "Recently Added";
        case LibrarySort::OLDEST_ADDED:
            return "Oldest Added";
        case LibrarySort::TITLE_AZ:
            return "Title (A-Z)";
        case LibrarySort::ARTIST_AZ:
            return "Artist (A-Z)";
    }
    return "Recently Added";
}

void SettingsStore::SetLibrarySort(LibrarySort sort)
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        library_sort_ = sort;
    }
    Save();
    for (auto& l : listeners_)
        l();
}

void SettingsStore::CycleLibrarySort(bool forward)
{
    int cur = static_cast<int>(library_sort_);
    int next = forward ? (cur + 1) % 4 : (cur + 3) % 4;
    SetLibrarySort(static_cast<LibrarySort>(next));
}

SettingsStore::CleanResult SettingsStore::CleanTempFiles()
{
    CleanResult result {};
#ifdef __SWITCH__
    // 1. Clean orphaned audio buffers in music_dir (.part, .stream_*.part, .dl_*.part)
    const std::string music_dir = library::LibraryStore::MusicDir();
    DIR* dir = opendir(music_dir.c_str());
    if (dir)
    {
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name.size() > 5 && name.substr(name.size() - 5) == ".part")
            {
                std::string full_path = music_dir + "/" + name;
                std::remove(full_path.c_str());
                ++result.buffers_removed;
                Log("settings: removed temp audio buffer " + full_path);
            }
        }
        closedir(dir);
    }

    // 2. Clean orphaned search thumbnails in thumbs_dir
    // Gather all saved track video_ids from library.json to guarantee 100% preservation of downloaded tracks' artwork
    auto saved_tracks = library::LibraryStore::Instance().GetTracks();
    std::unordered_set<std::string> saved_ids;
    for (const auto& t : saved_tracks)
        saved_ids.insert(t.video_id);

    const std::string thumbs_dir = "sdmc:/switch/StreamSnagNX/thumbs";
    DIR* tdir = opendir(thumbs_dir.c_str());
    if (tdir)
    {
        struct dirent* entry = nullptr;
        while ((entry = readdir(tdir)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".jpg")
            {
                std::string vid = name.substr(0, name.size() - 4);
                // If it's NOT in saved_ids, it was only a temporary search thumbnail
                if (saved_ids.find(vid) == saved_ids.end())
                {
                    std::string full_path = thumbs_dir + "/" + name;
                    std::remove(full_path.c_str());
                    ++result.thumbs_removed;
                    Log("settings: removed orphaned search thumb " + full_path);
                }
            }
        }
        closedir(tdir);
    }

    fsdevCommitDevice("sdmc");
#endif
    return result;
}

void SettingsStore::RegisterListener(std::function<void()> listener)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    listeners_.push_back(std::move(listener));
}

} // namespace ssnx::settings

