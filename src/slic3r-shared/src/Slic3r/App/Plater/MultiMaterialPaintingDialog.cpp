#include "Slic3r/App/Plater/MultiMaterialPaintingDialog.hpp"

#include "Slic3r/App/Plater/MultiMaterialPaintingGizmo.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/SliderWithInput.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ToggleButton.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Plater/MMPaintingUtils.hpp"
#include "Slic3r/App/Plater/MMPaintingColorDropdowns.hpp"
#include "Slic3r/App/Plater/MMPaintingColorSelector.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"


using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Algorithms;

namespace Slic3r::App::Plater {

static Yoga::Item* append(Yoga::Item* parent, Yoga::ItemPtr item)
{
    auto item_ptr{item.get()};
    parent->append(std::move(item));
    return item_ptr;
}


MultiMaterialPaintingDialog::Callbacks& MultiMaterialPaintingDialog::callbacks()
{
    return m_callbacks;
}

static const ImColor mouse_left_color{115, 151, 236};
static const ImColor mouse_right_color{175, 119, 255};
const Unit spacing{5_fpx};
const Unit prefered_icon_size{36_fpx};

ItemPtr label(const std::string& text)
{
    ItemPtr result{std::make_unique<Item>()};
    result->set_flex_grow(1);
    result->set_orientation(Orientation::Horizontal);
    result->set_align_items(YGAlignCenter);
    result->set_justify_content(YGJustifyFlexStart);
    result->emplace_back<Text>(text);
    return result;
}

void apply_icon_button_style(Item& item)
{
    item.set_align_items(YGAlignStretch);
    item.set_min_width(18_fpx);
    item.set_min_height(18_fpx);
    item.set_max_width(prefered_icon_size);
    item.set_max_height(prefered_icon_size);
    item.set_flex_grow(1);
    item.set_aspect_ratio(1);
}

ItemPtr icons_row()
{
    ItemPtr result{std::make_unique<Item>()};
    result->set_orientation(Orientation::Horizontal);
    result->set_height(prefered_icon_size);
    result->set_align_items(YGAlignCenter);
    result->set_gap(2 * spacing);
    result->set_justify_content(YGJustifyFlexStart);
    return result;
}

ItemPtr MultiMaterialPaintingDialog::brush_properties_picker()
{
    auto emplace_flex_button =
        [](Item* container, Render::Icon icon, const std::string& tooltip) -> LayoutButton*
    {
        LayoutButton* flex_button =
            container->emplace_back<LayoutButton>(std::string{}, icon, tooltip);
        flex_button->icon_object()->set_width(14_fpx);
        flex_button->icon_object()->set_height(14_fpx);
        apply_icon_button_style(*flex_button);
        flex_button->set_checkable(true);
        flex_button->set_content_padding(0);
        flex_button->set_content_align_items(YGAlignCenter);

        return flex_button;
    };

    ItemPtr result{std::make_unique<Item>()};
    result->set_gap(4 * spacing);
    auto labels{result->emplace_back<Item>()};
    labels->set_orientation(Orientation::Vertical);
    labels->set_gap(3 * spacing);

    Item* tools_label{Plater::append(labels, label(_u8L("Tool")))};
    tools_label->set_flex_grow(1);

    m_shapes_label = Plater::append(labels, label(_u8L("Shape")));
    m_shapes_label->set_flex_grow(1);

    auto icons{result->emplace_back<Item>()};
    icons->set_orientation(Orientation::Vertical);
    icons->set_align_items(YGAlignStretch);
    icons->set_gap(3 * spacing);
    icons->set_flex_grow(1);

    Item* tools_row{Plater::append(icons, icons_row())};

    m_brush_button = emplace_flex_button(tools_row, Render::Icon::PaintBrush, _u8L("Brush"));
    m_smart_fill_button =
        emplace_flex_button(tools_row, Render::Icon::WandMagicSparkles, _u8L("Smart fill"));
    m_bucket_fill_button =
        emplace_flex_button(tools_row, Render::Icon::FillDrip, _u8L("Bucket fill"));
    m_color_replace_button =
        emplace_flex_button(tools_row, Render::Icon::ColorReplace, _u8L("Color replace"));
    m_height_range_button =
        emplace_flex_button(tools_row, Render::Icon::LineHeight, _u8L("Height range fill"));

    m_tool_type_group.set_buttons(
        {m_brush_button,
         m_smart_fill_button,
         m_bucket_fill_button,
         m_color_replace_button,
         m_height_range_button}
    );
    m_tool_type_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton* last_checked)
    {
        if (current_checked == m_brush_button) {
            m_selected_tool_type = PaintOnGizmoBase::ToolType::BRUSH;
        } else if (current_checked == m_smart_fill_button) {
            m_selected_tool_type = PaintOnGizmoBase::ToolType::SMART_FILL;
        } else if (current_checked == m_bucket_fill_button) {
            m_selected_tool_type = PaintOnGizmoBase::ToolType::BUCKET_FILL;
        } else if (current_checked == m_color_replace_button) {
            m_selected_tool_type = PaintOnGizmoBase::ToolType::COLOR_REPLACE;
        } else if (current_checked == m_height_range_button) {
            m_selected_tool_type = PaintOnGizmoBase::ToolType::HEIGHT_RANGE;
        }

        update_visibility();
        m_callbacks.tool_type_changed(m_selected_tool_type);
    };

