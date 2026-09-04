#include "Slic3r/App/Plater/ArrangeDialog.hpp"

#include "Slic3r/Domain/BedRef.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/SegmentedControl.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"

using namespace Slic3r::Biz;

namespace Slic3r::App::Plater {

using Biz::Algorithms::Scaling::scaled;
using Biz::Algorithms::Scaling::unscaled;
using Biz::Arrange::GeometryHandling;
using Biz::Arrange::PivotPoint;
using Biz::Arrange::Settings;
using Domain::BedRefs;
using Domain::coord_t;
using Domain::SelectionId;
using Domain::Vec2f;
using Yoga::AbstractButton;
using Yoga::ButtonGroup;
using Yoga::Item;
using Yoga::ItemPtr;
using Yoga::LayoutButton;
using Yoga::Orientation;
using Yoga::SegmentedControl;
using Yoga::SliderWithInput;
using Yoga::Text;
using Yoga::ToggleButton;
using Yoga::operator""_fpx;

namespace {
std::optional<PivotPoint> get_pivot_point(const std::size_t row, const std::size_t column)
{
    if (row == 0 && column == 0) {
        return PivotPoint::TopLeft;
    }
    if (row == 0 && column == 2) {
        return PivotPoint::TopRight;
    }
    if (row == 1 && column == 1) {
        return PivotPoint::Center;
    }
    if (row == 2 && column == 0) {
        return PivotPoint::BottomLeft;
    }
    if (row == 2 && column == 2) {
        return PivotPoint::BottomRight;
    }
    return std::nullopt;
}

Render::Icon get_icon(const PivotPoint pivot)
{
    switch (pivot) {
    case PivotPoint::TopLeft:
        return Render::Icon::ArrangeTopLeft;
    case PivotPoint::TopRight:
        return Render::Icon::ArrangeTopRight;
    case PivotPoint::BottomLeft:
        return Render::Icon::ArrangeBottomLeft;
    case PivotPoint::BottomRight:
        return Render::Icon::ArrangeBottomRight;
    case PivotPoint::Center:
        return Render::Icon::ArrangeCenter;
    }
    PANIC("Invalid pivot");
}

} // namespace

class PivotPicker : public Item
{
public:
    PivotPicker()
    {
        const float button_size{16.0};
        set_orientation(Orientation::Vertical);
        for (std::size_t row_index{}; row_index < 3; ++row_index) {
            Item* row = emplace_back<Item>();
            for (std::size_t column_index{}; column_index < 3; ++column_index) {
                const std::optional<PivotPoint> pivot_point{
                    get_pivot_point(row_index, column_index)
                };
                if (pivot_point) {
                    LayoutButton* button{
                        row->emplace_back<LayoutButton>("", get_icon(*pivot_point))
                    };
                    button->set_rounding(0.0);
                    button->set_content_padding(Yoga::Paddings{0.f});
                    button->set_background_color(IM_COL32_BLACK_TRANS);
                    button->set_checkable(true);
                    button->set_width(button_size);
                    button->set_height(button_size);
                    if (pivot_point == PivotPoint::BottomLeft) {
                        button->set_checked(true);
                    }
                    m_group.insert_button(button);
                    m_pivot_buttons.insert({button, *pivot_point});
                } else {
                    Item* filler{row->emplace_back<Item>()};
                    filler->set_width(button_size);
                    filler->set_height(button_size);
                }
            }
        }
    }

