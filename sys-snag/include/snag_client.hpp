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
        // Core travels inline (28 B); strings land directly in the status
        // string region via an out-buffer (sys-tune GetCurrentQueueItem pattern).
        SnagStatusCore core{};
        Result rc = serviceDispatchOut(&srv_, SnagCmd_GetStatus, core,
            .buffer_attrs = {SfBufferAttr_Out | SfBufferAttr_HipcMapAlias},
            .buffers = {{status.current_path, SNAG_STRBUF_SIZE}}, );
        if (R_SUCCEEDED(rc))
        {
            status.is_playing     = core.is_playing;
            status.is_initialized = core.is_initialized;
            status.position_ms    = core.position_ms;
            status.duration_ms    = core.duration_ms;
            status.volume         = core.volume;
            status.track_index    = core.track_index;
            status.track_count    = core.track_count;
        }
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
        // Strings travel in one in-buffer blob (sys-tune tuneEnqueue pattern).
        char blob[SNAG_STRBUF_SIZE]{};
        std::strncpy(blob + SNAG_PATH_OFF, path, SNAG_PATH_LEN - 1);
        if (title)
            std::strncpy(blob + SNAG_TITLE_OFF, title, SNAG_TITLE_LEN - 1);
        if (artist)
            std::strncpy(blob + SNAG_ARTIST_OFF, artist, SNAG_ARTIST_LEN - 1);
        Result rc = serviceDispatch(&srv_, SnagCmd_PlayFile,
            .buffer_attrs = {SfBufferAttr_In | SfBufferAttr_HipcMapAlias},
            .buffers = {{blob, sizeof(blob)}}, );
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
