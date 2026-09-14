#include "gui_lyrics_hud.hpp"

bool LyricsHudElement::onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY,
                               s32 initialX, s32 initialY)
{
    if (event == tsl::elm::TouchEvent::Release && this->inBounds(currX, currY))
    {
        tsl::hlp::requestForeground(true);
        if (!playerPath_.empty())
        {
            ProbeLog("touch release relaunch player");
            tsl::setNextOverlay(playerPath_);
            tsl::Overlay::get()->close();
        }
        else
            tsl::goBack(); // entered from player: pop back, no growth
        return true;
    }
    return false;
}
