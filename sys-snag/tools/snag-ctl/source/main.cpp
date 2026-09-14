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

// Whitespace-tolerant "key" : "value" finder. library.json is written with
// JSON_INDENT(2), so `"key":"value"` never appears verbatim — there is
// always a space after the colon. Returns the value, the end offset, and
// the key position (for locating the enclosing object).
static bool JsonFindString(const std::string& json, const char* key, size_t from,
                           std::string& out_val, size_t& out_end, size_t& out_keypos)
{
    std::string qk = std::string("\"") + key + "\"";
    size_t search = from;
    while ((search = json.find(qk, search)) != std::string::npos)
    {
        out_keypos = search;
        size_t p = search + qk.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r'))
            p++;
        if (p >= json.size() || json[p] != ':')
        {
            search += qk.size();
            continue;
        }
        p++;
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r'))
            p++;
        if (p >= json.size() || json[p] != '"')
        {
            search += qk.size();
            continue;
        }
        p++;
        std::string val;
        while (p < json.size() && json[p] != '"')
        {
            if (json[p] == '\\' && p + 1 < json.size())
            {
                p++;
                if (json[p] == 'n')      val += '\n';
                else if (json[p] == 't') val += '\t';
                else                     val += json[p];
            }
            else
            {
                val += json[p];
            }
            p++;
        }
        if (p >= json.size())
            return false;
        out_val = val;
        out_end = p + 1;
        return true;
    }
    return false;
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

    std::string id, dummy;
    size_t id_end = 0, keypos = 0, search = 0;
    while (JsonFindString(data, "video_id", search, id, id_end, keypos))
    {
        search = id_end;
        if (id != video_id)
            continue;
        size_t obj_start = data.rfind('{', keypos);
        if (obj_start == std::string::npos)
            return false;
        size_t obj_end = data.find('}', id_end);
        if (obj_end == std::string::npos)
            return false;

        std::string obj = data.substr(obj_start, obj_end - obj_start + 1);
        size_t e1 = 0, e2 = 0;
        JsonFindString(obj, "title", 0, out_title, e1, e2);
        JsonFindString(obj, "artist", 0, out_artist, e1, e2);
        if (out_artist.empty())
            JsonFindString(obj, "subtitle", 0, out_artist, e1, e2);
        return !out_title.empty();
    }
    return false;
}

// Extract video_id from filename (strip extension — filename IS the video_id)
static std::string VideoIdFromFilename(const std::string& filename)
{
    size_t dot = filename.rfind('.');
    if (dot != std::string::npos)
        return filename.substr(0, dot);
    return filename;
}

struct CtlTrack {
    std::string video_id;
    std::string title;
    std::string artist;
    std::string path;    // full sdmc:/... path to the audio file
    std::string display; // title, or stripped filename when unknown
    bool from_library = false;
};

#define SNAGCTL_BUILDNO 6

static bool FileExists(const std::string& path)
{
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp)
    {
        fclose(fp);
        return true;
    }
    return false;
}

static const char* kAudioExts[] = { "opus", "webm", "ogg", "m4a" };

// Library-driven track list: every library entry whose audio file is
// actually on disk, with real titles. Skips entries with no file.
static std::vector<CtlTrack> LoadLibraryTracks()
{
    std::vector<CtlTrack> out;
    FILE* fp = fopen("sdmc:/switch/StreamSnagNX/library.json", "rb");
    if (!fp)
        return out;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 2 * 1024 * 1024) { fclose(fp); return out; }

    std::string data(static_cast<size_t>(sz), '\0');
    fread(&data[0], 1, static_cast<size_t>(sz), fp);
    fclose(fp);

    const std::string music_dir = "sdmc:/switch/StreamSnagNX/music/";
    size_t search = 0;
    std::string vid, field;
    size_t field_end = 0, keypos = 0, e1 = 0, e2 = 0;
    while (JsonFindString(data, "video_id", search, vid, field_end, keypos))
    {
        search = field_end;
        if (vid.empty())
            continue;

        // Skip repeat entries for the same video_id.
        bool seen = false;
        for (const auto& o : out)
            if (o.video_id == vid) { seen = true; break; }
        if (seen)
            continue;

        size_t obj_start = data.rfind('{', keypos);
        size_t obj_end = data.find('}', field_end);
        if (obj_start == std::string::npos || obj_end == std::string::npos)
            continue;
        std::string obj = data.substr(obj_start, obj_end - obj_start + 1);
        std::string title, artist, ext, local;
        JsonFindString(obj, "title", 0, title, e1, e2);
        JsonFindString(obj, "artist", 0, artist, e1, e2);
        if (artist.empty())
            JsonFindString(obj, "subtitle", 0, artist, e1, e2);
        JsonFindString(obj, "ext", 0, ext, e1, e2);
        JsonFindString(obj, "local_audio_path", 0, local, e1, e2);

        CtlTrack t;
        t.video_id = vid;
        // Prefer the stored absolute path when the file is really there.
        if (!local.empty() && FileExists(local))
        {
            t.path = local;
        }
        else
        {
            // Otherwise probe <video_id>.<ext>, then all known exts.
            if (!ext.empty())
            {
                std::string cand = music_dir + vid + "." + ext;
                if (FileExists(cand))
                    t.path = cand;
            }
            if (t.path.empty())
            {
                for (const char* e : kAudioExts)
                {
                    std::string cand = music_dir + vid + "." + e;
                    if (FileExists(cand))
                    {
                        t.path = cand;
                        break;
                    }
                }
            }
            if (t.path.empty())
                continue; // in library but not downloaded — skip
        }
        t.title = title;
        t.artist = artist;
        t.display = title.empty() ? vid : title;
        t.from_library = true;
        out.push_back(t);
    }
    return out;
}

