#include "BedShapeDialog.hpp"
#include "GUI_App.hpp"
#include "OptionsGroup.hpp"

#include <wx/wx.h> 
#include <wx/numformatter.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/tooltip.h>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>

#include "nanosvg/nanosvg.h"

namespace Slic3r {
namespace GUI {

BedShape::BedShape(const ConfigOptionPoints& points)
{
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    if (points.size() < 3) {
        m_type = Bed3D::EShapeType::Invalid;
        return;
    }
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

    // is this a rectangle ?
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    Vec2d min;
    Vec2d max;
    if (Bed3D::is_rectangle(points.values, &min, &max)) {
        m_type       = Bed3D::EShapeType::Rectangle;
        m_rectSize   = max - min;
        m_rectOrigin = -min;
        return;
    }
#else
    Polygon polygon = Polygon::new_scale(points.values);
    if (points.size() == 4) {
        auto lines = polygon.lines();
        if (lines[0].parallel_to(lines[2]) && lines[1].parallel_to(lines[3])) {
            // okay, it's a rectangle
            // find origin
            coordf_t x_min, x_max, y_min, y_max;
            x_max = x_min = points.values[0](0);
            y_max = y_min = points.values[0](1);
            for (auto pt : points.values)
            {
                x_min = std::min(x_min, pt(0));
                x_max = std::max(x_max, pt(0));
                y_min = std::min(y_min, pt(1));
                y_max = std::max(y_max, pt(1));
            }

            m_type          = Type::Rectangular;
            m_rectSize      = Vec2d(x_max - x_min, y_max - y_min);
            m_rectOrigin    = Vec2d(-x_min, -y_min);

            return;
        }
    }
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

    // is this a circle ?
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    Vec2d center;
    double radius;
    if (Bed3D::is_circle(points.values, &center, &radius)) {
        m_type     = Bed3D::EShapeType::Circle;
        m_diameter = 2.0 * radius;
        return;
    }

    // This is a custom bed shape, use the polygon provided.
    m_type = Bed3D::EShapeType::Custom;
#else
    {
        // Analyze the array of points.Do they reside on a circle ?
        auto center = polygon.bounding_box().center();
        std::vector<double> vertex_distances;
        double avg_dist = 0;
        for (auto pt : polygon.points)
        {
            double distance = (pt - center).cast<double>().norm();
            vertex_distances.push_back(distance);
            avg_dist += distance;
        }

        avg_dist /= vertex_distances.size();
        bool defined_value = true;
        for (auto el : vertex_distances)
        {
            if (abs(el - avg_dist) > 10 * SCALED_EPSILON)
                defined_value = false;
            break;
        }
        if (defined_value) {
            // all vertices are equidistant to center
            m_type      = Type::Circular;
            m_diameter  = unscale<double>(avg_dist * 2);

            return;
        }
    }

    if (points.size() < 3)
        return;

    // This is a custom bed shape, use the polygon provided.
    m_type = Type::Custom;
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
}

static std::string get_option_label(BedShape::Parameter param)
{
    switch (param) {
    case BedShape::Parameter::RectSize  : return L("Size");
    case BedShape::Parameter::RectOrigin: return L("Origin");
    case BedShape::Parameter::Diameter  : return L("Diameter");
    default:                              return "";
    }
}

void BedShape::append_option_line(ConfigOptionsGroupShp optgroup, Parameter param)
{
    ConfigOptionDef def;

    if (param == Parameter::RectSize) {
        def.type = coPoints;
        def.set_default_value(new ConfigOptionPoints{ Vec2d(200, 200) });
        def.min = 0;
        def.max = 1200;
        def.label = get_option_label(param);
        def.tooltip = L("Size in X and Y of the rectangular plate.");

        Option option(def, "rect_size");
        optgroup->append_single_option_line(option);
    }
    else if (param == Parameter::RectOrigin) {
        def.type = coPoints;
        def.set_default_value(new ConfigOptionPoints{ Vec2d(0, 0) });
        def.min = -600;
        def.max = 600;
        def.label = get_option_label(param);
        def.tooltip = L("Distance of the 0,0 G-code coordinate from the front left corner of the rectangle.");
        
        Option option(def, "rect_origin");
        optgroup->append_single_option_line(option);
    }
    else if (param == Parameter::Diameter) {
        def.type = coFloat;
        def.set_default_value(new ConfigOptionFloat(200));
        def.sidetext = L("mm");
        def.label = get_option_label(param);
        def.tooltip = L("Diameter of the print bed. It is assumed that origin (0,0) is located in the center.");

        Option option(def, "diameter");
        optgroup->append_single_option_line(option);
    }
}

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
wxString BedShape::get_name(Bed3D::EShapeType type)
{
    switch (type) {
    case Bed3D::EShapeType::Rectangle: { return _L("Rectangular"); }
    case Bed3D::EShapeType::Circle:    { return _L("Circular"); }
    case Bed3D::EShapeType::Custom:    { return _L("Custom"); }
    case Bed3D::EShapeType::Invalid:
    default: return _L("Invalid");
    }
}
#else
wxString BedShape::get_name(Type type)
{
    switch (type) {
    case Type::Rectangular: return _L("Rectangular");
    case Type::Circular: return _L("Circular");
    case Type::Custom: return _L("Custom");
    case Type::Invalid:
    default: return _L("Invalid");
    }
}
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

size_t BedShape::get_type()
{
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    return static_cast<size_t>(m_type == Bed3D::EShapeType::Invalid ? Bed3D::EShapeType::Rectangle : m_type);
#else
    return static_cast<size_t>(m_type == Type::Invalid ? Type::Rectangular : m_type);
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
}

wxString BedShape::get_full_name_with_params()
{
    wxString out = _L("Shape") + ": " + get_name(m_type);

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    if (m_type == Bed3D::EShapeType::Rectangle) {
#else
    if (m_type == Type::Rectangular) {
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        out += "\n" + _(get_option_label(Parameter::RectSize))  + ": [" + ConfigOptionPoint(m_rectSize).serialize()     + "]";
        out += "\n" + _(get_option_label(Parameter::RectOrigin))+ ": [" + ConfigOptionPoint(m_rectOrigin).serialize()   + "]";
    }
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    else if (m_type == Bed3D::EShapeType::Circle)
#else
    else if (m_type == Type::Circular)
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        out += "\n" + _L(get_option_label(Parameter::Diameter)) + ": [" + double_to_string(m_diameter)                  + "]";

    return out;
}

void BedShape::apply_optgroup_values(ConfigOptionsGroupShp optgroup)
{
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    if (m_type == Bed3D::EShapeType::Rectangle || m_type == Bed3D::EShapeType::Invalid) {
#else
    if (m_type == Type::Rectangular || m_type == Type::Invalid) {
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        optgroup->set_value("rect_size"     , new ConfigOptionPoints{ m_rectSize    });
        optgroup->set_value("rect_origin"   , new ConfigOptionPoints{ m_rectOrigin  });
    }
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    else if (m_type == Bed3D::EShapeType::Circle)
#else
    else if (m_type == Type::Circular)
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        optgroup->set_value("diameter", double_to_string(m_diameter));
}

void BedShapeDialog::build_dialog(const ConfigOptionPoints& default_pt, const ConfigOptionString& custom_texture, const ConfigOptionString& custom_model, const ConfigOptionBoundingBoxes& avoid_boundingboxes, const ConfigOptionBool& enable_avoid_boundingboxes, const ConfigOptionString avoid_boundingboxes_color)
{
    SetFont(wxGetApp().normal_font());

    m_panel = new BedShapePanel(this);
    m_panel->build_panel(default_pt, custom_texture, custom_model, avoid_boundingboxes, enable_avoid_boundingboxes, avoid_boundingboxes_color);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel, 1, wxEXPAND);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

    wxGetApp().UpdateDlgDarkUI(this, true);

    SetSizer(main_sizer);
    SetMinSize(GetSize());
    main_sizer->SetSizeHints(this);

    this->Bind(wxEVT_CLOSE_WINDOW, ([this](wxCloseEvent& evt) {
        EndModal(wxID_CANCEL);
    }));
}

void BedShapeDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    const int& em = em_unit();
    m_panel->m_shape_options_book->SetMinSize(wxSize(25 * em, -1));

    for (auto og : m_panel->m_optgroups)
        og->msw_rescale();

    const wxSize& size = wxSize(50 * em, -1);

    SetMinSize(size);
    SetSize(size);

    Refresh();
}

const std::string BedShapePanel::NONE = "None";
const std::string BedShapePanel::EMPTY_STRING = "";

void BedShapePanel::build_panel(const ConfigOptionPoints& default_pt, const ConfigOptionString& custom_texture, const ConfigOptionString& custom_model, const ConfigOptionBoundingBoxes& avoid_boundingboxes, const ConfigOptionBool& enable_avoid_boundingboxes, const ConfigOptionString avoid_boundingboxes_color)
{
    wxGetApp().UpdateDarkUI(this);
    m_shape = default_pt.values;
    m_custom_texture = custom_texture.value.empty() ? NONE : custom_texture.value;
    m_custom_model = custom_model.value.empty() ? NONE : custom_model.value;
    m_avoid_boundingboxes = avoid_boundingboxes.values;
    m_enable_avoid_boundingboxes = enable_avoid_boundingboxes.value;
    m_avoid_boundingboxes_color = avoid_boundingboxes_color.value;
    
    auto sbsizer = new wxStaticBoxSizer(wxVERTICAL, this, _L("Shape"));
    sbsizer->GetStaticBox()->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(sbsizer->GetStaticBox());

    // shape options
    m_shape_options_book = new wxChoicebook(this, wxID_ANY, wxDefaultPosition, wxSize(25*wxGetApp().em_unit(), -1), wxCHB_TOP);
    wxGetApp().UpdateDarkUI(m_shape_options_book->GetChoiceCtrl());

    sbsizer->Add(m_shape_options_book);

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    auto optgroup = init_shape_options_page(BedShape::get_name(Bed3D::EShapeType::Rectangle));
#else
    auto optgroup = init_shape_options_page(BedShape::get_name(BedShape::Type::Rectangular));
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    BedShape::append_option_line(optgroup, BedShape::Parameter::RectSize);
    BedShape::append_option_line(optgroup, BedShape::Parameter::RectOrigin);
    activate_options_page(optgroup);

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    optgroup = init_shape_options_page(BedShape::get_name(Bed3D::EShapeType::Circle));
#else
    optgroup = init_shape_options_page(BedShape::get_name(BedShape::Type::Circular));
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    BedShape::append_option_line(optgroup, BedShape::Parameter::Diameter);
    activate_options_page(optgroup);

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    optgroup = init_shape_options_page(BedShape::get_name(Bed3D::EShapeType::Custom));
#else
    optgroup = init_shape_options_page(BedShape::get_name(BedShape::Type::Custom));
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

	Line line{ "", "" };
	line.full_width = 1;
	line.widget = [this](wxWindow* parent) {
        wxButton* shape_btn = new wxButton(parent, wxID_ANY, _L("Load shape from STL..."));
        wxSizer* shape_sizer = new wxBoxSizer(wxHORIZONTAL);
        shape_sizer->Add(shape_btn, 1, wxEXPAND);

        wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(shape_sizer, 1, wxEXPAND);

        shape_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
			load_stl();
		});

