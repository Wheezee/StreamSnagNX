#pragma once
// Floating lyrics pill. sys-tune rule: data updates in update() (~7 Hz),
// draw() only paints the cached line. No IPC or file IO in draw().
//
// Display model (Status Monitor micro style): this screen stays open with
// the game visible behind it and the game keeping controller input, so it
// feels always-on-top. B goes back one screen to the player section; the
// Tesla/Ultrahand menu combo (LB + Down + RS click) is handled by the
// loader itself and drops straight back to the game.
#include <tesla.hpp>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include "lrc_parser.hpp"
#include "library_lookup.hpp"
#include "../../sys-snag/include/snag_client.hpp"

namespace {

// PROBE (temporary): SD flight recorder. Proves what the host actually
// delivers: input events, touch, chord progress. Delete before release.
inline void ProbeLog(const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    FILE* fp = std::fopen("sdmc:/switch/StreamSnagNX/ovl-probe.log", "a");
    if (!fp)
        return;
    std::fputs(buf, fp);
    std::fputc('\n', fp);
    std::fclose(fp);
}

} // namespace

namespace {

// Per-track lyric delay, mirroring the main app (settings.json:
// "lrc_offsets": { "<video_id>": <seconds> }, applied as pos + offset).
// Missing file/entry -> 0.0.
inline double LoadLrcOffsetSec(const std::string& video_id)
{
    if (video_id.empty())
        return 0.0;
    FILE* fp = std::fopen("sdmc:/switch/StreamSnagNX/settings.json", "rb");
    if (!fp)
        return 0.0;
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 256 * 1024)
    {
        std::fclose(fp);
        return 0.0;
    }
    std::string data(static_cast<size_t>(sz), '\0');
    size_t got = std::fread(&data[0], 1, static_cast<size_t>(sz), fp);
    std::fclose(fp);
    data.resize(got);

    size_t grp = data.find("\"lrc_offsets\"");
    if (grp == std::string::npos)
        return 0.0;
    size_t open = data.find('{', grp);
    size_t close = (open == std::string::npos) ? std::string::npos : data.find('}', open);
    if (open == std::string::npos || close == std::string::npos)
        return 0.0;

    std::string qk = std::string("\"") + video_id + "\"";
    size_t kp = data.find(qk, open);
    if (kp == std::string::npos || kp > close)
        return 0.0;
    size_t p = kp + qk.size();
    while (p < close && (data[p] == ' ' || data[p] == '\t' || data[p] == '\n' || data[p] == '\r'))
        p++;
    if (p >= close || data[p] != ':')
        return 0.0;
    p++;
    while (p < close && (data[p] == ' ' || data[p] == '\t' || data[p] == '\n' || data[p] == '\r'))
        p++;
    if (p >= close)
        return 0.0;
    char* endp = nullptr;
    double v = std::strtod(data.c_str() + p, &endp);
    if (endp == data.c_str() + p)
        return 0.0;
    if (v < -30.0 || v > 30.0) // sanity: offsets are 0.5s steps
        return 0.0;
    return v;
}

} // namespace

class LyricsHudElement : public tsl::elm::Element {
public:
    // playerPath: .ovl to relaunch for the player screen (float binary).
    // Empty: plain goBack (player binary's embedded float screen).
    explicit LyricsHudElement(const std::string& playerPath = "")
        : playerPath_(playerPath)
    {
    }

    void setLine(const std::string& line)
    {
        line_ = line;
    }

