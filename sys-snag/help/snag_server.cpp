#include "snag_server.hpp"
#include "player_daemon.hpp"
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
            SnagStatus status{};
            PlayerDaemon::Instance().GetStatus(status);
            *out_dataSize = sizeof(SnagStatus);
            std::memcpy(out_data, &status, sizeof(SnagStatus));
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
            if (r->data.size >= sizeof(SnagPlayFileRequest))
            {
                const auto* req = reinterpret_cast<const SnagPlayFileRequest*>(r->data.ptr);
                PlayerDaemon::Instance().PlayFile(req->path, req->title, req->artist);
                return 0;
            }
            break;
        }
        case SnagCmd_PlayIndex:
        {
            if (r->data.size >= sizeof(uint32_t))
            {
                uint32_t idx = *reinterpret_cast<const uint32_t*>(r->data.ptr);
                PlayerDaemon::Instance().PlayIndex(idx);
                return 0;
            }
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
