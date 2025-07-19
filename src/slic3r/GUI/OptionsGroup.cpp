///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
///|/
///|/ ported from lib/Slic3r/GUI/OptionsGroup.pm:
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2013 Scott Penrose
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2011 Richard Goodwin
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "OptionsGroup.hpp"
#include "Search.hpp"
#include "GUI_App.hpp"
#include "OG_CustomCtrl.hpp"
#include "format.hpp"

#include <utility>
#include <wx/bookctrl.h> // IWYU pragma: keep
#include <wx/numformatter.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include "slic3r/GUI/Search.hpp" // IWYU pragma: keep
#include "slic3r/GUI/Field.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

const t_field& OptionsGroup::build_field(const Option& opt) {
    return build_field(opt.opt_id, opt.opt);
}
const t_field& OptionsGroup::build_field(const t_config_option_key& id) {
	const ConfigOptionDef& opt = m_options.at(id).opt;
    return build_field(id, opt);
}

const t_field& OptionsGroup::build_field(const t_config_option_key& id, const ConfigOptionDef& opt) {
    // Check the gui_type field first, fall through
    // is the normal type.
    switch (opt.gui_type) {
    case ConfigOptionDef::GUIType::select_close:
    case ConfigOptionDef::GUIType::select_open:
    case ConfigOptionDef::GUIType::f_enum_open:
    case ConfigOptionDef::GUIType::i_enum_open:
        m_fields.emplace(id, Choice::Create<Choice>(this->ctrl_parent(), opt, id));
        break;
    case ConfigOptionDef::GUIType::color:
        m_fields.emplace(id, ColourPicker::Create<ColourPicker>(this->ctrl_parent(), opt, id));
        break;
    case ConfigOptionDef::GUIType::slider:
        m_fields.emplace(id, SliderCtrl::Create<SliderCtrl>(this->ctrl_parent(), opt, id));
        break;
    case ConfigOptionDef::GUIType::legend: // StaticText
        m_fields.emplace(id, StaticText::Create<StaticText>(this->ctrl_parent(), opt, id));
        break;
    case ConfigOptionDef::GUIType::one_string:
        m_fields.emplace(id, TextCtrl::Create<TextCtrl>(this->ctrl_parent(), opt, id));
        break;
    default:
        switch (opt.type) {
            case coFloatOrPercent:
            case coFloatsOrPercents:
            case coFloat:
            case coFloats:
			case coPercent:
			case coPercents:
			case coString:
			case coStrings:
                m_fields.emplace(id, TextCtrl::Create<TextCtrl>(this->ctrl_parent(), opt, id));
                break;
			case coBool:
			case coBools:
                m_fields.emplace(id, CheckBox::Create<CheckBox>(this->ctrl_parent(), opt, id));
				break;
			case coInt:
			case coInts:
                m_fields.emplace(id, SpinCtrl::Create<SpinCtrl>(this->ctrl_parent(), opt, id));
				break;
            case coEnum:
            case coEnums:
               m_fields.emplace(id, Choice::Create<Choice>(this->ctrl_parent(), opt, id));
				break;
            case coPoints:
                m_fields.emplace(id, PointCtrl::Create<PointCtrl>(this->ctrl_parent(), opt, id));
				break;
            case coNone:   break;
            default:
				throw Slic3r::LogicError("This control doesn't exist till now"); break;
        }
    }
    // Grab a reference to fields for convenience
    const t_field& field = m_fields[id];
	field->m_on_change = [this](const std::string& opt_id, const boost::any& value) {
			//! This function will be called from Field.
			//! Call OptionGroup._on_change(...)
			if (!m_disabled)
				this->on_change_OG(opt_id, value);
	};
    field->m_on_kill_focus = [this](const std::string& opt_id) {
			//! This function will be called from Field.
			if (!m_disabled)
				this->on_kill_focus(opt_id);
	};
    field->m_parent = parent();

    if (edit_custom_gcode && opt.is_code) {
        field->m_fn_edit_value = [this](std::string opt_id) {
            if (!m_disabled)
                this->edit_custom_gcode(opt_id);
        };
        field->set_edit_tooltip(_L("Edit Custom G-code"));
    }

	field->m_back_to_initial_value = [this](std::string opt_id) {
		if (!m_disabled)
			this->back_to_initial_value(opt_id);
	};
	field->m_back_to_sys_value = [this](std::string opt_id) {
		if (!m_disabled)
			this->back_to_sys_value(opt_id);
	};

	// assign function objects for callbacks, etc.
    return field;
}

OptionsGroup::OptionsGroup(	wxWindow* _parent, const wxString& title,
                            bool is_tab_opt /* = false */,
                            column_t extra_clmn /* = nullptr */) :
                m_parent(_parent), title(title),
                m_use_custom_ctrl(is_tab_opt),
                staticbox(title!=""), extra_column(extra_clmn)
{
}

