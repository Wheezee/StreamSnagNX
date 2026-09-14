#include "player_daemon.hpp"
#include "snag_log.h"
#include <dirent.h>
#include <cstring>
#include <strings.h>
#include <algorithm>

namespace {

// Library video_id set (bounded read, whitespace-tolerant — library.json
// is pretty-printed). Empty = no usable library (caller fails open).
std::vector<std::string> LoadLibraryIds()
{
    std::vector<std::string> ids;
    FILE* fp = fopen("sdmc:/switch/StreamSnagNX/library.json", "rb");
    if (!fp)
        return ids;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 256 * 1024)
    {
        fclose(fp);
        return ids;
    }
    std::string data(static_cast<size_t>(sz), '\0');
    if (fread(&data[0], 1, static_cast<size_t>(sz), fp) != static_cast<size_t>(sz))
    {
        fclose(fp);
        return ids;
    }
    fclose(fp);

    const std::string qk = "\"video_id\"";
    size_t search = 0;
    while ((search = data.find(qk, search)) != std::string::npos)
    {
        size_t p = search + qk.size();
        while (p < data.size() && (data[p] == ' ' || data[p] == '\t' || data[p] == '\n' || data[p] == '\r'))
            p++;
        if (p >= data.size() || data[p] != ':')
        {
            search += qk.size();
            continue;
        }
        p++;
        while (p < data.size() && (data[p] == ' ' || data[p] == '\t' || data[p] == '\n' || data[p] == '\r'))
            p++;
        if (p >= data.size() || data[p] != '"')
        {
            search += qk.size();
            continue;
        }
        p++;
        size_t e = data.find('"', p);
        if (e == std::string::npos)
            break;
        if (e > p)
            ids.push_back(data.substr(p, e - p));
        search = e + 1;
    }
    return ids;
}

} // namespace

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

    bool hw_ok = hwopus_.Initialize(48000, 2);
    SnagLog("hwopus init=%d", (int)hw_ok);
    if (!hw_ok)
        return false;

    bool au_ok = audren_.Initialize(48000, 2);
    SnagLog("audren init=%d", (int)au_ok);
    if (!au_ok)
    {
        hwopus_.Exit();
        return false;
    }
    audren_.SetAbortFlag(&abort_wait_);

    running_.store(true);

    Result rc = threadCreate(&worker_thread_, ThreadEntry, this, thread_stack_, sizeof(thread_stack_), 0x2B, 3);
    SnagLog("worker threadCreate rc=0x%08X", rc);
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
        play_requested_.store(false);
        abort_wait_.store(true);
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

    // Library filter: only files whose video_id is in library.json enter
    // the queue (hides caches/partials). Empty set = no usable library,
    // fail open so music never bricks.
    const std::vector<std::string> lib_ids = LoadLibraryIds();
    const bool filter = !lib_ids.empty();

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
                if (filter)
                {
                    std::string vid(ent->d_name, len);
                    size_t dot = vid.rfind('.');
                    if (dot != std::string::npos)
                        vid = vid.substr(0, dot);
                    if (std::find(lib_ids.begin(), lib_ids.end(), vid) == lib_ids.end())
                        continue; // not in library: cache/orphan, hidden
                }
                queue_.push_back(std::string("sdmc:/switch/StreamSnagNX/music/") + ent->d_name);
            }
        }
        closedir(dir);
        std::sort(queue_.begin(), queue_.end());
    }
    SnagLog("scan music: %u tracks (lib filter %s)", (unsigned)queue_.size(), filter ? "on" : "off");
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

void PlayerDaemon::RequestPlayFile(const std::string& path, const std::string& title, const std::string& artist)
{
    mutexLock(&mutex_);
    pending_path_ = path;
    pending_title_ = title;
    pending_artist_ = artist;
    stop_requested_.store(true);
    play_requested_.store(true);
    abort_wait_.store(true);
    mutexUnlock(&mutex_);
}

bool PlayerDaemon::WaitPlayAck(uint32_t ticket, int timeout_ms)
{
    int tries = timeout_ms / 5;
    for (int i = 0; i < tries; ++i)
    {
        if (play_done_.load() >= ticket)
            return play_ok_.load();
        svcSleepThread(5000000); // 5ms
    }
    return false;
}

