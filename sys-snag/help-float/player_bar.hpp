#pragma once
// sys-tune-style player bar (StatusBar), trimmed: title + artist, progress
// bar with knob + times, and prev / play-pause / next only.
// No repeat, shuffle, or seek. Refreshes itself in place via update()
// (called 4x/sec by the owning Gui) — screens are never rebuilt per click,
// so navigation never stacks Guis in memory.
#include <tesla.hpp>
#include <string>
#include <cstdio>
#include <algorithm>
#include "library_lookup.hpp"
#include "../../sys-snag/include/snag_client.hpp"

class PlayerBar final : public tsl::elm::Element {
public:
    PlayerBar()
    {
        this->refresh(true);
    }

    // Poll daemon (throttled to ~12 Hz unless forced). NOTE: the owning Gui
    // must call this every frame — no second throttle on top of this one.
    void refresh(bool force = false)
    {
        if (!force)
        {
            if ((++tick_ % 5) != 0)
                return;
        }
        SnagStatus st{};
        online_ = SnagClient::Instance().GetStatus(st);
        if (!online_)
            return;
        playing_ = st.is_playing != 0;
        pos_ms_ = st.position_ms;
        dur_ms_ = st.duration_ms;

        std::string path = st.current_path;
        if (path != last_path_)
        {
            last_path_ = path;
            title_.clear();
            artist_.clear();
            if (!path.empty())
            {
                ssnx::LibraryDb db = ssnx::LibraryDb::Load();
                db.lookup(ssnx::VideoIdFromPath(path), title_, artist_);
            }
            if (title_.empty())
                title_ = st.current_title;
            if (artist_.empty())
                artist_ = st.current_artist;
            if (title_.empty())
                title_ = last_path_.empty() ? "No Track" : ssnx::VideoIdFromPath(last_path_);
        }
    }

    virtual tsl::elm::Element* requestFocus(tsl::elm::Element* oldFocus, tsl::FocusDirection direction) override
    {
        return this;
    }

    virtual bool onClick(u64 keys) override
    {
        if (keys & HidNpadButton_A)
        {
            if (playing_)
                SnagClient::Instance().Pause();
            else
                SnagClient::Instance().Play();
            this->refresh(true);
            return true;
        }
        if (keys & HidNpadButton_Left)
        {
            SnagClient::Instance().Prev();
            this->refresh(true);
            return true;
        }
        if (keys & HidNpadButton_Right)
        {
            SnagClient::Instance().Next();
            this->refresh(true);
            return true;
        }
        return false;
    }

    virtual void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override
    {
        this->setBoundaries(this->getX(), this->getY(), this->getWidth(),
                            tsl::style::ListItemDefaultHeight * 3);
    }

    virtual void draw(tsl::gfx::Renderer* renderer) override
    {
        using namespace tsl::style::color;
        const s32 x = this->getX();
        const s32 y = this->getY();
        const s32 w = this->getWidth();

        if (!online_)
        {
            renderer->drawString("Daemon Offline", false, x + 15, y + 45, 23, ColorText);
            return;
        }

        // Title + artist.
        renderer->drawString(Trunc(title_, 40).c_str(), false, x + 15, y + 36, 24, ColorText);
        renderer->drawString(Trunc(artist_, 48).c_str(), false, x + 15, y + 60, 18, ColorDescription);

        // Progress bar.
        const s32 bar_y = y + 92;
        const s32 bar_w = w - 30;
        float p = 0.0f;
        if (dur_ms_ > 0)
            p = std::clamp(static_cast<float>(pos_ms_) / static_cast<float>(dur_ms_), 0.0f, 1.0f);
        renderer->drawRect(x + 15, bar_y, bar_w, 5, ColorHeaderBar);
        if (p > 0.0f)
        {
            renderer->drawRect(x + 15, bar_y, static_cast<s32>(bar_w * p), 5, ColorHighlight);
            renderer->drawCircle(x + 15 + static_cast<s32>(bar_w * p), bar_y + 2, 6, true, ColorHighlight);
        }

        // Times.
        renderer->drawString(FmtTime(pos_ms_).c_str(), false, x + 15, y + 118, 18, ColorText);
        std::string total = FmtTime(dur_ms_);
        auto [tw, th] = renderer->drawString(total.c_str(), false, 0, 0, 18, ColorTransparent);
        renderer->drawString(total.c_str(), false, x + w - 15 - tw, y + 118, 18, ColorText);

        // Controls: prev | play-pause | next, measured then centered as a
        // group with equal gaps (no eyeballed coordinates).
        const s32 cy = y + 168;
        const s32 cx = x + w / 2;
        const s32 gap = 44;
        const s32 prev_w = static_cast<s32>(
            renderer->drawString("\u25C0\u25C0", false, 0, 0, 30, ColorTransparent).first);
        const s32 mid_w = playing_ ? 30 : static_cast<s32>(
            renderer->drawString("\u25B6", false, 0, 0, 34, ColorTransparent).first);
        const s32 next_w = static_cast<s32>(
            renderer->drawString("\u25B6\u25B6", false, 0, 0, 30, ColorTransparent).first);
        const s32 x0 = cx - (prev_w + gap + mid_w + gap + next_w) / 2;

        renderer->drawString("\u25C0\u25C0", false, x0, cy, 30, ColorText);
        if (playing_)
        {
            // Pause bars aligned to the text baseline like the triangles.
            const s32 mx = x0 + prev_w + gap;
            renderer->drawRect(mx, cy - 30, 10, 30, ColorHighlight);
            renderer->drawRect(mx + 20, cy - 30, 10, 30, ColorHighlight);
        }
        else
        {
            renderer->drawString("\u25B6", false, x0 + prev_w + gap, cy, 34, ColorHighlight);
        }
        renderer->drawString("\u25B6\u25B6", false, x0 + prev_w + gap + mid_w + gap, cy, 30, ColorText);
    }

private:
    static std::string FmtTime(uint32_t ms)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%02u:%02u", (ms / 1000) / 60, (ms / 1000) % 60);
        return buf;
    }

    static std::string Trunc(const std::string& s, size_t max)
    {
        if (s.size() <= max)
            return s;
        return s.substr(0, max - 3) + "...";
    }

    bool online_ = false;
    bool playing_ = false;
    uint32_t pos_ms_ = 0;
    uint32_t dur_ms_ = 0;
    std::string last_path_;
    std::string title_;
    std::string artist_;
    u8 tick_ = 0;
};
