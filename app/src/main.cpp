// StreamSnagNX — offline YT audio grabber for Switch homebrew.
#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif

#include <borealis.hpp>

#include <cstdlib>
#include <fstream>
#include <string>

#include "audio/player.hpp"
#include "audio/queue_manager.hpp"
#include "library/download_manager.hpp"
#include "library/library_store.hpp"
#include "settings/settings_store.hpp"
#include "shell/app_shell.hpp"

#ifdef __SWITCH__
namespace
{

void EnsureStorage()
{
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/StreamSnagNX", 0777);
}

void BootLog(const std::string& line)
{
    EnsureStorage();
    std::ofstream stream("sdmc:/switch/StreamSnagNX/boot.log", std::ios::app);
    if (stream.is_open())
        stream << line << '\n';
}

void ShowStartupFailure(const std::string& message)
{
    BootLog("startup failure: " + message);
    consoleInit(nullptr);
    consoleClear();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    printf("StreamSnagNX failed to start.\n\n%s\n\n", message.c_str());
    printf("Log: sdmc:/switch/StreamSnagNX/boot.log\n\n");
    printf("Press PLUS or B to exit.\n");
    while (appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & (HidNpadButton_Plus | HidNpadButton_B))
            break;
        consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
}

} // namespace
#endif

int main(int /*argc*/, char** /*argv*/)
{
#ifdef __SWITCH__
    BootLog("boot: entered main()");
#endif
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);

    try
    {
        if (!brls::Application::init())
        {
#ifdef __SWITCH__
            ShowStartupFailure("brls::Application::init returned false");
#endif
            return EXIT_FAILURE;
        }

        brls::Application::createWindow("StreamSnagNX");
        brls::Application::setGlobalQuit(false);
        brls::Application::setFPSStatus(false);

        ssnx::library::LibraryStore::Instance().Init();
        ssnx::settings::SettingsStore::Instance().Init();
        ssnx::audio::QueueManager::Instance().Init();

        auto* shell = new ssnx::shell::AppShell();
        brls::Application::pushActivity(new brls::Activity(shell));

        brls::Application::getExitEvent()->subscribe([]() {
#ifdef __SWITCH__
            BootLog("exitEvent: shutting down subsystems before view destruction");
#endif
            ssnx::audio::Player::Instance().Stop();
            ssnx::library::DownloadManager::Instance().Shutdown();
            ssnx::settings::SettingsStore::Instance().Shutdown();
            ssnx::library::LibraryStore::Instance().Save();
#ifdef __SWITCH__
            fsdevCommitDevice("sdmc");
            BootLog("exitEvent: all subsystems stopped and sdmc committed");
#endif
        });

        while (brls::Application::mainLoop())
            ;

#ifdef __SWITCH__
        BootLog("exit: mainLoop returned, shutting down subsystems");
#endif
        ssnx::audio::Player::Instance().Stop();
        ssnx::library::DownloadManager::Instance().Shutdown();
        ssnx::settings::SettingsStore::Instance().Shutdown();
#ifdef __SWITCH__
        fsdevCommitDevice("sdmc");
        BootLog("exit: subsystems stopped cleanly");
#endif

        return EXIT_SUCCESS;
    }
#ifdef __SWITCH__
    catch (const std::exception& ex)
    {
        ShowStartupFailure(std::string("Unhandled exception: ") + ex.what());
    }
    catch (...)
    {
        ShowStartupFailure("Unhandled non-standard exception");
    }
#endif

    return EXIT_FAILURE;
}
