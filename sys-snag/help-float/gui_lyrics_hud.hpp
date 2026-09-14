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
#include "lrc_parser.hpp"
#include "library_lookup.hpp"
#include "../../sys-snag/include/snag_client.hpp"

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
    explicit LyricsHudElement(bool goPlayer)
        : goPlayer_(goPlayer)
    {
    }

    void setLine(const std::string& line)
    {
        line_ = line;
    }

    virtual void draw(tsl::gfx::Renderer* renderer) override
    {
        // Clear first: bare screens have no frame background, so without
        // this old pixels (ghost lyrics) survive forever.
        renderer->fillScreen(tsl::Color{ 0x0, 0x0, 0x0, 0x0 });

        const std::string text = line_.empty() ? "StreamSnagNX (No Track)" : line_;

        // Floating pill stuck to the very bottom of 1280x720 screen.
        const s32 hud_w = 760;
        const s32 hud_h = 52;
        const s32 hud_x = (tsl::cfg::FramebufferWidth - hud_w) / 2;
        const s32 hud_y = tsl::cfg::FramebufferHeight - hud_h - 6;

        // Background pill (dark translucent, alpha 0xC)
        renderer->drawRect(hud_x, hud_y, hud_w, hud_h, tsl::Color{ 0x1, 0x1, 0x1, 0xC });

        // Accent top border line (StreamSnag blue accent)
        renderer->drawRect(hud_x, hud_y, hud_w, 2, tsl::Color{ 0x3, 0x8, 0xF, 0xF });

        // Lyric line, proportional system font, shrunk to fit the pill.
        s32 size = 22;
        auto meas = renderer->drawString(text.c_str(), false, 0, 0, size,
                                         tsl::Color{ 0x0, 0x0, 0x0, 0x0 });
        while (meas.first > 700 && size > 15)
        {
            size -= 2;
            meas = renderer->drawString(text.c_str(), false, 0, 0, size,
                                        tsl::Color{ 0x0, 0x0, 0x0, 0x0 });
        }
        renderer->drawString(text.c_str(), false, tsl::cfg::FramebufferWidth / 2, hud_y + 32, size,
                             tsl::Color{ 0xF, 0xF, 0xF, 0xF });
    }

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override
    {
        this->setBoundaries(0, 0, tsl::cfg::FramebufferWidth, tsl::cfg::FramebufferHeight);
    }

    // Tap the pill -> back to the player (touch isn't a game button, so no
    // conflict with gameplay). Defined in gui_lyrics_hud.cpp (needs GuiMain).
    virtual bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY,
                         s32 initialX, s32 initialY) override;

private:
    std::string line_;
    bool goPlayer_ = false;
};

class GuiLyricsHud : public tsl::Gui {
public:
    // fromPlayer: true when opened from the player screen (tap goes back),
    // false when opened straight into (tap goes forward to the player).
    // Either way the stack never grows: back pops, forward pushes once.
    explicit GuiLyricsHud(bool fromPlayer = false)
        : hud_(nullptr)
        , tick_(0)
        , fromPlayer_(fromPlayer)
    {
        // Game keeps controller input while this screen is up.
        tsl::hlp::requestForeground(false);
    }

    virtual ~GuiLyricsHud()
    {
        // NOTE: deliberately no requestForeground(true) here (mirrors
        // Status Monitor micro): the loader owns focus on transitions,
        // restoring it here steals input back from the game.
    }

    virtual tsl::elm::Element* createUI() override
    {
        hud_ = new LyricsHudElement(fromPlayer_);
        return hud_;
    }

    virtual void update() override
    {
        if (!hud_ || (++tick_ % 8) != 0)
            return;

        // Re-assert every refresh: the framework re-acquires foreground on
        // show/focus changes, which would silently steal the gamepad back.
        tsl::hlp::requestForeground(false);

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

        std::string line;
        if (!lyrics_.empty())
        {
            line = LrcParser::GetActiveLine(lyrics_, use_ms);
            if (line.empty())
                line = "...";
        }
        else if (status.current_title[0] != '\0')
        {
            line = std::string("\u266A  ") + status.current_title;
            if (status.current_artist[0] != '\0')
                line += " \u2022 " + std::string(status.current_artist);
        }
        hud_->setLine(line);
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState& touchPos,
                             HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override
    {
        // Every button belongs to the game here (B included: dodging must
        // never yank you back to the player). Swallow B so Tesla never
        // navigates away on it.
        // The menu chord (LB + Down + RS click) is polled OURSELVES like
        // Status Monitor micro does: it relaunches our own player menu
        // instead of trusting the loader hide/show toggle (which traps you
        // flipping one screen on and off with no way back to any menu).
        static PadState pad;
        static bool padInit = false;
        if (!padInit)
        {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&pad);
            padInit = true;
        }
        padUpdate(&pad);
        const u64 CHORD = HidNpadButton_L | HidNpadButton_Down | HidNpadButton_StickR;
        if ((padGetButtons(&pad) & CHORD) == CHORD && (padGetButtonsDown(&pad) & CHORD) != 0)
        {
            // Relaunch ourselves into the PLAYER menu (never back to float:
            // "--player" overrides the auto-float rule). From the player,
            // B exits normally to the loader menu. No more loops.
            extern std::string g_ovlPath;
            if (!g_ovlPath.empty())
                tsl::setNextOverlay(g_ovlPath, "--player");
            tsl::Overlay::get()->close();
            return true;
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
    bool fromPlayer_ = false;
};