    PivotPoint get_selected_pivot() const
    {
        return m_pivot_buttons.at(m_group.checked_button());
    }

private:
    ButtonGroup m_group;
    std::map<AbstractButton*, PivotPoint> m_pivot_buttons;
};

SliderWithInput* append_spacing_slider(Item* container,
                                       const std::string& label,
                                       double initial_value,
                                       double max_width)
{
    auto spacing_section{container->emplace_back<Item>()};
    spacing_section->set_orientation(Orientation::Vertical);
    spacing_section->set_gap(10_fpx);
    auto spacing_label{spacing_section->emplace_back<Text>(label)};
    spacing_label->set_padding({0_fpx, 3_fpx, 0_fpx, 3_fpx});
    spacing_label->set_font_type(Render::ImguiFontType::Bold);

    auto slider{spacing_section->emplace_back<SliderWithInput>("mm")};
    slider->set_begin_value(0.0);
    slider->set_end_value(100.0);
    slider->set_step(1.0);
    slider->set_max_width(max_width);
    slider->set_value(initial_value);

    return slider;
}

static ItemPtr help()
{
    ItemPtr result{std::make_unique<Item>()};
    result->set_orientation(Orientation::Vertical);
    result->set_gap(10_fpx);

    auto all_beds_text{result->emplace_back<Text>(_u8L("All beds"))};
    all_beds_text->set_font_type(Render::ImguiFontType::Bold);
    all_beds_text->set_text_color(ImGui::GetColorU32(ImGuiCol_TextDisabled));
    GizmoHelpFactory all_beds_help;
    all_beds_help.init(result.get());
    all_beds_help.add_item({"A"}, _u8L("Arrange"));
    all_beds_help.add_item({"SHIFT", "A"}, _u8L("Arrange selection"));

    auto single_bed_text{result->emplace_back<Text>(_u8L("Current bed"))};
    single_bed_text->set_font_type(Render::ImguiFontType::Bold);
    single_bed_text->set_text_color(ImGui::GetColorU32(ImGuiCol_TextDisabled));
    GizmoHelpFactory single_bed_help;
    single_bed_help.init(result.get());
    single_bed_help.add_item({"D"}, _u8L("Arrange"));
    single_bed_help.add_item({"SHIFT", "D"}, _u8L("Arrange selection"));

    return result;
}

ArrangeDialog::ArrangeDialog(
    OnArrange on_arrange,
    OnCancel on_cancel,
    const Settings& settings
) :
    GizmoWindow(),
    m_on_arrange{on_arrange},
    m_on_cancel{on_cancel}
{
    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(0);
    content()->set_padding(0);

    using Segment = SegmentedControl::Segment;
    using Icon    = Render::Icon;
    auto segments = {
        Segment{
            .icon               = Icon::MultipleSquares,
            .tooltip            = Biz::_u8L("Printer group"),
            .initially_selected = true,
        },
        Segment{
            .icon               = Icon::SingleSquare,
            .tooltip            = Biz::_u8L("Selected beds"),
            .initially_selected = false,
        },
    };

    auto mode_spacing_section{content()->emplace_back<Item>()};
    mode_spacing_section->set_flex_shrink(0);
    mode_spacing_section->set_padding({20_fpx, 20_fpx, 20_fpx, 15_fpx});
    mode_spacing_section->set_gap(15_fpx);
    mode_spacing_section->set_orientation(Orientation::Vertical);

    auto mode_row{mode_spacing_section->emplace_back<Item>()};
    mode_row->set_align_items(YGAlignCenter);
    mode_row->set_gap(15_fpx);

    auto mode_label{mode_row->emplace_back<Text>(_u8L("Mode"))};
    mode_label->set_max_width(55_fpx);
    mode_label->set_flex_grow(1);

    m_mode = mode_row->emplace_back<SegmentedControl>(
        segments,
        [this](std::size_t index) {
            if (index == 0) {
                m_arrange_button->set_label(m_arrange_all_label);
            } else {
                m_arrange_button->set_label(m_arrange_beds_label);
            }
        }
    );

    m_offset_slider = append_spacing_slider(mode_spacing_section,
                          _u8L("Spacing"),
                          unscaled(static_cast<coord_t>(settings.scaled_offset)) * 2.0,
                          preffered_max_width());

    add_separator(content());

    auto bed_spacing_section{content()->emplace_back<Item>()};
    bed_spacing_section->set_flex_shrink(0);
    bed_spacing_section->set_padding({20_fpx, 15_fpx, 20_fpx, 15_fpx});
    bed_spacing_section->set_orientation(Orientation::Vertical);
    m_bed_offset_slider = append_spacing_slider(bed_spacing_section,
                          _u8L("Spacing from bed"),
                          settings.unscaled_bed_offset,
                          preffered_max_width());

    add_separator(content());

    auto rotation_geometry_section{content()->emplace_back<Item>()};
    rotation_geometry_section->set_flex_shrink(0);
    rotation_geometry_section->set_gap(10_fpx);
    rotation_geometry_section->set_orientation(Orientation::Vertical);
    rotation_geometry_section->set_padding({20_fpx, 15_fpx, 20_fpx, 15_fpx});

    auto rotation_row{rotation_geometry_section->emplace_back<Item>()};
    rotation_row->set_padding({0_fpx, 3_fpx, 0_fpx, 3_fpx});
    rotation_row->set_gap(10_fpx);
    m_enable_rotations_toggle = rotation_row->emplace_back<ToggleButton>();
    m_enable_rotations_toggle->set_checked(settings.allow_rotations);
    rotation_row->emplace_back<Text>(_u8L("Rotations (slow)"));

    m_geometry_handling = rotation_geometry_section->emplace_back<Yoga::ComboBox>(
        std::initializer_list{_u8L("Fast geometry"),
                              _u8L("Balanced geometry"),
                              _u8L("Accurate geometry")});
    m_geometry_handling->set_max_width(preffered_max_width());

    add_separator(content());

    m_bed_segments_section = content()->emplace_back<Item>();
    m_bed_segments_section->set_flex_shrink(0);
    m_bed_segments_section->set_gap(15_fpx);
    m_bed_segments_section->set_padding({20_fpx, 15_fpx, 20_fpx, 15_fpx});
    auto alignment_label{m_bed_segments_section->emplace_back<Text>(_u8L("Alignment"))};
    alignment_label->set_max_width(55_fpx);
    alignment_label->set_flex_grow(1);
    m_pivot_picker = m_bed_segments_section->emplace_back<PivotPicker>();
    m_bed_segments_separator = add_separator(content());
    set_bed_segments(settings.bed_segments);

    auto help_section{content()->emplace_back<Item>()};
    help_section->set_flex_shrink(0);
    help_section->set_padding({20_fpx, 15_fpx, 20_fpx, 15_fpx});
    help_section->set_flex_shrink(0);
    help_section->append(help());

    // empty item - stretch spacer
    content()->emplace_back<Item>()->set_flex_grow(1);
    add_separator(content());

    bottom_bar()->set_flex_shrink(0);
    bottom_bar()->set_padding(Yoga::Paddings{ 20_fpx });
    m_arrange_button = bottom_bar()->emplace_back<LayoutButton>(m_arrange_all_label);
    m_arrange_button->set_content_padding(Yoga::Paddings{10_fpx});
    m_arrange_button->callbacks().action = [this]() {
        m_on_arrange(static_cast<ArrangeMode>(m_mode->selected_index()));
    };

    m_arrange_button->set_flex_grow(1);

    constexpr ImColor color_primary{223, 93, 45};
    m_arrange_button->set_background_color(color_primary);
    m_arrange_button->set_label_font_type(Render::ImguiFontType::Bold);
}

void ArrangeDialog::update_segments_visibility() {
    if (m_bed_segments && !m_auxiliary_travel_anchor) {
        m_bed_segments_section->set_visible(true);
        m_bed_segments_separator->set_visible(true);
    } else {
        m_bed_segments_section->set_visible(false);
        m_bed_segments_separator->set_visible(false);
    }
}

void ArrangeDialog::set_bed_segments(const std::optional<Domain::BedSegments>& bed_segments)
{
    m_bed_segments = bed_segments;
    update_segments_visibility();
}

void ArrangeDialog::set_auxiliary_travel_anchor(
    const std::optional<Domain::Vec2d>& auxiliary_travel_anchor
)
{
    m_auxiliary_travel_anchor = auxiliary_travel_anchor;
    update_segments_visibility();
}

void ArrangeDialog::update_status(const ArrangeTaskStatus status)
{
    using namespace Biz;
    if (status == ArrangeTaskStatus::Running) {
        m_arrange_button->set_label(_u8L("Cancel"));
        m_arrange_button->callbacks().action = [this]() { m_on_cancel(); };
    } else if (status == ArrangeTaskStatus::Idle) {
        const auto mode{static_cast<ArrangeMode>(m_mode->selected_index())};
        if (mode == ArrangeMode::PrinterGroup) {
            m_arrange_button->set_label(m_arrange_all_label);
        } else {
            m_arrange_button->set_label(m_arrange_beds_label);
        }
        m_arrange_button->callbacks().action = [this, mode]() { m_on_arrange(mode); };
    } else {
        PANIC("Unknown arrange task status!");
    }
}

Settings ArrangeDialog::get_settings() const
{
    Settings result;

    result.scaled_offset       = scaled(m_offset_slider->value() / 2.0);
    result.unscaled_bed_offset = m_bed_offset_slider->value();

    const int geometry_handling{m_geometry_handling->current_index()};
    if (geometry_handling == 0) {
        result.fixed_geometry   = GeometryHandling::Convex;
        result.movable_geometry = GeometryHandling::Convex;
    } else if (geometry_handling == 1) {
        result.fixed_geometry   = GeometryHandling::Arbitrary;
        result.movable_geometry = GeometryHandling::Convex;
    } else {
        result.fixed_geometry   = GeometryHandling::Arbitrary;
        result.movable_geometry = GeometryHandling::Arbitrary;
    }

    if (m_bed_segments && !m_auxiliary_travel_anchor) {
        ASSERT(m_pivot_picker != nullptr);
        result.bed_pivot_point = m_pivot_picker->get_selected_pivot();
        result.bed_segments    = m_bed_segments;
    }

    result.auxiliary_travel_anchor = m_auxiliary_travel_anchor;
    result.allow_rotations = m_enable_rotations_toggle->checked();

    return result;
}

} // namespace Slic3r::App::Plater
