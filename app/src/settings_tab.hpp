#pragma once

#include "settings/settings_store.hpp"

#include <borealis.hpp>

#include <atomic>
#include <memory>

namespace ssnx
{

class SettingsTab : public brls::Box
{
  public:
    SettingsTab();
    ~SettingsTab() override;

  private:
    void BuildUI();
    void RefreshValues();

    brls::Box* content_box_ = nullptr;
    brls::Label* quality_val_ = nullptr;
    brls::Label* dl_format_val_ = nullptr;
    brls::Label* lyrics_val_ = nullptr;
    brls::Label* theme_val_ = nullptr;
    brls::Label* sleep_val_ = nullptr;
    brls::Label* clean_val_ = nullptr;
    brls::Label* storage_val_ = nullptr;
    brls::Label* dev_val_ = nullptr;

    std::shared_ptr<std::atomic<bool>> alive_ =
        std::make_shared<std::atomic<bool>>(true);
};

} // namespace ssnx