bool PlayerDaemon::PlayFile(const std::string& path, const std::string& title, const std::string& artist)
{
    if (!running_.load() && !Initialize())
        return false;

    if (fsdevGetDeviceFileSystem("sdmc") == nullptr)
    {
        fsdevMountSdmc();
    }

    // Post the open request for the worker (the ONLY thread that touches
    // the demuxer) and wait for it to finish opening.
    uint32_t ticket = play_seq_.load() + 1;
    play_seq_.store(ticket);
    RequestPlayFile(path, title, artist);
    bool ok = WaitPlayAck(ticket, 3000);
    if (!ok)
        SnagLog("PlayFile TIMEOUT/FAIL path=%.160s", path.c_str());
    return ok;
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
        // Fire-and-forget: safe from any thread, status visible via GetStatus.
        RequestPlayFile(path, "", "");
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
    play_requested_.store(false); // cancel a pending open too
    abort_wait_.store(true);
}

void PlayerDaemon::Seek(uint32_t timestamp_ms)
{
    if (playing_.load())
    {
        seek_requested_ms_.store(timestamp_ms);
        abort_wait_.store(true);
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
        RequestPlayFile(next_path, "", "");
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
        RequestPlayFile(prev_path, "", "");
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
        // Stop request: worker-exclusive demuxer Close.
        if (stop_requested_.exchange(false))
        {
            audren_.Stop();
            audren_.Flush();
            demuxer_.Close();
            playing_.store(false);
            paused_.store(false);
            mutexLock(&mutex_);
            current_position_ms_ = 0;
            mutexUnlock(&mutex_);
        }

        // Play request: worker-exclusive demuxer Open.
        if (play_requested_.exchange(false))
        {
            std::string p, t, a;
            mutexLock(&mutex_);
            p = pending_path_;
            t = pending_title_;
            a = pending_artist_;
            mutexUnlock(&mutex_);

            uint32_t ticket = play_seq_.load();
            bool opened = demuxer_.Open(p);
            if (opened)
            {
                mutexLock(&mutex_);
                current_path_ = p;
                current_title_ = t.empty() ? p.substr(p.find_last_of("/\\") + 1) : t;
                current_artist_ = a;
                current_position_ms_ = 0;
                duration_ms_ = demuxer_.GetDurationMs();

                // Find in queue or append
                auto it = std::find(queue_.begin(), queue_.end(), p);
                if (it != queue_.end())
                    queue_index_ = static_cast<int>(std::distance(queue_.begin(), it));
                else
                {
                    queue_.push_back(p);
                    queue_index_ = static_cast<int>(queue_.size() - 1);
                }

                paused_.store(false);
                playing_.store(true);
                mutexUnlock(&mutex_);
                audren_.Start();
                SnagLog("now playing dur=%ums queue=%u idx=%d path=%.160s",
                    duration_ms_, (unsigned)queue_.size(), queue_index_, p.c_str());
            }
            else
            {
                SnagLog("demux Open FAIL path=%.200s", p.c_str());
                playing_.store(false);
            }
            play_ok_.store(opened);
            play_done_.store(ticket);
        }

        // Recompute the audio-wait abort from still-pending requests.
        abort_wait_.store(stop_requested_.load() || play_requested_.load() ||
                          seek_requested_ms_.load() >= 0);

        int64_t seek_ms = seek_requested_ms_.exchange(-1);
        if (seek_ms >= 0 && playing_.load())
        {
            audren_.Flush();
            demuxer_.Seek(static_cast<uint32_t>(seek_ms));
            mutexLock(&mutex_);
            current_position_ms_ = static_cast<uint32_t>(seek_ms);
            mutexUnlock(&mutex_);
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
            mutexLock(&mutex_);
            current_position_ms_ = pkt.timestamp_ms;
            mutexUnlock(&mutex_);

            int samples = hwopus_.Decode(pkt.data.data(), pkt.data.size(), pcm_buffer, 5760);
            if (samples > 0)
            {
                audren_.SubmitBuffer(pcm_buffer, samples);
            }
            else
            {
                static int decode_errs = 0;
                if (decode_errs < 5)
                {
                    SnagLog("decode FAIL samples=%d pkt=%u", samples, (unsigned)pkt.data.size());
                    decode_errs++;
                }
            }
        }
        else
        {
            // End of track: let the queued audio finish (bounded drain),
            // then auto-advance. Never wait on the driver STATE alone: it
            // can idle in Started with no data, which wedged auto-advance
            // with position frozen just before duration end.
            for (int i = 0; i < 100 && running_.load() &&
                 !stop_requested_.load() && !play_requested_.load(); ++i)
            {
                if (!audren_.HasQueuedBuffers())
                    break;
                svcSleepThread(20000000); // 20ms
            }

            if (running_.load() && !stop_requested_.load() && !play_requested_.load())
            {
                SnagLog("track end, auto-next");
                Next(); // fire-and-forget: posts a request, never waits (worker-safe)
            }
        }
    }
}

