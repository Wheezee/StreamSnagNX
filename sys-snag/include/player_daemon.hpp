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

    // Fire-and-forget open request (safe from any thread, including worker).
    void RequestPlayFile(const std::string& path, const std::string& title, const std::string& artist);
    // Blocks (IPC threads only, never worker) until the worker opens it.
    bool WaitPlayAck(uint32_t ticket, int timeout_ms);

    Thread worker_thread_{};
    alignas(0x1000) uint8_t thread_stack_[0x8000]{};

    AudioDemuxer demuxer_; // WORKER-OWNED: only WorkerLoop touches it.
    HwopusEngine hwopus_;
    AudrenEngine audren_;

    Mutex mutex_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<int64_t> seek_requested_ms_{-1};

    // Play handoff: IPC threads post, worker consumes. Tickets pair each
    // waiter with its own completion (mashing Next just overwrites pending).
    std::atomic<bool> play_requested_{false};
    std::atomic<uint32_t> play_seq_{0};
    std::atomic<uint32_t> play_done_{0};
    std::atomic<bool> play_ok_{false};
    // Pokes the worker out of a blocking audio wait (checked each 20 ms).
    std::atomic<bool> abort_wait_{false};

    std::string current_path_;
    std::string current_title_;
    std::string current_artist_;
    uint32_t current_position_ms_ = 0;
    uint32_t duration_ms_ = 0;

    // Pending open request (written under mutex_, consumed by worker).
    std::string pending_path_;
    std::string pending_title_;
    std::string pending_artist_;

    std::vector<std::string> queue_;
    int queue_index_ = -1;
};

