#include "player.hpp"
#include "util/log.hpp"
#include "yt/sig.hpp"

#include <borealis.hpp>

#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <curl/curl.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>

namespace ssnx::audio
{
namespace
{

constexpr int kOutRate = 48000;
constexpr int kOutChannels = 2;
// ~100ms per audout buffer, 8 slots deep.
constexpr int kSamplesPerBuffer = 4800;
constexpr size_t kSlotCount = 8;
// Start decoding once this much is buffered; EOF retries while downloading.
constexpr uint64_t kStartBytes = 256 * 1024;
constexpr const char* kUserAgent = "Mozilla/5.0";

void PlayerLog(const std::string& line)
{
    ssnx::Log(line);
}

size_t CurlWrite(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* fp = static_cast<FILE*>(userdata);
    return fwrite(ptr, size, nmemb, fp);
}

struct PlayerCurlContext
{
    std::atomic<bool>* stop;
    std::atomic<uint64_t>* downloaded;
};

int CurlProgress(void* userdata, curl_off_t /*dltotal*/, curl_off_t dlnow,
                 curl_off_t /*ultotal*/, curl_off_t /*ulnow*/)
{
    auto* ctx = static_cast<PlayerCurlContext*>(userdata);
    if (ctx)
    {
        if (ctx->stop && ctx->stop->load())
            return 1;
        if (ctx->downloaded && dlnow > 0)
            ctx->downloaded->store(static_cast<uint64_t>(dlnow));
    }
    return 0;
}


} // namespace

Player::Player() = default;

Player::~Player()
{
    Stop();
}

Player& Player::Instance()
{
    static Player player;
    return player;
}

bool Player::Play(const std::string& url, const std::string& video_id,
                  const std::string& final_path,
                  const std::string& user_agent)
{
    Stop();
    stop_requested_.store(false);
    paused_.store(false);
    position_.store(0.0);
    duration_.store(0.0);
    downloaded_bytes_.store(0);
    total_bytes_.store(0);
    download_done_.store(false);
    download_ok_.store(false);
    playing_.store(true);
    worker_ = std::thread(&Player::Worker, this, url, video_id, final_path, user_agent);
    return true;
}

void Player::Stop()
{
    stop_requested_.store(true);
    playing_.store(false);
    if (worker_.joinable())
    {
        if (worker_.get_id() != std::this_thread::get_id())
            worker_.join();
        else
            worker_.detach();
    }
    stop_requested_.store(false);
}

void Player::SetPaused(bool paused)
{
    paused_.store(paused);
#ifdef __SWITCH__
    if (playing_.load())
    {
        if (paused)
            (void)audoutStopAudioOut();
        else
            (void)audoutStartAudioOut();
    }
#endif
}

double Player::Position() const
{
    return position_.load();
}

double Player::DownloadProgress() const
{
    const uint64_t total = total_bytes_.load();
    if (total == 0)
        return 0.0;
    return std::min(1.0, downloaded_bytes_.load() / static_cast<double>(total));
}

void Player::SeekRelative(double delta_seconds)
{
    if (!playing_.load())
        return;
    double cur = position_.load();
    double target = std::max(0.0, cur + delta_seconds);
    double dur = duration_.load();
    if (dur > 0.0)
        target = std::min(target, dur);
    SeekTo(target);
}

void Player::SeekTo(double seconds)
{
    if (!playing_.load())
        return;
    seek_requested_seconds_.store(seconds);
}

void Player::Worker(std::string url, std::string video_id, std::string final_path,
                    std::string user_agent)
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/StreamSnagNX", 0777);
    mkdir("sdmc:/switch/StreamSnagNX/music", 0777);
#endif

    const bool is_local = (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0);
    std::string part_path;
    std::thread downloader;

