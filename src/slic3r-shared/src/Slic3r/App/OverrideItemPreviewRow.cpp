#include "Slic3r/App/OverrideItemPreviewRow.hpp"

#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Config/ConfigItemPreview.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

static LayoutButton* add_button(
    Item* parent,
    Render::Icon icon,
    const std::string& tooltip,
    std::optional<ImColor> icon_tint_color = std::nullopt
)
{
    LayoutButton* button = parent->emplace_back<LayoutButton>(std::string(), icon, tooltip);
    button->set_background_color(Platform::Color::ButtonTransparent);
    if (icon_tint_color) {
        button->set_icon_tint(icon_tint_color.value());
    }
    button->set_width(22);
    button->set_height(22);
    button->set_content_padding(Paddings(2));
    button->set_flex_shrink(0);

    return button;
}

OverrideItemPreviewRow::OverrideItemPreviewRow(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::Preset::PresetInteractor& preset_interactor
) :
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    m_preset_interactor(preset_interactor)
{
    set_gap(10);
    set_padding({0, 0, 10, 0});
    set_flex_shrink(0);
    set_object_name("OverrideItemPreviewRow");
    set_height(26);
    set_align_items(YGAlignCenter);

    m_label = emplace_back<Text>(std::string());
    m_label->set_width(170);
    m_label->set_wrap_mode(Text::WrapMode::Wrap);

    Item* container = emplace_back<Item>();
    container->set_gap(3);
    container->set_flex_grow(1);
    container->set_align_items(YGAlignCenter);

    m_preview = container->emplace_back<ConfigItemPreview>();
    m_preview->set_text_font_type(Render::ImguiFontType::Italic);
    m_preview->set_flex_grow(1);

    m_add_button = add_button(
        container,
        Render::Icon::Plus,
        Biz::_u8L("Add override"),
        m_theme->color_imgui(Platform::Color::Text)
    );
    m_add_button->callbacks().action = [this]
    { m_preset_interactor.set_item_override(*m_state->config_item, true); };

    on_data_update();
}

void OverrideItemPreviewRow::on_data_update()
{
    ASSERT(m_state->is_override());

    const Domain::ConfigItem& item = *m_state->config_item;
    m_label->set_text(Biz::_u8(item.def().label));
    m_preview->set_data(item, item.value(), m_state->mixed);

    const Domain::ConfigValue* init_value =
        m_preset_interactor.get_override_original_value(item, 0);

    const bool is_changed_value = (init_value ? item.value() != *init_value : true) || m_state->mixed;
    m_label->set_text_color(m_theme->color_imgui(
        is_changed_value ? Platform::Color::AccentTertiary : Platform::Color::Text
    ));

    m_add_button->set_visible(!m_state->overriden.value());
}

} // namespace Slic3r::App
