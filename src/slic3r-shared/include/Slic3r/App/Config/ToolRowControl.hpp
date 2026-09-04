#pragma once

#include <Slic3r/Domain/ConfigDef.hpp>

#include "Slic3r/Biz/DataObserver.hpp"

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Config/ToolRowOverride.hpp"

namespace Slic3r::Domain {
class ConfigItem;
} // namespace Slic3r::Domain

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Icon;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class ConfigItemControl;
class ExtruderButton;

class ToolRowControl : public Biz::DataObserver<ToolRowOverrideGroup>, public Yoga::RectangleButton
{
public:
    using ExtruderClickedFn = std::function<void(size_t tool_index)>;
    using ExtruderDroppedFn = std::function<void(size_t dropped_tool_index, size_t index)>;

    explicit ToolRowControl(
        size_t index,
        const ToolRowOverrideGroup& data,
        Biz::IConfigBoxSetter& cb_setter,
        ExtruderClickedFn clicked_fn,
        ExtruderDroppedFn extruder_dropped_fn,
        bool small
    );

protected:
    void on_data_update() override;

    void dnd_accepted_internal(const Yoga::DnDPayload& dnd_payload) override;
    void dnd_could_accept_changed_internal(bool could_accept) override;

private:
    Biz::IConfigBoxSetter& m_cb_setter;
    ExtruderClickedFn m_extruder_clicked_fn;
    ExtruderDroppedFn m_extruder_dropped_fn;
    bool m_small{false};

    Domain::ConfigItemDef::GUIType m_control_gui_type{Domain::ConfigItemDef::GUIType::undefined};
    std::vector<size_t> m_last_indexes;
    std::vector<ExtruderButton*> m_extruders;

    Yoga::Item* m_icon_container{nullptr};
    ConfigItemControl* m_control{nullptr};
    Yoga::Item* m_input{nullptr};
};

} // namespace Slic3r::App
