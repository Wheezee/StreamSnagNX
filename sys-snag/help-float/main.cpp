#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <string>
#include "gui_main.hpp"
#include "../../sys-snag/include/snag_client.hpp"

// Own .ovl path (argv[0]), used to relaunch ourselves like Status Monitor
// does (setNextOverlay + close) instead of trusting hide/show toggles.
std::string g_ovlPath;
// "--player" in argv forces the player menu (used after a chord exit from
// float mode, so the relaunch doesn't bounce straight back to float).
bool g_forcePlayer = false;

class StreamSnagOverlay : public tsl::Overlay {
public:
    virtual void initServices() override
    {
        // Overlays don't get sdmc mounted automatically (unlike hbmenu NROs).
        // Without this, the song browser and lyrics LRC loading see nothing.
        fsdevMountSdmc();
        SnagClient::Instance().Initialize();
    }

    virtual void exitServices() override
    {
        SnagClient::Instance().Exit();
        fsdevUnmountAll();
    }

    virtual void onShow() override {}
    virtual void onHide() override {}

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override
    {
        // Music playing -> straight to floating lyrics (no digging),
        // unless forced to the player (e.g. chord exit from float mode).
        // Otherwise the player menu (or its offline notice).
        extern bool g_forcePlayer;
        SnagStatus st{};
        if (!g_forcePlayer && SnagClient::Instance().GetStatus(st) && st.is_playing)
            return initially<GuiLyricsHud>();
        return initially<GuiMain>();
    }
};

int main(int argc, char** argv)
{
    if (argc > 0 && argv[0])
        g_ovlPath = argv[0];
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] && std::string(argv[i]) == "--player")
            g_forcePlayer = true;
    }
    return tsl::loop<StreamSnagOverlay>(argc, argv);
}

