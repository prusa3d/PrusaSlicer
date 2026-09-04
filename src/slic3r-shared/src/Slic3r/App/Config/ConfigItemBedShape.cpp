#include "Slic3r/App/Config/ConfigItemBedShape.hpp"
#include "Slic3r/App/Config/BedShapePreview.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/Yoga/InputTextField.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Yoga/Validator.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"

#include <Slic3r/App/AppServices.hpp>
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/Wildcards.hpp"
#include "Slic3r/App/IDialogManager.hpp"

#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/Config/BedShape.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"

#include "libslic3r/TriangleMeshSlicer.hpp"

#include <numbers>
#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

static Item* add_row(Item* parent, const std::string& label_text)
{
    Item* row = parent->emplace_back<Item>();
    row->set_gap(10.f);

    Text* label = row->emplace_back<Text>(label_text + ":");
    label->set_font_type(Render::ImguiFontType::Bold);
    label->set_width(150.f);
    return row;
}

static int get_value(Yoga::InputTextField* input)
{
    return dynamic_cast<IntValidator*>(input->validator())->value();
}

static void set_value(Yoga::InputTextField* input, double value)
{
    input->set_text(fmt::format("{}", static_cast<int>(value)));
}

using namespace Biz::Config;

static InputTextField* add_input(
    Item* parent,
    const std::string& label_text,
    const BedShape::ParamAttributes& atributes,
    bool is_imperial_units,
    std::function<void()> callback
)
{
    Item* wrap = parent->emplace_back<Item>();
    wrap->set_gap(3.f);
    wrap->set_align_items(YGAlignCenter);

    if (!label_text.empty()) {
        wrap->emplace_back<Text>(label_text + ":");
    }

    InputTextField* ret_input = wrap->emplace_back<InputTextField>();
    ret_input->set_validator(std::make_unique<IntValidator>(atributes.min, atributes.max));
    ret_input->set_tooltip(atributes.tooltip);
    ret_input->set_text(fmt::format("{}", atributes.def_value));
    ret_input->callbacks().text_edited = callback;

    wrap->emplace_back<Text>(is_imperial_units ? Biz::_u8L("in") : Biz::_u8L("mm"));

    return ret_input;
}

ConfigItemBedShape::ConfigItemBedShape(
    size_t index,
    const Domain::ConfigItem& data,
    Biz::IConfigBoxSetter& cb_setter,
    std::vector<size_t> cbi_index
) :
    ConfigItemControl(index, data, cb_setter, cbi_index),
    m_bed_shape({})
{
    bool is_imperial_units{false}; // ToDo detect this value from app_config

    set_orientation(Orientation::Vertical);
    set_padding(10.f);
    set_gap(10.f);

    Item* shape_row = add_row(this, Biz::_u8L("Shape"));
    m_shape_combo   = shape_row->emplace_back<ComboBox>(std::initializer_list<std::string>{
        BedShape::get_type_name(BedShape::Type::Rectangle),
        BedShape::get_type_name(BedShape::Type::Circle),
        BedShape::get_type_name(BedShape::Type::Custom)
    });
    m_shape_combo->set_width(150.f);

    m_shape_combo->callbacks().selection_changed = [this](int selection)
    {
        m_ui_layout->set_current_index(selection);
        update_shape();
    };

    Tooltip& tooltip = m_shape_combo->tooltip();
    tooltip.set_text(tooltip_text());
    tooltip.set_text_wrap(true);
    tooltip.content_item()->set_width(350);

    m_ui_layout = emplace_back<StackLayout>();

    Item* rect_ui_item = m_ui_layout->emplace_back<Item>();
    rect_ui_item->set_orientation(Orientation::Vertical);
    rect_ui_item->set_padding(10.f);
    rect_ui_item->set_gap(10.f);

    auto callback = [this]() { update_shape(); };

    const BedShape::ParamAttributes& size_atribs =
        BedShape::attributes(BedShape::Parameter::RectSize);
    Item* m_size_row = add_row(rect_ui_item, size_atribs.name);
    m_size_x         = add_input(m_size_row, "x", size_atribs, is_imperial_units, callback);
    m_size_y         = add_input(m_size_row, "y", size_atribs, is_imperial_units, callback);

    const BedShape::ParamAttributes& origin_atribs =
        BedShape::attributes(BedShape::Parameter::RectOrigin);
    Item* m_origin_row = add_row(rect_ui_item, origin_atribs.name);
    m_origin_x         = add_input(m_origin_row, "x", origin_atribs, is_imperial_units, callback);
    m_origin_y         = add_input(m_origin_row, "y", origin_atribs, is_imperial_units, callback);

    Item* circle_ui_item = m_ui_layout->emplace_back<Item>();
    circle_ui_item->set_orientation(Orientation::Vertical);
    circle_ui_item->set_padding(10.f);

    const BedShape::ParamAttributes& diam_atribs =
        BedShape::attributes(BedShape::Parameter::Diameter);
    Item* m_diameter_row = add_row(circle_ui_item, diam_atribs.name);
    m_diameter           = add_input(m_diameter_row, "", diam_atribs, is_imperial_units, callback);

    Item* custom_ui_item = m_ui_layout->emplace_back<Item>();
    custom_ui_item->set_orientation(Orientation::Vertical);
    custom_ui_item->set_flex_grow(1);
    custom_ui_item->set_padding(10.f);

    Item* m_custom_row = custom_ui_item->emplace_back<Item>();
    m_custom_row->set_padding(10);
    m_custom_row->set_align_items(YGAlignCenter);
    m_custom_row->set_justify_content(YGJustifyCenter);
    m_load_btn =
        m_custom_row->emplace_back<LayoutButton>(Biz::_u8L("Load shape from STL") + " ...");

    m_load_btn->callbacks().action = [this]()
    {
        IDialogManager::FileCallback callback =
            [this](bool success, const std::vector<boost::filesystem::path>& file_paths)
        {
            if (success) {
                load_stl(file_paths.front());
            }
        };

        App::AppServices::instance().dialog_manager().show_file_dialog(
            FileDialogType::Open,
            Biz::_u8L("Choose an STL file to import bed shape from:"),
            AppServices::instance().app_config().get<std::string>("last_used_directory"),
            "",
            Wildcards::generate_wildcards(Wildcards::TypeFlag::Stl),
            callback
        );
    };

    create_preview();

    on_data_update();
}

