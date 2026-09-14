#pragma once
#include <switch.h>
#include <cstdint>
#include <array>

// Audio output engine wrapping audout — sys-tune's proven pattern.
//
// Rules:
//   - audoutInitialize() is called ONCE in main() before this is used.
//   - PCM pool is STATIC BSS (page-aligned), no heap allocation.
//   - SubmitBuffer() uses audoutWaitPlayFinish() to block when all
//     buffers are busy — same as sys-tune. No polling.
//   - audoutExit() is called in main() at shutdown, NOT here.
class AudrenEngine
{
public:
    static constexpr size_t kMaxFrameSamples = 5760; // 120ms at 48 kHz
    static constexpr size_t kChannels        = 2;
    static constexpr size_t kNumBufs         = 4;

    AudrenEngine();
    ~AudrenEngine();

    // Verify audout format and wire up the static PCM pool.
    // audoutInitialize() must already have been called by main().
    bool Initialize(uint32_t sample_rate, uint32_t channels);
    void Exit();

    // Copy pcm[0..sample_count-1] into a free buffer and append to audout.
    // If all buffers are busy, BLOCKS on audoutWaitPlayFinish — sys-tune pattern.
    bool SubmitBuffer(const int16_t* pcm, size_t sample_count);

    // Always returns true — blocking is handled inside SubmitBuffer.
    // Kept so WorkerLoop compiles unchanged.
    bool CanAcceptBuffer() const { return inited_; }

    void Start();    // audoutStartAudioOut if not already started
    void Stop();     // audoutStopAudioOut + flush
    void Pause();    // audoutStopAudioOut (buffers stay; resumed on Resume())
    void Resume();   // audoutStartAudioOut
    void Flush();    // stop + flush all queued buffers
    void Update() {} // no-op: no audrvUpdate needed with audout

    void     SetVolume(float v);
    float    GetVolume() const { return volume_; }
    bool     IsPlaying() const;
    uint32_t GetPlayedSampleCount() const { return 0; }
    bool     IsInitialized() const { return inited_; }

private:
    bool  inited_    = false;
    bool  playing_   = false;
    float volume_    = 1.0f;
};
