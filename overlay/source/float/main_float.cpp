#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <string>
#include "../gui_lyrics_hud.hpp"
#include "../../../sys-snag/include/snag_client.hpp"

#define SNAGFLOAT_BUILDNO_STR "F1"

namespace {
std::string g_playerPath;
}

class StreamSnagFloatOverlay : public tsl::Overlay {
public:
    virtual void initServices() override
    {
        fsdevMountSdmc();
        SnagClient::Instance().Initialize();
        ProbeLog("=== float boot " __DATE__ " " __TIME__ " (#" SNAGFLOAT_BUILDNO_STR ") ===");
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
        // Player path injected: tap/chord relaunch it (own lean process).
        return initially<GuiLyricsHud>(g_playerPath);
    }
};

int main(int argc, char** argv)
{
    // Lean strip framebuffer (Status Monitor micro: 1280x28; ours 1280x64).
    // Proportional layer math turns this into a 1920x96 layer — tiny
    // enough for the overlay heap budget that killed fullscreen (0x559).
    // Bottom-anchored on first draw (see LyricsHudElement).
    tsl::cfg::FramebufferWidth  = 1280;
    tsl::cfg::FramebufferHeight = 64;

    // Player .ovl lives next to us; chord/tap relaunch it.
    if (argc > 0 && argv[0])
    {
        std::string self = argv[0];
        size_t slash = self.find_last_of("/\\");
        std::string dir = (slash == std::string::npos) ? "" : self.substr(0, slash + 1);
        g_playerPath = dir + "StreamSnag.ovl";
    }
    return tsl::loop<StreamSnagFloatOverlay>(argc, argv);
}
