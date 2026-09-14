#include "webm_demuxer.hpp"
#include <cstring>
#include <cmath>

AudioDemuxer::AudioDemuxer() = default;

AudioDemuxer::~AudioDemuxer()
{
    Close();
}

void AudioDemuxer::Close()
{
    if (fp_)
    {
        fclose(fp_);
        fp_ = nullptr;
    }
    type_ = DemuxerType::UNKNOWN;
    duration_ms_ = 0;
    ogg_page_buffer_.clear();
    ogg_segment_table_.clear();
    ogg_page_offset_ = 0;
    ogg_segment_idx_ = 0;
}

bool AudioDemuxer::Open(const std::string& path)
{
    Close();

    fp_ = fopen(path.c_str(), "rb");
    if (!fp_)
        return false;

    fseek(fp_, 0, SEEK_END);
    file_size_ = ftell(fp_);
    fseek(fp_, 0, SEEK_SET);

    if (file_size_ < 16)
    {
        Close();
        return false;
    }

    uint8_t magic[4] = {};
    if (fread(magic, 1, 4, fp_) != 4)
    {
        Close();
        return false;
    }
    fseek(fp_, 0, SEEK_SET);

    // Check EBML header for WebM (0x1A 0x45 0xDF 0xA3)
    if (magic[0] == 0x1A && magic[1] == 0x45 && magic[2] == 0xDF && magic[3] == 0xA3)
    {
        type_ = DemuxerType::WEBM;
        if (!InitWebm())
        {
            Close();
            return false;
        }
        return true;
    }

    // Check Ogg header ("OggS" = 0x4F 0x67 0x67 0x53)
    if (magic[0] == 0x4F && magic[1] == 0x67 && magic[2] == 0x67 && magic[3] == 0x53)
    {
        type_ = DemuxerType::OGG;
        if (!InitOgg())
        {
            Close();
            return false;
        }
        return true;
    }

    Close();
    return false;
}

uint32_t AudioDemuxer::ReadEbmlId(int* out_len)
{
    int c = fgetc(fp_);
    if (c == EOF)
        return 0;

    uint8_t b = static_cast<uint8_t>(c);
    int len = 0;
    if (b & 0x80) len = 1;
    else if (b & 0x40) len = 2;
    else if (b & 0x20) len = 3;
    else if (b & 0x10) len = 4;
    else return 0;

    if (out_len)
        *out_len = len;

    uint32_t id = b;
    for (int i = 1; i < len; ++i)
    {
        c = fgetc(fp_);
        if (c == EOF)
            return 0;
        id = (id << 8) | static_cast<uint8_t>(c);
    }
    return id;
}

uint64_t AudioDemuxer::ReadVint(int* out_len, bool mask)
{
    int c = fgetc(fp_);
    if (c == EOF)
        return 0;

    uint8_t b = static_cast<uint8_t>(c);
    int len = 0;
    for (int i = 0; i < 8; ++i)
    {
        if (b & (0x80 >> i))
        {
            len = i + 1;
            break;
        }
    }
    if (len == 0)
        return 0;

    if (out_len)
        *out_len = len;

    uint64_t val = mask ? (b & ~(0x80 >> (len - 1))) : b;
    for (int i = 1; i < len; ++i)
    {
        c = fgetc(fp_);
        if (c == EOF)
            return 0;
        val = (val << 8) | static_cast<uint8_t>(c);
    }
    return val;
}

uint64_t AudioDemuxer::ReadUint(size_t size)
{
    uint64_t val = 0;
    for (size_t i = 0; i < size; ++i)
    {
        int c = fgetc(fp_);
        if (c == EOF)
            break;
        val = (val << 8) | static_cast<uint8_t>(c);
    }
    return val;
}

float AudioDemuxer::ReadFloat(size_t size)
{
    if (size == 4)
    {
        uint32_t val = static_cast<uint32_t>(ReadUint(4));
        float f;
        std::memcpy(&f, &val, 4);
        return f;
    }
    if (size == 8)
    {
        uint64_t val = ReadUint(8);
        double d;
        std::memcpy(&d, &val, 8);
        return static_cast<float>(d);
    }
    return 0.0f;
}

