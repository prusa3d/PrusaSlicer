
#include "Slic3r/App/Plater/MeasureDialog.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Circle.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Math.hpp"

#include "Slic3r/LegacyFormat.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::App::Plater::Measure;

namespace Slic3r::App::Plater {
static const ImColor FEATURE_1_COLOR = ImColor(64, 191, 191);
static const ImColor FEATURE_2_COLOR = ImColor(191, 64, 191);

using namespace Biz;

MeasureDialog::MeasureDialog() : GizmoWindow()
{
    const Vec2f shortcut_button_size{30.f, 30.f};

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(gap_size());

    // Create help panel

    m_helper_panel = content()->emplace_back<Item>();
    m_helper_panel->set_orientation(Orientation::Vertical);
    m_helper_panel->set_align_items(YGAlign::YGAlignFlexStart);
    m_helper_panel->set_gap(gap_size());

    m_extra_help.init(m_helper_panel);
    m_extra_help.add_item(
        {GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft}},
        _u8L("Select Objects")
    );

    // Create main panel

    m_main_panel = content()->emplace_back<Item>();
    m_main_panel->set_orientation(Orientation::Vertical);
    // m_main_panel->set_align_items(YGAlign::YGAlignFlexStart);
    m_main_panel->set_gap(gap_size());

    add_measure_rows();

    m_toggle_separator = add_separator(m_main_panel);
    m_toggle_separator->set_visible(false);

    std::unique_ptr<SpotDescription> spot1 = std::make_unique<SpotDescription>();
    m_spot1                                = spot1.get();
    add_spot_row(FEATURE_1_COLOR, _u8L("Feature 1"), std::move(spot1));

    std::unique_ptr<SpotDescription> spot2 = std::make_unique<SpotDescription>();
    m_spot2                                = spot2.get();
    add_spot_row(FEATURE_2_COLOR, _u8L("Feature 2"), std::move(spot2));

    add_separator(m_main_panel);

    Item* help_row = m_main_panel->emplace_back<Item>();
    help_row->set_min_height(50);
    help_row->set_justify_content(YGJustify::YGJustifySpaceEvenly);
    help_row->set_align_items(YGAlign::YGAlignCenter);
    help_row->set_padding(5);
    help_row->set_gap(15);
    help_row->set_flex_wrap(YGWrapWrap);

    {
        Item* help_section = help_row->emplace_back<Item>();
        help_section->set_align_items(YGAlignCenter);
        help_section->set_gap(5);
        m_help_select_icon = help_section->emplace_back<Icon>(Render::Icon::MouseLeft);
        m_help_select_icon->set_width(25);
        m_help_select_icon->set_height(25);
        m_help_select_icon->set_aspect_ratio(1);
        m_help_select_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
        m_help_select_text = help_section->emplace_back<Text>(_u8L("Select"));
        m_help_select_text->set_align({AlignH::Center, AlignV::Center});
    }
    {
        Item* help_section = help_row->emplace_back<Item>();
        help_section->set_align_items(YGAlignCenter);
        help_section->set_gap(5);
        m_help_unselect_icon = help_section->emplace_back<Icon>(Render::Icon::KeyBackspace);
        m_help_unselect_icon->set_width(shortcut_button_size.x());
        m_help_unselect_icon->set_height(shortcut_button_size.y());
        m_help_unselect_icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
        m_help_unselect_text = help_section->emplace_back<Text>(_u8L("Unselect"));
        m_help_unselect_text->set_align({AlignH::Center, AlignV::Center});
    }
    m_main_panel->set_visible(false);
}

void MeasureDialog::SpotDescription::reset()
{
    m_name->set_text(std::string{});
    m_value->set_text(std::string{});
}