    if (is_local)
    {
        part_path = url;
        PlayerLog("play: local file " + url);
        download_ok_.store(true);
        download_done_.store(true);
    }
    else
    {
        const size_t slash = final_path.rfind('/');
        const std::string dir = (slash != std::string::npos) ? final_path.substr(0, slash + 1) : "";
        const std::string filename = (slash != std::string::npos) ? final_path.substr(slash + 1) : final_path;
        part_path = dir + ".stream_" + filename + ".part";
        url = ssnx::yt::SolveN(url, video_id);
        PlayerLog("dl: start " + url.substr(0, 80) + "...");

        // Determine download User-Agent to match the client that minted the stream
        std::string download_ua = user_agent;
        if (download_ua.empty())
        {
            if (url.find("c=ANDROID_MUSIC") != std::string::npos)
                download_ua = "com.google.android.apps.youtube.music/8.39.42 (Linux; U; Android 15; en_US; Pixel 9 Pro; Build/AP4A.250205.002) gzip";
            else if (url.find("c=ANDROID_VR") != std::string::npos)
                download_ua = "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip";
            else if (url.find("c=IOS") != std::string::npos)
                download_ua = "com.google.ios.youtube/21.03.3 (iPad7,6; U; CPU iPadOS 17_7_10 like Mac OS X; en-US)";
            else
                download_ua = "com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip";
        }

        uint64_t total_size = 0;
        {
            const std::string key = "clen=";
            const size_t pos = url.find(key);
            if (pos != std::string::npos)
            {
                size_t end = url.find_first_of("&", pos + key.size());
                total_size = std::strtoull(
                    url.substr(pos + key.size(), end == std::string::npos
                                                    ? std::string::npos
                                                    : end - pos - key.size())
                        .c_str(),
                    nullptr, 10);
            }
        }
        if (total_size > 0)
            total_bytes_.store(total_size);
        PlayerLog("dl: total=" + std::to_string(total_size));

        downloader = std::thread([this, url, part_path, download_ua, total_size]() {
        FILE* fp = fopen(part_path.c_str(), "wb");
        if (!fp)
        {
            PlayerLog("dl: cannot open part file " + part_path + ": " + std::string(strerror(errno)));
            download_ok_.store(false);
            download_done_.store(true);
            return;
        }
        CURL* curl = curl_easy_init();
        if (!curl)
        {
            fclose(fp);
            download_ok_.store(false);
            download_done_.store(true);
            return;
        }

        const bool is_gv = (url.find("googlevideo.com/videoplayback") != std::string::npos);
        const bool has_ratebypass = (url.find("ratebypass=yes") != std::string::npos);

        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, download_ua.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        long http_code = 0;
        CURLcode rc = CURLE_OK;
        bool success = false;
        PlayerCurlContext curl_ctx {&stop_requested_, &downloaded_bytes_};
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &curl_ctx);

        if (is_gv && !has_ratebypass && total_size > 0)
        {
            // switch-newpipe chunked ranged download pattern: 1 MiB chunks on a reused keep-alive connection
            constexpr uint64_t kChunkBytes = 1024 * 1024;
            constexpr int kMaxRetries = 4;
            int rn = 0;
            uint64_t offset = 0;
            success = true;

            while (!stop_requested_.load() && offset < total_size)
            {
                const uint64_t chunk_end = std::min(total_size - 1, offset + kChunkBytes - 1);
                std::string chunk_url = url;
                chunk_url += (chunk_url.find('?') == std::string::npos ? "?" : "&");
                chunk_url += "range=" + std::to_string(offset) + "-" + std::to_string(chunk_end) +
                             "&rn=" + std::to_string(rn);

                const long chunk_start_pos = static_cast<long>(offset);
                bool chunk_ok = false;
                for (int retry = 0; retry < kMaxRetries && !stop_requested_.load(); ++retry)
                {
                    if (retry > 0)
                    {
                        PlayerLog("dl: chunk retry range=" + std::to_string(offset) + "-" +
                                  std::to_string(chunk_end) + " attempt=" + std::to_string(retry + 1));
                        fseek(fp, chunk_start_pos, SEEK_SET);
                        fflush(fp);
                        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
                        std::this_thread::sleep_for(std::chrono::milliseconds(500 * retry));
                    }
                    else
                    {
                        curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 0L);
                    }

                    curl_easy_setopt(curl, CURLOPT_URL, chunk_url.c_str());
                    rc = curl_easy_perform(curl);
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                    if (rc == CURLE_OK && (http_code == 200 || http_code == 206))
                    {
                        chunk_ok = true;
                        break;
                    }
                }

                if (!chunk_ok)
                {
                    PlayerLog("dl: chunk failed at " + std::to_string(offset) +
                              " curl=" + std::to_string(static_cast<int>(rc)) +
                              " http=" + std::to_string(http_code));
                    success = false;
                    break;
                }

                fflush(fp);
                const long pos = ftell(fp);
                if (pos > chunk_start_pos)
                {
                    offset = static_cast<uint64_t>(pos);
                    downloaded_bytes_.store(offset);
                }
                else
                {
                    PlayerLog("dl: chunk wrote 0 bytes at offset=" + std::to_string(offset));
                    success = false;
                    break;
                }
                ++rn;
            }
        }
        else
        {
            // Direct download
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);

            rc = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            fflush(fp);
            const long pos = ftell(fp);
            if (pos > 0)
                downloaded_bytes_.store(static_cast<uint64_t>(pos));
            success = (rc == CURLE_OK && (http_code == 200 || http_code == 206));
        }