bool AudioDemuxer::InitWebm()
{
    fseek(fp_, 0, SEEK_SET);

    // Read EBML header
    uint32_t id = ReadEbmlId();
    if (id != 0x1A45DFA3) // EBML Header
        return false;
    uint64_t size = ReadVint();
    fseek(fp_, size, SEEK_CUR);

    // Search for Segment (0x18538067)
    id = ReadEbmlId();
    if (id != 0x18538067)
        return false;
    (void)ReadVint(); // Segment size (often unknown)

    // Scan Segment elements: Info (0x1549A966), Tracks (0x1654AE6B), and stop at first Cluster (0x1F43B675)
    while (ftell(fp_) < file_size_)
    {
        long elem_start = ftell(fp_);
        id = ReadEbmlId();
        if (id == 0)
            break;

        size = ReadVint();

        if (id == 0x1549A966) // Info
        {
            long info_end = ftell(fp_) + size;
            while (ftell(fp_) < info_end)
            {
                uint32_t sub_id = ReadEbmlId();
                if (sub_id == 0) break;
                uint64_t sub_size = ReadVint();
                if (sub_id == 0x2AD7B1) // TimecodeScale
                {
                    timecode_scale_ns_ = ReadUint(sub_size);
                    if (timecode_scale_ns_ == 0) timecode_scale_ns_ = 1000000;
                }
                else if (sub_id == 0x4489) // Duration
                {
                    float dur = ReadFloat(sub_size);
                    duration_ms_ = static_cast<uint32_t>((dur * timecode_scale_ns_) / 1000000.0f);
                }
                else
                {
                    fseek(fp_, sub_size, SEEK_CUR);
                }
            }
        }
        else if (id == 0x1654AE6B) // Tracks
        {
            long tracks_end = ftell(fp_) + size;
            while (ftell(fp_) < tracks_end)
            {
                uint32_t sub_id = ReadEbmlId();
                if (sub_id == 0) break;
                uint64_t sub_size = ReadVint();
                if (sub_id == 0xAE) // TrackEntry
                {
                    long track_end = ftell(fp_) + sub_size;
                    uint64_t track_num = 1;
                    uint64_t track_type = 0;
                    while (ftell(fp_) < track_end)
                    {
                        uint32_t prop_id = ReadEbmlId();
                        if (prop_id == 0) break;
                        uint64_t prop_size = ReadVint();
                        if (prop_id == 0xD7) // TrackNumber
                            track_num = ReadUint(prop_size);
                        else if (prop_id == 0x83) // TrackType (2 = audio)
                            track_type = ReadUint(prop_size);
                        else
                            fseek(fp_, prop_size, SEEK_CUR);
                    }
                    if (track_type == 2)
                    {
                        audio_track_number_ = track_num;
                    }
                }
                else
                {
                    fseek(fp_, sub_size, SEEK_CUR);
                }
            }
        }
        else if (id == 0x1F43B675) // First Cluster
        {
            first_cluster_pos_ = elem_start;
            fseek(fp_, elem_start, SEEK_SET);
            break;
        }
        else
        {
            // Skip unknown element (e.g. SeekHead)
            fseek(fp_, size, SEEK_CUR);
        }
    }

    if (first_cluster_pos_ == 0)
        first_cluster_pos_ = ftell(fp_);

    return true;
}

bool AudioDemuxer::ReadWebmPacket(OpusPacket& out_packet)
{
    while (ftell(fp_) < file_size_)
    {
        uint32_t id = ReadEbmlId();
        if (id == 0)
            return false;

        uint64_t size = ReadVint();

        if (id == 0x1F43B675) // Cluster
        {
            // Inside cluster, continue reading children
            continue;
        }
        else if (id == 0xE7) // Cluster Timecode
        {
            uint64_t timecode = ReadUint(size);
            current_cluster_timecode_ms_ = (timecode * timecode_scale_ns_) / 1000000;
        }
        else if (id == 0xA3 || id == 0xA1) // SimpleBlock or Block
        {
            long block_start = ftell(fp_);
            int track_len = 0;
            uint64_t track_num = ReadVint(&track_len);

            int c1 = fgetc(fp_);
            int c2 = fgetc(fp_);
            if (c1 == EOF || c2 == EOF)
                return false;

            int16_t block_timecode = static_cast<int16_t>((c1 << 8) | c2);
            int flags = fgetc(fp_);
            if (flags == EOF)
                return false;

            long header_len = ftell(fp_) - block_start;
            long payload_size = static_cast<long>(size) - header_len;

            if (payload_size <= 0)
                continue;

            if (track_num == audio_track_number_)
            {
                out_packet.data.resize(payload_size);
                if (fread(out_packet.data.data(), 1, payload_size, fp_) != static_cast<size_t>(payload_size))
                    return false;

                out_packet.timestamp_ms = static_cast<uint32_t>(
                    static_cast<int64_t>(current_cluster_timecode_ms_) + block_timecode
                );
                return true;
            }
            else
            {
                fseek(fp_, payload_size, SEEK_CUR);
            }
        }
        else
        {
            // Skip other elements
            if (size > 0 && size != UINT64_MAX)
                fseek(fp_, size, SEEK_CUR);
        }
    }
    return false;
}

