#include "queue_manager.hpp"
#include "now_playing.hpp"
#include "player.hpp"
#include "../library/library_store.hpp"
#include "../util/log.hpp"
#include "../yt/innertube.hpp"

#include <borealis.hpp>
#include <cstdio>

namespace ssnx::audio
{

QueueManager& QueueManager::Instance()
{
    static QueueManager instance;
    return instance;
}

QueueManager::QueueManager()
{
    Player::Instance().SetOnTrackEnded([]() {
        QueueManager::Instance().OnTrackEnded();
    });
    initialized_ = true;
}

void QueueManager::Init()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (initialized_)
        return;
    initialized_ = true;
    Player::Instance().SetOnTrackEnded([]() {
        QueueManager::Instance().OnTrackEnded();
    });
}

void QueueManager::SetSearchTrack(const Track& track)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    source_ = QueueSource::SEARCH;
    queue_ = {track};
    current_index_ = 0;
    playlist_id_ = "search";
    Log("queue: set search track " + track.video_id + " (" + track.title + ")");
}

void QueueManager::SetLibraryQueue(const std::vector<Track>& tracks, size_t index, const std::string& playlist_id)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    source_ = QueueSource::LIBRARY;
    queue_ = tracks;
    current_index_ = (index < queue_.size() ? index : 0);
    playlist_id_ = playlist_id;
    Log("queue: set library queue count=" + std::to_string(queue_.size()) +
        " idx=" + std::to_string(current_index_) + " pl=" + playlist_id);
    PlayCurrentLocked();
}

void QueueManager::PlayCurrentLocked()
{
    if (current_index_ >= queue_.size())
        return;

    const Track track = queue_[current_index_];
    Log("queue: playing idx=" + std::to_string(current_index_) + "/" +
        std::to_string(queue_.size()) + " " + track.video_id + " (" + track.title + ")");

    // 1. Check if local audio file exists on SD
    std::string local_path;
    const auto lib_track = library::LibraryStore::Instance().GetTrack(track.video_id);
    if (lib_track.has_value() && !lib_track->local_audio_path.empty())
    {
        FILE* fp = fopen(lib_track->local_audio_path.c_str(), "rb");
        if (fp)
        {
            fclose(fp);
            local_path = lib_track->local_audio_path;
        }
    }

    if (local_path.empty())
    {
        for (const char* ext : {"m4a", "opus"})
        {
            const std::string existing = library::LibraryStore::AudioPath(track.video_id, ext);
            FILE* fp = fopen(existing.c_str(), "rb");
            if (fp)
            {
                fclose(fp);
                local_path = existing;
                break;
            }
        }
    }

    if (!local_path.empty())
    {
        NowPlaying::Instance().Set(track, track.subtitle);
        Player::Instance().Play(local_path, track.video_id, local_path, "");
        if (on_track_change_)
            on_track_change_(track);
        return;
    }

    // 2. If not on disk, resolve and play via network
    NowPlaying::Instance().Set(track, "Resolving stream...");
    brls::async([this, track]() {
        yt::InnertubeClient client;
        yt::ResolvedAudio audio = client.ResolveAudio(track.video_id);
        brls::sync([this, track, audio]() {
            if (audio.url.empty())
            {
                NowPlaying::Instance().Set(track, "", "No audio stream");
                return;
            }
            NowPlaying::Instance().Set(track, track.subtitle);
            const std::string path = library::LibraryStore::AudioPath(track.video_id, audio.ext);
            Player::Instance().Play(audio.url, track.video_id, path, audio.user_agent);
            if (on_track_change_)
                on_track_change_(track);
        });
    });
}

void QueueManager::Next()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (source_ == QueueSource::LIBRARY)
    {
        if (current_index_ + 1 < queue_.size())
        {
            current_index_++;
            PlayCurrentLocked();
        }
        else
        {
            Log("queue: reached end of library queue");
            Player::Instance().Stop();
        }
    }
    else
    {
        Log("queue: single track Next -> stopping");
        Player::Instance().Stop();
    }
}

void QueueManager::Previous()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (Player::Instance().Position() > 3.0)
    {
        Player::Instance().SeekTo(0.0);
        return;
    }

    if (source_ == QueueSource::LIBRARY && current_index_ > 0)
    {
        current_index_--;
        PlayCurrentLocked();
    }
    else
    {
        Player::Instance().SeekTo(0.0);
    }
}

void QueueManager::OnTrackEnded()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Log("queue: track ended naturally (source=" + std::to_string(static_cast<int>(source_)) + ")");
    if (source_ == QueueSource::LIBRARY)
    {
        if (current_index_ + 1 < queue_.size())
        {
            current_index_++;
            Log("queue: auto-advancing to idx=" + std::to_string(current_index_));
            PlayCurrentLocked();
        }
        else
        {
            Log("queue: auto-advance reached end of playlist");
            Player::Instance().Stop();
        }
    }
    else
    {
        Log("queue: search track ended -> stop");
        Player::Instance().Stop();
    }
}

bool QueueManager::HasNext() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return (source_ == QueueSource::LIBRARY && current_index_ + 1 < queue_.size());
}

bool QueueManager::HasPrev() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return (source_ == QueueSource::LIBRARY && current_index_ > 0);
}

QueueSource QueueManager::GetSource() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return source_;
}

size_t QueueManager::GetCurrentIndex() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return current_index_;
}

size_t QueueManager::GetQueueSize() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return queue_.size();
}

Track QueueManager::GetCurrentTrack() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (current_index_ < queue_.size())
        return queue_[current_index_];
    return {};
}

std::string QueueManager::GetPlaylistId() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return playlist_id_;
}

void QueueManager::SetOnTrackChange(std::function<void(const Track& track)> cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    on_track_change_ = std::move(cb);
}

} // namespace ssnx::audio
