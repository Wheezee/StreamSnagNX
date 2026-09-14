#include "snag_server.hpp"
#include "player_daemon.hpp"
#include "snag_log.h"
#include <cstring>

SnagServer& SnagServer::Instance()
{
    static SnagServer instance;
    return instance;
}

SnagServer::SnagServer() = default;

SnagServer::~SnagServer()
{
    Exit();
}

bool SnagServer::Initialize()
{
    if (running_.load())
        return true;

    Result rc = ipcServerInit(&server_, SNAG_SERVICE_NAME, 4);
    if (R_FAILED(rc))
        return false;

    running_.store(true);
    return true;
}

void SnagServer::Exit()
{
    if (running_.load())
    {
        running_.store(false);
        ipcServerExit(&server_);
    }
}

void SnagServer::LoopProcess()
{
    while (running_.load())
    {
        Result rc = ipcServerProcess(&server_, RequestHandler, this);
        if (rc == KERNELRESULT(Cancelled))
            break;
    }
}

Result SnagServer::RequestHandler(void* userdata, const IpcServerRequest* r, u8* out_data, size_t* out_dataSize)
{
    switch (r->data.cmdId)
    {
        case SnagCmd_GetStatus:
        {
            if (r->hipc.meta.num_recv_buffers < 1)
            {
                SnagLog("GetStatus NO BUFFER");
                break;
            }
            SnagStatus status{};
            PlayerDaemon::Instance().GetStatus(status);

            // Strings go to the client's out-buffer at the documented offsets.
            char* buf = static_cast<char*>(hipcGetBufferAddress(r->hipc.data.recv_buffers));
            size_t cap = hipcGetBufferSize(r->hipc.data.recv_buffers);
            size_t n = cap < SNAG_STRBUF_SIZE ? cap : SNAG_STRBUF_SIZE;
            std::memset(buf, 0, n);
            auto copy_at = [&](size_t off, size_t maxlen, const char* src) {
                if (off >= n || !src)
                    return;
                size_t room = n - off;
                size_t want = std::strlen(src) + 1;
                size_t take = want < maxlen ? want : maxlen;
                if (take > room)
                    take = room;
                std::memcpy(buf + off, src, take);
                buf[n - 1] = '\0';
            };
            copy_at(SNAG_PATH_OFF, SNAG_PATH_LEN, status.current_path);
            copy_at(SNAG_TITLE_OFF, SNAG_TITLE_LEN, status.current_title);
            copy_at(SNAG_ARTIST_OFF, SNAG_ARTIST_LEN, status.current_artist);

            // Scalars go inline (28 B).
            SnagStatusCore core{};
            core.is_playing     = status.is_playing;
            core.is_initialized = status.is_initialized;
            core.position_ms    = status.position_ms;
            core.duration_ms    = status.duration_ms;
            core.volume         = status.volume;
            core.track_index    = status.track_index;
            core.track_count    = status.track_count;
            *out_dataSize = sizeof(core);
            std::memcpy(out_data, &core, sizeof(core));
            // snag-ctl polls this at 10 Hz: log sparsely to avoid log spam.
            static int status_count = 0;
            if (++status_count == 1 || status_count % 50 == 1)
                SnagLog("GetStatus playing=%u init=%u pos=%ums bufcap=%u",
                    (unsigned)status.is_playing, (unsigned)status.is_initialized,
                    (unsigned)status.position_ms, (unsigned)cap);
            return 0;
        }
        case SnagCmd_Play:
        {
            PlayerDaemon::Instance().Play();
            return 0;
        }
        case SnagCmd_Pause:
        {
            PlayerDaemon::Instance().Pause();
            return 0;
        }
        case SnagCmd_Stop:
        {
            PlayerDaemon::Instance().Stop();
            return 0;
        }
        case SnagCmd_PlayFile:
        {
            if (r->hipc.meta.num_send_buffers < 1)
            {
                SnagLog("PlayFile NO BUFFER");
                break;
            }
            const char* blob = static_cast<const char*>(hipcGetBufferAddress(r->hipc.data.send_buffers));
            size_t cap = hipcGetBufferSize(r->hipc.data.send_buffers);
            char path[SNAG_PATH_LEN]{};
            char title[SNAG_TITLE_LEN]{};
            char artist[SNAG_ARTIST_LEN]{};
            auto take_at = [&](size_t off, char* dst, size_t dstlen) {
                if (off >= cap)
                    return;
                size_t room = cap - off;
                size_t len = 0;
                while (len + 1 < dstlen && len < room && blob[off + len] != '\0')
                    len++;
                std::memcpy(dst, blob + off, len);
                dst[len] = '\0';
            };
            // NOTE: consume all TLS-backed bytes BEFORE any SnagLog call:
            // logging performs IPC that clobbers the live request in TLS.
            take_at(SNAG_PATH_OFF, path, sizeof(path));
            take_at(SNAG_TITLE_OFF, title, sizeof(title));
            take_at(SNAG_ARTIST_OFF, artist, sizeof(artist));
            SnagLog("PlayFile blobcap=%u", (unsigned)cap);
            bool ok = PlayerDaemon::Instance().PlayFile(path, title, artist);
            SnagLog("PlayFile ok=%d path=%.200s", (int)ok, path);
            // Propagate file errors so the client can tell "unreadable file"
            // apart from an IPC transport failure.
            if (!ok)
                return MAKERESULT(Module_Libnx, LibnxError_NotFound);
            return 0;
        }
        case SnagCmd_PlayIndex:
        {
            if (r->data.size >= sizeof(uint32_t))
            {
                uint32_t idx = *reinterpret_cast<const uint32_t*>(r->data.ptr);
                bool ok = PlayerDaemon::Instance().PlayIndex(idx);
                SnagLog("PlayIndex ok=%d idx=%u", (int)ok, (unsigned)idx);
                if (!ok)
                    return MAKERESULT(Module_Libnx, LibnxError_NotFound);
                return 0;
            }
            SnagLog("PlayIndex BAD SIZE %u", (unsigned)r->data.size);
            break;
        }
        case SnagCmd_Seek:
        {
            if (r->data.size >= sizeof(uint32_t))
            {
                uint32_t seek_ms = *reinterpret_cast<const uint32_t*>(r->data.ptr);
                PlayerDaemon::Instance().Seek(seek_ms);
                return 0;
            }
            break;
        }
        case SnagCmd_SetVolume:
        {
            if (r->data.size >= sizeof(float))
            {
                float vol = *reinterpret_cast<const float*>(r->data.ptr);
                PlayerDaemon::Instance().SetVolume(vol);
                return 0;
            }
            break;
        }
        case SnagCmd_Next:
        {
            PlayerDaemon::Instance().Next();
            return 0;
        }
        case SnagCmd_Prev:
        {
            PlayerDaemon::Instance().Prev();
            return 0;
        }
        default:
            break;
    }
    return 0;
}