		return sizer;
	};
	optgroup->append_line(line);
    activate_options_page(optgroup);

    wxPanel* texture_panel = init_texture_panel();
    wxPanel* model_panel = init_model_panel();

    Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, ([this](wxCommandEvent& e) { update_shape(); }));

	// right pane with preview canvas
	m_canvas = new Bed_2D(this);
    m_canvas->Bind(wxEVT_PAINT, [this](wxPaintEvent& e) { m_canvas->repaint(m_shape); });
    m_canvas->Bind(wxEVT_SIZE, [this](wxSizeEvent& e) { m_canvas->Refresh(); });

    wxSizer* left_sizer = new wxBoxSizer(wxVERTICAL);
    left_sizer->Add(sbsizer, 0, wxEXPAND);
    left_sizer->Add(texture_panel, 1, wxEXPAND);
    left_sizer->Add(model_panel, 1, wxEXPAND);

    wxSizer* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->Add(left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 10);
    top_sizer->Add(m_canvas, 1, wxEXPAND | wxALL, 10);

	SetSizerAndFit(top_sizer);

	set_shape(default_pt);
	update_preview();
}

// Called from the constructor.
// Create a panel for a rectangular / circular / custom bed shape.
ConfigOptionsGroupShp BedShapePanel::init_shape_options_page(const wxString& title)
{
    wxPanel* panel = new wxPanel(m_shape_options_book);
    ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(panel, _L("Settings"));

    optgroup->label_width = 10;
    optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
        update_shape();
    };
	
    m_optgroups.push_back(optgroup);
