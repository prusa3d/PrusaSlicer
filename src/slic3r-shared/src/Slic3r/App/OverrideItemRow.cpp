#include "Slic3r/App/OverrideItemRow.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Config/ConfigItemControl.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

OverrideItemRow::OverrideItemRow(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::Preset::PresetInteractor& preset_interactor,
    bool enable_remove
) :
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    m_preset_interactor(preset_interactor)
{
    set_gap(5_fpx);
    set_flex_shrink(0);
    set_object_name("OverrideItemRow");
    set_align_items(YGAlignCenter);

    m_label = emplace_back<Text>(std::string());
    m_label->set_width(120_fpx);
    m_label->set_max_width(120_fpx);
    m_label->set_wrap_mode(Text::WrapMode::Wrap);
    m_label->set_flex_grow(1);

    Item* container = emplace_back<Item>();
    container->set_justify_content(YGJustifyFlexEnd);
    container->set_flex_grow(1);
    LayoutButton* remove_button = container->emplace_back<LayoutButton>(
        std::string(),
        Render::Icon::Minus,
        Biz::_u8L("Remove override")
    );
    remove_button->set_background_color(Platform::Color::ButtonTransparent);
    remove_button->set_width(22_fpx);
    remove_button->set_height(22_fpx);
    remove_button->set_content_padding(4_fpx);
    remove_button->set_flex_shrink(0);
    remove_button->callbacks().action = [this]
    { m_preset_interactor.set_item_override(*m_state->config_item, false); };

    on_data_update();
}

void OverrideItemRow::on_data_update()
{
    ASSERT(m_state->is_override());

    Domain::ConfigItemDef::GUIType gui_type = m_state->config_item->def().gui_type;
    if (m_gui_type != gui_type) {
        m_gui_type = gui_type;

        if (m_control_item) {
            remove(m_control_item);
            m_control_item = nullptr;
            m_control      = nullptr;
        }

        m_control = ConfigItemControl::config_item_control_factory(
            this,
            1,
            m_index,
            *m_state->config_item,
            m_preset_interactor,
            {0} // Object and Volume are always index 0
        );
        m_control_item = dynamic_cast<Item*>(m_control);
        ASSERT(m_control_item, "ConfigItem has to derive from Yoga::Item");
        m_control_item->set_flex_shrink(0);
        m_control_item->set_width(150_fpx);
        m_control_item->set_flex_grow(1);
        m_control_item->set_max_width(200_fpx);
    }

    m_label->set_text(Biz::_u8(m_state->config_item->def().label));
    m_control->set_mixed(m_state->mixed);
    m_control->set_overriden(m_state->overriden);
    m_control->set_state(*m_state->config_item);
}

} // namespace Slic3r::App