    m_brush_shape_row = Plater::append(icons, icons_row());

    m_sphere_brush_button =
        emplace_flex_button(m_brush_shape_row, Render::Icon::Sphere, _u8L("Sphere"));
    m_circle_brush_button =
        emplace_flex_button(m_brush_shape_row, Render::Icon::Circle, _u8L("Circle"));
    m_triangle_brush_button =
        emplace_flex_button(m_brush_shape_row, Render::Icon::Triangle, _u8L("Triangle"));
    m_brush_shape_group.set_buttons(
        {m_sphere_brush_button, m_circle_brush_button, m_triangle_brush_button}
    );
    m_brush_shape_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton* last_checked)
    {
        if (current_checked == m_sphere_brush_button) {
            m_selected_brush_type = TriangleSelector::CursorType::SPHERE;
        } else if (current_checked == m_circle_brush_button) {
            m_selected_brush_type = TriangleSelector::CursorType::CIRCLE;
        } else if (current_checked == m_triangle_brush_button) {
            m_selected_brush_type = TriangleSelector::CursorType::POINTER;
        } else {
            ASSERT(false);
        }

        this->update_visibility();
        m_callbacks.brush_shape_changed(m_selected_brush_type);
    };

    for (std::size_t i = m_brush_shape_group.buttons().size();
         i < m_tool_type_group.buttons().size();
         ++i)
    {
        auto spacer{m_brush_shape_row->emplace_back<Item>()};
        apply_icon_button_style(*spacer);
    }

    return result;
}

ItemPtr MultiMaterialPaintingDialog::brush_size_picker()
{
    ItemPtr result{std::make_unique<Item>()};
    m_brush_radius_row = result.get();
    result->set_orientation(Orientation::Vertical);
    result->set_gap(2 * spacing);

    auto label{result->emplace_back<Text>(_u8L("Brush size"))};
    label->set_font_type(Render::ImguiFontType::Bold);

    m_brush_radius_slider = result->emplace_back<SliderWithInput>(_u8L("mm"));
    m_brush_radius_slider->set_begin_value(MultiMaterialPaintingGizmo::CursorRadiusMin);
    m_brush_radius_slider->set_end_value(MultiMaterialPaintingGizmo::CursorRadiusMax);
    m_brush_radius_slider->set_step(0.01);
    m_brush_radius_slider->set_max_width(preffered_max_width());

    m_brush_radius_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.brush_radius_changed(value); };
    return result;
}

ItemPtr MultiMaterialPaintingDialog::smart_fill_angle_picker()
{
    ItemPtr result{std::make_unique<Item>()};
    m_smart_fill_angle_row = result.get();
    result->set_orientation(Orientation::Vertical);
    result->set_gap(2 * spacing);

    auto label{result->emplace_back<Text>(_u8L("Smart fill angle"))};
    label->set_font_type(Render::ImguiFontType::Bold);

    m_smart_fill_angle_slider = result->emplace_back<SliderWithInput>(_u8L("°"));
    m_smart_fill_angle_slider->set_begin_value(MultiMaterialPaintingGizmo::SmartFillAngleMin);
    m_smart_fill_angle_slider->set_end_value(MultiMaterialPaintingGizmo::SmartFillAngleMax);
    m_smart_fill_angle_slider->set_step(MultiMaterialPaintingGizmo::SmartFillAngleStep);
    m_smart_fill_angle_slider->set_max_width(preffered_max_width());
    m_smart_fill_angle_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.smart_fill_angle_changed(value); };
    return result;
}