void ConfigItemBedShape::on_data_update()
{
    const std::vector<Domain::Vec2d> data = m_state->get<std::vector<Domain::Vec2d>>();

    if (!m_bed_shape.is_equal_to(data)) {
        m_bed_shape = BedShape(data);
        m_shape_combo->set_current_index(static_cast<int>(m_bed_shape.get_type()));

        BedShape::Type type = m_bed_shape.get_type();
        m_ui_layout->set_current_index(static_cast<int>(type));

        if (type == BedShape::Type::Rectangle) {
            Domain::Vec2d size = m_bed_shape.get_size();
            set_value(m_size_x, size.x());
            set_value(m_size_y, size.y());

            Domain::Vec2d origin = m_bed_shape.get_origin();
            set_value(m_origin_x, origin.x());
            set_value(m_origin_y, origin.y());
        } else {
            // Reset size/origin controls to default values, if type is other then Rectangle
            const BedShape::ParamAttributes& size_attribs =
                BedShape::attributes(BedShape::Parameter::RectSize);
            set_value(m_size_x, size_attribs.def_value);
            set_value(m_size_y, size_attribs.def_value);
            const BedShape::ParamAttributes& origin_atribs =
                BedShape::attributes(BedShape::Parameter::RectOrigin);
            set_value(m_origin_x, origin_atribs.def_value);
            set_value(m_origin_y, origin_atribs.def_value);
        }

        if (type == BedShape::Type::Circle) {
            set_value(m_diameter, m_bed_shape.get_diameter());
        } else {
            // Reset diameter control to default value, if type is other then Circle
            const BedShape::ParamAttributes& diam_attribs =
                BedShape::attributes(BedShape::Parameter::Diameter);
            set_value(m_diameter, diam_attribs.def_value);
        }

        if (type != BedShape::Type::Custom) {
            // Reset last loaded custom contour , if type is other then Custom
            m_last_loaded_custom_contour.clear();
        }

        update_preview();
    }
}

void ConfigItemBedShape::send_data()
{
    if (m_bed_shape.contour().empty()) {
        // Do not propagate incomplete or empty shapes into config
        return;
    }

    set_item_value(Domain::ConfigValue{m_bed_shape.contour()});
}

