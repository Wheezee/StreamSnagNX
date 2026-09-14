#include "player_daemon.hpp"
#include <dirent.h>
#include <cstring>
#include <strings.h>
#include <algorithm>

PlayerDaemon& PlayerDaemon::Instance()
{
    static PlayerDaemon instance;
    return instance;
}

PlayerDaemon::PlayerDaemon()
{
    mutexInit(&mutex_);
}

PlayerDaemon::~PlayerDaemon()
{
    Exit();
}

void PlayerDaemon::ThreadEntry(void* arg)
{
    auto* self = static_cast<PlayerDaemon*>(arg);
    self->WorkerLoop();
}

bool PlayerDaemon::Initialize()
{
    if (running_.load())
        return true;

    if (!hwopus_.Initialize(48000, 2))
        return false;

    if (!audren_.Initialize(48000, 2))
    {
        hwopus_.Exit();
        return false;
    }

    running_.store(true);

    Result rc = threadCreate(&worker_thread_, ThreadEntry, this, thread_stack_, sizeof(thread_stack_), 0x2B, 3);
    if (R_FAILED(rc))
    {
        running_.store(false);
        audren_.Exit();
        hwopus_.Exit();
        return false;
    }

    rc = threadStart(&worker_thread_);
    if (R_FAILED(rc))
    {
        running_.store(false);
        threadClose(&worker_thread_);
        audren_.Exit();
        hwopus_.Exit();
        return false;
    }

    ScanMusicDir();
    return true;
}

void PlayerDaemon::Exit()
{
    if (running_.load())
    {
        running_.store(false);
        stop_requested_.store(true);
        threadWaitForExit(&worker_thread_);
        threadClose(&worker_thread_);
    }

    audren_.Exit();
    hwopus_.Exit();
    demuxer_.Close();
}

void PlayerDaemon::ScanMusicDir()
{
    if (fsdevGetDeviceFileSystem("sdmc") == nullptr)
    {
        fsdevMountSdmc();
    }

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/StreamSnagNX", 0777);
    mkdir("sdmc:/switch/StreamSnagNX/music", 0777);

    mutexLock(&mutex_);
    queue_.clear();
    queue_index_ = -1;

    DIR* dir = opendir("sdmc:/switch/StreamSnagNX/music");
    if (dir)
    {
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.')
                continue;
            const size_t len = std::strlen(ent->d_name);
            if (len > 4 && (strcasecmp(ent->d_name + len - 5, ".opus") == 0 ||
                            strcasecmp(ent->d_name + len - 5, ".webm") == 0 ||
                            strcasecmp(ent->d_name + len - 4, ".ogg") == 0 ||
                            strcasecmp(ent->d_name + len - 4, ".m4a") == 0))
            {
                queue_.push_back(std::string("sdmc:/switch/StreamSnagNX/music/") + ent->d_name);
            }
        }
        closedir(dir);
        std::sort(queue_.begin(), queue_.end());
    }
    mutexUnlock(&mutex_);
}

bool PlayerDaemon::PlayIndex(uint32_t index)
{
    mutexLock(&mutex_);
    if (queue_.empty())
    {
        mutexUnlock(&mutex_);
        ScanMusicDir();
        mutexLock(&mutex_);
    }

    if (queue_.empty() || index >= queue_.size())
    {
        mutexUnlock(&mutex_);
        return false;
    }

    std::string path = queue_[index];
    mutexUnlock(&mutex_);
    return PlayFile(path);
}

bool PlayerDaemon::PlayFile(const std::string& path, const std::string& title, const std::string& artist)
{
    if (!running_.load() && !Initialize())
        return false;

    if (fsdevGetDeviceFileSystem("sdmc") == nullptr)
    {
        fsdevMountSdmc();
    }

    mutexLock(&mutex_);
    stop_requested_.store(true);
    for (int i = 0; i < 40 && stop_requested_.load(); ++i)
    {
        svcSleepThread(5000000); // 5ms wait for worker, max 200ms
    }
    stop_requested_.store(false);

    current_path_ = path;
    current_title_ = title.empty() ? path.substr(path.find_last_of("/\\") + 1) : title;
    current_artist_ = artist;
    current_position_ms_ = 0;

    if (!demuxer_.Open(path))
    {
        mutexUnlock(&mutex_);
        return false;
    }

    duration_ms_ = demuxer_.GetDurationMs();

    // Find in queue or append
    auto it = std::find(queue_.begin(), queue_.end(), path);
    if (it != queue_.end())
        queue_index_ = static_cast<int>(std::distance(queue_.begin(), it));
    else
    {
        queue_.push_back(path);
        queue_index_ = static_cast<int>(queue_.size() - 1);
    }

    paused_.store(false);
    playing_.store(true);
    audren_.Start();
    mutexUnlock(&mutex_);
    return true;
}

