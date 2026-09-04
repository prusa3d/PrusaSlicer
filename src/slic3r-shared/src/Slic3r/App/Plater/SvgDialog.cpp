
#include "Slic3r/App/Plater/SvgDialog.hpp"
#include "Slic3r/App/Plater/DialogUtils.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"

#include "Slic3r/Domain/EmbossShape.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"
#include <fmt/format.h>
#include <boost/filesystem.hpp>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace {
// Constants
constexpr double SURFACE_DISTANCE_STEP = 0.01;
constexpr double ANGLE_DEG_STEP        = .1;
constexpr double ANGLE_RAD_STEP        = .01;
} // namespace

namespace Slic3r::App::Plater {
const double SvgDialog::MIN_DEPTH{1e-3};
const double SvgDialog::MAX_DEPTH{1e2};
const double SvgDialog::MIN_HEIGHT{1e-3};
const double SvgDialog::MAX_HEIGHT{1e3};
const double SvgDialog::MIN_WIDTH{1e-3};
const double SvgDialog::MAX_WIDTH{1e3};
constexpr float ButtonSize = 20;

SvgDialog::SvgDialog() : GizmoWindow()
{
    Paddings padding = content()->padding().source;

    top_bar()->set_gap(gap_size());
    top_bar()->set_padding({padding.left, 5.f});
    top_bar()->set_align_items(YGAlignCenter);
    top_bar()->set_flex_shrink(0.f);

    padding.top = 0.f;
    content()->set_padding(padding);
    content()->set_gap(2.f * gap_size());

    Item* filename_wrap = top_bar()->emplace_back<Item>();
    filename_wrap->set_flex_grow(1.f);
    m_filename = filename_wrap->emplace_back<Text>(std::string{});
    m_filename->set_flex_grow(1.f);
    m_filename->set_wrap_mode(Text::WrapMode::WrapElide);
    m_reload =
        top_bar()->emplace_back<LayoutButton>(std::string(), Render::Icon::Reload, _u8L("Reload"));
    m_reload->set_min_width(ButtonSize);
    m_reload->set_min_height(ButtonSize);
    m_reload->callbacks().action = [this]()
    {
        if (m_callbacks.reload_file)
            m_callbacks.reload_file();
    };

    LayoutButton* options_btn = top_bar()->emplace_back<LayoutButton>(
        std::string(),
        Render::Icon::CaretDown,
        _u8L("Options")
    );
    options_btn->set_min_width(ButtonSize);
    options_btn->set_min_height(ButtonSize);
    options_btn->callbacks().action = [this]() { m_options_menu->open(); };

    m_options_menu = options_btn->emplace_back<Menu>("Options Menu", Position::Bottom);
    m_options_menu->append_item(_u8L("Change file") + " ...")->callbacks().action = [this]()
    {
        if (m_callbacks.change_file)
            m_callbacks.change_file();
    };
    m_options_menu->append_item(_u8L("Forget the filepath"))->callbacks().action = [this]()
    {
        if (m_callbacks.forgot_filepath)
            m_callbacks.forgot_filepath();
    };
    m_options_menu->append_item(_u8L("Bake"))->callbacks().action = [this]()
    {
        if (m_callbacks.bake)
            m_callbacks.bake();
    };
    m_options_menu->append_item(_u8L("Save as") + " ...")->callbacks().action = [this]()
    {
        if (m_callbacks.save_as)
            m_callbacks.save_as();
    };

    m_warning =
        filename_wrap->emplace_back<LayoutButton>("", Render::Icon::WarningMarker, "Some warning");
    m_warning->set_self_align(YGAlignFlexStart);
    m_warning->set_min_width(20);
    m_warning->set_min_height(20);
    m_warning->set_visible(false);

    add_separator(content());

    // TEMPORARY workaround >> This part of code have to be changed after fix the SPE-3734
    Rectangle* preview_bg = content()->emplace_back<Rectangle>();
    preview_bg->set_orientation(Orientation::Vertical);
    preview_bg->set_flex_shrink(0.f);
    preview_bg->set_flex_grow(0.5f);
    preview_bg->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    preview_bg->set_padding(10.f);
    preview_bg->set_justify_content(YGJustifyCenter);
    preview_bg->set_align_items(YGAlignCenter);

    m_preview = preview_bg->emplace_back<Icon>(Render::Icon::None);
    m_preview->set_min_width(20);
    m_preview->set_min_height(20);
    // Todo: cleanup
    m_preview->set_width(
        Yoga::Unit{240.f - padding.horizontal().value}
        // from m_layout_right_column->set_min_size({240, YGUndefined});
    );
    m_preview->set_tint(ImColor(0, 0, 0));
    m_preview->set_flex_grow(1.f);
    m_preview->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    // <<

    Item* settings_panet = add_non_shrinked_wrap(content(), Orientation::Vertical, gap_size());

    auto add_dummy_item = [](Item* parent)
    {
        parent->emplace_back<Item>()->set_min_width(ButtonSize);
        parent->emplace_back<Item>()->set_min_height(ButtonSize);
    };

    Item* depth_wrap = add_row_with_spin_double(
        _u8L("Depth"),
        settings_panet,
        &m_depth,
        _u8L("mm"),
        "", // no revert button
        MIN_DEPTH,
        MAX_DEPTH,
        0.1,
        1.
    );
    m_depth->callbacks().text_edited = [this]()
    {
        if (m_callbacks.depth_changed)
            m_callbacks.depth_changed(std::stod(m_depth->text()));
    };
    add_dummy_item(depth_wrap);
    add_dummy_item(depth_wrap);

    Item* width_wrap = add_row_with_spin_double(
        _u8L("Width"),
        settings_panet,
        &m_width,
        _u8L("mm"),
        _u8L("Revert width"),
        MIN_DEPTH,
        MAX_DEPTH,
        1.,
        5.
    );
    m_width->callbacks().text_edited = [this]()
    {
        if (m_callbacks.size_changed)
            m_callbacks.size_changed(Domain::Vec2d{std::stod(m_width->text()), 0.});
    };
    add_dummy_item(width_wrap);

    auto height = add_row_with_spin_double(
        _u8L("Height"),
        settings_panet,
        &m_height,
        _u8L("mm"),
        _u8L("Revert height"),
        MIN_DEPTH,
        MAX_DEPTH,
        1.,
        5.
    );
    m_height->callbacks().text_edited = [this]()
    {
        if (m_callbacks.size_changed)
            m_callbacks.size_changed(Domain::Vec2d{0., std::stod(m_height->text())});
    };

    m_lock_size_btn = height->emplace_back<LayoutButton>(
        "",
        Render::Icon::Lock
    ); // Note: for tooltip need externaly set value
    m_lock_size_btn->set_min_width(ButtonSize);
    m_lock_size_btn->set_min_height(ButtonSize);
    m_lock_size_btn->set_self_align(YGAlignCenter);
    m_lock_size_btn->callbacks().checked_changed = [this](bool checked)
    {
        if (m_callbacks.unlock_size)
            m_callbacks.unlock_size(checked);
        m_lock_size_btn->set_icon(checked ? Render::Icon::Unlock : Render::Icon::Lock);
        m_lock_size_btn->set_tooltip(
            checked ? _u8L("Keep current aspect ration.") : _u8L("Free size changing.")
        );
    };
    m_lock_size_btn->set_checkable(false);
    m_lock_size_btn->set_checkable(true); // set valid tooltip

    Item* surface_panel = add_non_shrinked_wrap(content(), Orientation::Vertical, gap_size());

    m_use_surface_row = add_labeled_row(surface_panel, _u8L("Use Surface"));
    m_use_surface     = m_use_surface_row->emplace_back<ToggleButton>();
    m_use_surface->set_tooltip(_u8L(
        "If checked,\n"
        "model surface under the text's shape is shift in the embossing direction,\n"
        "otherwise text is flat and you have to deal with distance from surface."
    ));
    m_use_surface->callbacks().checked_changed = [this](bool checked)
    {
        if (m_callbacks.use_surface_checked)
            m_callbacks.use_surface_checked(checked);
    };

    add_row_with_slider(
        surface_panel,
        &m_surface_distance,
        _u8L("From surface"),
        _u8L("mm"),
        _u8L("Revert")
    );
    m_surface_distance->set_begin_value(-2.);
    m_surface_distance->set_end_value(2.);
    m_surface_distance->set_step(SURFACE_DISTANCE_STEP);
    m_surface_distance->set_default(0.);
    m_surface_distance->callbacks().value_changed = [this](double value)
    {
        if (m_use_inch)
            value *= INCH_TO_MM;
        if (m_callbacks.surface_distance_changed)
            m_callbacks.surface_distance_changed(value);
    };

    Item* rotation_row =
        add_row_with_slider(content(), &m_rotation, _u8L("Rotation"), _u8L("°"), _u8L("Revert"));
    m_rotation->set_begin_value(-180.);
    m_rotation->set_end_value(180.);
    m_rotation->set_step(ANGLE_DEG_STEP);
    m_rotation->set_default(0.);
    m_rotation->callbacks().value_changed = [this](double value)
    {
        if (m_use_deg)
            value *= DEG_TO_RAD;
        if (m_callbacks.rotation_changed)
            m_callbacks.rotation_changed(value);
    };

    m_lock_rotation_btn = rotation_row->emplace_back<LayoutButton>(
        "",
        Render::Icon::Lock
    ); // Note: for tooltip need externaly set value
    m_lock_rotation_btn->set_min_width(ButtonSize);
    m_lock_rotation_btn->set_min_height(ButtonSize);
    m_lock_rotation_btn->set_self_align(YGAlignCenter);
    m_lock_rotation_btn->callbacks().checked_changed = [this](bool checked)
    {
        if (m_callbacks.unlock_rotation)
            m_callbacks.unlock_rotation(checked);
        m_lock_rotation_btn->set_icon(checked ? Render::Icon::Unlock : Render::Icon::Lock);
        m_lock_rotation_btn->set_tooltip(
            checked ? _u8L("Lock for the 'up vector' during move above surface.") :
                      _u8L("Free move above surface (can rotate around emboss axe).")
        );
    };
    m_lock_rotation_btn->set_checkable(false);
    m_lock_rotation_btn->set_checkable(true); // set valid tooltip

    Item* mirror_row = add_labeled_row(content(), _u8L("Mirror"));
    m_mirror_x       = mirror_row->emplace_back<LayoutButton>("", Render::Icon::ReflectionX);
    m_mirror_x->set_min_width(30);
    m_mirror_x->set_min_height(20);
    m_mirror_x->callbacks().action = [this]()
    {
        if (m_callbacks.mirror_x)
            m_callbacks.mirror_x();
    };
    m_mirror_y = mirror_row->emplace_back<LayoutButton>("", Render::Icon::ReflectionY);
    m_mirror_y->set_min_width(30);
    m_mirror_y->set_min_height(20);
    m_mirror_y->callbacks().action = [this]()
    {
        if (m_callbacks.mirror_y)
            m_callbacks.mirror_y();
    };
    mirror_row->set_visible(false); // Temporary hide mirror until mirror in PS will be ready

    m_face_the_camera_btn = content()->emplace_back<LayoutButton>(_u8L("Face the camera"));
    m_face_the_camera_btn->set_flex_shrink(0.f);
    m_face_the_camera_btn->callbacks().action = [this]()
    {
        if (m_callbacks.face_the_camera)
            m_callbacks.face_the_camera();
    };

    // operation
    add_part_specific_panel();

    // update limits
    update_units(true);
    update_units(false);
    update_angle(false);
    update_angle(true);
}

SvgDialog::Callbacks& SvgDialog::callbacks()
{
    return m_callbacks;
}

void SvgDialog::set_warning(const std::string& warning)
{
    m_warning->set_visible(!warning.empty());
    m_warning->set_tooltip(warning);
}

// Round doubles for 1 digits to correct behavior of revert buttons
static double round_1(double value)
{
    return std::round(value * 10.0) / 10.0;
};

namespace {

std::string get_filename(const Domain::EmbossShape::SvgFile& svg)
{
    if (svg.path.empty() && svg.path_in_3mf.empty()) {
        return std::string("--" + _u8L("unknown") + "--");
    }

    const std::string& path = svg.path.empty() ? svg.path_in_3mf : svg.path;
    return boost::filesystem::path(path).filename().string();
}

} // namespace

void SvgDialog::set_shape(const Domain::EmbossShape& shape)
{
    m_filename->set_text(get_filename(*shape.svg_file));
    if (shape.svg_file) {
        m_preview->set_image(shape.svg_file->path);
    }
}

void SvgDialog::set_enable_reload_from_disk(bool enable)
{
    m_reload->set_visible(enable);
}

void SvgDialog::set_size(const Domain::Vec2d& size, const Domain::Vec2d& size_original)
{
    m_width->set_text(fmt::format("{:.1f}", size.x()));
    m_width->set_default(round_1(size_original.x()));
    m_height->set_text(fmt::format("{:.1f}", size.y()));
    m_height->set_default(round_1(size_original.y()));
}

void SvgDialog::set_size_lock(bool lock)
{
    m_lock_size_btn->set_checked(lock);
}

namespace {
std::vector<std::string> get_operation_names()
{
    // NOTE: odred must match to function to_type
    return {
        _u8L("Join with object"), // index 0
        _u8L("Cut from object"), // 1
        _u8L("Modify object") // 2
    };
}

Domain::ModelVolumeType to_type(size_t operation_index)
{
    switch (operation_index) {
    case 0:
        return Domain::ModelVolumeType::MODEL_PART;
    case 1:
        return Domain::ModelVolumeType::NEGATIVE_VOLUME;
    case 2:
        return Domain::ModelVolumeType::PARAMETER_MODIFIER;
    // should not appear
    default:
        return Domain::ModelVolumeType::MODEL_PART;
    }
}

int to_operation_index(Domain::ModelVolumeType type)
{
    switch (type) {
    case Domain::ModelVolumeType::MODEL_PART:
        return 0;
    case Domain::ModelVolumeType::NEGATIVE_VOLUME:
        return 1;
    case Domain::ModelVolumeType::PARAMETER_MODIFIER:
        return 2;
    // should not appear
    default:
        return 0;
    }
}
} // namespace

void SvgDialog::add_part_specific_panel()
{
    m_part_specific_panel =
        add_non_shrinked_wrap(content(), Orientation::Vertical, content()->gap());
    add_separator(m_part_specific_panel);

    add_row_with_combo_box(_u8L("Operation"), m_part_specific_panel, &m_operation);
    m_operation->set_items(get_operation_names());
    m_operation->callbacks().selection_changed = [this](int index)
    {
        if (m_callbacks.operation_selection_changed)
            m_callbacks.operation_selection_changed(to_type(index));
    };

    m_part_specific_panel->set_visible(false);
}

void SvgDialog::set_operation(Domain::ModelVolumeType type)
{
    m_operation->set_current_index(to_operation_index(type));
}

void SvgDialog::show_part_specific_panel(bool show)
{
    m_part_specific_panel->set_visible(show);
}

void SvgDialog::update_units(bool use_inch)
{
    if (use_inch == m_use_inch)
        return;

    m_use_inch            = use_inch;
    std::string unit_text = use_inch ? _u8L("in") : _u8L("mm");
    for (Text* unit : m_units) {
        unit->set_text(unit_text);
    }
    // update limits
    if (use_inch) {
        set_spin_limits(m_depth, .005, 4., .005, .05);
    } else {
        set_spin_limits(m_depth, .1, 100., .1, 1.);
    }
}

void SvgDialog::update_angle(bool use_deg)
{
    if (m_use_deg == use_deg)
        return; // already setted
    m_use_deg = use_deg;

    m_rotation->set_unit(use_deg ? _u8L("°") : _u8L("rad"));
    if (use_deg) {
        set_limit_step(m_rotation, 180., 1.);
    } else {
        set_limit_step(m_rotation, M_PI, .02);
    }
}

void SvgDialog::set_depth(double depth_in_mm)
{
    m_depth->set_text(fmt::format("{:.10g}", depth_in_mm));
}

void SvgDialog::set_use_surface(bool checked)
{
    m_use_surface->set_checked(checked);
}

void SvgDialog::set_surface_distance(double distance_in_mm, double max_distance_in_mm)
{
    if (m_use_inch) {
        distance_in_mm *= MM_TO_INCH;
        max_distance_in_mm *= MM_TO_INCH;
    }

    // use N steps from zero to be able set to zero
    max_distance_in_mm =
        std::ceil(max_distance_in_mm / SURFACE_DISTANCE_STEP) * SURFACE_DISTANCE_STEP;

    m_surface_distance->set_begin_value(-max_distance_in_mm);
    m_surface_distance->set_end_value(max_distance_in_mm);
    m_surface_distance->set_value(distance_in_mm);
    m_surface_distance->set_default(0.); // To show revert button
}

void SvgDialog::set_rotation(double angle_in_rad)
{
    if (m_use_deg)
        angle_in_rad *= RAD_TO_DEG;
    m_rotation->set_value(angle_in_rad);
    m_rotation->set_default(0.); // To show revert button
}

void SvgDialog::set_rotation_lock(bool lock)
{
    m_lock_rotation_btn->set_checked(lock);
}

void SvgDialog::set_enable_use_surface(bool enable)
{
    m_use_surface_row->set_enabled(enable);
}

void SvgDialog::set_enable_surface_distance(bool enable)
{
    m_surface_distance->set_enabled(enable);
}

} // namespace Slic3r::App::Plater