    virtual void draw(tsl::gfx::Renderer* renderer) override
    {
        // Bottom-anchor the strip layer once, float binary only (playerPath
        // set): Status Monitor micro pattern on our lean 1920x96 layer.
        // Guarded: layer position persists once set.
        static bool layerPlaced = false;
        if (!layerPlaced && !playerPath_.empty())
        {
            renderer->setLayerPos(0, 1080 - 96);
            layerPlaced = true;
        }

        // Clear first: bare screens have no frame background, so without
        // this old pixels (ghost lyrics) survive forever.
        renderer->fillScreen(tsl::Color{ 0x0, 0x0, 0x0, 0x0 });

        const std::string text = line_.empty() ? "StreamSnagNX (No Track)" : line_;

        // Full-width bottom strip, Status Monitor micro style: solid bar
        // (readable on any background), accent top border, left text.
        // (Layer is fullscreen 1280x720 now — see main().)
        const s32 bar_h = 64;
        const s32 bar_y = tsl::cfg::FramebufferHeight - bar_h;
        const s32 bar_w = tsl::cfg::FramebufferWidth;

        // Background bar (25% transparent black)
        renderer->drawRect(0, bar_y, bar_w, bar_h, tsl::Color{ 0x0, 0x0, 0x0, 0xB });

        // Accent top border line (StreamSnag blue accent)
        renderer->drawRect(0, bar_y, bar_w, 3, tsl::Color{ 0x3, 0x8, 0xF, 0xF });

        // Lyric line, proportional system font, shrunk to fit the bar,
        // then centered.
        s32 size = 26;
        auto meas = renderer->drawString(text.c_str(), false, 0, 0, size,
                                         tsl::Color{ 0x0, 0x0, 0x0, 0x0 });
        while (meas.first > static_cast<u32>(bar_w - 48) && size > 16)
        {
            size -= 2;
            meas = renderer->drawString(text.c_str(), false, 0, 0, size,
                                        tsl::Color{ 0x0, 0x0, 0x0, 0x0 });
        }
        s32 tx = (bar_w - static_cast<s32>(meas.first)) / 2;
        if (tx < 0)
            tx = 0;
        renderer->drawString(text.c_str(), false, tx, bar_y + 40, size,
                             tsl::Color{ 0xF, 0xF, 0xF, 0xF });

        // Chord hint, small and dim at the bar's right edge.
        const char* hint = "ZL+ZR+\u25BC";
        auto hmeas = renderer->drawString(hint, false, 0, 0, 16,
                                          tsl::Color{ 0x0, 0x0, 0x0, 0x0 });
        renderer->drawString(hint, false, bar_w - static_cast<s32>(hmeas.first) - 16, bar_y + 40, 16,
                             tsl::Color{ 0xA, 0xA, 0xA, 0xF });
    }

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override
    {
        this->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight);
        // PROBE: ground truth on what the host actually gives us.
        ProbeLog("hud layout x=%d y=%d w=%d h=%d cfg=%dx%d", this->getX(), this->getY(),
                 this->getWidth(), this->getHeight(),
                 (int)tsl::cfg::FramebufferWidth, (int)tsl::cfg::FramebufferHeight);
    }

    // Tap the pill -> back to the player (touch isn't a game button, so no
    // conflict with gameplay). Defined in gui_lyrics_hud.cpp (needs GuiMain).
    virtual bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY,
                         s32 initialX, s32 initialY) override;

private:
    std::string line_;
    std::string playerPath_;
};

class GuiLyricsHud : public tsl::Gui {
public:
    // playerPath: empty = entered from the player screen in-process (tap
    // and chord step back); set = separate float binary (tap and chord
    // relaunch the player .ovl).
    explicit GuiLyricsHud(const std::string& playerPath = "")
        : hud_(nullptr)
        , tick_(0)
        , playerPath_(playerPath)
    {
        // Game keeps controller input while this screen is up.
        tsl::hlp::requestForeground(false);
        ProbeLog("float created playerPath=%s", playerPath_.empty() ? "(in-stack)" : "relaunch");
    }

    virtual ~GuiLyricsHud()
    {
        // NOTE: deliberately no requestForeground(true) here (mirrors
        // Status Monitor micro): the loader owns focus on transitions,
        // restoring it here steals input back from the game.
    }

    virtual tsl::elm::Element* createUI() override
    {
        hud_ = new LyricsHudElement(playerPath_);
        return hud_;
    }

