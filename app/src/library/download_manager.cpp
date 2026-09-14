#include "download_manager.hpp"

#include "../lyrics/lrclib.hpp"
#include "../settings/settings_store.hpp"
#include "../util/log.hpp"
#include "../yt/innertube.hpp"
#include "../yt/sig.hpp"

#include <borealis.hpp>
#include <curl/curl.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#ifdef __SWITCH__
#include <switch.h>
#include <unistd.h>
#endif

namespace ssnx::library
{
namespace
{

size_t FileWrite(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* fp = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, fp);
}

struct DlProgressContext
{
    std::atomic<bool>* stop;
};

int DlProgress(void* userdata, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
               curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    auto* ctx = static_cast<DlProgressContext*>(userdata);
    if (ctx && ctx->stop && ctx->stop->load())
        return 1;
    return 0;
}

bool DownloadChunked(const std::string& url, const std::string& part_path,
                     const std::string& user_agent,
                     std::atomic<bool>* stop_flag,
                     std::function<void(double)> on_percent)
{
    FILE* fp = fopen(part_path.c_str(), "wb");
    if (!fp)
        return false;

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        fclose(fp);
        return false;
    }

    uint64_t total_size = 0;
    {
        const std::string key = "clen=";
        const size_t pos = url.find(key);
        if (pos != std::string::npos)
        {
            size_t end = url.find_first_of("&", pos + key.size());
            total_size = std::strtoull(
                url.substr(pos + key.size(), end == std::string::npos
                                                ? std::string::npos
                                                : end - pos - key.size())
                    .c_str(),
                nullptr, 10);
        }
    }

    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    DlProgressContext dl_ctx {stop_flag};
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, DlProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &dl_ctx);

    long http_code = 0;
    CURLcode rc = CURLE_OK;
    bool success = false;

    if (total_size > 0 && url.find("ratebypass=yes") == std::string::npos)
    {
        constexpr uint64_t kChunkBytes = 1024 * 1024;
        constexpr int kMaxRetries = 4;
        int rn = 0;
        uint64_t offset = 0;
        success = true;

        while ((!stop_flag || !stop_flag->load()) && offset < total_size)
        {
            const uint64_t chunk_end = std::min(total_size - 1, offset + kChunkBytes - 1);
            std::string chunk_url = url;
            chunk_url += (chunk_url.find('?') == std::string::npos ? "?" : "&");
            chunk_url += "range=" + std::to_string(offset) + "-" + std::to_string(chunk_end) +
                         "&rn=" + std::to_string(rn);

            const long chunk_start_pos = static_cast<long>(offset);
            bool chunk_ok = false;
            for (int retry = 0; retry < kMaxRetries && (!stop_flag || !stop_flag->load()); ++retry)
            {
                if (retry > 0)
                {
                    fseek(fp, chunk_start_pos, SEEK_SET);
                    fflush(fp);
                    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500 * retry));
                }
                else
                {
                    curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);
                }

                curl_easy_setopt(curl, CURLOPT_URL, chunk_url.c_str());
                rc = curl_easy_perform(curl);
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                if (rc == CURLE_OK && (http_code == 200 || http_code == 206))
                {
                    chunk_ok = true;
                    break;
                }
            }

            if (!chunk_ok)
            {
                success = false;
                break;
            }

            fflush(fp);
            const long pos = ftell(fp);
            if (pos > chunk_start_pos)
            {
                offset = static_cast<uint64_t>(pos);
                if (on_percent && total_size > 0)
                    on_percent(static_cast<double>(offset) / static_cast<double>(total_size));
            }
            else
            {
                success = false;
                break;
            }
            ++rn;
        }
    }
    else
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        success = (rc == CURLE_OK && (http_code == 200 || http_code == 206));
    }

    curl_easy_cleanup(curl);
    fclose(fp);

    if (!success)
        std::remove(part_path.c_str());

    return success;
}

} // namespace

DownloadManager& DownloadManager::Instance()
{
    static DownloadManager manager;
    return manager;
}

