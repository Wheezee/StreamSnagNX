#include "settings_tab.hpp"

#include "library/library_store.hpp"
#include "settings/settings_store.hpp"

namespace ssnx
{
namespace
{

brls::Box* MakeSettingRow(const std::string& title, const std::string& subtitle,
                          brls::Label** out_badge_val, std::function<void()> on_click)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setFocusable(true);
    row->setHeight(68);
    row->setCornerRadius(10);
    row->setBackgroundColor(nvgRGB(21, 21, 24));
    row->setMarginBottom(8);
    row->setPadding(0, 18, 0, 18);
    row->setAlignItems(brls::AlignItems::CENTER);

    auto* meta = new brls::Box(brls::Axis::COLUMN);
    meta->setGrow(1.0f);

    auto* title_lbl = new brls::Label();
    title_lbl->setText(title);
    title_lbl->setFontSize(16);
    title_lbl->setTextColor(nvgRGB(236, 236, 239));
    meta->addView(title_lbl);

    if (!subtitle.empty())
    {
        auto* sub_lbl = new brls::Label();
        sub_lbl->setText(subtitle);
        sub_lbl->setFontSize(12);
        sub_lbl->setTextColor(nvgRGB(140, 148, 158));
        meta->addView(sub_lbl);
    }
    row->addView(meta);

    auto* val_lbl = new brls::Label();
    val_lbl->setFontSize(14);
    val_lbl->setTextColor(settings::SettingsStore::Instance().GetAccentColor());
    row->addView(val_lbl);

    if (out_badge_val)
        *out_badge_val = val_lbl;

    if (on_click)
    {
        row->registerClickAction([on_click](brls::View*) {
            on_click();
            return true;
        });
    }

    return row;
}

brls::Label* MakeSectionHeader(const std::string& text)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(13);
    label->setTextColor(nvgRGB(110, 118, 130));
    label->setMarginTop(14);
    label->setMarginBottom(8);
    return label;
}

} // namespace

SettingsTab::SettingsTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(28, 40, 28, 40);
    setBackgroundColor(nvgRGB(12, 13, 16));

    auto* header = new brls::Header();
    header->setTitle("Settings");
    header->setSubtitle("Audio Quality  |  Theme Color  |  Sleep Timer  |  Storage");
    addView(header);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    content_box_ = new brls::Box(brls::Axis::COLUMN);
    content_box_->setGrow(1.0f);
    scroll->setContentView(content_box_);
    addView(scroll);

    BuildUI();

    auto alive = alive_;
    settings::SettingsStore::Instance().RegisterListener([this, alive]() {
        brls::sync([this, alive]() {
            if (!alive->load())
                return;
            RefreshValues();
        });
    });
}

SettingsTab::~SettingsTab()
{
    alive_->store(false);
}