// opt_index = 0, by the reason of zero-index in ConfigOptionVector by default (in case only one element)
void OptionsGroup::change_opt_value(DynamicPrintConfig& config, const t_config_option_key& opt_key, const boost::any& value, int opt_index /*= 0*/)
{
    try {

        if (config.def()->get(opt_key)->type == coBools && config.def()->get(opt_key)->nullable) {
            ConfigOptionBoolsNullable* vec_new = new ConfigOptionBoolsNullable{ boost::any_cast<unsigned char>(value) };
            config.option<ConfigOptionBoolsNullable>(opt_key)->set_at(vec_new, opt_index, 0);
            return;
        }

        const ConfigOptionDef* opt_def = config.def()->get(opt_key);
        switch (opt_def->type) {
        case coFloatOrPercent: {
            std::string str = boost::any_cast<std::string>(value);
            bool percent = false;
            if (str.back() == '%') {
                str.pop_back();
                percent = true;
            }
            double val = std::stod(str); // locale-dependent (on purpose - the input is the actual content of the field)
            config.set_key_value(opt_key, new ConfigOptionFloatOrPercent(val, percent));
            break; }
        case coPercent:
            config.set_key_value(opt_key, new ConfigOptionPercent(boost::any_cast<double>(value)));
            break;
        case coFloat: {
            double& val = config.opt_float(opt_key);
            val = boost::any_cast<double>(value);
            break;
        }
        case coFloatsOrPercents: {
            std::string str = boost::any_cast<std::string>(value);
            bool percent = false;
            if (str.back() == '%') {
                str.pop_back();
                percent = true;
            }

            const bool                    is_na_value = opt_def->nullable && str == _(L("N/A"));
            const FloatOrPercent          val         = is_na_value ? ConfigOptionFloatsOrPercentsNullable::nil_value() : FloatOrPercent{std::stod(str), percent};
            ConfigOptionFloatsOrPercents *vec_new     = new ConfigOptionFloatsOrPercents({val});
            config.option<ConfigOptionFloatsOrPercents>(opt_key)->set_at(vec_new, opt_index, opt_index);
            break;
        }
        case coPercents: {
            ConfigOptionPercents* vec_new = new ConfigOptionPercents{ boost::any_cast<double>(value) };
            config.option<ConfigOptionPercents>(opt_key)->set_at(vec_new, opt_index, opt_index);
            break;
        }
        case coFloats: {
            ConfigOptionFloats* vec_new = new ConfigOptionFloats{ boost::any_cast<double>(value) };
            config.option<ConfigOptionFloats>(opt_key)->set_at(vec_new, opt_index, opt_index);
            break;
        }
        case coString:
            config.set_key_value(opt_key, new ConfigOptionString(boost::any_cast<std::string>(value)));
            break;
        case coStrings: {
            if (opt_key == "compatible_prints" || opt_key == "compatible_printers" || opt_key == "gcode_substitutions") {
                config.option<ConfigOptionStrings>(opt_key)->values =
                    boost::any_cast<std::vector<std::string>>(value);
            }
            else if (config.def()->get(opt_key)->gui_flags.compare("serialized") == 0) {
                std::string str = boost::any_cast<std::string>(value);
                std::vector<std::string> values{};
                if (!str.empty()) {
                    if (str.back() == ';') str.pop_back();
                    // Split a string to multiple strings by a semi - colon.This is the old way of storing multi - string values.
                    // Currently used for the post_process config value only.
                    boost::split(values, str, boost::is_any_of(";"));
                    if (values.size() == 1 && values[0] == "")
                        values.resize(0);
                }
                config.option<ConfigOptionStrings>(opt_key)->values = values;
            }
            else {
                ConfigOptionStrings* vec_new = new ConfigOptionStrings{ boost::any_cast<std::string>(value) };
                config.option<ConfigOptionStrings>(opt_key)->set_at(vec_new, opt_index, 0);
            }
        }
                      break;
        case coBool:
            config.set_key_value(opt_key, new ConfigOptionBool(boost::any_cast<bool>(value)));
            break;
        case coBools: {
            ConfigOptionBools* vec_new = new ConfigOptionBools{ boost::any_cast<unsigned char>(value) != 0 };
            config.option<ConfigOptionBools>(opt_key)->set_at(vec_new, opt_index, 0);
            break; }
        case coInt: {
			//config.set_key_value(opt_key, new ConfigOptionInt(boost::any_cast<int>(value)));
			int& val_new = config.opt_int(opt_key);
			val_new = boost::any_cast<int>(value);
            break;
		}
        case coInts: {
            ConfigOptionInts* vec_new = new ConfigOptionInts{ boost::any_cast<int>(value) };
            config.option<ConfigOptionInts>(opt_key)->set_at(vec_new, opt_index, 0);
        }
                   break;
        case coEnum: {
            auto* opt = opt_def->default_value.get()->clone();
            opt->setInt(boost::any_cast<int>(value));
            config.set_key_value(opt_key, opt);
        }
                   break;
        case coEnums: {
            ConfigOptionEnumsGeneric* vec_new = new ConfigOptionEnumsGeneric(1, boost::any_cast<int>(value));;
            config.option<ConfigOptionEnumsGeneric>(opt_key)->set_at(vec_new, opt_index, 0);
            break; }
        case coPoints: {
            if (opt_key == "bed_shape") {
                config.option<ConfigOptionPoints>(opt_key)->values = boost::any_cast<std::vector<Vec2d>>(value);
                break;
            }
            ConfigOptionPoints* vec_new = new ConfigOptionPoints{ boost::any_cast<Vec2d>(value) };
            config.option<ConfigOptionPoints>(opt_key)->set_at(vec_new, opt_index, 0);
        }
                     break;
        case coNone:
            break;
        default:
            break;
        }
    }
    catch (const std::exception& e)
    {
        wxLogError(format_wxstr("Internal error when changing value for %1%: %2%", opt_key, e.what()));
    }
}

Option::Option(const ConfigOptionDef& _opt, t_config_option_key id) : opt(_opt), opt_id(id)
{
    if (!opt.tooltip.empty()) {
        wxString tooltip;
        if (opt.opt_key.rfind("branching", 0) == 0)
            tooltip = _L("Unavailable for this method.") + "\n";
        tooltip += _(opt.tooltip);

        // edit tooltip : change Slic3r to SLIC3R_APP_KEY
        // Temporary workaround for localization
        tooltip.Replace("Slic3r", SLIC3R_APP_KEY, true);

        opt.tooltip = into_u8(tooltip);
    }
}

