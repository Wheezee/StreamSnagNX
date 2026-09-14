#pragma once

#include <borealis.hpp>
#include <algorithm>

namespace ssnx::shell
{

enum class PlayerIconType
{
    Play,
    Pause,
    Prev,
    Next
};

class PlayerIconView : public brls::View
{
  public:
    PlayerIconView(PlayerIconType type, NVGcolor color, float size = 18.0f)
        : type_(type), color_(color), size_(size)
    {
        setWidth(size_);
        setHeight(size_);
    }

    void setIconType(PlayerIconType type)
    {
        if (type_ != type)
        {
            type_ = type;
            invalidate();
        }
    }

    void setColor(NVGcolor color)
    {
        color_ = color;
        invalidate();
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        (void)style;
        (void)ctx;

        const float cx = x + width * 0.5f;
        const float cy = y + height * 0.5f;
        const float s = std::min(width, height) * 0.5f;

        nvgBeginPath(vg);

        if (type_ == PlayerIconType::Play)
        {
            // Right-pointing triangle (centered visually)
            nvgMoveTo(vg, cx - s * 0.45f, cy - s * 0.75f);
            nvgLineTo(vg, cx + s * 0.75f, cy);
            nvgLineTo(vg, cx - s * 0.45f, cy + s * 0.75f);
            nvgClosePath(vg);
            nvgFillColor(vg, color_);
            nvgFill(vg);
        }
        else if (type_ == PlayerIconType::Pause)
        {
            // Two vertical rounded bars
            const float bar_w = s * 0.38f;
            const float bar_h = s * 1.4f;
            const float gap = s * 0.20f;
            const float r = s * 0.1f;

            nvgRoundedRect(vg, cx - bar_w - gap, cy - bar_h * 0.5f, bar_w, bar_h, r);
            nvgRoundedRect(vg, cx + gap, cy - bar_h * 0.5f, bar_w, bar_h, r);
            nvgFillColor(vg, color_);
            nvgFill(vg);
        }
        else if (type_ == PlayerIconType::Prev)
        {
            // Vertical bar on left + left-pointing triangle
            const float bar_w = s * 0.30f;
            const float bar_h = s * 1.3f;
            const float r = s * 0.1f;

            nvgRoundedRect(vg, cx - s * 0.75f, cy - bar_h * 0.5f, bar_w, bar_h, r);
            nvgMoveTo(vg, cx + s * 0.75f, cy - s * 0.7f);
            nvgLineTo(vg, cx - s * 0.25f, cy);
            nvgLineTo(vg, cx + s * 0.75f, cy + s * 0.7f);
            nvgClosePath(vg);
            nvgFillColor(vg, color_);
            nvgFill(vg);
        }
        else if (type_ == PlayerIconType::Next)
        {
            // Right-pointing triangle + vertical bar on right
            const float bar_w = s * 0.30f;
            const float bar_h = s * 1.3f;
            const float r = s * 0.1f;

            nvgMoveTo(vg, cx - s * 0.75f, cy - s * 0.7f);
            nvgLineTo(vg, cx + s * 0.25f, cy);
            nvgLineTo(vg, cx - s * 0.75f, cy + s * 0.7f);
            nvgClosePath(vg);
            nvgRoundedRect(vg, cx + s * 0.45f, cy - bar_h * 0.5f, bar_w, bar_h, r);
            nvgFillColor(vg, color_);
            nvgFill(vg);
        }
    }

  private:
    PlayerIconType type_;
    NVGcolor color_;
    float size_;
};

} // namespace ssnx::shell