void SettingsTab::BuildUI()
{
    content_box_->clearViews();

    // 1. AUDIO SECTION
    content_box_->addView(MakeSectionHeader("AUDIO"));

    content_box_->addView(MakeSettingRow(
        "Streaming Quality", "Preferred playback bitrate and format",
        &quality_val_, [this]() {
            settings::SettingsStore::Instance().CycleStreamQuality();
            RefreshValues();
        }));

    content_box_->addView(MakeSettingRow(
        "Download Format", "Preferred offline audio container on SD",
        &dl_format_val_, [this]() {
            settings::SettingsStore::Instance().CycleDownloadFormat();
            RefreshValues();
        }));

    content_box_->addView(MakeSettingRow(
        "Synced Lyrics (LRCLIB)", "Fetch and cache .lrc lyrics alongside audio",
        &lyrics_val_, [this]() {
            settings::SettingsStore::Instance().ToggleLyrics();
            RefreshValues();
        }));

    // 2. APPEARANCE SECTION
    content_box_->addView(MakeSectionHeader("APPEARANCE"));

    content_box_->addView(MakeSettingRow(
        "Theme Accent", "Color highlights across badges, buttons, and lyrics",
        &theme_val_, [this]() {
            settings::SettingsStore::Instance().CycleTheme();
            RefreshValues();
        }));

    // 3. SLEEP TIMER SECTION
    content_box_->addView(MakeSectionHeader("SLEEP TIMER"));

    content_box_->addView(MakeSettingRow(
        "Sleep Timer", "Automatically pause audio playback after duration",
        &sleep_val_, [this]() {
            settings::SettingsStore::Instance().CycleSleepTimer();
            RefreshValues();
        }));

    // 4. STORAGE & MAINTENANCE
    content_box_->addView(MakeSectionHeader("STORAGE & MAINTENANCE"));

    content_box_->addView(MakeSettingRow(
        "Library Storage", "sdmc:/switch/StreamSnagNX/music",
        &storage_val_, nullptr));

    content_box_->addView(MakeSettingRow(
        "Clean Temporary Files", "Remove unreferenced search thumbnails and orphaned audio buffers",
        &clean_val_, [this]() {
            const auto res = settings::SettingsStore::Instance().CleanTempFiles();
            std::string msg = "Cleaned " + std::to_string(res.total()) + " temporary file(s).\n\n" +
                              "• " + std::to_string(res.thumbs_removed) + " search thumbnail(s)\n" +
                              "• " + std::to_string(res.buffers_removed) + " stream/download buffer(s)\n\n" +
                              "All downloaded music and artwork were preserved.";
            brls::Dialog* dlg = new brls::Dialog(msg);
            dlg->addButton("OK", []() {});
            dlg->open();
            RefreshValues();
        }));

    // 5. ABOUT SECTION
    content_box_->addView(MakeSectionHeader("ABOUT"));

    auto* dev_row = MakeSettingRow(
        "Developer", "Created with passion for Nintendo Switch homebrew",
        &dev_val_, nullptr);
    content_box_->addView(dev_row);

    brls::Label* ver_badge = nullptr;
    auto* about_row = MakeSettingRow(
        "StreamSnagNX v0.1.0", "TitleID: 05534E41474E5858 · Zero Auth · Powered by InnerTube & LRCLIB",
        &ver_badge, nullptr);
    if (ver_badge)
        ver_badge->setText("[Offline Edition]");
    content_box_->addView(about_row);

    RefreshValues();
}

void SettingsTab::RefreshValues()
{
    auto& store = settings::SettingsStore::Instance();
    const NVGcolor accent = store.GetAccentColor();

    if (dev_val_)
    {
        dev_val_->setText("[Wheezee]");
        dev_val_->setTextColor(accent);
    }
    if (quality_val_)
    {
        quality_val_->setText("[" + store.GetStreamQualityString() + "]");
        quality_val_->setTextColor(accent);
    }
    if (dl_format_val_)
    {
        dl_format_val_->setText("[" + store.GetDownloadFormatString() + "]");
        dl_format_val_->setTextColor(accent);
    }
    if (lyrics_val_)
    {
        lyrics_val_->setText("[" + store.GetLyricsString() + "]");
        lyrics_val_->setTextColor(store.GetLyricsEnabled() ? accent : nvgRGB(120, 128, 140));
    }
    if (theme_val_)
    {
        theme_val_->setText("[" + store.GetThemeString() + "]");
        theme_val_->setTextColor(accent);
    }
    if (sleep_val_)
    {
        sleep_val_->setText("[" + store.GetSleepTimerString() + "]");
        sleep_val_->setTextColor(store.GetSleepTimerMinutes() > 0 ? nvgRGB(255, 180, 80) : accent);
    }
    if (clean_val_)
    {
        clean_val_->setText("[Clean: A]");
        clean_val_->setTextColor(accent);
    }
    if (storage_val_)
    {
        const auto tracks = library::LibraryStore::Instance().GetTracks();
        const auto playlists = library::LibraryStore::Instance().GetPlaylists();
        storage_val_->setText(std::to_string(tracks.size()) + " tracks | " +
                              std::to_string(playlists.size()) + " playlists");
        storage_val_->setTextColor(nvgRGB(150, 160, 175));
    }
}

} // namespace ssnx