// Loads an stl file, projects it to the XY plane and calculates a polygon.
void ConfigItemBedShape::load_stl(const boost::filesystem::path& file_path)
{
    std::string ext = file_path.extension().string();
    if (ext != ".stl") {
        App::AppServices::instance().dialog_manager().show_error_dialog(
            Biz::_u8L("Invalid file format.")
        );
        return;
    }

    auto loaded_mesh = Biz::load_stl(file_path.string());
    if (!loaded_mesh) {
        App::AppServices::instance().dialog_manager().show_error_dialog(
            Biz::_u8L("Invalid loaded data.")
        );
        return;
    }

    Domain::TriangleMesh& mesh = loaded_mesh.value();

    using namespace Biz::Algorithms;

    auto expolygons =
        ClipperUtils::union_ex(project_mesh(mesh.its, Domain::Transform3d::Identity(), []() {}));

    if (expolygons.size() == 0) {
        App::AppServices::instance().dialog_manager().show_error_dialog(
            Biz::_u8L("The selected file contains no geometry.")
        );
        return;
    }
    if (expolygons.size() > 1) {
        App::AppServices::instance().dialog_manager().show_error_dialog(
            Biz::_u8L("The selected file contains several disjoint areas. This is not supported.")
        );
        return;
    }

    auto polygon = expolygons[0].contour;
    m_last_loaded_custom_contour.clear();
    m_last_loaded_custom_contour.reserve(polygon.points.size());
    for (auto pt : polygon.points)
        m_last_loaded_custom_contour.push_back(Scaling::unscaled<double>(pt));

    update_shape();
}

void ConfigItemBedShape::update_shape()
{
    BedShape::Type type = static_cast<BedShape::Type>(m_shape_combo->current_index());

    using namespace Domain;

    // Rebuilds bed shape from UI controls and propagates it to config & preview.

    switch (type) {
    case BedShape::Type::Rectangle: {
        auto x = get_value(m_size_x);
        auto y = get_value(m_size_y);
        if (x == 0. || y == 0.)
            return;
        double x0 = 0.0;
        double y0 = 0.0;
        double x1 = x;
        double y1 = y;

        auto dx = get_value(m_origin_x);
        auto dy = get_value(m_origin_y);

        x0 -= dx;
        x1 -= dx;
        y0 -= dy;
        y1 -= dy;
        m_bed_shape = BedShape({Vec2d(x0, y0), Vec2d(x1, y0), Vec2d(x1, y1), Vec2d(x0, y1)});
        break;
    }

    case BedShape::Type::Circle: {
        double diameter = get_value(m_diameter);

        if (diameter == 0.0)
            return;
        auto r     = diameter / 2;
        auto twopi = 2 * std::numbers::pi;
        // Don't change this value without adjusting BuildVolume constructor detecting circle diameter!
        auto edges = 72;
        std::vector<Vec2d> points;
        for (int i = 1; i <= edges; ++i) {
            auto angle = i * twopi / edges;
            points.push_back(Vec2d(r * cos(angle), r * sin(angle)));
        }
        m_bed_shape = BedShape(points);
        break;
    }
    case BedShape::Type::Custom:
        m_bed_shape = BedShape(m_last_loaded_custom_contour);
        break;
    }

    send_data();
    update_preview();
}

void ConfigItemBedShape::create_preview()
{
    const ImColor shape_color{150, 150, 150};

    Rectangle* preview_bg = emplace_back<Rectangle>();
    preview_bg->set_fill(m_theme->color_imgui(Platform::Color::WindowBgAlternate));
    preview_bg->set_height(300.f);
    preview_bg->set_padding(15.f);
    preview_bg->set_justify_content(YGJustifyCenter);

    m_shape_preview = preview_bg->emplace_back<BedShapePreview>();
    m_shape_preview->set_shape_fill(shape_color);
    m_shape_preview->set_flex_grow(1.);
}

void ConfigItemBedShape::update_preview()
{
    m_shape_preview->set_visible(!m_bed_shape.contour().empty());

    BedShape::Type type = static_cast<BedShape::Type>(m_shape_combo->current_index());
    m_shape_preview->set_shape(
        m_bed_shape.contour(),
        (type == BedShape::Type::Custom) ? m_bed_shape.triangles() : std::vector<Domain::Vec2d>{},
        type == BedShape::Type::Rectangle ?
            Domain::Vec2d(get_value(m_origin_x), get_value(m_origin_y)) :
            Domain::Vec2d::Zero()
    );
}

} // namespace Slic3r::App
