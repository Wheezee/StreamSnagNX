#pragma once
#include <tesla.hpp>
#include <string>
#include "player_bar.hpp"
#include "gui_songs.hpp"
#include "gui_lyrics_hud.hpp"
#include "../../sys-snag/include/snag_client.hpp"

class GuiMain : public tsl::Gui {
public:
    GuiMain()
        : bar_(nullptr)
    {
    }

    virtual tsl::elm::Element* createUI() override
    {
        auto* frame = new tsl::elm::OverlayFrame("StreamSnag", "Background Music");
        auto* list = new tsl::elm::List();

        SnagStatus status{};
        if (!SnagClient::Instance().GetStatus(status))
        {
            list->addItem(new tsl::elm::CategoryHeader("Daemon Offline"));
            list->addItem(new tsl::elm::ListItem("sys-snag is not running"));
            list->addItem(new tsl::elm::ListItem("Install to atmosphere/contents/"));
            list->addItem(new tsl::elm::ListItem("TitleID: 4200000000534E47"));
            frame->setContent(list);
            return frame;
        }

        // Live player bar (updates in place — never rebuilt per click).
        bar_ = new PlayerBar();
        list->addItem(bar_, tsl::style::ListItemDefaultHeight * 3);

        // Volume stepper (icon only — the widget draws icon + bar itself).
        list->addItem(new tsl::elm::CategoryHeader("Volume"));
        auto* vol_bar = new tsl::elm::StepTrackBar("\uE13C", 10);
        vol_bar->setProgress(static_cast<u8>(status.volume * 10.0f));
        vol_bar->setValueChangedListener([](u8 progress) {
            SnagClient::Instance().SetVolume(progress / 10.0f);
        });
        list->addItem(vol_bar);

        // Library browser.
        auto* browse_btn = new tsl::elm::ListItem("Browse Library...", ">");
        browse_btn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A)
            {
                tsl::changeTo<GuiSongList>();
                return true;
            }
            return false;
        });
        list->addItem(browse_btn);

        // Lyrics HUD.
        auto* lyrics_hud_btn = new tsl::elm::ListItem("\u266A  Floating Lyrics", ">");
        lyrics_hud_btn->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A)
            {
                tsl::changeTo<GuiLyricsHud>(true);
                return true;
            }
            return false;
        });
        list->addItem(lyrics_hud_btn);

        frame->setContent(list);
        return frame;
    }

    // Live refresh every frame; the bar throttles itself (~12 Hz).
    // B goes back one screen (Tesla default); screens are never stacked,
    // so there is nothing stale to walk through.
    virtual void update() override
    {
        if (bar_)
            bar_->refresh();
    }

private:
    PlayerBar* bar_;
};
