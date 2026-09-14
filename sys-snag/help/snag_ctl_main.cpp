#include <switch.h>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>
#include <dirent.h>
#include <algorithm>
#include <sys/stat.h>
#include "snag_client.hpp"

static std::string FormatTime(uint32_t ms)
{
    uint32_t total_sec = ms / 1000;
    uint32_t m = total_sec / 60;
    uint32_t s = total_sec % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02u:%02u", m, s);
    return buf;
}

// Minimal JSON string-field extractor: finds "key":"value" and returns value.
// Good enough for our flat library.json objects; no full JSON parser needed.
static std::string JsonGetString(const std::string& json, const char* key)
{
    std::string needle = std::string("\"") + key + "\":\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return {};
    pos += needle.size();
    size_t end = json.find('"', pos);
    if (end == std::string::npos)
        return {};
    std::string result;
    result.reserve(end - pos);
    for (size_t i = pos; i < end; ++i)
    {
        if (json[i] == '\\' && i + 1 < end)
        {
            ++i;
            if (json[i] == 'n')       result += '\n';
            else if (json[i] == 't')  result += '\t';
            else                      result += json[i];
        }
        else
        {
            result += json[i];
        }
    }
    return result;
}

// Looks up a video_id in library.json and fills title/artist from metadata.
static bool LibraryLookup(const std::string& video_id,
                           std::string& out_title, std::string& out_artist)
{
    FILE* fp = fopen("sdmc:/switch/StreamSnagNX/library.json", "rb");
    if (!fp)
        return false;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 512 * 1024) { fclose(fp); return false; }

    std::string data(static_cast<size_t>(sz), '\0');
    fread(&data[0], 1, static_cast<size_t>(sz), fp);
    fclose(fp);

    std::string id_needle = std::string("\"video_id\":\"") + video_id + "\"";
    size_t id_pos = data.find(id_needle);
    if (id_pos == std::string::npos) return false;

    size_t obj_start = data.rfind('{', id_pos);
    if (obj_start == std::string::npos) return false;
    size_t obj_end = data.find('}', id_pos);
    if (obj_end == std::string::npos) return false;

    std::string obj = data.substr(obj_start, obj_end - obj_start + 1);
    out_title  = JsonGetString(obj, "title");
    out_artist = JsonGetString(obj, "artist");
    if (out_artist.empty())
        out_artist = JsonGetString(obj, "subtitle");
    return !out_title.empty();
}

// Extract video_id from filename (strip extension — filename IS the video_id)
static std::string VideoIdFromFilename(const std::string& filename)
{
    size_t dot = filename.rfind('.');
    if (dot != std::string::npos)
        return filename.substr(0, dot);
    return filename;
}

static std::vector<std::string> ScanLocalMusic()
{
    if (fsdevGetDeviceFileSystem("sdmc") == nullptr)
        fsdevMountSdmc();

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/StreamSnagNX", 0777);
    mkdir("sdmc:/switch/StreamSnagNX/music", 0777);

    std::vector<std::string> files;
    DIR* dir = opendir("sdmc:/switch/StreamSnagNX/music");
    if (dir)
    {
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr)
        {
            if (ent->d_name[0] == '.')
                continue;
            const size_t len = std::strlen(ent->d_name);
            if (len > 4 && (strcasecmp(ent->d_name + len - 5, ".opus") == 0 ||
                            strcasecmp(ent->d_name + len - 5, ".webm") == 0 ||
                            strcasecmp(ent->d_name + len - 4, ".ogg") == 0 ||
                            strcasecmp(ent->d_name + len - 4, ".m4a") == 0))
            {
                files.push_back(ent->d_name);
            }
        }
        closedir(dir);
        std::sort(files.begin(), files.end());
    }
    return files;
}


