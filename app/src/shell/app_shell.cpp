#include "app_shell.hpp"
#include "player_icon_view.hpp"

#include "../audio/now_playing.hpp"
#include "../audio/player.hpp"
#include "../library/library_store.hpp"
#include "../library_tab.hpp"
#include "../nowplaying_tab.hpp"
#include "../search_tab.hpp"
#include "../settings/settings_store.hpp"
#include "../settings_tab.hpp"

#include <algorithm>
#include <cstdio>

namespace ssnx::shell
{

AppShell::AppShell()
    : brls::Box(brls::Axis::COLUMN)
{
    setGrow(1.0f);
    setBackgroundColor(nvgRGB(9, 10, 12));

    // Create tab views once so their state is preserved across switches
    search_tab_ = new SearchTab();
    library_tab_ = new LibraryTab();
    nowplaying_tab_ = new NowPlayingTab();
    settings_tab_ = new SettingsTab();

    search_tab_->SetOnPlayStarted([this]() {
        SelectTab(2); // Jump to Now Playing
    });
    library_tab_->SetOnPlayStarted([this]() {
        SelectTab(2); // Jump to Now Playing
    });

    BuildHeader();

    // Divider between Header and Content
    auto* top_divider = new brls::Rectangle();
    top_divider->setHeight(1);
    top_divider->setColor(nvgRGBA(255, 255, 255, 20));
    addView(top_divider);

    // Content container (grows to fill available space between header and player)
    content_container_ = new brls::Box(brls::Axis::COLUMN);
    content_container_->setGrow(1.0f);
    addView(content_container_);

    // Divider between Content and Mini-Player
    auto* bottom_divider = new brls::Rectangle();
    bottom_divider->setHeight(1);
    bottom_divider->setColor(nvgRGBA(255, 255, 255, 20));
    addView(bottom_divider);

    BuildMiniPlayer();

    // Global bumper shortcuts for tab switching (LB / RB)
    registerAction(
        "Previous Tab", brls::BUTTON_LB,
        [this](brls::View*) {
            if (tabs_.empty())
                return false;
            int prev = (active_tab_index_ + static_cast<int>(tabs_.size()) - 1) %
                       static_cast<int>(tabs_.size());
            SelectTab(prev);
            return true;
        },
        true, false);

    registerAction(
        "Next Tab", brls::BUTTON_RB,
        [this](brls::View*) {
            if (tabs_.empty())
                return false;
            int next = (active_tab_index_ + 1) % static_cast<int>(tabs_.size());
            SelectTab(next);
            return true;
        },
        true, false);

    // Global seeking shortcuts (ZL = seek -3s, ZR = seek +3s)
    registerAction(
        "Seek -3s", brls::BUTTON_LT,
        [](brls::View*) {
            audio::Player::Instance().SeekRelative(-3.0);
            return true;
        },
        true, true);

    registerAction(
        "Seek +3s", brls::BUTTON_RT,
        [](brls::View*) {
            audio::Player::Instance().SeekRelative(3.0);
            return true;
        },
        true, true);

    // Default to Search tab
    SelectTab(0);
}

AppShell::~AppShell()
{
    if (active_content_)
    {
        content_container_->removeView(active_content_, false);
        active_content_ = nullptr;
    }
    delete search_tab_;
    delete library_tab_;
    delete nowplaying_tab_;
    delete settings_tab_;
}

void AppShell::BuildHeader()
{
    const auto accent = settings::SettingsStore::Instance().GetAccentColor();

    header_container_ = new brls::Box(brls::Axis::ROW);
    header_container_->setHeight(76);
    header_container_->setAlignItems(brls::AlignItems::CENTER);
    header_container_->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header_container_->setPadding(0, 28, 0, 28);
    header_container_->setBackgroundColor(nvgRGB(9, 10, 12));
    addView(header_container_);

    // Brand Container (left)
    auto* brand = new brls::Box(brls::Axis::ROW);
    brand->setWidth(240);
    brand->setShrink(0.0f);
    brand->setAlignItems(brls::AlignItems::CENTER);

    brand_dot_ = new brls::Rectangle();
    brand_dot_->setWidth(20);
    brand_dot_->setHeight(20);
    brand_dot_->setColor(accent);
    brand_dot_->setMarginRight(10);
    brand->addView(brand_dot_);

    brand_title_ = new brls::Label();
    brand_title_->setText("StreamSnagNX");
    brand_title_->setFontSize(21);
    brand_title_->setTextColor(nvgRGB(248, 248, 248));
    brand->addView(brand_title_);

    brand_badge_ = new brls::Label();
    brand_badge_->setText("NX");
    brand_badge_->setFontSize(11);
    brand_badge_->setTextColor(nvgRGB(91, 96, 105));
    brand_badge_->setMarginLeft(8);
    brand->addView(brand_badge_);

    header_container_->addView(brand);

    // Tabs Container (center)
    tabs_container_ = new brls::Box(brls::Axis::ROW);
    tabs_container_->setGrow(1.0f);
    tabs_container_->setAlignItems(brls::AlignItems::CENTER);
    tabs_container_->setJustifyContent(brls::JustifyContent::CENTER);
    header_container_->addView(tabs_container_);

    // Left bumper badge [LB]
    auto* lb_hint = new brls::Box(brls::Axis::ROW);
    lb_hint->setHeight(26);
    lb_hint->setPadding(2, 10, 2, 10);
    lb_hint->setCornerRadius(13);
    lb_hint->setBackgroundColor(nvgRGB(28, 30, 36));
    lb_hint->setAlignItems(brls::AlignItems::CENTER);
    lb_hint->setMarginRight(12);
    auto* lb_label = new brls::Label();
    lb_label->setText("LB");
    lb_label->setFontSize(12);
    lb_label->setTextColor(nvgRGB(170, 175, 185));
    lb_hint->addView(lb_label);
    lb_hint->registerClickAction([this](brls::View*) {
        if (tabs_.empty())
            return false;
        int prev = (active_tab_index_ + static_cast<int>(tabs_.size()) - 1) %
                   static_cast<int>(tabs_.size());
        SelectTab(prev);
        return true;
    });
    tabs_container_->addView(lb_hint);

    auto add_tab_item = [this](const std::string& name, brls::View* content_view) {
        TabItem item;
        item.name = name;
        item.content = content_view;

        item.box = new brls::Box(brls::Axis::COLUMN);
        item.box->setAlignItems(brls::AlignItems::CENTER);
        item.box->setJustifyContent(brls::JustifyContent::CENTER);
        item.box->setMarginLeft(6);
        item.box->setMarginRight(6);
        item.box->setHeight(56);
        item.box->setPadding(0, 22, 0, 22);
        item.box->setCornerRadius(6);
        item.box->setFocusable(true);

        item.label = new brls::Label();
        item.label->setText(name);
        item.label->setFontSize(18);
        item.label->setTextColor(nvgRGB(128, 133, 143));
        item.box->addView(item.label);

        auto* underline_wrap = new brls::Box(brls::Axis::ROW);
        underline_wrap->setWidth(56);
        underline_wrap->setHeight(4);
        underline_wrap->setMarginTop(4);
        underline_wrap->setAlignItems(brls::AlignItems::CENTER);
        underline_wrap->setJustifyContent(brls::JustifyContent::CENTER);

        item.underline = new brls::Rectangle();
        item.underline->setWidth(56);
        item.underline->setHeight(4);
        item.underline->setColor(nvgRGBA(0, 0, 0, 0));
        underline_wrap->addView(item.underline);
        item.box->addView(underline_wrap);

        const int index = static_cast<int>(tabs_.size());
        item.box->registerClickAction([this, index](brls::View*) {
            SelectTab(index);
            return true;
        });

        tabs_container_->addView(item.box);
        tabs_.push_back(item);
    };

    add_tab_item("Search", search_tab_);
    add_tab_item("Library", library_tab_);
    add_tab_item("Now Playing", nowplaying_tab_);
    add_tab_item("Settings", settings_tab_);

    // Right bumper badge [RB]
    auto* rb_hint = new brls::Box(brls::Axis::ROW);
    rb_hint->setHeight(26);
    rb_hint->setPadding(2, 10, 2, 10);
    rb_hint->setCornerRadius(13);
    rb_hint->setBackgroundColor(nvgRGB(28, 30, 36));
    rb_hint->setAlignItems(brls::AlignItems::CENTER);
    rb_hint->setMarginLeft(12);
    auto* rb_label = new brls::Label();
    rb_label->setText("RB");
    rb_label->setFontSize(12);
    rb_label->setTextColor(nvgRGB(170, 175, 185));
    rb_hint->addView(rb_label);
    rb_hint->registerClickAction([this](brls::View*) {
        if (tabs_.empty())
            return false;
        int next = (active_tab_index_ + 1) % static_cast<int>(tabs_.size());
        SelectTab(next);
        return true;
    });
    tabs_container_->addView(rb_hint);

    // Chips Container (right)
    chips_container_ = new brls::Box(brls::Axis::ROW);
    chips_container_->setWidth(240);
    chips_container_->setShrink(0.0f);
    chips_container_->setAlignItems(brls::AlignItems::CENTER);
    chips_container_->setJustifyContent(brls::JustifyContent::FLEX_END);
    header_container_->addView(chips_container_);

    lib_chip_box_ = new brls::Box(brls::Axis::ROW);
    lib_chip_box_->setHeight(32);
    lib_chip_box_->setAlignItems(brls::AlignItems::CENTER);
    lib_chip_box_->setPadding(4, 14, 4, 14);
    lib_chip_box_->setCornerRadius(16);
    lib_chip_box_->setBorderThickness(1.5f);
    lib_chip_box_->setBorderColor(accent);
    lib_chip_box_->setBackgroundColor(nvgRGBA(
        static_cast<int>(accent.r * 255),
        static_cast<int>(accent.g * 255),
        static_cast<int>(accent.b * 255), 25));

    lib_chip_label_ = new brls::Label();
    lib_chip_label_->setText("LIB: 0");
    lib_chip_label_->setFontSize(13);
    lib_chip_label_->setTextColor(nvgRGB(234, 255, 241));
    lib_chip_box_->addView(lib_chip_label_);
    chips_container_->addView(lib_chip_box_);
}

void AppShell::BuildMiniPlayer()
{
    const auto accent = settings::SettingsStore::Instance().GetAccentColor();

    mini_player_container_ = new brls::Box(brls::Axis::ROW);
    mini_player_container_->setHeight(88);
    mini_player_container_->setAlignItems(brls::AlignItems::CENTER);
    mini_player_container_->setPadding(0, 28, 0, 28);
    mini_player_container_->setBackgroundColor(nvgRGB(16, 16, 20));
    addView(mini_player_container_);

    // Left: Thumbnail Art (56x56)
    mini_thumb_ = new brls::Image();
    mini_thumb_->setWidth(56);
    mini_thumb_->setHeight(56);
    mini_thumb_->setCornerRadius(8);
    mini_thumb_->setScalingType(brls::ImageScalingType::FILL);
    mini_thumb_->setMarginRight(14);
    mini_thumb_->registerClickAction([this](brls::View*) {
        SelectTab(2); // Jump to Now Playing
        return true;
    });
    mini_player_container_->addView(mini_thumb_);

    // Left: Metadata (Title & Artist)
    auto* meta_box = new brls::Box(brls::Axis::COLUMN);
    meta_box->setWidth(240);
    meta_box->setShrink(0.0f);
    meta_box->setJustifyContent(brls::JustifyContent::CENTER);
    meta_box->registerClickAction([this](brls::View*) {
        SelectTab(2); // Jump to Now Playing
        return true;
    });

    mini_title_ = new brls::Label();
    mini_title_->setText("Not playing");
    mini_title_->setFontSize(15);
    mini_title_->setTextColor(nvgRGB(240, 240, 245));
    mini_title_->setSingleLine(true);
    meta_box->addView(mini_title_);

    mini_artist_ = new brls::Label();
    mini_artist_->setText("Select a track from Search or Library");
    mini_artist_->setFontSize(13);
    mini_artist_->setTextColor(nvgRGB(151, 159, 170));
    mini_artist_->setSingleLine(true);
    meta_box->addView(mini_artist_);

    mini_player_container_->addView(meta_box);

    // Center: Media Controls (Rewind, Play/Pause, Fast-Forward)
    auto* controls_box = new brls::Box(brls::Axis::ROW);
    controls_box->setAlignItems(brls::AlignItems::CENTER);
    controls_box->setMarginLeft(16);
    controls_box->setMarginRight(24);

    // Rewind Button
    mini_rw_box_ = new brls::Box(brls::Axis::ROW);
    mini_rw_box_->setWidth(42);
    mini_rw_box_->setHeight(42);
    mini_rw_box_->setCornerRadius(21);
    mini_rw_box_->setAlignItems(brls::AlignItems::CENTER);
    mini_rw_box_->setJustifyContent(brls::JustifyContent::CENTER);
    mini_rw_box_->setBackgroundColor(nvgRGB(35, 35, 44));
    mini_rw_box_->setFocusable(true);
    mini_rw_icon_ = new PlayerIconView(PlayerIconType::Prev, nvgRGB(240, 240, 245), 16.0f);
    mini_rw_box_->addView(mini_rw_icon_);
    mini_rw_box_->registerClickAction([](brls::View*) {
        audio::Player::Instance().SeekRelative(-3.0);
        return true;
    });
    controls_box->addView(mini_rw_box_);

    // Play/Pause Button
    mini_pp_box_ = new brls::Box(brls::Axis::ROW);
    mini_pp_box_->setWidth(48);
    mini_pp_box_->setHeight(48);
    mini_pp_box_->setCornerRadius(24);
    mini_pp_box_->setMarginLeft(10);
    mini_pp_box_->setMarginRight(10);
    mini_pp_box_->setAlignItems(brls::AlignItems::CENTER);
    mini_pp_box_->setJustifyContent(brls::JustifyContent::CENTER);
    mini_pp_box_->setBackgroundColor(accent);
    mini_pp_box_->setFocusable(true);

    mini_pp_icon_ = new PlayerIconView(PlayerIconType::Play, nvgRGB(10, 26, 51), 18.0f);
    mini_pp_box_->addView(mini_pp_icon_);

    mini_pp_box_->registerClickAction([](brls::View*) {
        auto& player = audio::Player::Instance();
        if (player.IsPlaying())
            player.SetPaused(!player.IsPaused());
        return true;
    });
    controls_box->addView(mini_pp_box_);

    // Fast-Forward Button
    mini_ff_box_ = new brls::Box(brls::Axis::ROW);
    mini_ff_box_->setWidth(42);
    mini_ff_box_->setHeight(42);
    mini_ff_box_->setCornerRadius(21);
    mini_ff_box_->setAlignItems(brls::AlignItems::CENTER);
    mini_ff_box_->setJustifyContent(brls::JustifyContent::CENTER);
    mini_ff_box_->setBackgroundColor(nvgRGB(35, 35, 44));
    mini_ff_box_->setFocusable(true);
    mini_ff_icon_ = new PlayerIconView(PlayerIconType::Next, nvgRGB(240, 240, 245), 16.0f);
    mini_ff_box_->addView(mini_ff_icon_);
    mini_ff_box_->registerClickAction([](brls::View*) {
        audio::Player::Instance().SeekRelative(3.0);
        return true;
    });
    controls_box->addView(mini_ff_box_);

    mini_player_container_->addView(controls_box);

    // Right: Progress Bar + Time
    auto* progress_box = new brls::Box(brls::Axis::COLUMN);
    progress_box->setGrow(1.0f);
    progress_box->setJustifyContent(brls::JustifyContent::CENTER);

    mini_progress_bar_ = new brls::Box(brls::Axis::ROW);
    mini_progress_bar_->setHeight(8);
    mini_progress_bar_->setCornerRadius(4);
    mini_progress_bar_->setBackgroundColor(nvgRGB(38, 42, 48));

    mini_progress_fill_ = new brls::Box();
    mini_progress_fill_->setWidth(0);
    mini_progress_fill_->setHeight(8);
    mini_progress_fill_->setCornerRadius(4);
    mini_progress_fill_->setBackgroundColor(accent);
    mini_progress_bar_->addView(mini_progress_fill_);
    progress_box->addView(mini_progress_bar_);

    auto* times_row = new brls::Box(brls::Axis::ROW);
    times_row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    times_row->setMarginTop(4);

    mini_time_label_ = new brls::Label();
    mini_time_label_->setText("0:00 / 0:00");
    mini_time_label_->setFontSize(12);
    mini_time_label_->setTextColor(nvgRGB(151, 159, 170));
    times_row->addView(mini_time_label_);

    progress_box->addView(times_row);
    mini_player_container_->addView(progress_box);
}

void AppShell::SelectTab(int index)
{
    if (index < 0 || index >= static_cast<int>(tabs_.size()))
        return;
    if (active_tab_index_ == index && active_content_ != nullptr)
        return;

    active_tab_index_ = index;
    const auto accent = settings::SettingsStore::Instance().GetAccentColor();

    for (size_t i = 0; i < tabs_.size(); ++i)
    {
        if (static_cast<int>(i) == index)
        {
            tabs_[i].label->setTextColor(nvgRGB(255, 255, 255));
            tabs_[i].underline->setColor(accent);
            tabs_[i].box->setBackgroundColor(nvgRGBA(
                static_cast<int>(accent.r * 255),
                static_cast<int>(accent.g * 255),
                static_cast<int>(accent.b * 255), 35));
        }
        else
        {
            tabs_[i].label->setTextColor(nvgRGB(128, 133, 143));
            tabs_[i].underline->setColor(nvgRGBA(0, 0, 0, 0));
            tabs_[i].box->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
        }
    }

    if (active_content_)
    {
        content_container_->removeView(active_content_, false);
        active_content_ = nullptr;
    }

    brls::View* new_content = tabs_[index].content;
    if (new_content)
    {
        if (index == 1 && library_tab_)
            library_tab_->Refresh();

        new_content->setGrow(1.0f);
        content_container_->addView(new_content);
        active_content_ = new_content;
        brls::Application::giveFocus(new_content);
    }
}

void AppShell::onFocusGained()
{
    brls::Box::onFocusGained();
    if (active_content_)
        brls::Application::giveFocus(active_content_);
}

void AppShell::draw(NVGcontext* vg, float x, float y, float width, float height,
                    brls::Style style, brls::FrameContext* ctx)
{
    settings::SettingsStore::Instance().CheckSleepTimer();
    UpdateMiniPlayer();
    UpdateAccentColors();
    brls::Box::draw(vg, x, y, width, height, style, ctx);
}

void AppShell::UpdateAccentColors()
{
    const auto& settings = settings::SettingsStore::Instance();
    const uint32_t current_theme = static_cast<uint32_t>(settings.GetTheme());
    if (current_theme == last_theme_idx_)
        return;
    last_theme_idx_ = current_theme;

    const NVGcolor accent = settings.GetAccentColor();
    if (brand_dot_)
        brand_dot_->setColor(accent);

    if (lib_chip_box_)
    {
        lib_chip_box_->setBorderColor(accent);
        lib_chip_box_->setBackgroundColor(nvgRGBA(
            static_cast<int>(accent.r * 255),
            static_cast<int>(accent.g * 255),
            static_cast<int>(accent.b * 255), 25));
    }

    if (mini_progress_fill_)
        mini_progress_fill_->setBackgroundColor(accent);

    if (mini_pp_box_)
        mini_pp_box_->setBackgroundColor(accent);

    for (size_t i = 0; i < tabs_.size(); ++i)
    {
        if (static_cast<int>(i) == active_tab_index_)
        {
            tabs_[i].underline->setColor(accent);
            tabs_[i].box->setBackgroundColor(nvgRGBA(
                static_cast<int>(accent.r * 255),
                static_cast<int>(accent.g * 255),
                static_cast<int>(accent.b * 255), 35));
        }
    }
}

void AppShell::UpdateMiniPlayer()
{
    auto& player = audio::Player::Instance();
    const auto info = audio::NowPlaying::Instance().Get();

    // Check if track changed
    if (info.track.video_id != last_mini_vid_ || info.track.title != last_mini_title_)
    {
        last_mini_vid_ = info.track.video_id;
        last_mini_title_ = info.track.title;

        if (!info.track.title.empty())
        {
            mini_title_->setText(info.track.title);
            mini_artist_->setText(info.track.artist.empty() ? info.track.subtitle
                                                            : info.track.artist);

            const std::string thumb_file =
                "sdmc:/switch/StreamSnagNX/thumbs/" + info.track.video_id + ".jpg";
            FILE* probe = fopen(thumb_file.c_str(), "rb");
            if (probe)
            {
                fclose(probe);
                mini_thumb_->setImageFromFile(thumb_file);
            }
        }
        else
        {
            mini_title_->setText("Not playing");
            mini_artist_->setText("Select a track from Search or Library");
        }
    }

    // Play/Pause icon
    const bool is_playing = player.IsPlaying();
    const bool is_paused = player.IsPaused();
    if (is_playing != last_mini_playing_ || is_paused != last_mini_paused_)
    {
        last_mini_playing_ = is_playing;
        last_mini_paused_ = is_paused;
        if (mini_pp_icon_)
            mini_pp_icon_->setIconType(is_paused || !is_playing ? PlayerIconType::Play : PlayerIconType::Pause);
    }

    // Progress bar & timestamps
    const double pos = player.Position();
    const double dur = player.Duration();
    char time_str[64];
    if (is_playing && dur > 0.0)
    {
        snprintf(time_str, sizeof(time_str), "%d:%02d / %d:%02d",
                 static_cast<int>(pos) / 60, static_cast<int>(pos) % 60,
                 static_cast<int>(dur) / 60, static_cast<int>(dur) % 60);

        const float pct = std::clamp(static_cast<float>(pos / dur), 0.0f, 1.0f);
        // Progress bar container width is dynamic, fill based on percentage
        const float total_w = mini_progress_bar_->getWidth();
        if (total_w > 0.0f)
            mini_progress_fill_->setWidth(pct * total_w);
        else
            mini_progress_fill_->setWidth(pct * 360.0f);
    }
    else if (is_playing)
    {
        snprintf(time_str, sizeof(time_str), "%d:%02d",
                 static_cast<int>(pos) / 60, static_cast<int>(pos) % 60);
        mini_progress_fill_->setWidth(0.0f);
    }
    else
    {
        snprintf(time_str, sizeof(time_str), "0:00 / 0:00");
        mini_progress_fill_->setWidth(0.0f);
    }
    mini_time_label_->setText(time_str);

    // Live Library count in chip
    const int lib_count =
        static_cast<int>(library::LibraryStore::Instance().GetTracks().size());
    if (lib_count != last_lib_count_)
    {
        last_lib_count_ = lib_count;
        lib_chip_label_->setText("LIB: " + std::to_string(lib_count));
    }
}

} // namespace ssnx::shell