void Line::clear()
{
    if (near_label_widget_win)
        near_label_widget_win = nullptr;

    if (widget_sizer) {
        widget_sizer->Clear(true);
        delete widget_sizer;
        widget_sizer = nullptr;
    }

    if (extra_widget_sizer) {
        extra_widget_sizer->Clear(true);
        delete extra_widget_sizer;
        extra_widget_sizer = nullptr;
    }
}

wxWindow* OptionsGroup::ctrl_parent() const
{
	return this->custom_ctrl && m_use_custom_ctrl_as_parent ? static_cast<wxWindow*>(this->custom_ctrl) : (this->stb ? static_cast<wxWindow*>(this->stb) : this->parent());
}

bool OptionsGroup::is_legend_line()
{
	if (m_lines.size() == 1) {
		const std::vector<Option>& option_set = m_lines.front().get_options();
		return !option_set.empty() && option_set.front().opt.gui_type == ConfigOptionDef::GUIType::legend;
	}
	return false;
}

void OptionsGroup::set_max_win_width(int max_win_width)
{
    if (custom_ctrl)
        custom_ctrl->set_max_win_width(max_win_width);
}

void OptionsGroup::show_field(const t_config_option_key& opt_key, bool show/* = true*/)
{
    Field* field = get_field(opt_key);
    if (!field) return;
    wxWindow* win = field->getWindow();
    if (!win) return;
    wxSizerItem* win_item = m_grid_sizer->GetItem(win, true);
    if (!win_item) return;

    const size_t cols = (size_t)m_grid_sizer->GetCols();
    const size_t rows = (size_t)m_grid_sizer->GetEffectiveRowsCount();

    auto show_row = [this, show, cols, win_item](wxSizerItem* item, size_t row_shift) {
        // check if item contanes required win
        if (!item->IsWindow() || item != win_item)
            return false;
        // show/hide hole line contanes this window
        for (size_t i = 0; i < cols; ++i)
            m_grid_sizer->Show(row_shift + i, show);
        return true;
    };

    size_t row_shift = 0;
    for (size_t j = 0; j < rows; ++j) {
        for (size_t i = 0; i < cols; ++i) {
            wxSizerItem* item = m_grid_sizer->GetItem(row_shift + i);
            if (!item)
                continue;
            if (item->IsSizer()) {
                for (wxSizerItem* child_item : item->GetSizer()->GetChildren())
                    if (show_row(child_item, row_shift))
                        return;
            }
            else if (show_row(item, row_shift))
                return;
        }
        row_shift += cols;
    }
}

void OptionsGroup::append_line(const Line& line)
{
	m_lines.emplace_back(line);

	if (line.full_width && (
		line.widget != nullptr ||
		!line.get_extra_widgets().empty())
		)
		return;

	auto option_set = line.get_options();
	for (auto opt : option_set)
		m_options.emplace(opt.opt_id, opt);

	// add mode value for current line to m_options_mode
    if (!option_set.empty())
        m_options_mode.push_back(option_set[0].opt.mode);
}

void OptionsGroup::append_separator()
{
    m_lines.emplace_back(Line());
}

