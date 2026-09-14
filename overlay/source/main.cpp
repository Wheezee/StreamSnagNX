#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <string>
#include "gui_main.hpp"
#include "../../sys-snag/include/snag_client.hpp"

// Own .ovl path (argv[0]): the player launches the separate fullscreen
// lyrics binary (StreamSnagFloat.ovl) from the same folder.
std::string g_ovlPath;

class StreamSnagOverlay : public tsl::Overlay {
public:
    virtual void initServices() override
    {
        // Overlays don't get sdmc mounted automatically (unlike hbmenu NROs).
        // Without this, the song browser and lyrics LRC loading see nothing.
        fsdevMountSdmc();
        SnagClient::Instance().Initialize();
        // Boot stamp: the probe log appends forever, so every boot marks
        // itself — only lines after the LAST stamp are current.
        ProbeLog("=== ovl boot " __DATE__ " " __TIME__ " (#" SNAGOVL_BUILDNO_STR ") ===");
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
        // Always the player menu first. (Auto-float on open was tried and
        // removed: it left chord-exits with nothing underneath, dumping
        // out to HOME instead of the player. Strict machine below.)
        return initially<GuiMain>();
    }
};

int main(int argc, char** argv)
{
    // Player keeps the default 448x720 sidebar layer. (Fullscreen belongs
    // to the separate StreamSnagFloat binary.)
    if (argc > 0 && argv[0])
        g_ovlPath = argv[0];
    return tsl::loop<StreamSnagOverlay>(argc, argv);
}

