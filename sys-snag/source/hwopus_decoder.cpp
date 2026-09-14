#include "hwopus_decoder.hpp"
#include <cstring>

HwopusEngine::HwopusEngine()
{
    input_buffer_.resize(sizeof(HwopusHeader) + 8192);
}

HwopusEngine::~HwopusEngine()
{
    Exit();
}

bool HwopusEngine::Initialize(uint32_t sample_rate, uint32_t channels)
{
    if (initialized_)
        Exit();

    Result rc = hwopusDecoderInitialize(&decoder_, sample_rate, channels);
    if (R_FAILED(rc))
        return false;

    initialized_ = true;
    return true;
}

void HwopusEngine::Exit()
{
    if (initialized_)
    {
        hwopusDecoderExit(&decoder_);
        initialized_ = false;
    }
}

int HwopusEngine::Decode(const uint8_t* packet_data, size_t packet_size, int16_t* pcm_out, size_t max_samples)
{
    if (!initialized_ || !packet_data || packet_size == 0)
        return -1;

    const size_t total_size = sizeof(HwopusHeader) + packet_size;
    if (total_size > input_buffer_.size())
        input_buffer_.resize(total_size + 4096);

    HwopusHeader* hdr = reinterpret_cast<HwopusHeader*>(input_buffer_.data());
    hdr->size = __builtin_bswap32(static_cast<uint32_t>(packet_size));
    hdr->final_range = 0;

    std::memcpy(input_buffer_.data() + sizeof(HwopusHeader), packet_data, packet_size);

    s32 decoded_data_size = 0;
    s32 decoded_samples = 0;

    Result rc = hwopusDecodeInterleaved(
        &decoder_,
        &decoded_data_size,
        &decoded_samples,
        input_buffer_.data(),
        total_size,
        pcm_out,
        max_samples * 2 * sizeof(int16_t)
    );

    if (R_FAILED(rc))
        return -1;

    return decoded_samples;
}

