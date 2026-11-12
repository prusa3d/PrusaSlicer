#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/OverrideItem.hpp"

#include "Slic3r/App/IConfigNavigable.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz {
class IConfigBoxSetter;
class OverridableConfigBoxInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class ToggleButton;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigRowItem;

class OverridableConfigRowItem :
    public Biz::DataObserver<Biz::OverrideItem>,
    public IConfigNavigable,
    public Yoga::Item
{
public:
    OverridableConfigRowItem(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::IConfigBoxSetter& cb_setter,
        Biz::OverridableConfigBoxInteractor& cbi,
        size_t cbi_index
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

protected:
    void on_data_update() override;

private:
    Biz::IConfigBoxSetter& m_cb_setter;
    Biz::OverridableConfigBoxInteractor& m_cbi;
    size_t m_cbi_index{0};

    bool m_last_is_override{false};

    Yoga::ToggleButton* m_override_toggle_button{nullptr};
    ConfigRowItem* m_config_row_item{nullptr};
};

} // namespace Slic3r::App
