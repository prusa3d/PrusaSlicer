#include "Slic3r/App/Config/PrintToolRowButton.hpp"

#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Config/ConfigItemPreview.hpp"
#include "Slic3r/App/Config/ConfigItemUtils.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrintToolRowButton::PrintToolRowButton(Biz::IConfigBoxSetter& cb_setter) : m_cb_setter(cb_setter)
{
    set_content_orientation(Orientation::Horizontal);
    set_content_align_items(YGAlignCenter);
    set_content_justify_content(YGJustifyFlexStart);
    set_background_color(Platform::Color::ButtonTransparent);
    set_allow_overlap(true);
    set_checkable(true);

    m_icon_caret = emplace_back<Icon>(Render::Icon::CloseArrow);
    m_icon_caret->set_width(22);
    m_icon_caret->set_height(22);
    m_label = emplace_back<Text>(std::string{});
    m_label->set_flex_grow(1);
    m_label->set_wrap_mode(Text::WrapMode::WrapElide);

    m_revert_button = emplace_back<LayoutButton>(
        std::string{},
        Render::Icon::UndoGizmo,
        Biz::_u8L("Revert to the initial profile value")
    );
    m_revert_button->set_background_color(Platform::Color::ButtonTransparent);
    m_revert_button->set_icon_tint(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->set_self_align(YGAlignCenter);
    m_revert_button->set_content_padding(3);
    m_revert_button->set_width(20);
    m_revert_button->set_height(20);
    m_revert_button->set_flex_shrink(0.f);
    m_revert_button->callbacks().action = [this]()
    {
        std::size_t tool_count = m_last_print_tool_item->tool_overrides.size();
        while (tool_count > 0) {
            tool_count--;
            m_cb_setter.set_from_original_value(
                *m_last_print_tool_item->tool_overrides.at(tool_count),
                tool_count
            );
        }
    };

    m_compatibility_rule_rect = emplace_back<Rectangle>();
    m_compatibility_rule_rect->set_fill(IM_COL32_BLACK_TRANS);
    m_compatibility_rule_rect->set_border_color(
        m_theme->color_imgui(Platform::Color::AccentPrimary)
    );
    m_compatibility_rule_rect->set_gap(5);
    m_compatibility_rule_rect->set_padding(4);
    m_compatibility_rule_label =
        m_compatibility_rule_rect->emplace_back<Text>(std::string{"conflict resolved"});
    m_compatibility_rule_label->set_text_color(
        m_theme->color_imgui(Platform::Color::AccentPrimary)
    );
    m_compatibility_rule_label->set_visible(false);
    m_config_item_preview = m_compatibility_rule_rect->emplace_back<ConfigItemPreview>();
    m_compatibility_rule_rect->set_flex_shrink(0.f);

    m_per_extruder_label = emplace_back<Text>(Biz::_u8L("Per tool"), Render::ImguiFontType::Italic);
    m_per_extruder_label->set_visible(false);

    m_tooltip->set_text_wrap(true);
    m_tooltip->content_item()->set_width(350);
}

void PrintToolRowButton::update_data(const Biz::PrintToolItem* print_tool_item)
{
    ASSERT(print_tool_item);
    m_last_print_tool_item = print_tool_item;
    bool show_preview      = false;
    std::optional<Domain::ConfigValue> value;

    if (print_tool_item->print_item->def().compatibility_rule != Domain::CompatibilityRule::Undefined) {
        show_preview = true;
    } else if (print_tool_item->shared_context.extruder_candidates.empty()) {
        // Extruder candidates are empty, just compare all extruder values
        const std::size_t tool_count           = print_tool_item->tool_overrides.size();
        const Domain::ConfigValue& first_value = print_tool_item->tool_value(0);
        bool all_same                          = true;
        for (std::size_t extruder_id{1}; extruder_id < tool_count; ++extruder_id) {
            if (print_tool_item->tool_value(extruder_id) != first_value) {
                all_same = false;
            }
        }
        show_preview = all_same;
        value        = first_value;
    } else {
        // Extruder candidates are not empty, compare their values
        const Domain::ConfigValue& first_value = print_tool_item->tool_value(
            *print_tool_item->shared_context.extruder_candidates.cbegin()
        );
        show_preview = std::all_of(
            print_tool_item->shared_context.extruder_candidates.cbegin(),
            print_tool_item->shared_context.extruder_candidates.cend(),
            [&](unsigned extruder)
            {
                if (extruder >= print_tool_item->tool_overrides.size()) {
                    // Tool overrides were still not updated
                    return false;
                } else {
                    return print_tool_item->tool_value(extruder) == first_value;
                }
            }
        );
        value = first_value;
    }

    if (show_preview) {
        m_config_item_preview->set_data(
            *print_tool_item->print_item,
            value.value_or(print_tool_item->value.first),
            false
        );
    }
    m_config_item_preview->set_visible(show_preview);
    m_per_extruder_label->set_visible(!show_preview);

    update_rule_visibility();

    set_tooltip(ConfigItemUtils::config_item_tooltip(*print_tool_item->print_item));
    m_label->set_text(Biz::_u8(print_tool_item->print_item->def().label));

    const bool can_revert = print_tool_item->is_dirty_tool();
    m_revert_button->set_visible(can_revert);
    m_label->set_text_color(
        m_theme->color_imgui(can_revert ? Platform::Color::AccentTertiary : Platform::Color::Text)
    );
}

void PrintToolRowButton::checked_updated_internal()
{
    m_icon_caret->set_icon(checked() ? Render::Icon::OpenArrow : Render::Icon::CloseArrow);
    set_draw_flags(checked() ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
}

void PrintToolRowButton::update_rule_visibility()
{
    bool rule_visible =
        m_last_print_tool_item->tool_overrides.size() > 1 && m_last_print_tool_item->value.second;
    m_compatibility_rule_label->set_visible(rule_visible);
    m_compatibility_rule_rect->set_border_width(rule_visible ? 1 : 0);
}

} // namespace Slic3r::App