        curl_easy_cleanup(curl);
        fclose(fp);

        download_ok_.store(success);
        PlayerLog(std::string("dl: done ok=") + (success ? "1" : "0") +
                  " curl=" + std::to_string(static_cast<int>(rc)) +
                  " http=" + std::to_string(http_code) +
                  " bytes=" + std::to_string(downloaded_bytes_.load()));
        download_done_.store(true);
    });
    }

    // Wait for enough bytes to open the container, or full download.
    bool have_start = false;
    for (int i = 0; i < 300 && !stop_requested_.load(); ++i)
    {
        if (downloaded_bytes_.load() >= kStartBytes || (download_done_.load() && download_ok_.load()))
        {
            have_start = true;
            break;
        }
        if (download_done_.load() && !download_ok_.load())
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!have_start || (!download_ok_.load() && downloaded_bytes_.load() < kStartBytes))
    {
        PlayerLog("dl: failed before start threshold (bytes=" +
                  std::to_string(downloaded_bytes_.load()) +
                  " ok=" + std::to_string(download_ok_.load() ? 1 : 0) + ")");
        stop_requested_.store(true);
        if (downloader.joinable())
            downloader.join();
        playing_.store(false);
        return;
    }

#ifdef __SWITCH__
    if (R_FAILED(audoutInitialize()) ||
        audoutGetSampleRate() != kOutRate || audoutGetChannelCount() != kOutChannels ||
        audoutGetPcmFormat() != PcmFormat_Int16)
    {
        PlayerLog("audio: audout init/format failed");
        stop_requested_.store(true);
        if (downloader.joinable())
            downloader.join();
#ifdef __SWITCH__
        audoutExit();
#endif
        playing_.store(false);
        return;
    }
