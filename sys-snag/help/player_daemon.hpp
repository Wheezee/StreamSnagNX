#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <atomic>
#include "webm_demuxer.hpp"
#include "hwopus_decoder.hpp"
#include "audren_engine.hpp"
#include "snag_ipc.hpp"

class PlayerDaemon {
public:
    static PlayerDaemon& Instance();

    bool Initialize();
    void Exit();

    bool PlayFile(const std::string& path, const std::string& title = "", const std::string& artist = "");
    bool PlayIndex(uint32_t index);
    void Play();
    void Pause();
    void Stop();
    void Seek(uint32_t timestamp_ms);
    void SetVolume(float volume);
    void Next();
    void Prev();

    void GetStatus(SnagStatus& out_status);

private:
    PlayerDaemon();
    ~PlayerDaemon();

    void WorkerLoop();
    static void ThreadEntry(void* arg);
    void ScanMusicDir();

    Thread worker_thread_{};
    alignas(0x1000) uint8_t thread_stack_[0x8000]{};

    AudioDemuxer demuxer_;
    HwopusEngine hwopus_;
    AudrenEngine audren_;

    Mutex mutex_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<int64_t> seek_requested_ms_{-1};

    std::string current_path_;
    std::string current_title_;
    std::string current_artist_;
    uint32_t current_position_ms_ = 0;
    uint32_t duration_ms_ = 0;

    std::vector<std::string> queue_;
    int queue_index_ = -1;
};

