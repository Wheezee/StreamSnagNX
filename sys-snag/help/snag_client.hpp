#pragma once
#include <switch.h>
#include <cstring>
#include "snag_ipc.hpp"

class SnagClient {
public:
    static SnagClient& Instance()
    {
        static SnagClient inst;
        return inst;
    }

    bool Initialize()
    {
        if (serviceIsActive(&srv_))
            return true;
        Handle handle = 0;
        Result rc = svcConnectToNamedPort(&handle, SNAG_SERVICE_NAME);
        if (R_SUCCEEDED(rc))
        {
            serviceCreate(&srv_, handle);
            return true;
        }
        return false;
    }

    void Exit()
    {
        if (serviceIsActive(&srv_))
        {
            serviceClose(&srv_);
        }
    }

    bool IsConnected()
    {
        return Initialize();
    }

    bool GetStatus(SnagStatus& status, Result* out_rc = nullptr)
    {
        if (!Initialize())
        {
            if (out_rc) *out_rc = MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            return false;
        }
        Result rc = serviceDispatchOut(&srv_, SnagCmd_GetStatus, status);
        if (out_rc) *out_rc = rc;
        return R_SUCCEEDED(rc);
    }

    bool Play()
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatch(&srv_, SnagCmd_Play);
        return R_SUCCEEDED(rc);
    }

    bool Pause()
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatch(&srv_, SnagCmd_Pause);
        return R_SUCCEEDED(rc);
    }

    bool Stop()
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatch(&srv_, SnagCmd_Stop);
        return R_SUCCEEDED(rc);
    }

    bool PlayIndex(uint32_t index)
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatchIn(&srv_, SnagCmd_PlayIndex, index);
        return R_SUCCEEDED(rc);
    }

    bool PlayFile(const char* path, const char* title = "", const char* artist = "")
    {
        if (!Initialize() || !path)
            return false;
        SnagPlayFileRequest req{};
        std::strncpy(req.path, path, sizeof(req.path) - 1);
        if (title)
            std::strncpy(req.title, title, sizeof(req.title) - 1);
        if (artist)
            std::strncpy(req.artist, artist, sizeof(req.artist) - 1);
        Result rc = serviceDispatchIn(&srv_, SnagCmd_PlayFile, req);
        return R_SUCCEEDED(rc);
    }

    bool Seek(uint32_t ms)
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatchIn(&srv_, SnagCmd_Seek, ms);
        return R_SUCCEEDED(rc);
    }

    bool SetVolume(float vol)
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatchIn(&srv_, SnagCmd_SetVolume, vol);
        return R_SUCCEEDED(rc);
    }

    bool Next()
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatch(&srv_, SnagCmd_Next);
        return R_SUCCEEDED(rc);
    }

    bool Prev()
    {
        if (!Initialize())
            return false;
        Result rc = serviceDispatch(&srv_, SnagCmd_Prev);
        return R_SUCCEEDED(rc);
    }

private:
    SnagClient() = default;
    ~SnagClient()
    {
        Exit();
    }
    Service srv_{};
};