#endif

    AVFormatContext* format = nullptr;
    std::string av_path = part_path;
    if (av_path.rfind("file:", 0) != 0)
        av_path = "file:" + av_path;

    for (int open_retry = 0; open_retry < 100 && !stop_requested_.load(); ++open_retry)
    {
        const int open_rc = avformat_open_input(&format, av_path.c_str(), nullptr, nullptr);
        if (open_rc == 0)
            break;

        if (download_done_.load())
        {
            char errbuf[128] = {};
            av_strerror(open_rc, errbuf, sizeof(errbuf));
            PlayerLog("open: cannot open part file: " + std::string(errbuf) + " (" +
                      std::to_string(open_rc) + ")");
            stop_requested_.store(true);
            if (downloader.joinable())
                downloader.join();
#ifdef __SWITCH__
            audoutExit();
#endif
            playing_.store(false);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    if (!format)
    {
        PlayerLog("open: failed to open container within timeout");
        stop_requested_.store(true);
        if (downloader.joinable())
            downloader.join();
#ifdef __SWITCH__
        audoutExit();
#endif
        playing_.store(false);
        return;
    }
    const int info_rc = avformat_find_stream_info(format, nullptr);
    if (info_rc < 0)
    {
        char errbuf[128] = {};
        av_strerror(info_rc, errbuf, sizeof(errbuf));
        PlayerLog("open: stream info failed: " + std::string(errbuf) + " (" +
                  std::to_string(info_rc) + ")");
        avformat_close_input(&format);
        stop_requested_.store(true);
        if (downloader.joinable())
            downloader.join();
#ifdef __SWITCH__
        audoutExit();
#endif
        playing_.store(false);
        return;
    }
    PlayerLog("open: container ok");

    if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
        duration_.store(format->duration / static_cast<double>(AV_TIME_BASE));

    int stream_index = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0)
    {
        PlayerLog("open: no audio stream");
        avformat_close_input(&format);
        stop_requested_.store(true);
        if (downloader.joinable())
            downloader.join();
#ifdef __SWITCH__
        audoutExit();
#endif
        playing_.store(false);
        return;
    }

    AVStream* stream = format->streams[stream_index];
    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0 && stream->time_base.den > 0)
    {
        const double seconds = stream->duration * av_q2d(stream->time_base);
        if (seconds > 0.0 && seconds < 36000.0)
            duration_.store(seconds);
    }

    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* decoder = nullptr;
    if (codec)
    {
        decoder = avcodec_alloc_context3(codec);
        if (decoder && avcodec_parameters_to_context(decoder, stream->codecpar) >= 0 &&
            avcodec_open2(decoder, codec, nullptr) >= 0)
        {
            // Ready.
        }
        else
        {
            avcodec_free_context(&decoder);
            decoder = nullptr;
        }
    }
    if (!decoder)
    {
        PlayerLog("open: no decoder");
        avformat_close_input(&format);
        stop_requested_.store(true);
        if (downloader.joinable())
            downloader.join();
#ifdef __SWITCH__
        audoutExit();
#endif
        playing_.store(false);
        return;
    }

    AVChannelLayout in_layout = stream->codecpar->ch_layout;
    if ((in_layout.u.mask == 0 || in_layout.order == AV_CHANNEL_ORDER_UNSPEC) &&
        in_layout.nb_channels > 0)
        av_channel_layout_default(&in_layout, in_layout.nb_channels);
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, kOutChannels);
    SwrContext* resampler = nullptr;
    const int swr_rc = swr_alloc_set_opts2(
        &resampler, &out_layout, AV_SAMPLE_FMT_S16, kOutRate, &in_layout,
        static_cast<AVSampleFormat>(stream->codecpar->format),
        stream->codecpar->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&out_layout);
    if (swr_rc < 0 || !resampler || swr_init(resampler) < 0)
    {
        PlayerLog("open: resampler failed");
        swr_free(&resampler);
        avcodec_free_context(&decoder);
        avformat_close_input(&format);
        stop_requested_.store(true);
        if (downloader.joinable())
            downloader.join();
#ifdef __SWITCH__
        audoutExit();
#endif
        playing_.store(false);
        return;
    }
    PlayerLog("decode: ready");