ItemPtr MultiMaterialPaintingDialog::bucket_fill_angle_picker()
{
    ItemPtr result{std::make_unique<Item>()};
    m_bucket_fill_angle_row = result.get();
    result->set_orientation(Orientation::Vertical);
    result->set_gap(2 * spacing);

    auto label{result->emplace_back<Text>(_u8L("Bucket fill angle"))};
    label->set_font_type(Render::ImguiFontType::Bold);

    m_bucket_fill_angle_slider = result->emplace_back<SliderWithInput>(_u8L("°"));
    m_bucket_fill_angle_slider->set_begin_value(MultiMaterialPaintingGizmo::SmartFillAngleMin);
    m_bucket_fill_angle_slider->set_end_value(MultiMaterialPaintingGizmo::SmartFillAngleMax);
    m_bucket_fill_angle_slider->set_step(MultiMaterialPaintingGizmo::SmartFillAngleStep);
    m_bucket_fill_angle_slider->set_max_width(preffered_max_width());
    m_bucket_fill_angle_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.bucket_fill_angle_changed(value); };
    return result;
}

ItemPtr MultiMaterialPaintingDialog::height_range_picker()
{
    ItemPtr result{std::make_unique<Item>()};
    m_height_range_row = result.get();
    result->set_orientation(Orientation::Vertical);
    result->set_gap(2 * spacing);

    auto label{result->emplace_back<Text>(_u8L("Height range"))};
    label->set_font_type(Render::ImguiFontType::Bold);

    m_height_range_slider = result->emplace_back<SliderWithInput>(_u8L("mm"));
    m_height_range_slider->set_begin_value(MultiMaterialPaintingGizmo::HeightRangeZRangeMin);
    m_height_range_slider->set_end_value(MultiMaterialPaintingGizmo::HeightRangeZRangeMax);
    m_height_range_slider->set_step(MultiMaterialPaintingGizmo::HeightRangeZRangeStep);
    m_height_range_slider->set_max_width(preffered_max_width());
    m_height_range_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.height_range_changed(value); };
    return result;
}

static ItemPtr help()
{
    ItemPtr result{std::make_unique<Item>()};
    result->set_orientation(Orientation::Vertical);
    result->set_gap(2 * spacing);
    GizmoHelpFactory help;
    help.init(result.get());
    const Unit icon_size{16_fpx};
    help.add_item(
        {GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft, icon_size, icon_size}},
        _u8L("Paint 1. color")
    );
    help.add_item(
        {GizmoHelpFactory::HelpIcon{Render::Icon::MouseRight, icon_size, icon_size}},
        _u8L("Paint 2. color")
    );
    help.add_item(
        {{"SHIFT"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseLeft, icon_size, icon_size}},
        _u8L("Remove painted color")
    );
    help.add_item({{"X"}}, _u8L("Swap colors"));

    help.add_item(
        {{"ALT"}, GizmoHelpFactory::HelpIcon{Render::Icon::MouseWheel, icon_size, icon_size}},
        _u8L("Tool size")
    );
    return result;
}

