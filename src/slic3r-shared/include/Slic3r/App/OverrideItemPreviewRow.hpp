#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz::Preset {
class PresetInteractor;
} // namespace Slic3r::Biz::Preset

namespace Slic3r::App::Yoga {
class Text;
class LayoutButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemPreview;

class OverrideItemPreviewRow : public Biz::DataObserver<Biz::OverrideItem>, public Yoga::Item
{
public:
    explicit OverrideItemPreviewRow(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::Preset::PresetInteractor& preset_interactor
    );

protected:
    void on_data_update() override;

private:
    Biz::Preset::PresetInteractor& m_preset_interactor;

    Yoga::Text* m_label{nullptr};
    ConfigItemPreview* m_preview{nullptr};
    Yoga::LayoutButton* m_add_button{nullptr};
    Domain::ConfigItemDef::GUIType m_gui_type{Domain::ConfigItemDef::GUIType::undefined};
};

} // namespace Slic3r::App