int main(int argc, char* argv[])
{
    consoleInit(NULL);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    printf("\x1b[2J\x1b[1;1H");
    printf("\x1b[1;36m=== StreamSnagNX sys-snag Controller ===\x1b[0m\n\n");
    printf("Starting up...\n");
    consoleUpdate(NULL);

    SnagClient& client = SnagClient::Instance();
    uint64_t last_tick = 0;
    uint64_t last_connect_attempt = 0;
    Result last_pm_rc = 0;
    u64 last_pid = 0;
    bool attempted_launch = false;

    std::vector<std::string> music_files = ScanLocalMusic();
    int selected_idx = 0;
    std::string last_action = "Ready";

    // Try to launch sysmodule via pmshell if not already started
    {
        Result pm_rc = pmshellInitialize();
        if (R_SUCCEEDED(pm_rc))
        {
            u64 pid = 0;
            Result get_rc = pmshellGetProcessId(&pid, 0x4200000000534E47ULL);
            if (R_SUCCEEDED(get_rc) && pid != 0)
            {
                last_pid = pid;
            }
            else
            {
                const NcmProgramLocation loc{
                    .program_id = 0x4200000000534E47ULL,
                    .storageID  = NcmStorageId_None,
                };
                last_pm_rc = pmshellLaunchProgram(0, &loc, &pid);
                last_pid = pid;
                attempted_launch = true;
            }
            pmshellExit();
        }
        else
        {
            last_pm_rc = pm_rc;
        }
    }

    while (appletMainLoop())
    {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        if (kDown & HidNpadButton_Plus)
            break;

        uint64_t now = armGetSystemTick();
        uint64_t freq = armGetSystemTickFreq();

        bool is_connected = client.IsConnected();

        if (!is_connected)
        {
            // Retry connection every 500ms
            if (now - last_connect_attempt > freq / 2)
            {
                last_connect_attempt = now;
                if (last_pid == 0)
                {
                    Result pm_rc = pmshellInitialize();
                    if (R_SUCCEEDED(pm_rc))
                    {
                        u64 pid = 0;
                        const NcmProgramLocation loc{
                            .program_id = 0x4200000000534E47ULL,
                            .storageID  = NcmStorageId_None,
                        };
                        last_pm_rc = pmshellLaunchProgram(0, &loc, &pid);
                        last_pid = pid;
                        attempted_launch = true;
                        pmshellExit();
                    }
                }
                is_connected = client.Initialize();
            }
        }
        else
        {
            // Navigation
            if (kDown & HidNpadButton_Up)
            {
                if (selected_idx > 0)
                    selected_idx--;
                else if (!music_files.empty())
                    selected_idx = static_cast<int>(music_files.size()) - 1;
            }
            if (kDown & HidNpadButton_Down)
            {
                if (selected_idx + 1 < static_cast<int>(music_files.size()))
                    selected_idx++;
                else
                    selected_idx = 0;
            }

            // Button A: Play / Pause toggle
            if (kDown & HidNpadButton_A)
            {
                SnagStatus st{};
                Result rc = 0;
                bool has_st = client.GetStatus(st, &rc);

                if (has_st && st.is_playing)
                {
                    client.Pause();
                    last_action = "Paused playback";
                }
                else if (!music_files.empty() && selected_idx >= 0 && selected_idx < static_cast<int>(music_files.size()))
                {
                    const std::string& fname = music_files[selected_idx];
                    std::string full_path = "sdmc:/switch/StreamSnagNX/music/" + fname;
                    std::string title, artist;
                    LibraryLookup(VideoIdFromFilename(fname), title, artist);
                    bool ok = client.PlayFile(full_path.c_str(),
                                              title.empty()  ? nullptr : title.c_str(),
                                              artist.empty() ? nullptr : artist.c_str());
                    last_action = ok ? ("Playing [" + std::to_string(selected_idx + 1) + "]: " +
                                        (title.empty() ? fname : title))
                                     : ("Failed to play: " + fname);
                }
                else
                {
                    client.Play();
                    last_action = "Sent Play command";
                }
            }

            // Button X: Force play highlighted track
            if (kDown & HidNpadButton_X)
            {
                if (!music_files.empty() && selected_idx >= 0 && selected_idx < static_cast<int>(music_files.size()))
                {
                    const std::string& fname = music_files[selected_idx];
                    std::string full_path = "sdmc:/switch/StreamSnagNX/music/" + fname;
                    std::string title, artist;
                    LibraryLookup(VideoIdFromFilename(fname), title, artist);
                    bool ok = client.PlayFile(full_path.c_str(),
                                              title.empty()  ? nullptr : title.c_str(),
                                              artist.empty() ? nullptr : artist.c_str());
                    last_action = ok ? ("Forced play: " + (title.empty() ? fname : title))
                                     : ("Failed to play: " + fname);
                }
                else
                {
                    client.Next();
                    last_action = "Next Track";
                }
            }

            // Button Y: Re-scan SD card music folder
            if (kDown & HidNpadButton_Y)
            {
                music_files = ScanLocalMusic();
                if (selected_idx >= static_cast<int>(music_files.size()))
                    selected_idx = 0;
                last_action = "Scanned SD: " + std::to_string(music_files.size()) + " track(s) found";
            }

            // Volume controls
            if (kDown & HidNpadButton_L)
            {
                SnagStatus st{};
                float vol = 1.0f;
                if (client.GetStatus(st)) vol = st.volume;
                vol = std::max(0.0f, vol - 0.05f);
                client.SetVolume(vol);
                last_action = "Volume: " + std::to_string(static_cast<int>(vol * 100.0f)) + "%";
            }
            if (kDown & HidNpadButton_R)
            {
                SnagStatus st{};
                float vol = 1.0f;
                if (client.GetStatus(st)) vol = st.volume;
                vol = std::min(1.0f, vol + 0.05f);
                client.SetVolume(vol);
                last_action = "Volume: " + std::to_string(static_cast<int>(vol * 100.0f)) + "%";
            }

            // Seek
            if (kDown & HidNpadButton_Left)
            {
                SnagStatus st{};
                if (client.GetStatus(st))
                {
                    uint32_t pos = st.position_ms > 10000 ? st.position_ms - 10000 : 0;
                    client.Seek(pos);
                    last_action = "Seek -10s (" + FormatTime(pos) + ")";
                }
            }
            if (kDown & HidNpadButton_Right)
            {
                SnagStatus st{};
                if (client.GetStatus(st))
                {
                    uint32_t pos = st.position_ms + 10000;
                    client.Seek(pos);
                    last_action = "Seek +10s (" + FormatTime(pos) + ")";
                }
            }
        }

        // Render UI unconditionally every 100ms
        if (now - last_tick > freq / 10)
        {
            last_tick = now;
            printf("\x1b[1;1H");

            printf("\x1b[2K\x1b[1;36m=== StreamSnagNX sys-snag Controller ===\x1b[0m\n\n");

            if (!is_connected)
            {
                printf("\x1b[2K\x1b[1;33m[OFFLINE]\x1b[0m Waiting for sysmodule 'snag'...\n\n");
                if (last_pid != 0)
                {
                    printf("\x1b[2KProcess active (PID: %lu), connecting to named port...\n", last_pid);
                }
                else if (attempted_launch)
                {
                    printf("\x1b[2Kpmshell launch result: 0x%08X\n", last_pm_rc);
                }
                printf("\x1b[2KInstall path: sdmc:/atmosphere/contents/4200000000534E47/exefs.nsp\n\n");
                printf("\x1b[2KPress \x1b[1;37m[+]\x1b[0m to return to hbmenu.\n");
            }
            else
            {
                printf("\x1b[2K\x1b[1;32m[CONNECTED]\x1b[0m sys-snag daemon active (PID: %lu)\n\n", last_pid);

                SnagStatus st{};
                Result get_rc = 0;
                bool has_status = client.GetStatus(st, &get_rc);

                if (has_status)
                {
                    printf("\x1b[2KNow Playing:  %s  (Engine: %s)\n",
                        st.is_playing ? "\x1b[1;32m[PLAYING]\x1b[0m" : "\x1b[1;33m[PAUSED/STOPPED]\x1b[0m",
                        st.is_initialized ? "\x1b[1;32mReady\x1b[0m" : "\x1b[1;31mInit Failed\x1b[0m");
                    printf("\x1b[2KTrack:        \x1b[1;37m%s\x1b[0m\n", st.current_title[0] ? st.current_title : "(None)");
                    printf("\x1b[2KArtist:       %s\n", st.current_artist[0] ? st.current_artist : "(None)");
                    printf("\x1b[2KTime:         %s / %s\n", FormatTime(st.position_ms).c_str(), FormatTime(st.duration_ms).c_str());
                    printf("\x1b[2KVolume:       %.0f%%\n\n", st.volume * 100.0f);
                }
                else
                {
                    printf("\x1b[2KNow Playing:  \x1b[1;31m[IPC Status Error 0x%08X]\x1b[0m\n", get_rc);
                    printf("\x1b[2KTrack:        (Awaiting status query...)\n");
                    printf("\x1b[2KArtist:       (None)\n");
                    printf("\x1b[2KTime:         --:-- / --:--\n");
                    printf("\x1b[2KVolume:       --\n\n");
                }

                printf("\x1b[2K--- Music Library (%zu track(s) in sdmc:/switch/StreamSnagNX/music/) ---\n", music_files.size());
                if (music_files.empty())
                {
                    printf("\x1b[2K\x1b[1;31m[!] No music files found on SD card!\x1b[0m\n");
                    printf("\x1b[2K    Download songs in StreamSnagNX, or copy .opus files to SD.\n");
                    printf("\x1b[2K    Press \x1b[1;37m[Y]\x1b[0m to re-scan folder.\n\n");
                }
                else
                {
                    int start_view = std::max(0, selected_idx - 3);
                    int end_view = std::min(static_cast<int>(music_files.size()), start_view + 7);
                    for (int i = start_view; i < end_view; ++i)
                    {
                        bool is_cur = (i == selected_idx);
                        printf("\x1b[2K %s [%d] %s\x1b[0m\n",
                            is_cur ? "\x1b[1;36m>\x1b[1;37m" : " ",
                            i + 1,
                            music_files[i].c_str());
                    }
                    printf("\n");
                }

                if (!last_action.empty())
                {
                    printf("\x1b[2K\x1b[1;33mAction: %s\x1b[0m\n\n", last_action.c_str());
                }
                else
                {
                    printf("\x1b[2K\n\n");
                }

                printf("\x1b[2KControls:\n");
                printf("\x1b[2K  [A] Play / Pause        [X] Play Highlighted Track\n");
                printf("\x1b[2K  [Y] Re-scan SD Library  [L/R] Volume Down / Up\n");
                printf("\x1b[2K  [Left/Right] -10s/+10s  [+] Exit to hbmenu\n");
                printf("\x1b[2K\x1b[1;32mMusic continues playing in background after exiting!\x1b[0m\n");
            }

            consoleUpdate(NULL);
        }
    }

    client.Exit();
    consoleExit(NULL);
    return 0;
}
