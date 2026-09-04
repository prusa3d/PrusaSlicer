#include "Slic3r/App/Plater/HeightRangeRow.hpp"

#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Plater/HeightRangeButton.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App::Plater {

const constexpr float HEIGHT_RANGE_ROW_HEIGHT = 26.f;

HeightRangeRow::HeightRangeRow(const HeightRangeEntry& height_range) : m_height_range(height_range)
{
    this->set_object_name("HeightRangeRow");
    this->set_flex_shrink(0);
    this->set_orientation(Orientation::Horizontal);
    this->set_height(HEIGHT_RANGE_ROW_HEIGHT);
    this->set_gap(4.f);

    m_height_range_button                              = emplace_back<HeightRangeButton>();
    m_height_range_button->callbacks().action          = [this]() { m_callbacks.selected(); };
    m_height_range_button->callbacks().hovered_changed = [this](bool hovered)
    { m_callbacks.hovered(hovered); };

    m_undo_button = emplace_back<LayoutButton>("", Render::Icon::UndoGizmo);
    m_undo_button->set_width(22);
    m_undo_button->set_height(22);
    m_undo_button->set_self_align(YGAlignCenter);
    m_undo_button->set_flex_shrink(0);
    m_undo_button->set_background_color(Platform::Color::ButtonTransparent);
    m_undo_button->callbacks().action = [this]()
    {
        if (m_has_overrides) {
            m_callbacks.undo_clicked();
        }
    };

    m_delete_button = emplace_back<LayoutButton>("", Render::Icon::Minus);
    m_delete_button->set_width(22);
    m_delete_button->set_height(22);
    m_delete_button->set_self_align(YGAlignCenter);
    m_delete_button->set_flex_shrink(0);
    m_delete_button->set_background_color(Platform::Color::ButtonTransparent);
    m_delete_button->callbacks().action = [this]() { m_callbacks.delete_clicked(); };

    this->update_labels();
    this->update_undo_button();
}

HeightRangeRow::Callbacks& HeightRangeRow::callbacks()
{
    return m_callbacks;
}

const HeightRangeEntry& HeightRangeRow::height_range() const
{
    return m_height_range;
}

void HeightRangeRow::set_height_range(const HeightRangeEntry& height_range)
{
    m_height_range = height_range;
    this->update_labels();
}

void HeightRangeRow::set_has_overrides(const bool has_overrides)
{
    m_has_overrides = has_overrides;
    this->update_undo_button();
}

void HeightRangeRow::set_checked(bool checked)
{
    m_height_range_button->set_checked(checked);
}

void HeightRangeRow::set_highlighted(bool highlighted)
{
    m_height_range_button->set_highlighted(highlighted);
}

void HeightRangeRow::update_labels()
{
    const auto format_trimmed = [](double value, int precision) -> std::string
    {
        std::string value_str = fmt::format("{:.{}f}", value, precision);
        if (value_str.find('.') != std::string::npos) {
            value_str.erase(value_str.find_last_not_of('0') + 1, std::string::npos);
            if (value_str.back() == '.') {
                value_str.pop_back();
            }
        }

        return value_str;
    };

    m_height_range_button->set_range_label(
        format_trimmed(m_height_range.min_z, 3)
        + " - "
        + format_trimmed(m_height_range.max_z, 3)
        + " mm"
    );
    m_height_range_button->set_height_label(format_trimmed(m_height_range.layer_height, 2) + " mm");
}

void HeightRangeRow::update_undo_button()
{
    if (m_has_overrides) {
        m_undo_button->set_enabled(true);
        m_undo_button->set_icon(Render::Icon::UndoGizmo);
        m_undo_button->set_background_color(Platform::Color::ButtonTransparent);
    } else {
        m_undo_button->set_enabled(false);
        m_undo_button->set_icon(Render::Icon::None);
        m_undo_button->set_background_color(Platform::Color::ButtonTransparent);
    }
}

} // namespace Slic3r::App::Plater
