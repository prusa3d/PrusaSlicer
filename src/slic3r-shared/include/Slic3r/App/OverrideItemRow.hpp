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
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemControl;

class OverrideItemRow : public Biz::DataObserver<Biz::OverrideItem>, public Yoga::Item
{
public:
    explicit OverrideItemRow(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::Preset::PresetInteractor& preset_interactor,
        bool enable_remove
    );

protected:
    void on_data_update() override;

private:
    Biz::Preset::PresetInteractor& m_preset_interactor;

    Yoga::Text* m_label{nullptr};
    ConfigItemControl* m_control{nullptr};
    Yoga::Item* m_control_item{nullptr};
    Domain::ConfigItemDef::GUIType m_gui_type{Domain::ConfigItemDef::GUIType::undefined};
};

} // namespace Slic3r::App