#ifdef __SWITCH__
    struct Slot
    {
        AudioOutBuffer buffer {};
        std::vector<uint8_t> storage;
        bool in_use = false;
    };
    std::array<Slot, kSlotCount> slots;
    for (auto& slot : slots)
    {
        slot.storage.resize(static_cast<size_t>(kSamplesPerBuffer) * kOutChannels * sizeof(int16_t));
        slot.buffer.next = nullptr;
        slot.buffer.buffer = slot.storage.data();
        slot.buffer.buffer_size = slot.storage.size();
        slot.buffer.data_size = 0;
        slot.buffer.data_offset = 0;
        slot.in_use = false;
    }
    bool started = false;
    uint64_t submitted_samples = 0;
    bool is_hardware_paused = false;

    std::vector<int16_t> pcm(static_cast<size_t>(kSamplesPerBuffer) * kOutChannels);
    std::vector<int16_t> pending;
    pending.reserve(pcm.size() * 2);

    auto reclaim_buffers = [&]() {
        AudioOutBuffer* released = nullptr;
        u32 released_count = 0;
        while (R_SUCCEEDED(audoutGetReleasedAudioOutBuffer(&released, &released_count)) &&
               released && released_count > 0)
        {
            for (auto& slot : slots)
            {
                if (&slot.buffer == released)
                {
                    slot.in_use = false;
                    break;
                }
            }
            released = nullptr;
            released_count = 0;
        }
    };

    auto acquire_free_slot = [&]() -> Slot* {
        reclaim_buffers();
        for (auto& slot : slots)
        {
            if (!slot.in_use)
                return &slot;
        }
        return nullptr;
    };

    auto flush_pending = [&]() {
        while (pending.size() >= pcm.size() && !stop_requested_.load())
        {
            // If seek was requested while in flush_pending, return immediately to process it!
            if (seek_requested_seconds_.load() >= 0.0)
                break;

            while (paused_.load() && !stop_requested_.load())
            {
                if (!is_hardware_paused && started)
                {
                    (void)audoutStopAudioOut();
                    is_hardware_paused = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (stop_requested_.load())
                break;

            if (is_hardware_paused)
            {
                if (started)
                    (void)audoutStartAudioOut();
                is_hardware_paused = false;
            }

            Slot* slot = acquire_free_slot();
            if (!slot)
            {
                for (int tries = 0; tries < 200 && !stop_requested_.load() && !slot; ++tries)
                {
                    if (seek_requested_seconds_.load() >= 0.0)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    slot = acquire_free_slot();
                }
            }
            if (!slot || seek_requested_seconds_.load() >= 0.0)
                break;

            std::memcpy(slot->storage.data(), pending.data(), pcm.size() * sizeof(int16_t));
            pending.erase(pending.begin(), pending.begin() + pcm.size());
            slot->buffer.data_size = pcm.size() * sizeof(int16_t);
            slot->buffer.data_offset = 0;
            slot->in_use = true;

            if (R_FAILED(audoutAppendAudioOutBuffer(&slot->buffer)))
            {
                slot->in_use = false;
                break;
            }

            if (!started)
            {
                (void)audoutStartAudioOut();
                started = true;
                is_hardware_paused = false;
            }

            submitted_samples += kSamplesPerBuffer;
            position_.store(submitted_samples / static_cast<double>(kOutRate));
        }
    };
#else
    std::vector<int16_t> pcm(static_cast<size_t>(kSamplesPerBuffer) * kOutChannels);
    std::vector<int16_t> pending;
    pending.reserve(pcm.size() * 2);

    auto flush_pending = [&]() {
        pending.clear();
    };
#endif

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool eof = false;
    int eof_retries = 0;
    bool first_frame_after_seek = false;
    double pending_seek_target = -1.0;

    while (!stop_requested_.load())
    {
        const double seek_target = seek_requested_seconds_.exchange(-1.0);
        if (seek_target >= 0.0)
        {
            const int64_t ts = static_cast<int64_t>(seek_target / av_q2d(stream->time_base));
            if (av_seek_frame(format, stream_index, ts, AVSEEK_FLAG_BACKWARD) >= 0)
            {
                avcodec_flush_buffers(decoder);
                pending.clear();
#ifdef __SWITCH__
                (void)audoutStopAudioOut();
                bool flushed = false;
                (void)audoutFlushAudioOutBuffers(&flushed);
                reclaim_buffers();
                for (auto& s : slots)
                    s.in_use = false;
                started = false;
                is_hardware_paused = false;
#endif
                pending_seek_target = seek_target;
                first_frame_after_seek = true;
                position_.store(seek_target);
                eof = false;
                eof_retries = 0;
                PlayerLog("seek: jumped to " + std::to_string(seek_target) + "s");
            }
            else
            {
                PlayerLog("seek: failed to seek to " + std::to_string(seek_target) + "s");
            }
        }

        const int read_rc = eof ? AVERROR_EOF : av_read_frame(format, packet);
        if (read_rc >= 0)
        {
            eof_retries = 0;
            if (packet->stream_index != stream_index)
            {
                av_packet_unref(packet);
                continue;
            }
            if (avcodec_send_packet(decoder, packet) < 0)
            {
                av_packet_unref(packet);
                continue;
            }
            av_packet_unref(packet);

            while (!stop_requested_.load())
            {
                if (seek_requested_seconds_.load() >= 0.0)
                    break;

                if (avcodec_receive_frame(decoder, frame) < 0)
                    break;

                if (first_frame_after_seek)
                {
                    first_frame_after_seek = false;
                    double actual_time = pending_seek_target;
                    if (frame->pts != AV_NOPTS_VALUE && stream->time_base.den > 0)
                    {
                        const double pts_sec = frame->pts * av_q2d(stream->time_base);
                        if (pts_sec >= 0.0 && pts_sec < 36000.0)
                            actual_time = pts_sec;
                    }
#ifdef __SWITCH__
                    submitted_samples = static_cast<uint64_t>(actual_time * kOutRate);
#endif
                    position_.store(actual_time);
                }

                uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(pcm.data())};
                const int got = swr_convert(resampler, out_planes,
                                            static_cast<int>(pcm.size() / kOutChannels),
                                            const_cast<const uint8_t**>(frame->data),
                                            frame->nb_samples);
                if (got > 0)
                    pending.insert(pending.end(), pcm.begin(),
                                   pcm.begin() + static_cast<size_t>(got) * kOutChannels);
                flush_pending();
            }
        }
        else
        {
            // EOF on a growing file: wait for the downloader unless done.
            if (!download_done_.load() && eof_retries < 600 && !stop_requested_.load())
            {
                ++eof_retries;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            break;
        }
    }

    // Drain decoder, then play out the remainder.
    if (!stop_requested_.load())
    {
        avcodec_send_packet(decoder, nullptr);
        while (avcodec_receive_frame(decoder, frame) >= 0 && !stop_requested_.load())
        {
            uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(pcm.data())};
            const int got = swr_convert(resampler, out_planes,
                                        static_cast<int>(pcm.size() / kOutChannels),
                                        const_cast<const uint8_t**>(frame->data),
                                        frame->nb_samples);
            if (got > 0)
                pending.insert(pending.end(), pcm.begin(),
                               pcm.begin() + static_cast<size_t>(got) * kOutChannels);
            flush_pending();
        }
    }

#ifdef __SWITCH__
    while (!pending.empty() && !stop_requested_.load() && pending.size() >= pcm.size())
        flush_pending();
    // Drain remaining in-flight audio buffers (~300-800ms) with instant cancel on stop
    for (int w = 0; w < 40 && !stop_requested_.load(); ++w)
    {
        reclaim_buffers();
        bool any_in_use = false;
        for (const auto& s : slots)
        {
            if (s.in_use)
            {
                any_in_use = true;
                break;
            }
        }
        if (!any_in_use)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    if (!stop_requested_.load() && duration_.load() > 0.0)
    {
        position_.store(duration_.load());
    }
    audoutStopAudioOut();
    bool flushed = false;
    audoutFlushAudioOutBuffers(&flushed);
    audoutExit();
#endif

    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&resampler);
    avcodec_free_context(&decoder);
    avformat_close_input(&format);

    if (downloader.joinable())
        downloader.join();

    // Keep the finished file as the cached track if not already saved.
    if (download_ok_.load() && !stop_requested_.load())
    {
        if (!is_local)
        {
            FILE* probe = fopen(final_path.c_str(), "rb");
            if (probe)
            {
                fclose(probe);
                std::remove(part_path.c_str());
            }
            else
            {
                std::remove(final_path.c_str());
                std::rename(part_path.c_str(), final_path.c_str());
                PlayerLog("dl: kept " + final_path);
#ifdef __SWITCH__
                fsdevCommitDevice("sdmc");
#endif
            }
        }
    }
    else if (!is_local)
    {
        std::remove(part_path.c_str());
    }

    if (!stop_requested_.load())
    {
        playing_.store(false);
        if (on_track_ended_)
        {
            brls::sync([this]() {
                if (on_track_ended_)
                    on_track_ended_();
            });
        }
    }
}

} // namespace ssnx::audio