DownloadManager::~DownloadManager()
{
    Shutdown();
}

void DownloadManager::Shutdown()
{
    stop_.store(true);
}

DownloadProgress DownloadManager::GetProgress(const std::string& video_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = progress_map_.find(video_id);
    if (it != progress_map_.end())
        return it->second;
    return {};
}

bool DownloadManager::IsBusy(const std::string& video_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = progress_map_.find(video_id);
    if (it != progress_map_.end())
    {
        return it->second.state == DownloadState::RESOLVING ||
               it->second.state == DownloadState::DOWNLOADING ||
               it->second.state == DownloadState::SAVING_LYRICS ||
               it->second.state == DownloadState::SAVING_THUMB;
    }
    return false;
}

void DownloadManager::StartDownload(const Track& track,
                                   std::function<void(const DownloadProgress&)> on_progress)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (IsBusy(track.video_id))
        return;

    auto& p = progress_map_[track.video_id];
    p.state = DownloadState::RESOLVING;
    p.progress = 0.0;
    p.message = "Resolving stream...";

    brls::async([this, track, on_progress]() {
        Worker(track, on_progress);
    });
}

void DownloadManager::Worker(Track track,
                            std::function<void(const DownloadProgress&)> on_progress)
{
    auto notify = [this, track, on_progress](DownloadState state, double progress,
                                              const std::string& msg) {
        DownloadProgress p;
        p.state = state;
        p.progress = progress;
        p.message = msg;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            progress_map_[track.video_id] = p;
        }
        Log("dlmgr: progress " + track.video_id + " -> " + msg + " (" + std::to_string(progress) + ")");
        if (on_progress)
            on_progress(p);
    };

    try
    {
        Log("dlmgr: worker started for " + track.video_id + " (" + track.title + ")");
        LibraryStore::Instance().Init();

        // 1. Resolve stream
        notify(DownloadState::RESOLVING, 0.05, "Resolving stream...");
        yt::InnertubeClient client;
        yt::ResolvedAudio audio = client.ResolveAudio(track.video_id);
        if (stop_.load())
            return;
        if (audio.url.empty())
        {
            Log("dlmgr: resolve failed for " + track.video_id);
            notify(DownloadState::FAILED, 0.0, "Failed to resolve stream");
            return;
        }
        Log("dlmgr: resolved via " + audio.via);

        // 2. Solve n transform
        std::string stream_url = yt::SolveN(audio.url, track.video_id);
        if (stop_.load())
            return;

        // 3. Download audio file
        notify(DownloadState::DOWNLOADING, 0.1, "Downloading audio...");
        const std::string ext = audio.ext.empty() ? "m4a" : audio.ext;
        const std::string final_audio_path = LibraryStore::AudioPath(track.video_id, ext);
        const std::string part_audio_path = LibraryStore::MusicDir() + "/.dl_" + track.video_id + "." + ext + ".part";

        std::string download_ua = audio.user_agent;
        if (download_ua.empty())
        {
            if (stream_url.find("c=ANDROID_MUSIC") != std::string::npos)
                download_ua = "com.google.android.apps.youtube.music/8.39.42 (Linux; U; Android 15; en_US; Pixel 9 Pro; Build/AP4A.250205.002) gzip";
            else if (stream_url.find("c=ANDROID_VR") != std::string::npos)
                download_ua = "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip";
            else if (stream_url.find("c=IOS") != std::string::npos)
                download_ua = "com.google.ios.youtube/21.03.3 (iPad7,6; U; CPU iPadOS 17_7_10 like Mac OS X; en-US)";
            else
                download_ua = "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
        }

        const bool dl_ok = DownloadChunked(
            stream_url, part_audio_path, download_ua, &stop_, [notify](double pct) {
                notify(DownloadState::DOWNLOADING, 0.1 + pct * 0.7,
                       "Downloading: " + std::to_string(static_cast<int>(pct * 100)) + "%");
            });

        if (stop_.load())
            return;

        if (!dl_ok)
        {
            Log("dlmgr: audio download failed for " + track.video_id);
            notify(DownloadState::FAILED, 0.0, "Audio download failed");
            return;
        }
        std::remove(final_audio_path.c_str());
        if (std::rename(part_audio_path.c_str(), final_audio_path.c_str()) != 0)
        {
            Log("dlmgr: rename audio failed: " + std::string(strerror(errno)));
        }
#ifdef __SWITCH__
        fsdevCommitDevice("sdmc");
#endif

        // 4. Fetch & save lyrics
        std::string final_lrc_path;
        if (!stop_.load() && settings::SettingsStore::Instance().GetLyricsEnabled())
        {
            notify(DownloadState::SAVING_LYRICS, 0.85, "Saving lyrics...");
            try
            {
                const auto lyr = lyrics::LrclibClient::FetchLyrics(track.title, track.artist);
                if (lyr.is_synced || lyr.is_plain)
                {
                    final_lrc_path = LibraryStore::LrcPath(track.video_id);
                    std::ofstream lrc_file(final_lrc_path);
                    if (lrc_file.is_open())
                    {
                        if (lyr.is_synced)
                        {
                            for (const auto& line : lyr.synced)
                            {
                                const int min = static_cast<int>(line.timestamp) / 60;
                                const int sec = static_cast<int>(line.timestamp) % 60;
                                const int cs = static_cast<int>((line.timestamp - std::floor(line.timestamp)) * 100);
                                char time_tag[32];
                                std::snprintf(time_tag, sizeof(time_tag), "[%02d:%02d.%02d]", min, sec, cs);
                                lrc_file << time_tag << line.text << "\n";
                            }
                        }
                        else
                        {
                            for (const auto& line : lyr.plain)
                                lrc_file << line << "\n";
                        }
                        lrc_file.flush();
                        lrc_file.close();
#ifdef __SWITCH__
                        fsdevCommitDevice("sdmc");
#endif
                    }
                }
            }
            catch (const std::exception& e)
            {
                Log("dlmgr: lyrics exception: " + std::string(e.what()));
            }
        }

        if (stop_.load())
            return;

        // 5. Download & save thumb
        notify(DownloadState::SAVING_THUMB, 0.95, "Saving cover art...");
        const std::string final_thumb_path = LibraryStore::ThumbPath(track.video_id);
        if (!track.thumb_url.empty() && !stop_.load())
        {
            try
            {
                yt::InnertubeClient::DownloadFile(track.thumb_url, final_thumb_path);
#ifdef __SWITCH__
                fsdevCommitDevice("sdmc");
#endif
            }
            catch (const std::exception& e)
            {
                Log("dlmgr: thumb exception: " + std::string(e.what()));
            }
        }

        // 6. Save to LibraryStore
        LibraryTrack lib_track;
        lib_track.video_id = track.video_id;
        lib_track.title = track.title;
        lib_track.artist = track.artist.empty() ? track.subtitle : track.artist;
        lib_track.subtitle = track.subtitle;
        lib_track.thumb_url = track.thumb_url;
        lib_track.local_audio_path = final_audio_path;
        lib_track.local_lrc_path = final_lrc_path;
        lib_track.local_thumb_path = final_thumb_path;
        lib_track.ext = ext;
        lib_track.added_at = static_cast<uint64_t>(
            std::chrono::system_clock::now().time_since_epoch().count());

        LibraryStore::Instance().AddOrUpdateTrack(lib_track);

        Log("dlmgr: download completed successfully for " + track.video_id);
        notify(DownloadState::COMPLETED, 1.0, "Saved to Library");
    }
    catch (const std::exception& ex)
    {
        Log("dlmgr: exception in worker: " + std::string(ex.what()));
        notify(DownloadState::FAILED, 0.0, std::string("Exception: ") + ex.what());
    }
    catch (...)
    {
        Log("dlmgr: unknown exception in worker");
        notify(DownloadState::FAILED, 0.0, "Unknown exception in worker");
    }
}

} // namespace ssnx::library