    virtual void update() override
    {
        if (!hud_ || (++tick_ % 8) != 0)
            return;

        // Re-assert every refresh: the framework re-acquires foreground on
        // show/focus changes, which would silently steal the gamepad back.
        tsl::hlp::requestForeground(false);

        // PROBE heartbeat (~7s): proves the loop is alive + last input seen.
        if ((probeTick_++ % 50) == 0)
            ProbeLog("float alive down=%llx held=%llx", (unsigned long long)lastDown_, (unsigned long long)lastHeld_);

        SnagStatus status{};
        if (!SnagClient::Instance().GetStatus(status))
        {
            hud_->setLine("");
            return;
        }

        // Reload LRC if track changed (file IO here, never in draw()).
        if (std::string(status.current_path) != last_path_)
        {
            last_path_ = status.current_path;
            lyrics_.clear();
            lrc_offset_sec_ = 0.0;
            if (!last_path_.empty())
            {
                std::string vid = ssnx::VideoIdFromPath(last_path_);
                lrc_offset_sec_ = LoadLrcOffsetSec(vid);
                std::string lrc_path = last_path_;
                size_t dot = lrc_path.rfind('.');
                if (dot != std::string::npos)
                {
                    lrc_path = lrc_path.substr(0, dot) + ".lrc";
                    lyrics_ = LrcParser::LoadFromFile(lrc_path);
                }
            }
        }

        // Same sign as the main app: effective_pos = position + offset.
        int64_t eff_ms = static_cast<int64_t>(status.position_ms) +
                         static_cast<int64_t>(lrc_offset_sec_ * 1000.0);
        uint32_t use_ms = eff_ms < 0 ? 0 : static_cast<uint32_t>(eff_ms);

        std::string title_line;
        if (status.current_title[0] != '\0')
        {
            title_line = std::string("\u266A  ") + status.current_title;
            if (status.current_artist[0] != '\0')
                title_line += " \u2022 " + std::string(status.current_artist);
        }

        std::string line;
        if (!lyrics_.empty())
        {
            // Before the first timestamp (or gaps): show the song, never "...".
            line = LrcParser::GetActiveLine(lyrics_, use_ms);
            if (line.empty())
                line = title_line;
        }
        else
            line = title_line;
        hud_->setLine(line);
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                             HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override
    {
        // Every button belongs to the game here (B included: dodging must
        // never yank you back to the player). Swallow B so Tesla never
        // navigates away on it.
        // Exit chord, polled OURSELVES like Status Monitor micro does.
        // NOTE: this must NOT be L + Down + RS-click — that combo belongs
        // to the loader itself, so sharing it races the loader. ZL + ZR +
        // Minus held 200 ms instead (same debounce Status Monitor ships).
        //
        // The chord steps BACK to the player screen with a plain in-stack
        // pop (entered from player) or exits to the loader menu (opened
        // straight into float: the stack empties). No setNextOverlay, no
        // close(): those depend on loader APIs Ultrahand visibly ignores,
        // which is what trapped us in the hide/show loop. From the player,
        // B exits normally. Every screen has an exit again.
        static PadState pad;
        static bool padInit = false;
        static uint64_t chordStartTick = 0;
        if (!padInit)
        {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&pad);
            padInit = true;
        }
        padUpdate(&pad);
        lastDown_ = keysDown;
        lastHeld_ = keysHeld;
        if (keysDown != 0)
            ProbeLog("input down=%llx held=%llx", (unsigned long long)keysDown, (unsigned long long)keysHeld);
        // Exit chord: ZL + ZR + Down-pad, held 200 ms.
        const u64 CHORD = HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Down;
        if ((padGetButtons(&pad) & CHORD) == CHORD)
        {
            if (chordStartTick == 0)
            {
                chordStartTick = armGetSystemTick();
                ProbeLog("chord hold start");
            }
            if (armTicksToNs(armGetSystemTick() - chordStartTick) >= 200000000ull)
            {
                chordStartTick = 0;
                if (!playerPath_.empty())
                {
                    ProbeLog("CHORD FIRED relaunch player");
                    tsl::setNextOverlay(playerPath_);
                    tsl::Overlay::get()->close();
                }
                else
                {
                    ProbeLog("CHORD FIRED -> goBack");
                    tsl::goBack();
                }
                return true;
            }
        }
        else
        {
            chordStartTick = 0;
        }
        if (keysDown & HidNpadButton_B)
            return true;
        return false;
    }

private:
    LyricsHudElement* hud_;
    std::string last_path_;
    std::vector<LyricEntry> lyrics_;
    double lrc_offset_sec_ = 0.0;
    u8 tick_;
    std::string playerPath_;
    // PROBE state (temporary).
    u32 probeTick_ = 0;
    u64 lastDown_ = 0;
    u64 lastHeld_ = 0;
};
