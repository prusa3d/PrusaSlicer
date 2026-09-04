#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemControl;

class ObjectConfigItem : public Biz::DataObserver<Biz::OverrideItem>, public Yoga::Item
{
public:
    ObjectConfigItem(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::IConfigBoxSetter& cbi_container
    );

private:
    void on_data_update() override;

private:
    Biz::IConfigBoxSetter& m_cbi_container;

    ConfigItemControl* m_control{nullptr};
    Yoga::Item* m_control_item{nullptr};
    Yoga::Text* m_label{nullptr};
    Domain::ConfigItemDef::GUIType m_gui_type{Domain::ConfigItemDef::GUIType::undefined};
};

} // namespace Slic3r::App