void MeasureDialog::SpotDescription::set_from(const FeatureItem& feature)
{
    switch (feature.feature.type()) {
    case SurfaceFeatureType::Undefined: {
        reset();
        break;
    }
    case SurfaceFeatureType::Point: {
        if (feature.parent.has_value()) {
            if (feature.parent->type() == SurfaceFeatureType::Circle) {
                auto [center, radius, normal] = feature.parent->circle();
                if (feature.feature.point().isApprox(center)) {
                    m_name->set_text(_u8L("Center of circle"));
                    m_value->set_text(std::string{});
                    break;
                }
            } else {
                std::string parent_type;
                switch (feature.parent->type()) {
                case SurfaceFeatureType::Edge: {
                    parent_type = _u8L("edge");
                    break;
                }
                case SurfaceFeatureType::Circle: {
                    parent_type = _u8L("circle");
                    break;
                }
                case SurfaceFeatureType::Plane: {
                    parent_type = _u8L("plane");
                    break;
                }
                default:
                    PANIC("invalid parent");
                }
                m_name->set_text(Slic3r::format("%1% %2%", _u8L("Point on"), parent_type));
            }
        } else
            m_name->set_text(_u8L("Vertex"));
        m_value->set_text(std::string{});
        break;
    }
    case SurfaceFeatureType::Edge: {
        auto [from, to] = feature.feature.edge();
        m_name->set_text(_u8L("Edge"));
        m_value->set_text(Slic3r::format("%1%: %2$.3f mm", _u8L("Length"), (to - from).norm()));
        break;
    }
    case SurfaceFeatureType::Circle: {
        auto [center, radius, normal] = feature.feature.circle();
        m_name->set_text(_u8L("Circle"));
        m_value->set_text(Slic3r::format("%1%: %2$.3f mm", _u8L("Diameter"), 2.0f * float(radius)));
        break;
    }
    case SurfaceFeatureType::Plane: {
        m_name->set_text(_u8L("Plane"));
        m_value->set_text(std::string{});
        break;
    }
    }
    m_name->set_visible(!m_name->text().empty());
    m_value->set_visible(!m_value->text().empty());
}

MeasureDialog::SpotDescription& MeasureDialog::spot1()
{
    return *m_spot1;
}

MeasureDialog::SpotDescription& MeasureDialog::spot2()
{
    return *m_spot2;
}

void MeasureDialog::update(
    const Measure::MeasurementResult& result,
    const Measure::FeatureCache& features
)
{
    if (!m_main_panel->is_visible())
        return;

    bool first_selected  = features.first_selected().has_value();
    bool second_selected = features.second_selected().has_value();
    m_help_unselect_icon->set_enabled(first_selected);
    m_help_unselect_text->set_enabled(first_selected);

    ImColor select_color;
    std::string select_text = _u8L("Select");
    if (features.hover_id == HoverID::FirstSelectedFeature) {
        select_color = FEATURE_1_COLOR;
        select_text  = _u8L("Unselect");
    } else if (features.hover_id == HoverID::SecondSelectedFeature) {
        select_color = FEATURE_2_COLOR;
        select_text  = _u8L("Unselect");
    } else if (features.hover_id == HoverID::FirstCircleCenterFeature) {
        select_color = FEATURE_1_COLOR;
        select_text  = (!first_selected || !features.first_selected()->parent.has_value()) ?
            _u8L("Select") :
            _u8L("Unselect");
    } else if (features.hover_id == HoverID::SecondCircleCenterFeature) {
        select_color = FEATURE_2_COLOR;
        select_text  = (!second_selected || !features.second_selected()->parent.has_value()) ?
            _u8L("Select") :
            _u8L("Unselect");
    } else if (first_selected)
        select_color = FEATURE_2_COLOR;
    else
        select_color = FEATURE_1_COLOR;

    m_help_select_icon->set_tint(select_color);
    m_help_select_text->set_text_color(select_color);

    const ImColor unselect_color = m_help_unselect_icon->enabled() ?
        (second_selected ? FEATURE_2_COLOR : FEATURE_1_COLOR) :
        m_theme->color_imgui(Platform::Color::Text);
    m_help_unselect_text->set_text_color(unselect_color);
    m_help_unselect_icon->set_tint(unselect_color);

    set_measure(result);
}

void MeasureDialog::show_measure(bool show)
{
    m_helper_panel->set_visible(!show);
    m_main_panel->set_visible(show);
}