MultiMaterialPaintingDialog::MultiMaterialPaintingDialog(
    Biz::ProjectInteractor& project_interactor
) :
    GizmoWindow()
{
    const Paddings padding{4 * spacing};

    content()->set_orientation(Orientation::Vertical);
    content()->set_flex_grow(1);
    content()->set_padding(0);

    revert_button()->set_visible(true);
    revert_button()->callbacks().action = [this]() { m_callbacks.painting_reset(); };
    revert_button()->set_tooltip(_u8L("Reset painting"));

    auto selector_section{content()->emplace_back<Item>()};
    selector_section->set_padding(padding);
    selector_section->set_orientation(Orientation::Vertical);
    selector_section->set_gap(3 * spacing);
    selector_section->set_flex_shrink(0);

    m_color_dropdowns = selector_section->emplace_back<ColorDropdowns>(
        project_interactor,
        spacing,
        mouse_left_color,
        mouse_right_color
    );
    m_color_dropdowns->set_max_width(preffered_max_width());
    m_color_selector =
        selector_section->emplace_back<ColorSelector>(mouse_left_color, mouse_right_color);

    auto on_color_selected{[this](SelectedColor color, std::size_t index)
                           {
                               if (color == SelectedColor::Primary) {
                                   callbacks().first_brush_color_changed(index);
                               }
                               if (color == SelectedColor::Secondary) {
                                   callbacks().second_brush_color_changed(index);
                               }
                           }};

    m_color_dropdowns->on_color_selected =
        [this, on_color_selected](SelectedColor color, std::size_t index)
    {
        m_color_selector->select_color_index(color, index);
        on_color_selected(color, index);
    };
    m_color_selector->on_color_selected =
        [this, on_color_selected](SelectedColor color, std::size_t index)
    {
        m_color_dropdowns->select_color_index(color, index);
        on_color_selected(color, index);
    };

    add_separator(content());

    auto brush_section{content()->emplace_back<Item>()};
    brush_section->set_padding(padding);
    brush_section->set_orientation(Orientation::Vertical);
    brush_section->set_gap(padding.left);
    brush_section->set_flex_shrink(0);

    Plater::append(brush_section, brush_properties_picker());
    Plater::append(brush_section, brush_size_picker());
    Plater::append(brush_section, smart_fill_angle_picker());
    Plater::append(brush_section, bucket_fill_angle_picker());
    Plater::append(brush_section, height_range_picker());

    add_separator(content());

    auto view_clipper_section{content()->emplace_back<Item>()};
    view_clipper_section->set_padding(padding);
    view_clipper_section->set_orientation(Orientation::Vertical);
    view_clipper_section->set_gap(2 * spacing);
    view_clipper_section->set_gap(spacing);
    view_clipper_section->set_flex_shrink(0);
    auto view_clipper_label{view_clipper_section->emplace_back<Text>(_u8L("Clipping of view"))};

    auto view_clipper_slider_row{view_clipper_section->emplace_back<Item>()};
    view_clipper_slider_row->set_gap(spacing);
    view_clipper_slider_row->set_align_items(YGAlignCenter);
    view_clipper_slider_row->set_width_percent(100);
    view_clipper_slider_row->set_flex_shrink(0);

    view_clipper_label->set_font_type(Render::ImguiFontType::Bold);
    m_clipping_of_view_reset_direction_button = view_clipper_slider_row->emplace_back<LayoutButton>(
        "",
        Render::Icon::ArrowUpToLine,
        _u8L("Reset clipping direction")
    );
    m_clipping_of_view_reset_direction_button->set_height_percent(80);
    m_clipping_of_view_reset_direction_button->set_content_padding(3_fpx);
    m_clipping_of_view_reset_direction_button->callbacks().action = [this]()
    { m_callbacks.clipping_of_view_reset_direction(); };

    m_clipping_of_view_slider = view_clipper_slider_row->emplace_back<SliderWithInput>();
    m_clipping_of_view_slider->set_flex_grow(1);
    m_clipping_of_view_slider->set_begin_value(0.);
    m_clipping_of_view_slider->set_end_value(1.);
    m_clipping_of_view_slider->set_step(0.01);
    m_clipping_of_view_slider->set_max_width(preffered_max_width());
    m_clipping_of_view_slider->callbacks().value_changed = [this](double value)
    { m_callbacks.clipping_of_view_value_changed(value); };

    add_separator(content());

    m_split_triangles_section = content()->emplace_back<Item>();
    m_split_triangles_section->set_padding(padding);
    m_split_triangles_section->set_orientation(Orientation::Horizontal);
    m_split_triangles_section->set_align_items(YGAlignFlexEnd);
    m_split_triangles_section->set_gap(2 * spacing);
    m_split_triangles_section->set_flex_shrink(0);
    m_split_triangles_toggle = m_split_triangles_section->emplace_back<ToggleButton>();
    m_split_triangles_toggle->callbacks().checked_changed = [this](bool checked)
    { m_callbacks.split_triangles_value_changed(checked); };
    m_split_triangles_section->emplace_back<Text>(_u8L("Split triangles"));

    m_split_triangles_separator = add_separator(content());

    auto help_section{content()->emplace_back<Item>()};
    help_section->set_padding(padding);
    help_section->set_flex_shrink(0);
    Plater::append(help_section, help());
}

void MultiMaterialPaintingDialog::set_brush_radius(const double brush_radius)
{
    m_brush_radius_slider->set_value(brush_radius);
}

void MultiMaterialPaintingDialog::set_first_brush_color_index(size_t color_idx)
{
    if (color_idx >= m_color_selector->colors_count()) {
        return;
    }
    m_color_selector->select_color_index(SelectedColor::Primary, color_idx);
    m_color_dropdowns->select_color_index(SelectedColor::Primary, color_idx);
}

void MultiMaterialPaintingDialog::set_second_brush_color_index(size_t color_idx)
{
    if (color_idx >= m_color_selector->colors_count()) {
        return;
    }
    m_color_selector->select_color_index(SelectedColor::Secondary, color_idx);
    m_color_dropdowns->select_color_index(SelectedColor::Secondary, color_idx);
}

