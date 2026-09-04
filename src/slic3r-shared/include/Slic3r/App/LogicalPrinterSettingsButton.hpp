#pragma once

#include "Slic3r/App/Yoga/PrinterSettingsButton.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::Yoga {
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class LogicalPrinterSettingsButton :
    public Yoga::PrinterSettingsButton,
    public Biz::DataObserver<Biz::Preset::PresetItem>
{
public:
    using FnIndexClicked = std::function<void(size_t)>;

    LogicalPrinterSettingsButton(
        size_t index,
        const Biz::Preset::PresetItem& logical_printer_preset,
        FnIndexClicked on_clicked,
        FnIndexClicked on_cog_clicked,
        FnIndexClicked on_favorite_clicked,
        const Biz::Preset::PresetInteractor& preset_interactor
    );

    const Biz::Preset::PresetItem& preset_item() const;

protected:
    void on_data_update() override;

    void update_btns_visibility() override;

    void update_favorite_state();

    bool is_favorited() const;

private:
    FnIndexClicked m_on_clicked;
    FnIndexClicked m_on_cog_clicked;
    FnIndexClicked m_on_favorite_clicked;
    const Biz::Preset::PresetInteractor& m_preset_interactor;

    Yoga::LayoutButton* m_favorite_button{nullptr};
};

} // namespace Slic3r::App