bool AudioDemuxer::SeekWebm(uint32_t timestamp_ms)
{
    if (timestamp_ms == 0 || duration_ms_ == 0)
    {
        fseek(fp_, first_cluster_pos_, SEEK_SET);
        current_cluster_timecode_ms_ = 0;
        return true;
    }

    // Proportional seek estimate
    double ratio = static_cast<double>(timestamp_ms) / static_cast<double>(duration_ms_);
    if (ratio > 1.0) ratio = 1.0;

    long estimated_pos = first_cluster_pos_ + static_cast<long>((file_size_ - first_cluster_pos_) * ratio);
    estimated_pos = std::max(first_cluster_pos_, estimated_pos - 65536);
    fseek(fp_, estimated_pos, SEEK_SET);

    // Scan for next Cluster header: 0x1F 0x43 0xB6 0x75
    uint8_t win[4] = {};
    while (ftell(fp_) < file_size_)
    {
        win[0] = win[1];
        win[1] = win[2];
        win[2] = win[3];
        int c = fgetc(fp_);
        if (c == EOF) break;
        win[3] = static_cast<uint8_t>(c);

        if (win[0] == 0x1F && win[1] == 0x43 && win[2] == 0xB6 && win[3] == 0x75)
        {
            fseek(fp_, -4, SEEK_CUR);
            return true;
        }
    }

    // Fallback to beginning
    fseek(fp_, first_cluster_pos_, SEEK_SET);
    return true;
}

// ------------------- OGG IMPLEMENTATION -------------------

bool AudioDemuxer::InitOgg()
{
    fseek(fp_, 0, SEEK_SET);
    // Find first OpusHead page
    uint8_t header[27] = {};
    if (fread(header, 1, 27, fp_) != 27)
        return false;

    if (std::memcmp(header, "OggS", 4) != 0)
        return false;

    uint8_t num_segments = header[26];
    std::vector<uint8_t> segs(num_segments);
    if (fread(segs.data(), 1, num_segments, fp_) != num_segments)
        return false;

    first_ogg_page_pos_ = ftell(fp_);

    // Check OpusHead
    uint8_t head_magic[8] = {};
    if (fread(head_magic, 1, 8, fp_) == 8 && std::memcmp(head_magic, "OpusHead", 8) == 0)
    {
        sample_rate_ = 48000;
        channels_ = 2;
    }

    fseek(fp_, first_ogg_page_pos_, SEEK_SET);
    return true;
}

bool AudioDemuxer::ReadOggPacket(OpusPacket& out_packet)
{
    while (ftell(fp_) < file_size_ || ogg_segment_idx_ < ogg_segment_table_.size())
    {
        if (ogg_segment_idx_ >= ogg_segment_table_.size())
        {
            // Read next OggS page
            uint8_t header[27] = {};
            if (fread(header, 1, 27, fp_) != 27)
                return false;

            if (std::memcmp(header, "OggS", 4) != 0)
                return false;

            uint64_t granulepos = 0;
            std::memcpy(&granulepos, &header[6], 8);
            if (granulepos != UINT64_MAX)
            {
                out_packet.timestamp_ms = static_cast<uint32_t>(granulepos / 48);
            }

            uint8_t num_segments = header[26];
            ogg_segment_table_.resize(num_segments);
            if (fread(ogg_segment_table_.data(), 1, num_segments, fp_) != num_segments)
                return false;

            ogg_segment_idx_ = 0;
        }

        // Assemble packet from lacing values
        out_packet.data.clear();
        while (ogg_segment_idx_ < ogg_segment_table_.size())
        {
            uint8_t seg_len = ogg_segment_table_[ogg_segment_idx_++];
            if (seg_len > 0)
            {
                size_t old_size = out_packet.data.size();
                out_packet.data.resize(old_size + seg_len);
                if (fread(out_packet.data.data() + old_size, 1, seg_len, fp_) != seg_len)
                    return false;
            }
            if (seg_len < 255)
            {
                // Completed packet
                if (!out_packet.data.empty())
                {
                    // Skip OpusHead / OpusTags headers
                    if (out_packet.data.size() >= 8 &&
                        (std::memcmp(out_packet.data.data(), "OpusHead", 8) == 0 ||
                         std::memcmp(out_packet.data.data(), "OpusTags", 8) == 0))
                    {
                        break; // Loop around to read real audio packet
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

bool AudioDemuxer::SeekOgg(uint32_t timestamp_ms)
{
    fseek(fp_, first_ogg_page_pos_, SEEK_SET);
    ogg_segment_table_.clear();
    ogg_segment_idx_ = 0;
    return true;
}

bool AudioDemuxer::ReadPacket(OpusPacket& out_packet)
{
    if (!fp_)
        return false;

    if (type_ == DemuxerType::WEBM)
        return ReadWebmPacket(out_packet);
    else if (type_ == DemuxerType::OGG)
        return ReadOggPacket(out_packet);

    return false;
}

bool AudioDemuxer::Seek(uint32_t timestamp_ms)
{
    if (!fp_)
        return false;

    if (type_ == DemuxerType::WEBM)
        return SeekWebm(timestamp_ms);
    else if (type_ == DemuxerType::OGG)
        return SeekOgg(timestamp_ms);

    return false;
}