void MeasureDialog::add_measure_rows()
{
    for (size_t i = 0; i < 2; ++i) {
        MeasureRowItem& row_item = m_measure_rows[i];
        row_item.row             = m_main_panel->emplace_back<Item>();
        row_item.row->set_gap(gap_size());

        row_item.name = row_item.row->emplace_back<Text>("");
        row_item.name->set_self_align(YGAlignCenter);
        row_item.name->set_width_percent(50);

        Item* value_item = row_item.row->emplace_back<Item>();
        value_item->set_width_percent(50);
        value_item->set_gap(gap_size());
        row_item.value = value_item->emplace_back<Text>("");
        row_item.value->set_self_align(YGAlignCenter);
        row_item.value->set_font_type(Render::ImguiFontType::Bold);
        row_item.value->set_text_color(m_theme->color_imgui(Platform::Color::AccentTertiary));
        row_item.value->set_flex_grow(1);

        row_item.copy_btn = value_item->emplace_back<LayoutButton>("", Render::Icon::CopyForGizmo);
        row_item.copy_btn->set_width(20_fpx);
        row_item.copy_btn->set_height(20_fpx);
        row_item.copy_btn->set_background_color(Platform::Color::Transparent);
        row_item.copy_btn->callbacks().action = [this, i]()
        { ImGui::SetClipboardText(m_measure_rows[i].clipboard_text.c_str()); };
        row_item.row->set_visible(false);
    }
}

void MeasureDialog::add_spot_row(
    const ImColor& marker_color,
    const std::string& title,
    Yoga::ItemPtr controls
)
{
    Item* row = m_main_panel->emplace_back<Item>();
    row->set_gap(gap_size());
    row->set_flex_shrink(0);

    Item* label_with_marker = row->emplace_back<Item>();
    label_with_marker->set_gap(gap_size());
    label_with_marker->set_width_percent(50);
    label_with_marker->set_align_items(YGAlignCenter);

    Circle* marker = label_with_marker->emplace_back<Circle>();
    marker->set_height(16_fpx);
    marker->set_flex_shrink(0);
    marker->set_fill(marker_color);
    label_with_marker->emplace_back<Text>(title);

    controls->set_width_percent(50);
    row->append(std::move(controls));
}

void MeasureDialog::set_measure(const MeasurementResult& result)
{
    for (auto& row : m_measure_rows) {
        row.row->set_visible(false);
    }
    m_toggle_separator->set_visible(result.has_any_data());

    if (!result.has_any_data())
        return;

    bool show_strict = result.distance_strict.has_value()
        && (!result.distance_infinite.has_value()
            || std::abs(result.distance_strict->dist - result.distance_infinite->dist)
                > Domain::EPSILON);

    size_t row_idx = 0;
    if (result.angle.has_value())
        set_measure_row(
            row_idx++,
            _u8L("Angle"),
            Slic3r::format("%1$.3f %2%", rad2deg(result.angle->angle), _u8L("°"))
        );
    if (result.distance_infinite.has_value())
        set_measure_row(
            row_idx++,
            show_strict ? _u8L("Perpendicular distance") : _u8L("Distance"),
            Slic3r::format("%1$.3f %2%", result.distance_infinite->dist, _u8L("mm"))
        );
    if (show_strict)
        set_measure_row(
            row_idx++,
            _u8L("Direct distance"),
            Slic3r::format("%1$.3f %2%", result.distance_strict->dist, _u8L("mm"))
        );
    if (result.distance_xyz.has_value())
        set_measure_row(
            row_idx++,
            _u8L("Distance XYZ"),
            Slic3r::format(
                "x:%1$.3f y:%2$.3f z:%3$.3f %4%",
                result.distance_xyz->x(),
                result.distance_xyz->y(),
                result.distance_xyz->z(),
                _u8L("mm")
            )
        );
}

void MeasureDialog::set_measure_row(size_t id, const std::string& name, const std::string& value)
{
    MeasureRowItem& row = m_measure_rows[id];
    row.name->set_text(name);
    row.value->set_text(value);
    row.name->set_visible(!name.empty());
    row.value->set_visible(!value.empty());
    row.row->set_visible(true);
}

MeasureDialog::SpotDescription::SpotDescription() : Item()
{
    set_orientation(Yoga::Orientation::Vertical);
    set_flex_shrink(0);
    m_name = emplace_back<Text>(std::string{});
    m_name->set_font_type(Render::ImguiFontType::Bold);
    m_value = emplace_back<Text>(std::string{});
    m_name->set_visible(false);
    m_value->set_visible(false);
}

} // namespace Slic3r::App::Plater