// Library-driven track list ONLY: every library entry whose audio file is
// actually on disk, with real titles. On-disk files with no library entry
// (caches/partials) are deliberately not shown.
static std::vector<CtlTrack> BuildTrackList()
{
    if (fsdevGetDeviceFileSystem("sdmc") == nullptr)
        fsdevMountSdmc();

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/StreamSnagNX", 0777);
    mkdir("sdmc:/switch/StreamSnagNX/music", 0777);

    return LoadLibraryTracks();
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

    std::vector<CtlTrack> music_files = BuildTrackList();
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
                    const CtlTrack& track = music_files[selected_idx];
                    bool ok = client.PlayFile(track.path.c_str(),
                                              track.title.empty()  ? nullptr : track.title.c_str(),
                                              track.artist.empty() ? nullptr : track.artist.c_str());
                    last_action = ok ? ("Playing [" + std::to_string(selected_idx + 1) + "]: " + track.display)
                                     : ("Failed to play: " + track.display);
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
                    const CtlTrack& track = music_files[selected_idx];
                    bool ok = client.PlayFile(track.path.c_str(),
                                              track.title.empty()  ? nullptr : track.title.c_str(),
                                              track.artist.empty() ? nullptr : track.artist.c_str());
                    last_action = ok ? ("Forced play: " + track.display)
                                     : ("Failed to play: " + track.display);
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
                music_files = BuildTrackList();
                if (selected_idx >= static_cast<int>(music_files.size()))
                    selected_idx = 0;
                last_action = "Read library: " + std::to_string(music_files.size()) + " track(s) with audio";
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

            printf("\x1b[2K\x1b[1;36m=== StreamSnagNX sys-snag Controller ===\x1b[0m\n");
            printf("\x1b[2K\x1b[1;30msnag-ctl build %s %s (#%d)\x1b[0m\n\n", __DATE__, __TIME__, SNAGCTL_BUILDNO);

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
                    // Resolve display names from library.json (daemon only stores
                    // what the client sent; fall back to its strings, then the
                    // filename with the extension stripped).
                    std::string disp_title = st.current_title;
                    std::string disp_artist = st.current_artist;
                    {
                        std::string base = st.current_path;
                        size_t slash = base.find_last_of("/\\");
                        if (slash != std::string::npos)
                            base = base.substr(slash + 1);
                        std::string lib_title, lib_artist;
                        if (LibraryLookup(VideoIdFromFilename(base), lib_title, lib_artist))
                        {
                            disp_title = lib_title;
                            if (!lib_artist.empty())
                                disp_artist = lib_artist;
                        }
                        else if (!disp_title.empty())
                        {
                            for (const char* ext : {".opus", ".webm", ".ogg", ".m4a"})
                            {
                                size_t elen = std::strlen(ext);
                                if (disp_title.size() > elen &&
                                    strcasecmp(disp_title.c_str() + disp_title.size() - elen, ext) == 0)
                                {
                                    disp_title = disp_title.substr(0, disp_title.size() - elen);
                                    break;
                                }
                            }
                        }
                    }
                    printf("\x1b[2KTrack:        \x1b[1;37m%s\x1b[0m\n", disp_title.empty() ? "(None)" : disp_title.c_str());
                    printf("\x1b[2KArtist:       %s\n", disp_artist.empty() ? "(None)" : disp_artist.c_str());
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

                printf("\x1b[2K--- Music Library (%zu track(s) in sdmc:/switch/StreamSnagNX/music/) ---\n", music_files.size());                if (music_files.empty())
                {
                    printf("\x1b[2K\x1b[1;31m[!] No library tracks with downloaded audio!\x1b[0m\n");
                    printf("\x1b[2K    Download songs in StreamSnagNX first.\n");
                    printf("\x1b[2K    Press \x1b[1;37m[Y]\x1b[0m to re-read library.json.\n\n");
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
                            music_files[i].display.c_str());
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
                printf("\x1b[2K  [Y] Reload library       [L/R] Volume Down / Up\n");
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