void MultiMaterialPaintingDialog::set_clipping_of_view_value(const double clipping_of_view_value)
{
    m_clipping_of_view_slider->set_value(clipping_of_view_value);
}

void MultiMaterialPaintingDialog::set_smart_fill_angle(const double smart_fill_angle)
{
    m_smart_fill_angle_slider->set_value(smart_fill_angle);
}

void MultiMaterialPaintingDialog::set_bucket_fill_angle(double bucket_fill_angle)
{
    m_bucket_fill_angle_slider->set_value(bucket_fill_angle);
}

void MultiMaterialPaintingDialog::set_height_range(double height_range)
{
    m_height_range_slider->set_value(height_range);
}

void MultiMaterialPaintingDialog::set_split_triangles_value(const bool split_triangles)
{
    m_split_triangles_toggle->set_checked(split_triangles);
}

void MultiMaterialPaintingDialog::set_painting_colors(const PaintingPalette& palette)
{
    m_color_selector->set_colors(palette);
}

void MultiMaterialPaintingDialog::switch_colors()
{
    m_color_dropdowns->switch_colors();
    auto [primary_color_index, secondary_color_index]{m_color_dropdowns->current_indicies()};
    m_color_selector->select_color_index(SelectedColor::Primary, primary_color_index);
    m_color_selector->select_color_index(SelectedColor::Secondary, secondary_color_index);
    m_callbacks.first_brush_color_changed(primary_color_index);
    m_callbacks.second_brush_color_changed(secondary_color_index);
}

void MultiMaterialPaintingDialog::set_tool_type(const PaintOnGizmoBase::ToolType& tool_type)
{
    switch (tool_type) {
    case PaintOnGizmoBase::ToolType::BRUSH:
        m_brush_button->set_checked(true);
        break;
    case PaintOnGizmoBase::ToolType::BUCKET_FILL:
        m_bucket_fill_button->set_checked(true);
        break;
    case PaintOnGizmoBase::ToolType::SMART_FILL:
        m_smart_fill_button->set_checked(true);
        break;
    case PaintOnGizmoBase::ToolType::COLOR_REPLACE:
        m_color_replace_button->set_checked(true);
        break;
    case PaintOnGizmoBase::ToolType::HEIGHT_RANGE:
        m_height_range_button->set_checked(true);
        break;
    default:
        ASSERT(false);
        break;
    }

    this->update_visibility();
}

void MultiMaterialPaintingDialog::set_brush_type(
    const Biz::Algorithms::TriangleSelector::CursorType& brush_type
)
{
    return;
    using namespace Slic3r::Biz::Algorithms;

    switch (brush_type) {
    case TriangleSelector::CursorType::SPHERE:
        m_sphere_brush_button->set_checked(true);
        break;
    case TriangleSelector::CursorType::CIRCLE:
        m_circle_brush_button->set_checked(true);
        break;
    case TriangleSelector::CursorType::POINTER:
        m_triangle_brush_button->set_checked(true);
        break;
    default:
        ASSERT(false);
        break;
    }

    this->update_visibility();
}

void MultiMaterialPaintingDialog::update_visibility()
{
    m_brush_shape_row->set_visible(false);
    m_shapes_label->set_visible(false);
    m_brush_radius_row->set_visible(false);
    m_split_triangles_section->set_visible(false);
    m_split_triangles_separator->set_visible(false);
    m_smart_fill_angle_row->set_visible(false);
    m_bucket_fill_angle_row->set_visible(false);
    m_height_range_row->set_visible(false);

    if (m_selected_tool_type == PaintOnGizmoBase::ToolType::BRUSH) {
        m_brush_shape_row->set_visible(true);
        m_shapes_label->set_visible(true);

        if (m_selected_brush_type != TriangleSelector::CursorType::POINTER) {
            m_brush_radius_row->set_visible(true);
            m_split_triangles_section->set_visible(true);
            m_split_triangles_separator->set_visible(true);
        }
    } else if (m_selected_tool_type == PaintOnGizmoBase::ToolType::SMART_FILL) {
        m_smart_fill_angle_row->set_visible(true);
    } else if (m_selected_tool_type == PaintOnGizmoBase::ToolType::BUCKET_FILL) {
        m_bucket_fill_angle_row->set_visible(true);
    } else if (m_selected_tool_type == PaintOnGizmoBase::ToolType::HEIGHT_RANGE) {
        m_height_range_row->set_visible(true);
    }
}

} // namespace Slic3r::App::Plater
