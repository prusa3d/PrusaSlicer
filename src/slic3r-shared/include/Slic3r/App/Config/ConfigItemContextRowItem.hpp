#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/ConfigItemContext.hpp"

#include "Slic3r/App/IConfigNavigable.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigRowItem;

class ConfigItemContextRowItem :
    public Biz::DataObserver<Biz::ConfigItemContext>,
    public IConfigNavigable,
    public Yoga::Item
{
public:
    ConfigItemContextRowItem(
        size_t index,
        const Biz::ConfigItemContext& data,
        Biz::IConfigBoxSetter& cb_setter,
        size_t cbi_index
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

protected:
    void on_data_update() override;

private:
    Biz::IConfigBoxSetter& m_cb_setter;
    size_t m_cbi_index{0};

    Domain::ConfigItemDef::GUIType m_last_gui_type{Domain::ConfigItemDef::GUIType::undefined};

    ConfigRowItem* m_config_row_item{nullptr};
};

} // namespace Slic3r::App