void PlayerDaemon::Play()
{
    if (!running_.load() && !Initialize())
        return;

    if (playing_.load() && paused_.load())
    {
        paused_.store(false);
        audren_.Resume();
        return;
    }

    mutexLock(&mutex_);
    if (queue_.empty())
    {
        mutexUnlock(&mutex_);
        ScanMusicDir();
        mutexLock(&mutex_);
    }

    if (!queue_.empty())
    {
        if (queue_index_ < 0 || queue_index_ >= static_cast<int>(queue_.size()))
            queue_index_ = 0;
        const std::string path = queue_[queue_index_];
        mutexUnlock(&mutex_);
        PlayFile(path);
    }
    else
    {
        mutexUnlock(&mutex_);
    }
}

void PlayerDaemon::Pause()
{
    if (playing_.load() && !paused_.load())
    {
        paused_.store(true);
        audren_.Pause();
    }
}

void PlayerDaemon::Stop()
{
    stop_requested_.store(true);
}

void PlayerDaemon::Seek(uint32_t timestamp_ms)
{
    if (playing_.load())
    {
        seek_requested_ms_.store(timestamp_ms);
    }
}

void PlayerDaemon::SetVolume(float volume)
{
    audren_.SetVolume(volume);
}

void PlayerDaemon::Next()
{
    mutexLock(&mutex_);
    if (queue_.empty())
    {
        ScanMusicDir();
    }

    if (!queue_.empty())
    {
        queue_index_ = (queue_index_ + 1) % queue_.size();
        const std::string next_path = queue_[queue_index_];
        mutexUnlock(&mutex_);
        PlayFile(next_path);
    }
    else
    {
        mutexUnlock(&mutex_);
    }
}

void PlayerDaemon::Prev()
{
    mutexLock(&mutex_);
    if (current_position_ms_ > 3000)
    {
        // Restart current track
        mutexUnlock(&mutex_);
        Seek(0);
        return;
    }

    if (queue_.empty())
    {
        ScanMusicDir();
    }

    if (!queue_.empty())
    {
        queue_index_ = (queue_index_ - 1 + queue_.size()) % queue_.size();
        const std::string prev_path = queue_[queue_index_];
        mutexUnlock(&mutex_);
        PlayFile(prev_path);
    }
    else
    {
        mutexUnlock(&mutex_);
    }
}

void PlayerDaemon::GetStatus(SnagStatus& out_status)
{
    mutexLock(&mutex_);
    std::memset(&out_status, 0, sizeof(out_status));
    out_status.is_playing = (playing_.load() && !paused_.load()) ? 1 : 0;
    out_status.is_initialized = (running_.load() && hwopus_.IsInitialized()) ? 1 : 0;
    out_status.position_ms = current_position_ms_;
    out_status.duration_ms = duration_ms_;
    out_status.volume = audren_.GetVolume();
    out_status.track_index = (queue_index_ >= 0) ? static_cast<uint32_t>(queue_index_) : 0;
    out_status.track_count = static_cast<uint32_t>(queue_.size());
    std::strncpy(out_status.current_path, current_path_.c_str(), sizeof(out_status.current_path) - 1);
    std::strncpy(out_status.current_title, current_title_.c_str(), sizeof(out_status.current_title) - 1);
    std::strncpy(out_status.current_artist, current_artist_.c_str(), sizeof(out_status.current_artist) - 1);
    mutexUnlock(&mutex_);
}

void PlayerDaemon::WorkerLoop()
{
    static int16_t pcm_buffer[5760 * 2]; // 120ms max Opus frame stereo

    while (running_.load())
    {
        if (stop_requested_.exchange(false))
        {
            audren_.Stop();
            audren_.Flush();
            demuxer_.Close();
            playing_.store(false);
            paused_.store(false);
            current_position_ms_ = 0;
            continue;
        }

        int64_t seek_ms = seek_requested_ms_.exchange(-1);
        if (seek_ms >= 0 && playing_.load())
        {
            audren_.Flush();
            demuxer_.Seek(static_cast<uint32_t>(seek_ms));
            current_position_ms_ = static_cast<uint32_t>(seek_ms);
            continue;
        }

        if (!playing_.load() || paused_.load())
        {
            svcSleepThread(15000000); // 15ms
            continue;
        }

        if (!audren_.CanAcceptBuffer())
        {
            svcSleepThread(5000000); // 5ms
            continue;
        }

        OpusPacket pkt;
        if (demuxer_.ReadPacket(pkt))
        {
            current_position_ms_ = pkt.timestamp_ms;

            int samples = hwopus_.Decode(pkt.data.data(), pkt.data.size(), pcm_buffer, 5760);
            if (samples > 0)
            {
                audren_.SubmitBuffer(pcm_buffer, samples);
            }
        }
        else
        {
            // End of current track: wait for audio driver to finish playing queued buffers
            while (audren_.IsPlaying() && !stop_requested_.load() && running_.load())
            {
                svcSleepThread(20000000); // 20ms
            }

            if (running_.load() && !stop_requested_.load())
            {
                Next();
            }
        }
    }
}

