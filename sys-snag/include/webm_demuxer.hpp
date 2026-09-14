#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct OpusPacket {
    std::vector<uint8_t> data;
    uint32_t timestamp_ms = 0;
};

enum class DemuxerType {
    UNKNOWN,
    WEBM,
    OGG
};

class AudioDemuxer {
public:
    AudioDemuxer();
    ~AudioDemuxer();

    bool Open(const std::string& path);
    void Close();

    bool ReadPacket(OpusPacket& out_packet);
    bool Seek(uint32_t timestamp_ms);

    bool IsOpen() const { return fp_ != nullptr; }
    DemuxerType GetType() const { return type_; }
    uint32_t GetDurationMs() const { return duration_ms_; }
    uint32_t GetSampleRate() const { return sample_rate_; }
    uint32_t GetChannels() const { return channels_; }

private:
    FILE* fp_ = nullptr;
    DemuxerType type_ = DemuxerType::UNKNOWN;
    uint32_t duration_ms_ = 0;
    uint32_t sample_rate_ = 48000;
    uint32_t channels_ = 2;
    long file_size_ = 0;

    // WebM state
    uint64_t timecode_scale_ns_ = 1000000; // 1 ms
    uint64_t current_cluster_timecode_ms_ = 0;
    long first_cluster_pos_ = 0;
    uint64_t audio_track_number_ = 1;

    bool InitWebm();
    bool ReadWebmPacket(OpusPacket& out_packet);
    bool SeekWebm(uint32_t timestamp_ms);

    uint64_t ReadVint(int* out_len = nullptr, bool mask = true);
    uint32_t ReadEbmlId(int* out_len = nullptr);
    uint64_t ReadUint(size_t size);
    float ReadFloat(size_t size);

    // Ogg state
    long first_ogg_page_pos_ = 0;
    std::vector<uint8_t> ogg_page_buffer_;
    size_t ogg_page_offset_ = 0;
    std::vector<uint8_t> ogg_segment_table_;
    size_t ogg_segment_idx_ = 0;

    bool InitOgg();
    bool ReadOggPacket(OpusPacket& out_packet);
    bool SeekOgg(uint32_t timestamp_ms);
};

