#include "gui_lyrics_hud.hpp"
#include "gui_main.hpp"

bool LyricsHudElement::onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY,
                               s32 initialX, s32 initialY)
{
    if (event == tsl::elm::TouchEvent::Release && this->inBounds(currX, currY))
    {
        tsl::hlp::requestForeground(true);
        if (goPlayer_)
            tsl::goBack(); // entered from player: pop back, no growth
        else
            tsl::changeTo<GuiMain>(); // opened straight in: push player once
        return true;
    }
    return false;
}