//    panel->SetSizerAndFit(optgroup->sizer);
    m_shape_options_book->AddPage(panel, title);

    return optgroup;
}

void BedShapePanel::activate_options_page(ConfigOptionsGroupShp options_group)
{
    options_group->activate();
    options_group->parent()->SetSizerAndFit(options_group->sizer);
}

wxPanel* BedShapePanel::init_texture_panel()
{
    wxPanel* panel = new wxPanel(this);
    wxGetApp().UpdateDarkUI(panel, true);
    ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(panel, _L("Texture"));

    optgroup->label_width = 10;
    optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
        update_shape();
    };

    Line line{ "", "" };
    line.full_width = 1;
    line.widget = [this](wxWindow* parent) {
        wxButton* load_btn = new wxButton(parent, wxID_ANY, _L("Load..."));
        wxSizer* load_sizer = new wxBoxSizer(wxHORIZONTAL);
        load_sizer->Add(load_btn, 1, wxEXPAND);

        wxStaticText* filename_lbl = new wxStaticText(parent, wxID_ANY, _(NONE));

        wxSizer* filename_sizer = new wxBoxSizer(wxHORIZONTAL);
        filename_sizer->Add(filename_lbl, 1, wxEXPAND);

        wxButton* remove_btn = new wxButton(parent, wxID_ANY, _L("Remove"));
        wxSizer* remove_sizer = new wxBoxSizer(wxHORIZONTAL);
        remove_sizer->Add(remove_btn, 1, wxEXPAND);

        wxColourPickerCtrl* avoid_color = new wxColourPickerCtrl(parent, wxID_ANY, wxColour(m_avoid_boundingboxes_color), wxDefaultPosition, wxDefaultSize, wxCLRP_DEFAULT_STYLE, wxDefaultValidator, _L("Avoid bed stroke color"));
        avoid_color->Bind(wxEVT_COLOURPICKER_CHANGED, ([this](wxCommandEvent& e) {
            m_avoid_boundingboxes_color = dynamic_cast<wxColourPickerCtrl*>(e.GetEventObject())->GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
            update_shape();
        }));
        wxCheckBox* avoid_box = new wxCheckBox(parent, wxID_ANY, _L("Avoid bed regions"));
        avoid_box->SetValue(m_enable_avoid_boundingboxes);
        avoid_color->Enable(m_enable_avoid_boundingboxes);
        avoid_box->Bind(wxEVT_CHECKBOX, ([this, avoid_color](wxCommandEvent& e) {
            avoid_color->Enable(e.IsChecked());
            m_enable_avoid_boundingboxes = e.IsChecked();
            update_shape();
        }));
        wxSizer* avoid_sizer = new wxBoxSizer(wxHORIZONTAL);
        avoid_sizer->Add(avoid_box, 1, wxEXPAND);
        avoid_sizer->Add(avoid_color, 1, wxEXPAND);

        wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(filename_sizer, 1, wxEXPAND);
        sizer->Add(load_sizer, 1, wxEXPAND);
        sizer->Add(remove_sizer, 1, wxEXPAND | wxTOP, 2);
        sizer->Add(avoid_sizer, 1, wxEXPAND | wxTOP);

        load_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent& e) { load_texture(); }));
        remove_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent& e) {
                m_custom_texture = NONE;
                update_shape();
            }));

        filename_lbl->Bind(wxEVT_UPDATE_UI, ([this](wxUpdateUIEvent& e) {
                e.SetText(_(boost::filesystem::path(m_custom_texture).filename().string()));
                wxStaticText* lbl = dynamic_cast<wxStaticText*>(e.GetEventObject());
                if (lbl != nullptr) {
                    bool exists = (m_custom_texture == NONE) || boost::filesystem::exists(m_custom_texture);
                    lbl->SetForegroundColour(exists ? /*wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)*/wxGetApp().get_label_clr_default() : wxColor(*wxRED));

                    wxString tooltip_text = "";
                    if (m_custom_texture != NONE) {
                        if (!exists)
                            tooltip_text += _L("Not found:") + " ";

                        tooltip_text += _(m_custom_texture);
                    }

                    wxToolTip* tooltip = lbl->GetToolTip();
                    if ((tooltip == nullptr) || (tooltip->GetTip() != tooltip_text))
                        lbl->SetToolTip(tooltip_text);
                }
            }));

        remove_btn->Bind(wxEVT_UPDATE_UI, ([this](wxUpdateUIEvent& e) { e.Enable(m_custom_texture != NONE); }));

        return sizer;
    };
    optgroup->append_line(line);
    optgroup->activate();

    panel->SetSizerAndFit(optgroup->sizer);

    return panel;
}