void OptionsGroup::activate_line(Line& line)
{
    if (line.is_separator())
        return;

    m_use_custom_ctrl_as_parent = false;

	if (line.full_width && (
		line.widget != nullptr ||
		!line.get_extra_widgets().empty())
		) {
        if (line.widget != nullptr) {
			// description lines
            sizer->Add(line.widget(this->ctrl_parent()), 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
            return;
        }
		if (!line.get_extra_widgets().empty()) {
			const auto h_sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(h_sizer, 1, wxEXPAND | wxALL, wxOSX ? 0 : 15);

            bool is_first_item = true;
			for (auto extra_widget : line.get_extra_widgets()) {
				h_sizer->Add(extra_widget(this->ctrl_parent()), is_first_item ? 1 : 0, wxLEFT, 15);
				is_first_item = false;
			}
			return;
		}
    }

    const std::vector<Option>& option_set = line.get_options();
	bool is_legend_line = option_set.front().opt.gui_type == ConfigOptionDef::GUIType::legend;

    if (!custom_ctrl && m_use_custom_ctrl) {
        custom_ctrl = new OG_CustomCtrl(is_legend_line || !staticbox ? this->parent() : static_cast<wxWindow*>(this->stb), this);
        wxGetApp().UpdateDarkUI(custom_ctrl);
		if (is_legend_line)
			sizer->Add(custom_ctrl, 0, wxEXPAND | wxLEFT, wxOSX ? 0 : 10);
		else
            sizer->Add(custom_ctrl, 0, wxEXPAND | wxALL, wxOSX || !staticbox ? 0 : 5);
    }

	// Set sidetext width for a better alignment of options in line
	// "m_show_modified_btns==true" means that options groups are in tabs
	if (option_set.size() > 1 && m_use_custom_ctrl) {
		sidetext_width = Field::def_width_thinner();
	}

	// if we have a single option with no label, no sidetext just add it directly to sizer
    if (option_set.size() == 1 && label_width == 0 && option_set.front().opt.full_width &&
		option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr &&
		line.get_extra_widgets().size() == 0) {

		const auto& option = option_set.front();
		const auto& field = build_field(option);

		if (is_window_field(field))
			sizer->Add(field->getWindow(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		if (is_sizer_field(field))
			sizer->Add(field->getSizer(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		return;
	}

    if (custom_ctrl)
        m_use_custom_ctrl_as_parent = true;

    // if we have an extra column, build it
    if (extra_column) {
        m_extra_column_item_ptrs.push_back(extra_column(this->ctrl_parent(), line));
        m_grid_sizer->Add(m_extra_column_item_ptrs.back(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	}

	// Build a label if we have it
	wxStaticText* label=nullptr;
    if (label_width != 0) {
        if (custom_ctrl) {
            if (line.near_label_widget)
                line.near_label_widget_win = line.near_label_widget(this->ctrl_parent());
        }
        else {
            if (!line.near_label_widget || !line.label.IsEmpty()) {
                // Only create the label if it is going to be displayed.
                long label_style = staticbox ? 0 : wxALIGN_RIGHT;
#ifdef __WXGTK__
                // workaround for correct text align of the StaticBox on Linux
                // flags wxALIGN_RIGHT and wxALIGN_CENTRE don't work when Ellipsize flags are _not_ given.
                // Text is properly aligned only when Ellipsize is checked.
                label_style |= staticbox ? 0 : wxST_ELLIPSIZE_END;
#endif /* __WXGTK__ */
                label = new wxStaticText(this->ctrl_parent(), wxID_ANY, line.label + (line.label.IsEmpty() ? "" : ": "),
                    wxDefaultPosition, wxSize(label_width * wxGetApp().em_unit(), -1), label_style);
                label->SetBackgroundStyle(wxBG_STYLE_PAINT);
                label->SetFont(wxGetApp().normal_font());
                label->Wrap(label_width * wxGetApp().em_unit()); // avoid a Linux/GTK bug
            }
            if (!line.near_label_widget)
                m_grid_sizer->Add(label, 0, (staticbox ? 0 : wxALIGN_RIGHT | wxRIGHT) | wxALIGN_CENTER_VERTICAL, line.label.IsEmpty() ? 0 : 5);
            else if (!line.label.IsEmpty()) {
                // If we're here, we have some widget near the label
                // so we need a horizontal sizer to arrange these things
                auto sizer = new wxBoxSizer(wxHORIZONTAL);
                m_grid_sizer->Add(sizer, 0, wxEXPAND | (staticbox ? wxALL : wxBOTTOM | wxTOP | wxLEFT), staticbox ? 0 : 1);
                sizer->Add(label, 0, (staticbox ? 0 : wxALIGN_RIGHT | wxRIGHT) | wxALIGN_CENTER_VERTICAL, 5);
            }
            if (label != nullptr && line.label_tooltip != "")
                label->SetToolTip(line.label_tooltip);
        }
    }

	// If there's a widget, build it and add the result to the sizer.
	if (line.widget != nullptr) {
		auto wgt = line.widget(this->ctrl_parent());
        if (custom_ctrl)
            line.widget_sizer = wgt;
        else
            m_grid_sizer->Add(wgt, 0, wxEXPAND | wxBOTTOM | wxTOP, (wxOSX || line.label.IsEmpty()) ? 0 : 5);
		return;
	}

	// If we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things
    wxBoxSizer* h_sizer{ nullptr };
    if (!custom_ctrl) {
        // but this sizer is currently used just for NON-custom_ctrl cases
        h_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_grid_sizer->Add(h_sizer, 0, wxEXPAND | (staticbox ? wxALL : wxBOTTOM | wxTOP | wxLEFT), staticbox ? 0 : 1);
    }

    // If we have a single option with no sidetext just add it directly to the grid sizer
    if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
		option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0) {
		const auto& option = option_set.front();
		const auto& field = build_field(option);

        if (!custom_ctrl) {
            if (is_window_field(field))
                h_sizer->Add(field->getWindow(), option.opt.full_width ? 1 : 0,
                    wxBOTTOM | wxTOP | (option.opt.full_width ? int(wxEXPAND) : int(wxALIGN_CENTER_VERTICAL)), (wxOSX || !staticbox) ? 0 : 2);
            if (is_sizer_field(field))
                h_sizer->Add(field->getSizer(), 1, (option.opt.full_width ? int(wxEXPAND) : int(wxALIGN_CENTER_VERTICAL)), 0);
        }
        return;
	}

    for (const Option& opt : option_set) {
        // add field
        auto& field = build_field(opt);

        if (!custom_ctrl) {
            ConfigOptionDef option = opt.opt;
            // add label if any
            if ((option_set.size() > 1 || line.label.IsEmpty()) && !option.label.empty()) {
                // those two parameter names require localization with context
                wxString str_label = (option.label == "Top" || option.label == "Bottom") ?
                    _CTX(option.label, "Layers") :
                    _(option.label);
                label = new wxStaticText(this->ctrl_parent(), wxID_ANY, str_label + ": ", wxDefaultPosition, //wxDefaultSize);
                    wxSize(sublabel_width != -1 ? sublabel_width * wxGetApp().em_unit() : -1, -1), wxALIGN_RIGHT);
                label->SetBackgroundStyle(wxBG_STYLE_PAINT);
                label->SetFont(wxGetApp().normal_font());
                h_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
            }

            if (option_set.size() == 1 && option_set.front().opt.full_width)
            {
                const auto v_sizer = new wxBoxSizer(wxVERTICAL);
                h_sizer->Add(v_sizer, 1, wxEXPAND);
                is_sizer_field(field) ?
                    v_sizer->Add(field->getSizer(), 0, wxEXPAND) :
                    v_sizer->Add(field->getWindow(), 0, wxEXPAND);
                break;
            }

            is_sizer_field(field) ?
                h_sizer->Add(field->getSizer(), 0, wxALIGN_CENTER_VERTICAL, 0) :
                h_sizer->Add(field->getWindow(), 0, wxALIGN_CENTER_VERTICAL, 0);

            // add sidetext if any
            if (!option.sidetext.empty() || sidetext_width > 0) {
                auto sidetext = new wxStaticText(this->ctrl_parent(), wxID_ANY, _(option.sidetext), wxDefaultPosition,
                    wxSize(sidetext_width != -1 ? sidetext_width * wxGetApp().em_unit() : -1, -1), wxALIGN_LEFT);
                sidetext->SetBackgroundStyle(wxBG_STYLE_PAINT);
                sidetext->SetFont(wxGetApp().normal_font());
                h_sizer->Add(sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);
            }

            // add side widget if any
            if (opt.side_widget != nullptr) {
                h_sizer->Add(opt.side_widget(this->ctrl_parent())/*!.target<wxWindow>()*/, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1);    //! requires verification
            }

            if (opt.opt_id != option_set.back().opt_id) //! istead of (opt != option_set.back())
                h_sizer->AddSpacer(6);
        }
    }

    // add extra sizers if any
    for (auto extra_widget : line.get_extra_widgets())
    {
        if (line.get_extra_widgets().size() == 1 && !staticbox)
        {
            // extra widget for non-staticbox option group (like for the frequently used parameters on the sidebar) should be wxALIGN_RIGHT
            const auto v_sizer = new wxBoxSizer(wxVERTICAL);
            h_sizer->Add(v_sizer, option_set.size() == 1 ? 0 : 1, wxEXPAND);
            v_sizer->Add(extra_widget(this->ctrl_parent()), 0, wxALIGN_RIGHT);
            return;
        }

        line.extra_widget_sizer = extra_widget(this->ctrl_parent());
        if (!custom_ctrl)
            h_sizer->Add(line.extra_widget_sizer, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);        //! requires verification
    }
}

// create all controls for the option group from the m_lines
bool OptionsGroup::activate(std::function<void()> throw_if_canceled/* = [](){}*/, int horiz_alignment/* = wxALIGN_LEFT*/)
{
	if (sizer)//(!sizer->IsEmpty())
		return false;

	try {
		if (staticbox) {
			stb = new wxStaticBox(m_parent, wxID_ANY, _(title));
			if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
			stb->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
			wxGetApp().UpdateDarkUI(stb);
		}
		else
			stb = nullptr;
		sizer = (staticbox ? new wxStaticBoxSizer(stb, wxVERTICAL) : new wxBoxSizer(wxVERTICAL));

		auto num_columns = 1U;
		size_t grow_col = 1;

		if (label_width == 0)
			grow_col = 0;
		else
			num_columns++;

		if (extra_column) {
			num_columns++;
			grow_col++;
		}

		m_grid_sizer = new wxFlexGridSizer(0, num_columns, 1, 0);
		static_cast<wxFlexGridSizer*>(m_grid_sizer)->SetFlexibleDirection(wxBOTH);
		static_cast<wxFlexGridSizer*>(m_grid_sizer)->AddGrowableCol(grow_col);

		sizer->Add(m_grid_sizer, 0, wxEXPAND | wxALL, wxOSX || !staticbox ? 0 : 5);

		// activate lines
		for (Line& line: m_lines) {
			throw_if_canceled();
			activate_line(line);
		}

        ctrl_horiz_alignment = horiz_alignment;
        if (custom_ctrl)
            custom_ctrl->init_max_win_width();
	} catch (UIBuildCanceled&) {
		auto p = sizer;
		this->clear();
		p->Clear(true);
		delete p;
		throw;
	}

	return true;
}
// delete all controls from the option group
void OptionsGroup::clear(bool destroy_custom_ctrl)
{
	if (!sizer)
		return;

	m_grid_sizer = nullptr;
	sizer = nullptr;

    for (Line& line : m_lines)
        line.clear();

    if (custom_ctrl) {
        for (auto const &item : m_fields) {
            wxWindow* win = item.second.get()->getWindow();
            if (win)
                win = nullptr;
        }
        if (destroy_custom_ctrl)
            custom_ctrl->Destroy();
        else
            custom_ctrl = nullptr;
    }

	m_extra_column_item_ptrs.clear();
	m_fields.clear();
}

Line OptionsGroup::create_single_option_line(const Option& option, const std::string& path/* = std::string()*/) const
{
    Line retval{ _(option.opt.label), from_u8(option.opt.tooltip) };

	retval.label_path = path;
    retval.append_option(option);
    return retval;
}

void OptionsGroup::clear_fields_except_of(const std::vector<std::string> left_fields)
{
    auto it = m_fields.begin();
    while (it != m_fields.end()) {
        if (std::find(left_fields.begin(), left_fields.end(), it->first) == left_fields.end())
            it = m_fields.erase(it);
        else
            it++;
    }
}

void OptionsGroup::on_change_OG(const t_config_option_key& opt_id, const boost::any& value) {
	if (on_change != nullptr)
		on_change(opt_id, value);
}

Option ConfigOptionsGroup::get_option(const std::string& opt_key, int opt_index /*= -1*/)
{
	if (!m_config->has(opt_key)) {
		std::cerr << "No " << opt_key << " in ConfigOptionsGroup config.\n";
	}

	std::string opt_id = opt_index == -1 ? opt_key : opt_key + "#" + std::to_string(opt_index);
	std::pair<std::string, int> pair(opt_key, opt_index);
	m_opt_map.emplace(opt_id, pair);

    if (m_use_custom_ctrl) {
        // fill group and category values just for options from Settings Tab
        wxGetApp().searcher().add_key(opt_id, static_cast<Preset::Type>(this->config_type()), title, this->config_category());
    }

	return Option(*m_config->def()->get(opt_key), opt_id);
}

void ConfigOptionsGroup::on_change_OG(const t_config_option_key& opt_id, const boost::any& value)
{
	if (!m_opt_map.empty())
	{
		auto it = m_opt_map.find(opt_id);
		if (it == m_opt_map.end())
		{
			OptionsGroup::on_change_OG(opt_id, value);
			return;
		}

		auto 				itOption  = it->second;
		const std::string  &opt_key   = itOption.first;
		int 			    opt_index = itOption.second;

		this->change_opt_value(opt_key, value, opt_index == -1 ? 0 : opt_index);
	}

	OptionsGroup::on_change_OG(opt_id, value);
}

void ConfigOptionsGroup::back_to_initial_value(const std::string& opt_key)
{
	if (get_initial_config == nullptr)
		return;
	back_to_config_value(get_initial_config(), opt_key);
}

void ConfigOptionsGroup::back_to_sys_value(const std::string& opt_key)
{
	if (get_sys_config == nullptr)
		return;
	if (!have_sys_config())
		return;
	back_to_config_value(get_sys_config(), opt_key);
}

void ConfigOptionsGroup::back_to_config_value(const DynamicPrintConfig& config, const std::string& opt_key)
{
	boost::any value;
	if (opt_key == "bed_shape") {
        for (const std::string& key : {"bed_custom_texture", "bed_custom_model"}) {
            value = config.opt_string(key);
            this->change_opt_value(key, value);
        }
	}
	if (opt_key == "extruders_count") {
		auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
		value = int(nozzle_diameter->values.size());
	}
    else if (m_opt_map.find(opt_key) == m_opt_map.end() ||
		    // This option doesn't have corresponded field
             is_option_without_field(opt_key) ) {
        value = get_config_value(config, opt_key);
        this->change_opt_value(opt_key, value);
        OptionsGroup::on_change_OG(opt_key, value);
        return;
    }
	else
	{
		auto opt_id = m_opt_map.find(opt_key)->first;
		std::string opt_short_key = m_opt_map.at(opt_id).first;
		int opt_index = m_opt_map.at(opt_id).second;
		value = get_config_value(config, opt_short_key, opt_index);
	}

	set_value(opt_key, value);
	on_change_OG(opt_key, get_value(opt_key));
}

void ConfigOptionsGroup::on_kill_focus(const std::string& opt_key)
{
    if (fill_empty_value)
        fill_empty_value(opt_key);
    else
	    reload_config();
}

void ConfigOptionsGroup::reload_config()
{
	for (auto &kvp : m_opt_map) {
		// Name of the option field (name of the configuration key, possibly suffixed with '#' and the index of a scalar inside a vector.
		const std::string &opt_id    = kvp.first;
		// option key (may be scalar or vector)
		const std::string &opt_key   = kvp.second.first;
		// index in the vector option, zero for scalars
		int 			   opt_index = kvp.second.second;
		const ConfigOptionDef &option = m_options.at(opt_id).opt;
		this->set_value(opt_id, config_value(opt_key, opt_index, option.gui_flags == "serialized"));
	}
}

void ConfigOptionsGroup::Hide()
{
    Show(false);
}

void ConfigOptionsGroup::Show(const bool show)
{
    if (sizer) sizer->ShowItems(show);
#if 0//#ifdef __WXGTK__
    m_panel->Show(show);
    m_grid_sizer->Show(show);
#endif /* __WXGTK__ */
}

bool ConfigOptionsGroup::is_visible(ConfigOptionMode mode)
{
    if (m_options_mode.empty())
        return true;
    if (m_options_mode.size() == 1)
        return m_options_mode[0] <= mode;

    size_t hidden_row_cnt = 0;
    for (auto opt_mode : m_options_mode)
        if (opt_mode > mode)
            hidden_row_cnt++;

    return hidden_row_cnt != m_options_mode.size();
}

bool ConfigOptionsGroup::update_visibility(ConfigOptionMode mode)
{
	if (m_options_mode.empty() || !m_grid_sizer)
		return true;

    if (custom_ctrl) {
        bool show = custom_ctrl->update_visibility(mode);
        this->Show(show);
        return show;
    }

	int opt_mode_size = m_options_mode.size();
	if (m_grid_sizer->GetEffectiveRowsCount() != opt_mode_size &&
		opt_mode_size == 1)
		return m_options_mode[0] <= mode;

	Show(true);

    int coef = 0;
    int hidden_row_cnt = 0;
    const int cols = m_grid_sizer->GetCols();
    for (auto opt_mode : m_options_mode) {
		const bool show = opt_mode <= mode;
        if (!show) {
            hidden_row_cnt++;
            for (int i = 0; i < cols; ++i)
                m_grid_sizer->Show(coef + i, show);
        }
        coef+= cols;
	}

    if (hidden_row_cnt == opt_mode_size) {
        sizer->ShowItems(false);
        return false;
    }
    return true;
}

void ConfigOptionsGroup::msw_rescale()
{
    // update bitmaps for extra column items (like "mode markers" or buttons on settings panel)
    if (rescale_extra_column_item)
        for (auto extra_col : m_extra_column_item_ptrs)
            rescale_extra_column_item(extra_col);

    // update undo buttons : rescale bitmaps
    for (const auto& field : m_fields)
        field.second->msw_rescale();

    auto rescale = [](wxSizer* sizer) {
        for (wxSizerItem* item : sizer->GetChildren())
            if (item->IsWindow()) {
                wxWindow* win = item->GetWindow();
                // check if window is ScalableButton
                ScalableButton* sc_btn = dynamic_cast<ScalableButton*>(win);
                if (sc_btn) {
                    sc_btn->SetSize(sc_btn->GetBestSize());
                    return;
                }
                // check if window is wxButton
                wxButton* btn = dynamic_cast<wxButton*>(win);
                if (btn) {
                    btn->SetSize(btn->GetBestSize());
                    return;
                }
            }
    };

    // scale widgets and extra widgets if any exists
    for (const Line& line : m_lines) {
        if (line.widget_sizer)
            rescale(line.widget_sizer);
        if (line.extra_widget_sizer)
            rescale(line.extra_widget_sizer);
    }

    if (custom_ctrl)
        custom_ctrl->msw_rescale();
}

void ConfigOptionsGroup::sys_color_changed()
{
#ifdef _WIN32
    if (staticbox && stb) {
        wxGetApp().UpdateAllStaticTextDarkUI(stb);
        // update bitmaps for extra column items (like "delete" buttons on settings panel)
        for (auto extra_col : m_extra_column_item_ptrs)
            wxGetApp().UpdateDarkUI(extra_col);
    }

    if (custom_ctrl)
        wxGetApp().UpdateDarkUI(custom_ctrl);
#endif

    auto update = [](wxSizer* sizer) {
        for (wxSizerItem* item : sizer->GetChildren())
            if (item->IsWindow()) {
                wxWindow* win = item->GetWindow();
                // check if window is ScalableButton
                if (ScalableButton* sc_btn = dynamic_cast<ScalableButton*>(win)) {
                    sc_btn->sys_color_changed();
                    return;
                }
                wxGetApp().UpdateDarkUI(win, dynamic_cast<wxButton*>(win) != nullptr);
            }
    };

    // scale widgets and extra widgets if any exists
    for (const Line& line : m_lines) {
        if (line.widget_sizer)
            update(line.widget_sizer);
        if (line.extra_widget_sizer)
            update(line.extra_widget_sizer);
    }

	// update undo buttons : rescale bitmaps
	for (const auto& field : m_fields)
		field.second->sys_color_changed();
}

void ConfigOptionsGroup::refresh()
{
    if (custom_ctrl)
        custom_ctrl->Refresh();
}

boost::any ConfigOptionsGroup::config_value(const std::string& opt_key, int opt_index, bool deserialize) {

	if (deserialize) {
		// Want to edit a vector value(currently only multi - strings) in a single edit box.
		// Aggregate the strings the old way.
		// Currently used for the post_process config value only.
		if (opt_index != -1)
			throw Slic3r::OutOfRange("Can't deserialize option indexed value");
// 		return join(';', m_config->get(opt_key)});
		return get_config_value(*m_config, opt_key);
	}
	else {
//		return opt_index == -1 ? m_config->get(opt_key) : m_config->get_at(opt_key, opt_index);
		return get_config_value(*m_config, opt_key, opt_index);
	}
}

boost::any ConfigOptionsGroup::get_config_value(const DynamicPrintConfig& config, const std::string& opt_key, int opt_index /*= -1*/)
{
	size_t idx = opt_index == -1 ? 0 : opt_index;

	boost::any ret;
	wxString text_value = wxString("");
	const ConfigOptionDef* opt = config.def()->get(opt_key);

    if (opt->nullable)
    {
        switch (opt->type)
        {
        case coFloat:
            if (config.option(opt_key)->is_nil())
                ret = _L("N/A");
            else
                ret = double_to_string(config.option<ConfigOptionFloatNullable>(opt_key)->value);

            break;
        case coInt:
            ret = config.option<ConfigOptionIntNullable>(opt_key)->value;
            break;
        case coPercents:
        case coFloats: {
            if (config.option(opt_key)->is_nil())
                ret = _(L("N/A"));
            else {
                double val = opt->type == coFloats ?
                            config.option<ConfigOptionFloatsNullable>(opt_key)->get_at(idx) :
                            config.option<ConfigOptionPercentsNullable>(opt_key)->get_at(idx);
                ret = double_to_string(val); }
            }
            break;
        case coFloatsOrPercents: {
            if (config.option(opt_key)->is_nil()) {
                ret = _(L("N/A"));
            } else {
                const auto &config_option = config.option<ConfigOptionFloatsOrPercentsNullable>(opt_key)->get_at(idx);

                text_value = double_to_string(config_option.value);
                if (config_option.percent) {
                    text_value += "%";
                }

                ret = text_value;
            }
            break;
        }
        case coBools:
            ret = config.option<ConfigOptionBoolsNullable>(opt_key)->values[idx];
            break;
        case coInts:
            ret = config.option<ConfigOptionIntsNullable>(opt_key)->get_at(idx);
            break;
        default:
            break;
        }
        return ret;
    }

	switch (opt->type) {
	case coFloatOrPercent:{
		const auto &value = *config.option<ConfigOptionFloatOrPercent>(opt_key);

        text_value = double_to_string(value.value);
		if (value.percent)
			text_value += "%";

		ret = text_value;
		break;
	}
	case coFloatsOrPercents:{
		const auto& val = config.option<ConfigOptionFloatsOrPercents>(opt_key)->get_at(idx);
        text_value = double_to_string(val.value);
		if (val.percent)
			text_value += "%";
		ret = text_value;
		break;
	}
	case coPercent:{
		double val = config.option<ConfigOptionPercent>(opt_key)->value;
		text_value = wxString::Format(_T("%i"), int(val));
		ret = text_value;// += "%";
	}
		break;
	case coPercents:
	case coFloats:
	case coFloat:{
		double val = opt->type == coFloats ?
					config.opt_float(opt_key, idx) :
						opt->type == coFloat ? config.opt_float(opt_key) :
						config.option<ConfigOptionPercents>(opt_key)->get_at(idx);
		ret = double_to_string(val);
		}
		break;
	case coString: {
        ret = from_u8(config.opt_string(opt_key));
        break;
    }
	case coStrings:
		if (opt_key == "compatible_printers" || opt_key == "compatible_prints" || opt_key == "gcode_substitutions") {
			ret = config.option<ConfigOptionStrings>(opt_key)->values;
			break;
		}
		if (opt_key == "filament_ramming_parameters") {
			ret = config.opt_string(opt_key, static_cast<unsigned int>(idx));
			break;
		}
		if (config.option<ConfigOptionStrings>(opt_key)->values.empty())
			ret = text_value;
		else if (opt->gui_flags == "serialized") {
			std::vector<std::string> values = config.option<ConfigOptionStrings>(opt_key)->values;
			if (!values.empty() && !values[0].empty())
				for (const std::string& el : values)
					text_value += from_u8(el) + ";";
			ret = text_value;
		}
		else
			ret = from_u8(config.opt_string(opt_key, static_cast<unsigned int>(idx)));
		break;
	case coBool:
		ret = config.opt_bool(opt_key);
		break;
	case coBools:
		ret = config.opt_bool(opt_key, idx);
		break;
	case coInt:
		ret = config.opt_int(opt_key);
		break;
	case coInts:
		ret = config.opt_int(opt_key, idx);
		break;
	case coEnum:
        ret = config.option(opt_key)->getInt();
		break;
	case coEnums:
        ret = config.option(opt_key)->getInts()[idx];
		break;
	case coPoints:
		if (opt_key == "bed_shape")
			ret = config.option<ConfigOptionPoints>(opt_key)->values;
		else
			ret = config.option<ConfigOptionPoints>(opt_key)->get_at(idx);
		break;
	case coNone:
	default:
		break;
	}
	return ret;
}

Field* ConfigOptionsGroup::get_fieldc(const t_config_option_key& opt_key, int opt_index)
{
	Field* field = get_field(opt_key);
	if (field != nullptr)
		return field;
	std::string opt_id = "";
	for (t_opt_map::iterator it = m_opt_map.begin(); it != m_opt_map.end(); ++it) {
		if (opt_key == m_opt_map.at(it->first).first && opt_index == m_opt_map.at(it->first).second) {
			opt_id = it->first;
			break;
		}
	}
	return opt_id.empty() ? nullptr : get_field(opt_id);
}

std::pair<OG_CustomCtrl*, bool*> ConfigOptionsGroup::get_custom_ctrl_with_blinking_ptr(const t_config_option_key& opt_key, int opt_index/* = -1*/)
{
	Field* field = get_fieldc(opt_key, opt_index);

	if (field)
		return {custom_ctrl, field->get_blink_ptr()};

	for (Line& line : m_lines)
		for (const Option& opt : line.get_options())
			if (opt.opt_id == opt_key && line.widget)
				return { custom_ctrl, line.get_blink_ptr() };

	return { nullptr, nullptr };
}

// Change an option on m_config, possibly call ModelConfig::touch().
void ConfigOptionsGroup::change_opt_value(const t_config_option_key& opt_key, const boost::any& value, int opt_index /*= 0*/)

{
    OptionsGroup::change_opt_value(const_cast<DynamicPrintConfig&>(*m_config), opt_key, value, opt_index);
	if (m_modelconfig)
		m_modelconfig->touch();
}

wxString OptionsGroup::get_url(const std::string& path_end)
{
    if (path_end.empty())
        return wxEmptyString;

    wxString language = wxGetApp().current_language_code_safe();
    wxString lang_marker = language.IsEmpty() ? "en" : language.BeforeFirst('_');

    return wxString("https://help.prusa3d.com/") + lang_marker + wxString("/article/" + path_end);
}

bool OptionsGroup::launch_browser(const std::string& path_end)
{
    return wxGetApp().open_browser_with_warning_dialog(OptionsGroup::get_url(path_end), wxGetApp().tab_panel());
}

// list of options, which doesn't have a related filed
static const std::set<std::string> options_without_field = {
    "compatible_printers",
    "compatible_prints",
    "bed_shape",
    "filament_ramming_parameters",
    "gcode_substitutions",
};

bool OptionsGroup::is_option_without_field(const std::string& opt_key)
{
    return  options_without_field.find(opt_key) != options_without_field.end();
}


//-------------------------------------------------------------------------------------------
// ogStaticText
//-------------------------------------------------------------------------------------------

ogStaticText::ogStaticText(wxWindow* parent, const wxString& text) :
    wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize)
{
    if (!text.IsEmpty()) {
		Wrap(60 * wxGetApp().em_unit());
		GetParent()->Layout();
    }
}


void ogStaticText::SetText(const wxString& value, bool wrap/* = true*/)
{
	SetLabel(value);
    if (wrap) Wrap(60 * wxGetApp().em_unit());
	GetParent()->Layout();
}

void ogStaticText::SetPathEnd(const std::string& link)
{
#ifndef __linux__

    Bind(wxEVT_ENTER_WINDOW, [this, link](wxMouseEvent& event) {
        SetToolTip(OptionsGroup::get_url(get_app_config()->get("suppress_hyperlinks") != "1" ? link : std::string()));
        FocusText(true);
        event.Skip();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) { FocusText(false); event.Skip(); });

    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (HasCapture())
            return;
        this->CaptureMouse();
        event.Skip();
    });
    Bind(wxEVT_LEFT_UP, [link, this](wxMouseEvent& event) {
        if (!HasCapture())
            return;
        ReleaseMouse();
        OptionsGroup::launch_browser(link);
        event.Skip();
    });

#else

    // Workaround: On Linux wxStaticText doesn't receive wxEVT_ENTER(LEAVE)_WINDOW events,
    // so implement this behaviour trough wxEVT_MOTION events for this control and it's parent
    Bind(wxEVT_MOTION, [link, this](wxMouseEvent& event) {
        SetToolTip(OptionsGroup::get_url(!get_app_config()->get_bool("suppress_hyperlinks") ? link : std::string()));
        FocusText(true);
        event.Skip();
    });
    GetParent()->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        FocusText(false);
        event.Skip();
    });

    // On Linux a mouse capturing causes a totally application freeze
    Bind(wxEVT_LEFT_UP, [link, this](wxMouseEvent& event) {
        OptionsGroup::launch_browser(link);
        event.Skip();
    });

#endif
}

void ogStaticText::FocusText(bool focus)
{
    if (get_app_config()->get_bool("suppress_hyperlinks"))
        return;

    SetFont(focus ? Slic3r::GUI::wxGetApp().link_font() :
        Slic3r::GUI::wxGetApp().normal_font());
#ifdef __linux__
    this->GetContainingSizer()->Layout();
#endif
    Refresh();
}

} // GUI
} // Slic3r
