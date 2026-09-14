// AudrenEngine — audout wrapper using sys-tune's proven pattern.
//
// Key differences from the old audren-based version:
//   - Static, page-aligned PCM pool in BSS (not heap-allocated).
//     Mirrors sys-tune: alignas(0x1000) s16 AudioMemoryPool[N][SIZE]
//   - SubmitBuffer() calls audoutWaitPlayFinish() when all buffers are busy,
//     instead of spinning on CanAcceptBuffer(). This is exactly what sys-tune
//     does in its PlayTrack() inner loop.
//   - No audoutInitialize() / audoutExit() here — main() owns those.
#include "audren_engine.hpp"
#include <cstring>
#include <algorithm>

// ── Static PCM pool ────────────────────────────────────────────────────────
// Page-aligned, lives in BSS. Same pattern as sys-tune's AudioMemoryPool.
// Size: 4 buffers × ceil(5760 samples × 2 ch × 2 bytes, 0x1000) = 4 × 0x6000 = 96 KB BSS.
namespace {
    constexpr size_t kBufBytes =
        (AudrenEngine::kMaxFrameSamples * AudrenEngine::kChannels * sizeof(s16) + 0xFFF) & ~0xFFF;

    alignas(0x1000) s16 s_pool[AudrenEngine::kNumBufs][kBufBytes / sizeof(s16)];
    AudioOutBuffer      s_bufs[AudrenEngine::kNumBufs];
    bool                s_pool_wired = false; // set up once, survives Exit/re-Initialize
}

AudrenEngine::AudrenEngine()  = default;
AudrenEngine::~AudrenEngine() { Exit(); }

bool AudrenEngine::Initialize(uint32_t sample_rate, uint32_t channels)
{
    // audoutInitialize() was called in main() — verify the session format.
    if (audoutGetSampleRate()   != sample_rate ||
        audoutGetChannelCount() != channels    ||
        audoutGetPcmFormat()    != PcmFormat_Int16)
        return false;

    // Wire static pool to AudioOutBuffer descriptors (done once).
    if (!s_pool_wired)
    {
        for (size_t i = 0; i < kNumBufs; ++i)
        {
            s_bufs[i]             = {};
            s_bufs[i].buffer      = s_pool[i];
            s_bufs[i].buffer_size = kBufBytes;
        }
        s_pool_wired = true;
    }

    audoutSetAudioOutVolume(volume_);
    inited_ = true;
    return true;
}

void AudrenEngine::Exit()
{
    if (!inited_) return;
    audoutStopAudioOut();
    bool dummy = false;
    audoutFlushAudioOutBuffers(&dummy);
    inited_  = false;
    playing_ = false;
}

bool AudrenEngine::SubmitBuffer(const int16_t* pcm, size_t sample_count)
{
    if (!inited_ || !pcm || !sample_count) return false;

    sample_count = std::min(sample_count, kMaxFrameSamples);
    const size_t bytes = sample_count * kChannels * sizeof(s16);

    // Find a buffer not currently queued in audout.
    AudioOutBuffer* buf = nullptr;
    for (size_t i = 0; i < kNumBufs; ++i)
    {
        bool queued = false;
        audoutContainsAudioOutBuffer(&s_bufs[i], &queued);
        if (!queued) { buf = &s_bufs[i]; break; }
    }

    // All buffers busy — wait for audout to release one, in short slices
    // so a track switch / stop / seek (abort flag) interrupts quickly
    // instead of wedging the worker. On sustained backpressure the packet
    // is dropped (tiny glitch) and the worker stays responsive.
    if (!buf)
    {
        for (int i = 0; i < 100 && !buf; ++i)
        {
            if (abort_ && abort_->load())
                return false;
            u32 cnt = 0;
            AudioOutBuffer* rel = nullptr;
            if (R_SUCCEEDED(audoutWaitPlayFinish(&rel, &cnt, 20000000ULL)) && rel)
                buf = rel;
        }
        if (!buf)
            return false;
    }

    std::memcpy(buf->buffer, pcm, bytes);
    armDCacheFlush(buf->buffer, bytes);
    buf->data_size   = bytes;
    buf->data_offset = 0;

    return R_SUCCEEDED(audoutAppendAudioOutBuffer(buf));
}

void AudrenEngine::Start()
{
    if (!inited_) return;
    AudioOutState state;
    if (R_SUCCEEDED(audoutGetAudioOutState(&state)) && state == AudioOutState_Stopped)
        audoutStartAudioOut();
    playing_ = true;
}

void AudrenEngine::Stop()
{
    if (!inited_) return;
    audoutStopAudioOut();
    bool dummy = false;
    audoutFlushAudioOutBuffers(&dummy);
    playing_ = false;
}

void AudrenEngine::Pause()
{
    if (!inited_) return;
    audoutStopAudioOut();
    playing_ = false;
}

void AudrenEngine::Resume()
{
    if (!inited_) return;
    AudioOutState state;
    if (R_SUCCEEDED(audoutGetAudioOutState(&state)) && state == AudioOutState_Stopped)
        audoutStartAudioOut();
    playing_ = true;
}

void AudrenEngine::Flush()
{
    if (!inited_) return;
    audoutStopAudioOut();
    bool dummy = false;
    audoutFlushAudioOutBuffers(&dummy);
    if (playing_)
        audoutStartAudioOut();
}

void AudrenEngine::SetVolume(float v)
{
    volume_ = std::clamp(v, 0.0f, 1.0f);
    if (inited_) audoutSetAudioOutVolume(volume_);
}

bool AudrenEngine::IsPlaying() const
{
    if (!inited_ || !playing_) return false;
    AudioOutState state;
    return R_SUCCEEDED(audoutGetAudioOutState(const_cast<AudioOutState*>(&state)))
        && state == AudioOutState_Started;
}

bool AudrenEngine::HasQueuedBuffers() const
{
    if (!inited_)
        return false;
    for (size_t i = 0; i < kNumBufs; ++i)
    {
        bool queued = false;
        audoutContainsAudioOutBuffer(&s_bufs[i], &queued);
        if (queued)
            return true;
    }
    return false;
}