wxPanel* BedShapePanel::init_model_panel()
{
    wxPanel* panel = new wxPanel(this);
    wxGetApp().UpdateDarkUI(panel, true);
    ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(panel, _L("Model"));

    optgroup->label_width = 10;
    optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
        update_shape();
    };

    Line line{ "", "" };
    line.full_width = 1;
    line.widget = [this](wxWindow* parent) {
        wxButton* load_btn = new wxButton(parent, wxID_ANY, _L("Load..."));
        wxSizer* load_sizer = new wxBoxSizer(wxHORIZONTAL);
        load_sizer->Add(load_btn, 1, wxEXPAND);

        wxStaticText* filename_lbl = new wxStaticText(parent, wxID_ANY, _(NONE));
        wxSizer* filename_sizer = new wxBoxSizer(wxHORIZONTAL);
        filename_sizer->Add(filename_lbl, 1, wxEXPAND);

        wxButton* remove_btn = new wxButton(parent, wxID_ANY, _L("Remove"));
        wxSizer* remove_sizer = new wxBoxSizer(wxHORIZONTAL);
        remove_sizer->Add(remove_btn, 1, wxEXPAND);

        wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(filename_sizer, 1, wxEXPAND);
        sizer->Add(load_sizer, 1, wxEXPAND);
        sizer->Add(remove_sizer, 1, wxEXPAND | wxTOP, 2);

        load_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent& e) { load_model(); }));

        remove_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent& e) {
                m_custom_model = NONE;
                update_shape();
            }));

        filename_lbl->Bind(wxEVT_UPDATE_UI, ([this](wxUpdateUIEvent& e) {
                e.SetText(_(boost::filesystem::path(m_custom_model).filename().string()));
                wxStaticText* lbl = dynamic_cast<wxStaticText*>(e.GetEventObject());
                if (lbl != nullptr) {
                    bool exists = (m_custom_model == NONE) || boost::filesystem::exists(m_custom_model);
                    lbl->SetForegroundColour(exists ? /*wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)*/wxGetApp().get_label_clr_default() : wxColor(*wxRED));

                    wxString tooltip_text = "";
                    if (m_custom_model != NONE) {
                        if (!exists)
                            tooltip_text += _L("Not found:") + " ";

                        tooltip_text += _(m_custom_model);
                    }

                    wxToolTip* tooltip = lbl->GetToolTip();
                    if ((tooltip == nullptr) || (tooltip->GetTip() != tooltip_text))
                        lbl->SetToolTip(tooltip_text);
                }
            }));

        remove_btn->Bind(wxEVT_UPDATE_UI, ([this](wxUpdateUIEvent& e) { e.Enable(m_custom_model != NONE); }));

        return sizer;
    };
    optgroup->append_line(line);
    optgroup->activate();

    panel->SetSizerAndFit(optgroup->sizer);

    return panel;
}

