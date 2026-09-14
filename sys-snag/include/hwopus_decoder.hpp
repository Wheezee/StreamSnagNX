#pragma once
#include <switch.h>
#include <cstdint>
#include <vector>

class HwopusEngine {
public:
    HwopusEngine();
    ~HwopusEngine();

    bool Initialize(uint32_t sample_rate = 48000, uint32_t channels = 2);
    void Exit();

    // Decodes an Opus packet into interleaved 16-bit PCM.
    // pcm_out should hold at least 5760 samples (120ms max Opus frame * channels).
    // Returns number of decoded samples per channel, or negative on error.
    int Decode(const uint8_t* packet_data, size_t packet_size, int16_t* pcm_out, size_t max_samples);

    bool IsInitialized() const { return initialized_; }

private:
    HwopusDecoder decoder_{};
    bool initialized_ = false;
    std::vector<uint8_t> input_buffer_;
};

