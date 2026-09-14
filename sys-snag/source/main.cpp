#include <switch.h>
#include "player_daemon.hpp"
#include "snag_server.hpp"
#include "snag_log.h"

// 500 KB heap — Ogg page buffers + hwopus input vec + std::string/vector overhead.
// NO audout work buffer needed: PCM pool is static BSS in audren_engine.cpp.
// Matches sys-tune's approach (they use 250 KB).
#define INNER_HEAP_SIZE (1024 * 500)

#ifdef __cplusplus
extern "C" {
#endif

u32 __nx_applet_type     = AppletType_None;
u32 __nx_fs_num_sessions = 1;

void __libnx_initheap(void)
{
    static char inner_heap[INNER_HEAP_SIZE];
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

// __appInit: only services that are guaranteed ready at boot2.
// audout is NOT here — it may not be ready this early.
// sys-tune does the same: audoutInitialize() is called later from main().
void __appInit(void)
{
    Result rc = smInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);

    rc = setsysInitialize();
    if (R_SUCCEEDED(rc))
    {
        SetSysFirmwareVersion fw;
        rc = setsysGetFirmwareVersion(&fw);
        if (R_SUCCEEDED(rc))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
        setsysExit();
    }

    rc = fsInitialize();
    if (R_FAILED(rc)) diagAbortWithResult(rc);

    fsdevMountSdmc();
}

void __appExit(void)
{
    fsdevUnmountAll();
    fsExit();
    smExit();
}

#ifdef __cplusplus
}
#endif

int main(int, char*[])
{
    // audout initialized here from main(), NOT __appInit.
    // Audio services are guaranteed up by the time main() runs.
    // Matches sys-tune: audoutInitialize() inside tune::impl::Initialize() from main().
    SnagLog("sys-snag boot diag1");

    Result rc = audoutInitialize();
    SnagLog("audoutInitialize rc=0x%08X sr=%u ch=%u", rc, audoutGetSampleRate(), audoutGetChannelCount());
    if (R_FAILED(rc))
        diagAbortWithResult(rc);

    bool pd_ok = PlayerDaemon::Instance().Initialize();
    SnagLog("PlayerDaemon::Initialize=%d", (int)pd_ok);

    if (!SnagServer::Instance().Initialize())
    {
        SnagLog("SnagServer::Initialize FAILED");
        PlayerDaemon::Instance().Exit();
        audoutExit();
        return 1;
    }

    SnagLog("entering LoopProcess");
    SnagServer::Instance().LoopProcess();
    SnagLog("LoopProcess exited");

    SnagServer::Instance().Exit();
    PlayerDaemon::Instance().Exit();
    audoutExit();
    return 0;
}