// Called from the constructor.
// Set the initial bed shape from a list of points.
// Deduce the bed shape type(rect, circle, custom)
// This routine shall be smart enough if the user messes up
// with the list of points in the ini file directly.
void BedShapePanel::set_shape(const ConfigOptionPoints& points)
{
    BedShape shape(points);

    m_shape_options_book->SetSelection(shape.get_type());
    shape.apply_optgroup_values(m_optgroups[shape.get_type()]);

    // Copy the polygon to the canvas, make a copy of the array, if custom shape is selected
    if (shape.is_custom())
        m_loaded_shape = points.values;

    update_shape();

    return;
}

void BedShapePanel::update_preview()
{
	if (m_canvas) m_canvas->Refresh();
	Refresh();
}

// Update the bed shape from the dialog fields.
void BedShapePanel::update_shape()
{
	auto page_idx = m_shape_options_book->GetSelection();
    auto opt_group = m_optgroups[page_idx];
    Vec2d rect_size(Vec2d::Zero()); // Only updated to non-zero in the case of rectangular bed.
    
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    Bed3D::EShapeType page_type = static_cast<Bed3D::EShapeType>(page_idx);
#else
    BedShape::Type page_type = static_cast<BedShape::Type>(page_idx);
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    if (page_type == Bed3D::EShapeType::Rectangle) {
#else
    if (page_type == BedShape::Type::Rectangular) {
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
		Vec2d rect_origin(Vec2d::Zero());

		try { rect_size = boost::any_cast<Vec2d>(opt_group->get_value("rect_size")); }
        catch (const std::exception& /* e */) { return; }

        try { rect_origin = boost::any_cast<Vec2d>(opt_group->get_value("rect_origin")); }
		catch (const std::exception & /* e */)  { return; }
 		
		auto x = rect_size(0);
		auto y = rect_size(1);
		// empty strings or '-' or other things
		if (x == 0 || y == 0)	return;
		double x0 = 0.0;
		double y0 = 0.0;
		double x1 = x;
		double y1 = y;

		auto dx = rect_origin(0);
		auto dy = rect_origin(1);

		x0 -= dx;
		x1 -= dx;
		y0 -= dy;
		y1 -= dy;
        m_shape = { Vec2d(x0, y0),
                    Vec2d(x1, y0),
                    Vec2d(x1, y1),
                    Vec2d(x0, y1) };
    }
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    else if (page_type == Bed3D::EShapeType::Circle) {
#else
    else if (page_type == BedShape::Type::Circular) {
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        double diameter;
		try { diameter = boost::any_cast<double>(opt_group->get_value("diameter")); }
		catch (const std::exception & /* e */) { return; } 

 		if (diameter == 0.0) return ;
		auto r = diameter / 2;
		auto twopi = 2 * PI;
        auto edges = 72;
        std::vector<Vec2d> points;
        for (int i = 1; i <= edges; ++i) {
            auto angle = i * twopi / edges;
			points.push_back(Vec2d(r*cos(angle), r*sin(angle)));
		}
        m_shape = points;
    }
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    else if (page_type == Bed3D::EShapeType::Custom)
#else
    else if (page_type == BedShape::Type::Custom)
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        m_shape = m_loaded_shape;

    update_preview();
    if (rect_size != Vec2d::Zero()) populate_avoid_boundingboxes(rect_size);
}

// Loads an stl file, projects it to the XY plane and calculates a polygon.
void BedShapePanel::load_stl()
{
    wxFileDialog dialog(this, _L("Choose an STL file to import bed shape from:"), "", "", file_wildcards(FT_STL), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    std::string file_name = dialog.GetPath().ToUTF8().data();
    if (!boost::algorithm::iends_with(file_name, ".stl")) {
        show_error(this, _L("Invalid file format."));
        return;
    }

    wxBusyCursor wait;

	Model model;
	try {
        model = Model::read_from_file(file_name);
	}
	catch (std::exception &) {
        show_error(this, _L("Error! Invalid model"));
        return;
    }

	auto mesh = model.mesh();
	auto expolygons = mesh.horizontal_projection();

	if (expolygons.size() == 0) {
		show_error(this, _L("The selected file contains no geometry."));
		return;
	}
	if (expolygons.size() > 1) {
		show_error(this, _L("The selected file contains several disjoint areas. This is not supported."));
		return;
	}

	auto polygon = expolygons[0].contour;
	std::vector<Vec2d> points;
	for (auto pt : polygon.points)
		points.push_back(unscale(pt));

    m_loaded_shape = points;
    update_shape();
}

void BedShapePanel::load_texture()
{
    wxFileDialog dialog(this, _L("Choose a file to import bed texture from (PNG/SVG):"), "", "",
        file_wildcards(FT_TEX), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK)
        return;

    m_custom_texture = NONE;

    std::string file_name = dialog.GetPath().ToUTF8().data();
    if (!boost::algorithm::iends_with(file_name, ".png") && !boost::algorithm::iends_with(file_name, ".svg")) {
        show_error(this, _L("Invalid file format."));
        return;
    }

    wxBusyCursor wait;

    m_custom_texture = file_name;
    update_shape();
}

void BedShapePanel::populate_avoid_boundingboxes(const Vec2d& bed_rect_size)
{
    m_avoid_boundingboxes.clear();
    if (!m_enable_avoid_boundingboxes) return;
    if (m_avoid_boundingboxes_color.empty()) return;
    uint32_t avoid_color = wxColour(m_avoid_boundingboxes_color).GetRGBA();

    if (!boost::algorithm::iends_with(m_custom_texture, ".svg")) {
        BOOST_LOG_TRIVIAL(info) << "Non-SVG texture, not populating avoid_boundingboxes: " << m_custom_texture;
        return;
    }

    NSVGimage *image = nsvgParseFromFile(m_custom_texture.c_str(), "px", 96.0f);
    if (image == nullptr) {
        BOOST_LOG_TRIVIAL(error)
                << "bed_custom_texture failed to parse SVG from: "
                << m_custom_texture;
        return;
    }
    const double width = bed_rect_size.x();
    const double height = bed_rect_size.y();
    for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
        if (shape->stroke.type != NSVG_PAINT_COLOR ||
            shape->stroke.color != avoid_color) {
            continue;
        }
        // SVG paths may come in many flavors (ellipses,
        // bezier curves, non-convex, etc) that are poor fits
        // for arrange()'s modeling of exclusion zones in
        // terms of closed polygons. Work around this by
        // excluding the entire axis-aligned bounding box of
        // each avoidance shape. Users can work around this
        // limitation by using smaller rects to cover any
        // non-rectangular zones they'd like to avoid.
        
        // NSVGshape.bounds orders as [minx,miny,maxx,maxy],
        // but Y axis is inverted in SVG relative to bed
        // coordinates, hence the height- and 3/1 inversions
        // below.
        float minx = scaled(width*shape->bounds[0]/image->width);
        float maxx = scaled(width*shape->bounds[2]/image->width);
        float miny = scaled(height-(height*shape->bounds[3]/image->height));
        float maxy = scaled(height-(height*shape->bounds[1]/image->height));
        m_avoid_boundingboxes.push_back(BoundingBox({minx, miny}, {maxx, maxy}));
    }
    nsvgDelete(image);
}

void BedShapePanel::load_model()
{
    wxFileDialog dialog(this, _L("Choose an STL file to import bed model from:"), "", "",
        file_wildcards(FT_STL), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() != wxID_OK)
        return;

    m_custom_model = NONE;

    std::string file_name = dialog.GetPath().ToUTF8().data();
    if (!boost::algorithm::iends_with(file_name, ".stl")) {
        show_error(this, _L("Invalid file format."));
        return;
    }

    wxBusyCursor wait;

    m_custom_model = file_name;
    update_shape();
}

} // GUI
} // Slic3r
