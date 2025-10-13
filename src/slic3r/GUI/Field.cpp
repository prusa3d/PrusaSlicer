///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Vojtěch Král @vojtechkral, Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2020 Pascal de Bruijn @pmjdebruijn
///|/ Copyright (c) 2018 - 2020 Martin Loidl @LoidlM
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Field.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/enum_bitmask.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"

#include <regex>
#include <wx/numformatter.h>
#include <wx/bookctrl.h> // IWYU pragma: keep
#include <wx/tooltip.h> // IWYU pragma: keep
#include <wx/notebook.h>
#include <wx/listbook.h>
#include <wx/tokenzr.h>
#include <boost/algorithm/string/predicate.hpp>
#include "OG_CustomCtrl.hpp"
#include "MsgDialog.hpp"
#include "BitmapComboBox.hpp"

#include "Widgets/ComboBox.hpp"

#ifdef __WXOSX__
#define wxOSX true
#else
#define wxOSX false
#endif

namespace Slic3r :: GUI {

wxString double_to_string(double const value, const int max_precision /*= 4*/)
{
// Style_NoTrailingZeroes does not work on OSX. It also does not work correctly with some locales on Windows.
//	return wxNumberFormatter::ToString(value, max_precision, wxNumberFormatter::Style_NoTrailingZeroes);

	wxString s = wxNumberFormatter::ToString(value, std::abs(value) < 0.0001 ? 10 : max_precision, wxNumberFormatter::Style_None);

	// The following code comes from wxNumberFormatter::RemoveTrailingZeroes(wxString& s)
	// with the exception that here one sets the decimal separator explicitely to dot.
    // If number is in scientific format, trailing zeroes belong to the exponent and cannot be removed.
    if (s.find_first_of("eE") == wxString::npos) {
        char dec_sep = is_decimal_separator_point() ? '.' : ',';
        const size_t posDecSep = s.find(dec_sep);
	    // No decimal point => removing trailing zeroes irrelevant for integer number.
	    if (posDecSep != wxString::npos) {
		    // Find the last character to keep.
		    size_t posLastNonZero = s.find_last_not_of("0");
            // If it's the decimal separator itself, don't keep it either.
		    if (posLastNonZero == posDecSep)
		        -- posLastNonZero;
		    s.erase(posLastNonZero + 1);
		    // Remove sign from orphaned zero.
		    if (s.compare("-0") == 0)
		        s = "0";
		}
	}

    return s;
}

ThumbnailErrors validate_thumbnails_string(wxString& str, const wxString& def_ext = "PNG")
{
    std::string input_string = into_u8(str);

    str.Clear();

    auto [thumbnails_list, errors] = GCodeThumbnails::make_and_check_thumbnail_list(input_string);
    if (!thumbnails_list.empty()) {
        const auto& extentions = ConfigOptionEnum<GCodeThumbnailsFormat>::get_enum_names();
        for (const auto& [format, size] : thumbnails_list)
            str += format_wxstr("%1%x%2%/%3%, ", size.x(), size.y(), extentions[int(format)]);
        str.resize(str.Len() - 2);
    }

    return errors;
}

Field::~Field()
{
	if (m_on_kill_focus)
		m_on_kill_focus = nullptr;
	if (m_on_change)
		m_on_change = nullptr;
	if (m_back_to_initial_value)
		m_back_to_initial_value = nullptr;
	if (m_back_to_sys_value)
		m_back_to_sys_value = nullptr;
	if (getWindow()) {
		wxWindow* win = getWindow();
		win->Destroy();
		win = nullptr;
	}
}

void Field::PostInitialize()
{
	switch (m_opt.type)
	{
	case coPercents:
	case coFloats:
	case coStrings:
	case coBools:
	case coInts: {
		auto tag_pos = m_opt_id.find("#");
		if (tag_pos != std::string::npos)
			m_opt_idx = stoi(m_opt_id.substr(tag_pos + 1, m_opt_id.size()));
		break;
	}
	default:
		break;
	}

    // initialize m_unit_value
    m_em_unit = em_unit(m_parent);
    parent_is_custom_ctrl = dynamic_cast<OG_CustomCtrl*>(m_parent) != nullptr;

	BUILD();

	// For the mode, when settings are in non-modal dialog, neither dialog nor tabpanel doesn't receive wxEVT_KEY_UP event, when some field is selected.
	// So, like a workaround check wxEVT_KEY_UP event for the Filed and switch between tabs if Ctrl+(1-4) was pressed
	if (getWindow())
		getWindow()->Bind(wxEVT_KEY_UP, [](wxKeyEvent& evt) {
		    if ((evt.GetModifiers() & wxMOD_CONTROL) != 0) {
			    int tab_id = -1;
			    switch (evt.GetKeyCode()) {
			    case '1': { tab_id = 0; break; }
			    case '2': { tab_id = 1; break; }
				case '3': { tab_id = 2; break; }
				case '4': { tab_id = 3; break; }
#ifdef __APPLE__
				case 'f':
#else /* __APPLE__ */
				case WXK_CONTROL_F:
#endif /* __APPLE__ */
				case 'F': { wxGetApp().show_search_dialog(); break; }
			    default: break;
			    }
			    if (tab_id >= 0)
					wxGetApp().mainframe->select_tab(tab_id);
				if (tab_id > 0)
					// tab panel should be focused for correct navigation between tabs
				    wxGetApp().tab_panel()->SetFocus();
		    }

		    evt.Skip();
	    });
}

// Values of width to alignments of fields
int Field::def_width()			{ return 8; }
int Field::def_width_wider()	{ return 16; }
int Field::def_width_thinner()	{ return 4; }

void Field::on_kill_focus()
{
	// call the registered function if it is available
    if (m_on_kill_focus!=nullptr)
        m_on_kill_focus(m_opt_id);
}

void Field::on_change_field()
{
//       std::cerr << "calling Field::_on_change \n";
    if (m_on_change != nullptr  && !m_disable_change_event)
        m_on_change(m_opt_id, get_value());
}

void Field::on_back_to_initial_value()
{
	if (m_back_to_initial_value != nullptr && m_is_modified_value)
		m_back_to_initial_value(m_opt_id);
}

void Field::on_back_to_sys_value()
{
	if (m_back_to_sys_value != nullptr && m_is_nonsys_value)
		m_back_to_sys_value(m_opt_id);
}

void Field::on_edit_value()
{
	if (m_fn_edit_value)
		m_fn_edit_value(m_opt_id);
}

wxString Field::get_tooltip_text(const wxString& default_string)
{
    if (m_opt.tooltip.empty())
        return "";

    std::string opt_id = m_opt_id;
    auto hash_pos = opt_id.find('#');
    if (hash_pos != std::string::npos) {
        opt_id.replace(hash_pos, 1,"[");
        opt_id += "]";
    }

    bool newline_after_name = boost::iends_with(opt_id, "_gcode") && opt_id != "binary_gcode";

	return from_u8(m_opt.tooltip) + "\n" + _L("default value") + "\t: " +
        (newline_after_name ? "\n" : "") + default_string +
        (newline_after_name ? "" : "\n") +
        _L("parameter name") + "\t: " + opt_id;
}

bool Field::is_matched(const std::string& string, const std::string& pattern)
{
	std::regex regex_pattern(pattern, std::regex_constants::icase); // use ::icase to make the matching case insensitive like /i in perl
	return std::regex_match(string, regex_pattern);
}

static wxString na_value(bool for_spin_ctrl = false)
{
#ifdef __linux__
    if (for_spin_ctrl)
        return "";
#endif
    return _(L("N/A"));
}

void Field::get_value_by_opt_type(wxString& str, const bool check_value/* = true*/)
{
	switch (m_opt.type) {
	case coInt:
		m_value = wxAtoi(str);
		break;
	case coPercent:
	case coPercents:
	case coFloats:
	case coFloat:{
		if (m_opt.type == coPercent && !str.IsEmpty() &&  str.Last() == '%')
			str.RemoveLast();
		else if (!str.IsEmpty() && str.Last() == '%')
        {
            if (!check_value) {
                m_value.clear();
                break;
            }

			wxString label = m_opt.full_label.empty() ? _(m_opt.label) : _(m_opt.full_label);
            show_error(m_parent, format_wxstr(_L("%s doesn't support percentage"), label));
			set_value(double_to_string(m_opt.min), true);
			m_value = double(m_opt.min);
			break;
		}
        double val;

        bool is_na_value = m_opt.nullable && str == na_value();

        const char dec_sep = is_decimal_separator_point() ? '.' : ',';
        const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
        // Replace the first incorrect separator in decimal number, 
        // if this value doesn't "N/A" value in some language
        // see https://github.com/prusa3d/PrusaSlicer/issues/6921
        if (!is_na_value && str.Replace(dec_sep_alt, dec_sep, false) != 0)
            set_value(str, false);

        if (str == dec_sep)
            val = 0.0;
        else
        {
            if (is_na_value)
                val = ConfigOptionFloatsNullable::nil_value();
            else if (!str.ToDouble(&val))
            {
                if (!check_value) {
                    m_value.clear();
                    break;
                }
                show_error(m_parent, _(L("Invalid numeric input.")));
                set_value(double_to_string(val), true);
            }
            if (m_opt.min > val || val > m_opt.max)
            {
                if (!check_value) {
                    m_value.clear();
                    break;
                }
                if (m_opt_id == "extrusion_multiplier") {
                    if (m_value.empty() || boost::any_cast<double>(m_value) != val) {
                        wxString msg_text = format_wxstr(_L("Input value is out of range\n"
                            "Are you sure that %s is a correct value and that you want to continue?"), str);
//                        wxMessageDialog dialog(m_parent, msg_text, _L("Parameter validation") + ": " + m_opt_id, wxICON_WARNING | wxYES | wxNO);
                        WarningDialog dialog(m_parent, msg_text, _L("Parameter validation") + ": " + m_opt_id, wxYES | wxNO);
                        if (dialog.ShowModal() == wxID_NO) {
                            if (m_value.empty()) {
                                if (m_opt.min > val) val = m_opt.min;
                                if (val > m_opt.max) val = m_opt.max;
                            }
                            else
                                val = boost::any_cast<double>(m_value);
                            set_value(double_to_string(val), true);
                        }
                    }
                }
                else {
                    if (val < (m_opt.min - EPSILON) || val > (m_opt.max + EPSILON)) {
                        show_error(m_parent, _L("Input value is out of range"));
                    }

                    val = std::clamp(static_cast<float>(val), m_opt.min, m_opt.max);
                    set_value(double_to_string(val), true);
                }
            }
        }
        m_value = val;
		break; }
	case coString:
	case coStrings:
    case coFloatsOrPercents:
    case coFloatOrPercent: {
        if (m_opt.type == coFloatOrPercent && m_opt.opt_key == "first_layer_height" && !str.IsEmpty() && str.Last() == '%') {
            // Workaroud to avoid of using of the % for first layer height
            // see https://github.com/prusa3d/PrusaSlicer/issues/7418
            wxString label = m_opt.full_label.empty() ? _(m_opt.label) : _(m_opt.full_label);
            show_error(m_parent, format_wxstr(_L("%s doesn't support percentage"), label));
            const wxString stVal = double_to_string(0.01, 2);
            set_value(stVal, true);
            m_value = into_u8(stVal);;
            break;
        }
        if ((m_opt.type == coFloatOrPercent || m_opt.type == coFloatsOrPercents) && !str.IsEmpty() &&  str.Last() != '%')
        {
            double val = 0.;

            bool is_na_value = m_opt.nullable && str == na_value();

            const char dec_sep = is_decimal_separator_point() ? '.' : ',';
            const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
            // Replace the first incorrect separator in decimal number.
            if (!is_na_value && str.Replace(dec_sep_alt, dec_sep, false) != 0)
                set_value(str, false);

            // remove space and "mm" substring, if any exists
            str.Replace(" ", "", true);
            str.Replace("m", "", true);

            if (is_na_value) {
                val = ConfigOptionFloatsOrPercentsNullable::nil_value().value;
            } else if (!str.ToDouble(&val)) {
                if (!check_value) {
                    m_value.clear();
                    break;
                }
                show_error(m_parent, _(L("Invalid numeric input.")));
                set_value(double_to_string(val), true);
            }
            else if (((m_opt.sidetext.rfind("mm/s") != std::string::npos && val > m_opt.max) ||
                     (m_opt.sidetext.rfind("mm ") != std::string::npos && val > /*1*/m_opt.max_literal)) &&
                     (m_value.empty() || into_u8(str) != boost::any_cast<std::string>(m_value)))
            {
                if (!check_value) {
                    m_value.clear();
                    break;
                }

                bool infill_anchors = m_opt.opt_key == "infill_anchor" || m_opt.opt_key == "infill_anchor_max";

                const std::string sidetext = m_opt.sidetext.rfind("mm/s") != std::string::npos ? "mm/s" : "mm";
                const wxString stVal = double_to_string(val, 2);
                // TRN %1% = Value, %2% = units
                const wxString msg_text = format_wxstr(_L("Do you mean %1%%% instead of %1% %2%?\n"
                    "Select YES if you want to change this value to %1%%%, \n"
                    "or NO if you are sure that %1% %2% is a correct value."), stVal, sidetext);
                WarningDialog dialog(m_parent, msg_text, _L("Parameter validation") + ": " + m_opt_id, wxYES | wxNO);
                if ((!infill_anchors || val > 100) && dialog.ShowModal() == wxID_YES) {
                    set_value(from_u8((boost::format("%s%%") % stVal).str()), false/*true*/);
                    str += "%%";
                }
				else
					set_value(stVal, false); // it's no needed but can be helpful, when inputted value contained "," instead of "."
            }
        }

        if (m_opt.opt_key == "thumbnails") {
            wxString str_out = str;
            ThumbnailErrors errors = validate_thumbnails_string(str_out);
            if (errors != enum_bitmask<ThumbnailError>()) {
                set_value(str_out, true);
                wxString error_str;
                if (errors.has(ThumbnailError::InvalidVal))
                    error_str += format_wxstr(_L("Invalid input format. Expected vector of dimensions in the following format: \"%1%\""), "XxY/EXT, XxY/EXT, ...");
                if (errors.has(ThumbnailError::OutOfRange)) {
                    if (!error_str.empty())
                        error_str += "\n\n";
                    error_str += _L("Input value is out of range");
                }
                if (errors.has(ThumbnailError::InvalidExt)) {
                    if (!error_str.empty())
                        error_str += "\n\n";
                    error_str += _L("Some extension in the input is invalid");
                }
                show_error(m_parent, error_str);
            }
            else if (str_out != str) {
                str = str_out;
                set_value(str, true);
            }
        }

        m_value = into_u8(str);
		break;
    }

	default:
		break;
	}
}

void Field::msw_rescale()
{
	// update em_unit value
	m_em_unit = em_unit(m_parent);
}

void Field::sys_color_changed()
{
#ifdef _WIN32
	if (wxWindow* win = this->getWindow())
		wxGetApp().UpdateDarkUI(win);
#endif
}

template<class T>
bool is_defined_input_value(wxWindow* win, const ConfigOptionType& type)
{
    if (!win || (static_cast<T*>(win)->GetValue().empty() && type != coString && type != coStrings && type != coPoints))
        return false;
    return true;
}

void TextCtrl::BUILD() {
    auto size = wxSize(def_width()*m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	wxString text_value = wxString("");

	switch (m_opt.type) {
	case coFloatOrPercent:
	{
		text_value = double_to_string(m_opt.default_value->getFloat());
		if (m_opt.get_default_value<ConfigOptionFloatOrPercent>()->percent)
			text_value += "%";
		break;
	}
    case coFloatsOrPercents: {
		const auto val =  m_opt.get_default_value<ConfigOptionFloatsOrPercents>()->get_at(m_opt_idx);
        text_value = double_to_string(val.value);
        if (val.percent) {
            text_value += "%";
        }

        m_last_meaningful_value = text_value;
        break;
	}
	case coPercent:
	{
		text_value = wxString::Format(_T("%i"), int(m_opt.default_value->getFloat()));
		text_value += "%";
		break;
	}
	case coPercents:
	case coFloats:
	case coFloat:
	{
		double val = m_opt.type == coFloats ?
			m_opt.get_default_value<ConfigOptionFloats>()->get_at(m_opt_idx) :
			m_opt.type == coFloat ?
				m_opt.default_value->getFloat() :
				m_opt.get_default_value<ConfigOptionPercents>()->get_at(m_opt_idx);
		text_value = double_to_string(val);
        m_last_meaningful_value = text_value;
		break;
	}
	case coString:
		text_value = m_opt.get_default_value<ConfigOptionString>()->value;
		break;
	case coStrings:
	{
		const ConfigOptionStrings *vec = m_opt.get_default_value<ConfigOptionStrings>();
		if (vec == nullptr || vec->empty()) break; //for the case of empty default value
		text_value = vec->get_at(m_opt_idx);
		break;
	}
	default:
		break;
	}

    long style = m_opt.multiline ? wxTE_MULTILINE : wxTE_PROCESS_ENTER;
	auto temp = new text_ctrl(m_parent, text_value, "", "", wxDefaultPosition, size, style);
    if (parent_is_custom_ctrl && m_opt.height < 0)
        opt_height = (double)temp->GetSize().GetHeight()/m_em_unit;
    temp->SetFont(m_opt.is_code ?
                  Slic3r::GUI::wxGetApp().code_font():
                  Slic3r::GUI::wxGetApp().normal_font());
	wxGetApp().UpdateDarkUI(temp);

    if (! m_opt.multiline && !wxOSX)
		// Only disable background refresh for single line input fields, as they are completely painted over by the edit control.
		// This does not apply to the multi-line edit field, where the last line and a narrow frame around the text is not cleared.
		temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

	temp->SetToolTip(get_tooltip_text(text_value));

    if (style & wxTE_PROCESS_ENTER) {
        temp->Bind(wxEVT_TEXT_ENTER, ([this, temp](wxEvent& e)
        {
#if !defined(__WXGTK__)
            e.Skip();
            temp->GetToolTip()->Enable(true);
#endif // __WXGTK__
            EnterPressed enter(this);
            propagate_value();
        }), temp->GetId());
    }

	temp->Bind(wxEVT_LEFT_DOWN, ([temp](wxEvent& event)
	{
		//! to allow the default handling
		event.Skip();
		//! eliminating the g-code pop up text description
		bool flag = false;
#ifdef __WXGTK__
		// I have no idea why, but on GTK flag works in other way
		flag = true;
#endif // __WXGTK__
		temp->GetToolTip()->Enable(flag);
	}), temp->GetId());

	temp->Bind(wxEVT_KILL_FOCUS, ([this, temp](wxEvent& e)
	{
		e.Skip();
#if !defined(__WXGTK__)
		temp->GetToolTip()->Enable(true);
#endif // __WXGTK__
        if (!bEnterPressed)
            propagate_value();
	}), temp->GetId());
/*
	// select all text using Ctrl+A
	temp->Bind(wxEVT_CHAR, ([temp](wxKeyEvent& event)
	{
		if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_CONTROL))
			temp->SetSelection(-1, -1); //select all
		event.Skip();
	}));
*/
    // recast as a wxWindow to fit the calling convention
    window = dynamic_cast<wxWindow*>(temp);
}

bool TextCtrl::value_was_changed()
{
    if (m_value.empty())
        return true;

    boost::any val = m_value;
    wxString ret_str = static_cast<text_ctrl*>(window)->GetValue();
    // update m_value!
    // ret_str might be changed inside get_value_by_opt_type
    get_value_by_opt_type(ret_str);

    switch (m_opt.type) {
    case coInt:
        return boost::any_cast<int>(m_value) != boost::any_cast<int>(val);
    case coPercent:
    case coPercents:
    case coFloats:
    case coFloat: {
        if (m_opt.nullable && std::isnan(boost::any_cast<double>(m_value)) &&
                              std::isnan(boost::any_cast<double>(val)))
            return false;
        return boost::any_cast<double>(m_value) != boost::any_cast<double>(val);
    }
    case coString:
    case coStrings:
    case coFloatOrPercent:
    case coFloatsOrPercents:
        return boost::any_cast<std::string>(m_value) != boost::any_cast<std::string>(val);
    case coPoints:
        return boost::any_cast<std::vector<Vec2d>>(m_value) != boost::any_cast<std::vector<Vec2d>>(val);
    default:
        return true;
    }
}

void TextCtrl::propagate_value()
{
    wxString val = dynamic_cast<text_ctrl*>(window)->GetValue();
    if (m_opt.nullable && val != na_value())
        m_last_meaningful_value = val;

    if (!is_defined_input_value<text_ctrl>(window, m_opt.type) )
        // on_kill_focus() cause a call of OptionsGroup::reload_config(),
        // Thus, do it only when it's really needed (when undefined value was input)
        on_kill_focus();
    else if (value_was_changed())
        on_change_field();
}

void TextCtrl::set_value(const boost::any& value, bool change_event/* = false*/) {
    m_disable_change_event = !change_event;
    if (m_opt.nullable) {
        const bool m_is_na_val = boost::any_cast<wxString>(value) == na_value();
        if (!m_is_na_val)
            m_last_meaningful_value = value;
        dynamic_cast<text_ctrl*>(window)->SetValue(m_is_na_val ? na_value() : boost::any_cast<wxString>(value));
    }
    else
        dynamic_cast<text_ctrl*>(window)->SetValue(boost::any_cast<wxString>(value));
    m_disable_change_event = false;

    if (!change_event) {
        wxString ret_str = static_cast<text_ctrl*>(window)->GetValue();
        /* Update m_value to correct work of next value_was_changed().
         * But after checking of entered value, don't fix the "incorrect" value and don't show a warning message,
         * just clear m_value in this case.
         */
        get_value_by_opt_type(ret_str, false);
    }
}

void TextCtrl::set_last_meaningful_value()
{
    dynamic_cast<text_ctrl*>(window)->SetValue(boost::any_cast<wxString>(m_last_meaningful_value));
    propagate_value();
}

void TextCtrl::set_na_value()
{
    dynamic_cast<text_ctrl*>(window)->SetValue(na_value());
    propagate_value();
}

boost::any& TextCtrl::get_value()
{
	wxString ret_str = static_cast<text_ctrl*>(window)->GetValue();
	// update m_value
	get_value_by_opt_type(ret_str);

	return m_value;
}

void TextCtrl::msw_rescale()
{
    Field::msw_rescale();
    auto size = wxSize(def_width() * m_em_unit, wxDefaultCoord);

    if (m_opt.height >= 0)
        size.SetHeight(m_opt.height*m_em_unit);
    else if (parent_is_custom_ctrl && opt_height > 0)
        size.SetHeight(lround(opt_height*m_em_unit));
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

    if (size != wxDefaultSize) {
        if (::TextInput* text_input = dynamic_cast<::TextInput*>(window)) {
            text_input->SetCtrlSize(size);
            return;
        }
        wxTextCtrl* field = dynamic_cast<wxTextCtrl*>(window);
        if (parent_is_custom_ctrl)
            field->SetSize(size);
        else
            field->SetMinSize(size);
    }

}

void TextCtrl::enable()  { dynamic_cast<text_ctrl*>(window)->Enable(); }
void TextCtrl::disable() { dynamic_cast<text_ctrl*>(window)->Disable();}

#ifdef __WXGTK__
void TextCtrl::change_field_value(wxEvent& event)
{
    if ((bChangedValueEvent = (event.GetEventType()==wxEVT_KEY_UP)))
		on_change_field();
    event.Skip();
};
#endif //__WXGTK__


wxWindow* CheckBox::GetNewWin(wxWindow* parent, const wxString& label /*= wxEmptyString*/)
{
    if (wxGetApp().suppress_round_corners())
        return new ::CheckBox(parent, label);
    
    return new ::SwitchButton(parent, label);
}

void CheckBox::SetValue(wxWindow* win, bool value)
{
    if (wxGetApp().suppress_round_corners()) {
        if (::CheckBox* ch_b = dynamic_cast<::CheckBox*>(win))
            ch_b->SetValue(value);
    }
    else {
        if (::SwitchButton* ch_b = dynamic_cast<::SwitchButton*>(win))
            ch_b->SetValue(value);
    }
}

bool CheckBox::GetValue(wxWindow* win)
{
    if (wxGetApp().suppress_round_corners())
        return dynamic_cast<::CheckBox*>(win)->GetValue();

    return dynamic_cast<::SwitchButton*>(win)->GetValue();
}

void CheckBox::Rescale(wxWindow* win)
{
    if (wxGetApp().suppress_round_corners())
        dynamic_cast<::CheckBox*>(win)->Rescale();
    else
        dynamic_cast<::SwitchButton*>(win)->Rescale();
}

void CheckBox::SysColorChanged(wxWindow* win)
{
    if (!wxGetApp().suppress_round_corners())
        dynamic_cast<::SwitchButton*>(win)->SysColorChange();
}

void CheckBox::SetValue(bool value)
{
    if (wxGetApp().suppress_round_corners())
        dynamic_cast<::CheckBox*>(window)->SetValue(value);
    else
        dynamic_cast<::SwitchButton*>(window)->SetValue(value);
}

bool CheckBox::GetValue()
{
    if (wxGetApp().suppress_round_corners())
        return dynamic_cast<::CheckBox*>(window)->GetValue();

    return dynamic_cast<::SwitchButton*>(window)->GetValue();
}

void CheckBox::BUILD() {
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) 
        size.SetHeight(m_opt.height*m_em_unit);
	if (m_opt.width >= 0) 
        size.SetWidth(m_opt.width*m_em_unit);

	bool check_value =	m_opt.type == coBool ?
						m_opt.default_value->getBool() : m_opt.type == coBools ?
							m_opt.get_default_value<ConfigOptionBools>()->get_at(m_opt_idx) :
    						false;

    m_last_meaningful_value = static_cast<unsigned char>(check_value);

	// Set Label as a string of at least one space simbol to correct system scaling of a CheckBox
    window = GetNewWin(m_parent);
    wxGetApp().UpdateDarkUI(window);
	window->SetFont(wxGetApp().normal_font());
	if (!wxOSX) 
        window->SetBackgroundStyle(wxBG_STYLE_PAINT);
	if (m_opt.readonly) 
        window->Disable();

	SetValue(check_value);

	window->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent e) {
        m_is_na_val = false;
	    on_change_field();
	});

	window->SetToolTip(get_tooltip_text(check_value ? "true" : "false"));
}

void CheckBox::set_value(const bool value, bool change_event/* = false*/)
{
    m_disable_change_event = !change_event;
    SetValue(value);
    m_disable_change_event = false;
}

void CheckBox::set_value(const boost::any& value, bool change_event)
{
    m_disable_change_event = !change_event;
    if (m_opt.nullable) {
        m_is_na_val = boost::any_cast<unsigned char>(value) == ConfigOptionBoolsNullable::nil_value();
        if (!m_is_na_val)
            m_last_meaningful_value = value;
        SetValue(m_is_na_val ? false : boost::any_cast<unsigned char>(value) != 0);
    }
    else
        SetValue(boost::any_cast<bool>(value));
    m_disable_change_event = false;
}

void CheckBox::set_last_meaningful_value()
{
    if (m_opt.nullable) {
        m_is_na_val = false;
        SetValue(boost::any_cast<unsigned char>(m_last_meaningful_value) != 0);
        on_change_field();
    }
}

void CheckBox::set_na_value()
{
    if (m_opt.nullable) {
        m_is_na_val = true;
        on_change_field();
    }
}

boost::any& CheckBox::get_value()
{
	bool value = GetValue();
	if (m_opt.type == coBool)
		m_value = static_cast<bool>(value);
	else
		m_value = m_is_na_val ? ConfigOptionBoolsNullable::nil_value() : static_cast<unsigned char>(value);
 	return m_value;
}

void CheckBox::msw_rescale()
{
    Field::msw_rescale();
    window->SetInitialSize(window->GetBestSize());
}

void CheckBox::sys_color_changed()
{
    Field::sys_color_changed();
    if (auto switch_btn = dynamic_cast<::SwitchButton*>(window))
        switch_btn->SysColorChange();
}

void CheckBox::enable()
{
    window->Enable();
}

void CheckBox::disable()
{
    window->Disable();
}


void SpinCtrl::BUILD() {
	auto size = wxSize(def_width() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	wxString	text_value = wxString("");
	int			default_value = UNDEF_VALUE;

	switch (m_opt.type) {
	case coInt:
		default_value = m_opt.default_value->getInt();
        m_last_meaningful_value = default_value;
		break;
	case coInts:
	{
		default_value = m_opt.get_default_value<ConfigOptionInts>()->get_at(m_opt_idx);
        if (m_opt.nullable) {
            if (default_value == ConfigOptionIntsNullable::nil_value())
                m_last_meaningful_value = m_opt.opt_key == "idle_temperature" ? 30 : static_cast<int>(m_opt.max);
            else
                m_last_meaningful_value = default_value;
        }
		break;
	}
	default:
		break;
	}

    if (default_value != UNDEF_VALUE)
        text_value = wxString::Format(_T("%i"), default_value);

    const int min_val = m_opt.min == -FLT_MAX ? (int)0 : (int)m_opt.min;
	const int max_val = m_opt.max < FLT_MAX ? (int)m_opt.max : INT_MAX;

	auto temp = new ::SpinInput(m_parent, text_value, "", wxDefaultPosition, size,
		wxTE_PROCESS_ENTER | wxSP_ARROW_KEYS

		, min_val, max_val, default_value);

#ifdef __WXGTK3__
	wxSize best_sz = temp->GetBestSize();
	if (best_sz.x > size.x)
		temp->SetSize(wxSize(size.x + 2 * best_sz.y, best_sz.y));
#endif //__WXGTK3__
	temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
	wxGetApp().UpdateDarkUI(temp);

    if (m_opt.height < 0 && parent_is_custom_ctrl) {
        opt_height = (double)temp->GetSize().GetHeight() / m_em_unit;
    }

	temp->Bind(wxEVT_KILL_FOCUS, ([this](wxEvent& e)
	{
        e.Skip();
        if (bEnterPressed) {
            bEnterPressed = false;
            return;
        }

        propagate_value();
	}));

    temp->Bind(wxEVT_SPINCTRL, ([this](wxCommandEvent e) {  propagate_value();  }), temp->GetId());

    temp->Bind(wxEVT_TEXT_ENTER, ([this](wxCommandEvent e)
    {
        e.Skip();
        propagate_value();
        bEnterPressed = true;
    }), temp->GetId());
	temp->SetToolTip(get_tooltip_text(text_value));

    temp->Bind(wxEVT_TEXT, [this, temp](wxCommandEvent e) {
        long value;
        if (!e.GetString().ToLong(&value))
            return;
        if (value < INT_MIN || value > INT_MAX)
            tmp_value = UNDEF_VALUE;
        else {
            tmp_value = std::min(std::max((int)value, temp->GetMin()), temp->GetMax());
            // update value for the control only if it was changed in respect to the Min/max values
            if (tmp_value != (int)value) {
                temp->SetValue(tmp_value);
                // But after SetValue() cursor ison the first position
                // so put it to the end of string
                int pos = std::to_string(tmp_value).length();
                temp->SetSelection(pos, pos);
            }
        }
    }, temp->GetId());

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);
}

void SpinCtrl::set_value(const boost::any& value, bool change_event/* = false*/)
{
    m_disable_change_event = !change_event;
    tmp_value = boost::any_cast<int>(value);
    m_value = value;
    if (m_opt.nullable) {
        const bool m_is_na_val = tmp_value == ConfigOptionIntsNullable::nil_value();
        if (m_is_na_val)
            dynamic_cast<::SpinInput*>(window)->SetValue(na_value(true));
        else {
            m_last_meaningful_value = value;
            dynamic_cast<::SpinInput*>(window)->SetValue(tmp_value);
        }
    }
    else
        dynamic_cast<::SpinInput*>(window)->SetValue(tmp_value);
    m_disable_change_event = false;
}

void SpinCtrl::set_last_meaningful_value()
{
    const int val = boost::any_cast<int>(m_last_meaningful_value);
    dynamic_cast<::SpinInput*>(window)->SetValue(val);
    tmp_value = val;
    propagate_value();
}

void SpinCtrl::set_na_value()
{
    dynamic_cast<::SpinInput*>(window)->SetValue(na_value(true));
    m_value = ConfigOptionIntsNullable::nil_value();
    propagate_value();
}

boost::any& SpinCtrl::get_value()
{
    ::SpinInput* spin = static_cast<::SpinInput*>(window);
    if (spin->GetTextValue() == na_value(true))
        return m_value;

    int value = spin->GetValue();
    return m_value = value;
}

void SpinCtrl::propagate_value()
{
    // check if value was really changed
    if (boost::any_cast<int>(m_value) == tmp_value)
        return;

    if (m_opt.nullable && tmp_value != ConfigOptionIntsNullable::nil_value())
        m_last_meaningful_value = tmp_value;

    if (tmp_value == UNDEF_VALUE) {
        on_kill_focus();
    } else {
        on_change_field();
    }
}

void SpinCtrl::msw_rescale()
{
    Field::msw_rescale();

    auto field = dynamic_cast<::SpinInput*>(window);
    if (parent_is_custom_ctrl)
        field->SetSize(wxSize(def_width() * m_em_unit, lround(opt_height * m_em_unit)));
    else
        field->SetMinSize(wxSize(def_width() * m_em_unit, int(1.9f*field->GetFont().GetPixelSize().y)));
}

#if 1
using choice_ctrl = ::ComboBox;
#else
#ifdef __WXOSX__
static_assert(wxMAJOR_VERSION >= 3, "Use of wxBitmapComboBox on Settings Tabs requires wxWidgets 3.0 and newer");
using choice_ctrl = wxBitmapComboBox;
#else
#ifdef _WIN32
using choice_ctrl = BitmapComboBox;
#else
using choice_ctrl = wxComboBox;
#endif
#endif // __WXOSX__
#endif

void Choice::BUILD() {
    wxSize size(def_width_wider() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	choice_ctrl* temp;
    if (m_opt.gui_type != ConfigOptionDef::GUIType::undefined 
        && m_opt.gui_type != ConfigOptionDef::GUIType::select_close) {
        m_is_editable = true;
        temp = new choice_ctrl(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxTE_PROCESS_ENTER | DD_NO_CHECK_ICON);
    }
    else {
#if 0  //#ifdef __WXOSX__
        /* wxBitmapComboBox with wxCB_READONLY style return NULL for GetTextCtrl(),
         * so ToolTip doesn't shown.
         * Next workaround helps to solve this problem
         */
        temp = new choice_ctrl();
        temp->SetTextCtrlStyle(wxTE_READONLY);
        temp->Create(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr);
#else
        temp = new choice_ctrl(m_parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
#endif //__WXOSX__
    }

#ifdef __WXGTK3__
    wxSize best_sz = temp->GetBestSize();
    if (best_sz.x > size.x)
        temp->SetSize(best_sz);
#endif //__WXGTK3__

	temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

    if (m_opt.enum_def) {
        if (auto& labels = m_opt.enum_def->labels(); !labels.empty()) {
            bool localized = m_opt.enum_def->has_labels();
            for (const std::string& el : labels)
                temp->Append(localized ? _(from_u8(el)) : from_u8(el));
            set_selection();
        }
	}

    temp->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) {
        if (m_suppress_scroll && !m_is_dropped)
            e.StopPropagation();
        else
            e.Skip();
        });
    temp->Bind(wxEVT_COMBOBOX_DROPDOWN, [this](wxCommandEvent&) { m_is_dropped = true; });
    temp->Bind(wxEVT_COMBOBOX_CLOSEUP,  [this](wxCommandEvent&) { m_is_dropped = false; });

    temp->Bind(wxEVT_COMBOBOX,          [this](wxCommandEvent&) { on_change_field(); }, temp->GetId());

    if (m_is_editable) {
        temp->Bind(wxEVT_KILL_FOCUS, [this](wxEvent& e) {
            e.Skip();
            if (!bEnterPressed)
                propagate_value();
        } );

        temp->Bind(wxEVT_TEXT_ENTER, [this](wxEvent& e) {
            EnterPressed enter(this);
            propagate_value();
        } );
    }

	temp->SetToolTip(get_tooltip_text(temp->GetValue()));
}

void Choice::propagate_value()
{
    if (m_opt.type == coStrings) {
        on_change_field();
        return;
    }

    if (is_defined_input_value<choice_ctrl>(window, m_opt.type)) {
        switch (m_opt.type) {
        case coFloatOrPercent:
        {
            std::string old_val = !m_value.empty() ? boost::any_cast<std::string>(m_value) : "";
            if (old_val == boost::any_cast<std::string>(get_value()))
                return;
            break;
        }
        case coInt:
        {
            int old_val = !m_value.empty() ? boost::any_cast<int>(m_value) : 0;
            if (old_val == boost::any_cast<int>(get_value()))
                return;
            break;
        }
        default:
        {
            double old_val = !m_value.empty() ? boost::any_cast<double>(m_value) : -99999;
            if (fabs(old_val - boost::any_cast<double>(get_value())) <= 0.0001)
                return;
        }
        }
        on_change_field();
    }
    else
        on_kill_focus();
}

void Choice::suppress_scroll()
{
    m_suppress_scroll = true;
}

void Choice::set_selection()
{
    /* To prevent earlier control updating under OSX set m_disable_change_event to true
     * (under OSX wxBitmapComboBox send wxEVT_COMBOBOX even after SetSelection())
     */
    m_disable_change_event = true;

	wxString text_value = wxString("");

    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);
	switch (m_opt.type) {
	case coEnum:{
        field->SetSelection(m_opt.default_value->getInt());
		break;
	}
	case coEnums:{
        field->SetSelection(m_opt.default_value->getInts()[m_opt_idx]);
		break;
	}
	case coFloat:
	case coPercent:	{
		double val = m_opt.default_value->getFloat();
		text_value = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 1);
		break;
	}
	case coInt:{
		text_value = wxString::Format(_T("%i"), int(m_opt.default_value->getInt()));
		break;
	}
	case coStrings:{
		text_value = m_opt.get_default_value<ConfigOptionStrings>()->get_at(m_opt_idx);
		break;
	}
	case coFloatOrPercent: {
		text_value = double_to_string(m_opt.default_value->getFloat());
		if (m_opt.get_default_value<ConfigOptionFloatOrPercent>()->percent)
			text_value += "%";
		break;
	}
    default: break;
	}

	if (!text_value.IsEmpty()) {
		if (auto opt = m_opt.enum_def->value_to_index(into_u8(text_value)); opt.has_value())
            // This enum has a value field of the same content as text_value. Select it.
            field->SetSelection(*opt);
        else
            field->SetValue(text_value);
	}
}

void Choice::set_value(const std::string& value, bool change_event)  //! Redundant?
{
	m_disable_change_event = !change_event;
    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);
    if (auto opt = m_opt.enum_def->value_to_index(value); opt.has_value())
        // This enum has a value field of the same content as text_value. Select it.
        field->SetSelection(*opt);
    else
        field->SetValue(value);
    m_disable_change_event = false;
}

void Choice::set_value(const boost::any& value, bool change_event)
{
	m_disable_change_event = !change_event;

    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);

	switch (m_opt.type) {
	case coInt:
	case coFloat:
	case coPercent:
	case coFloatOrPercent:
	case coString:
	case coStrings: {
		wxString text_value = m_opt.type == coInt ? 
            wxString::Format(_T("%i"), int(boost::any_cast<int>(value))) :
            boost::any_cast<wxString>(value);
        int sel_idx = -1;
        if (m_opt.enum_def) {
            if (auto idx = m_opt.enum_def->label_to_index(into_u8(text_value)); idx.has_value())
                sel_idx = *idx;
            else if (idx = m_opt.enum_def->value_to_index(into_u8(text_value)); idx.has_value())
                sel_idx = *idx;
        }

        if (sel_idx >= 0 )
            field->SetSelection(sel_idx);
        else {
            // For editable Combobox under OSX is needed to set selection to -1 explicitly,
            // otherwise selection doesn't be changed
            field->SetSelection(-1);
            field->SetValue(text_value);
        }

        if (!m_value.empty() && m_opt.opt_key == "fill_density") {
            // If m_value was changed before, then update m_value here too to avoid case 
            // when control's value is already changed from the ConfigManipulation::update_print_fff_config(),
            // but m_value doesn't respect it.
            if (double val; text_value.ToDouble(&val))
                m_value = val;
        }

		break;
	}
	case coEnum:
	case coEnums: {
		auto val = m_opt.enum_def->enum_to_index(boost::any_cast<int>(value));
        assert(val.has_value());
		field->SetSelection(val.has_value() ? *val : 0);
		break;
	}
	default:
		break;
	}

	m_disable_change_event = false;
}

//! it's needed for _update_serial_ports()
void Choice::set_values(const std::vector<std::string>& values)
{
	if (values.empty())
		return;
	m_disable_change_event = true;

// 	# it looks that Clear() also clears the text field in recent wxWidgets versions,
// 	# but we want to preserve it
	auto ww = dynamic_cast<choice_ctrl*>(window);
	auto value = ww->GetValue();
	ww->Clear();
	ww->Append("");
	for (const std::string& el : values)
		ww->Append(from_u8(el));
	ww->SetValue(value);

	m_disable_change_event = false;
}

void Choice::set_values(const wxArrayString &values)
{
	if (values.empty())
		return;

	m_disable_change_event = true;

	// 	# it looks that Clear() also clears the text field in recent wxWidgets versions,
	// 	# but we want to preserve it
	auto ww = dynamic_cast<choice_ctrl*>(window);
	auto value = ww->GetValue();
	ww->Clear();
//	ww->Append("");
	for (const auto &el : values)
		ww->Append(el);
	ww->SetValue(value);

	m_disable_change_event = false;
}

boost::any& Choice::get_value()
{
    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);

	wxString ret_str = field->GetValue();

	// options from right panel
	std::vector <std::string> right_panel_options{ "support", "pad", "scale_unit" };
	for (auto rp_option: right_panel_options)
		if (m_opt_id == rp_option)
			return m_value = boost::any(ret_str);

	if (m_opt.type == coEnum || m_opt.type == coEnums)
        // Closed enum: The combo box item index returned by the field must be convertible to an enum value.
        m_value = m_opt.enum_def->index_to_enum(field->GetSelection());
    else if (m_opt.gui_type == ConfigOptionDef::GUIType::f_enum_open || m_opt.gui_type == ConfigOptionDef::GUIType::i_enum_open) {
        // Open enum: The combo box item index returned by the field 
        const int ret_enum = field->GetSelection();
        if (ret_enum < 0 || ! m_opt.enum_def->has_values() || m_opt.type == coStrings ||
            (into_u8(ret_str) != m_opt.enum_def->value(ret_enum) && ret_str != _(m_opt.enum_def->label(ret_enum))))
			// modifies ret_string!
            get_value_by_opt_type(ret_str);
        else if (m_opt.type == coFloatOrPercent)
            m_value = m_opt.enum_def->value(ret_enum);
        else if (m_opt.type == coInt)
            m_value = atoi(m_opt.enum_def->value(ret_enum).c_str());
        else
            m_value = string_to_double_decimal_point(m_opt.enum_def->value(ret_enum));
    }
	else
		// modifies ret_string!
        get_value_by_opt_type(ret_str);

	return m_value;
}

void Choice::enable()  { dynamic_cast<choice_ctrl*>(window)->Enable(); }
void Choice::disable() { dynamic_cast<choice_ctrl*>(window)->Disable(); }

void Choice::msw_rescale()
{
    Field::msw_rescale();

    choice_ctrl* field = dynamic_cast<choice_ctrl*>(window);
#ifdef __WXOSX__
    const wxString selection = field->GetValue();// field->GetString(index);

	/* To correct scaling (set new controll size) of a wxBitmapCombobox
	 * we need to refill control with new bitmaps. So, in our case :
	 * 1. clear control
	 * 2. add content
	 * 3. add scaled "empty" bitmap to the at least one item
	 */
    field->Clear();
    wxSize size(wxDefaultSize);
    size.SetWidth((m_opt.width > 0 ? m_opt.width : def_width_wider()) * m_em_unit);

    // Set rescaled min height to correct layout
    field->SetMinSize(wxSize(-1, int(1.5f*field->GetFont().GetPixelSize().y + 0.5f)));
    // Set rescaled size
    field->SetSize(size);

    if (m_opt.enum_def) {
        if (auto& labels = m_opt.enum_def->labels(); !labels.empty()) {
            const bool localized = m_opt.enum_def->has_labels();
            for (const std::string& el : labels)
                field->Append(localized ? _(from_u8(el)) : from_u8(el));

            if (auto opt = m_opt.enum_def->label_to_index(into_u8(selection)); opt.has_value())
                // This enum has a value field of the same content as text_value. Select it.
                field->SetSelection(*opt);
            else
                field->SetValue(selection);
        }
    }
#else
#ifdef _WIN32
    field->Rescale();
#endif
    auto size = wxSize(def_width_wider() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height * m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width * m_em_unit);

    if (parent_is_custom_ctrl)
        field->SetSize(size);
    else
        field->SetMinSize(size);
#endif
}

void ColourPicker::BUILD()
{
	auto size = wxSize(def_width() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

	// Validate the color
	wxString clr_str(m_opt.type == coString ? m_opt.get_default_value<ConfigOptionString>()->value : m_opt.get_default_value<ConfigOptionStrings>()->get_at(m_opt_idx));
	wxColour clr(clr_str);
	if (clr_str.IsEmpty() || !clr.IsOk()) {
		clr = wxTransparentColour;
	}

	auto temp = new wxColourPickerCtrl(m_parent, wxID_ANY, clr, wxDefaultPosition, size);
    if (parent_is_custom_ctrl && m_opt.height < 0)
        opt_height = (double)temp->GetSize().GetHeight() / m_em_unit;
    temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

	wxGetApp().UpdateDarkUI(temp->GetPickerCtrl());

	// 	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	temp->Bind(wxEVT_COLOURPICKER_CHANGED, ([this](wxCommandEvent e) { on_change_field(); }), temp->GetId());

	temp->SetToolTip(get_tooltip_text(clr_str));
}

void ColourPicker::set_undef_value(wxColourPickerCtrl* field)
{
    field->SetColour(wxTransparentColour);

    wxButton* btn = dynamic_cast<wxButton*>(field->GetPickerCtrl());
    wxBitmap bmp = btn->GetBitmap();
    wxMemoryDC dc(bmp);
    if (!dc.IsOk()) return;
    dc.SetTextForeground(*wxWHITE);
    dc.SetFont(wxGetApp().normal_font());

    const wxRect rect = wxRect(0, 0, bmp.GetWidth(), bmp.GetHeight());
    dc.DrawLabel("undef", rect, wxALIGN_CENTER_HORIZONTAL|wxALIGN_CENTER_VERTICAL);

    dc.SelectObject(wxNullBitmap);
    btn->SetBitmapLabel(bmp);
}

void ColourPicker::set_value(const boost::any& value, bool change_event)
{
    m_disable_change_event = !change_event;
    const wxString clr_str(boost::any_cast<wxString>(value));
    auto field = dynamic_cast<wxColourPickerCtrl*>(window);

    wxColour clr(clr_str);
    if (clr_str.IsEmpty() || !clr.IsOk())
        set_undef_value(field);
    else
        field->SetColour(clr);

    m_disable_change_event = false;
}

boost::any& ColourPicker::get_value()
{
	auto colour = static_cast<wxColourPickerCtrl*>(window)->GetColour();
    m_value = (colour == wxTransparentColour) ? std::string("") : encode_color(ColorRGB(colour.Red(), colour.Green(), colour.Blue()));
    return m_value;
}

void ColourPicker::msw_rescale()
{
    Field::msw_rescale();

	wxColourPickerCtrl* field = dynamic_cast<wxColourPickerCtrl*>(window);
    auto size = wxSize(def_width() * m_em_unit, wxDefaultCoord);
    if (m_opt.height >= 0)
        size.SetHeight(m_opt.height * m_em_unit);
    else if (parent_is_custom_ctrl && opt_height > 0)
        size.SetHeight(lround(opt_height * m_em_unit));
    if (m_opt.width >= 0) size.SetWidth(m_opt.width * m_em_unit);
    if (parent_is_custom_ctrl)
        field->SetSize(size);
    else
        field->SetMinSize(size);

    if (field->GetColour() == wxTransparentColour)
        set_undef_value(field);
}

void ColourPicker::sys_color_changed()
{
#ifdef _WIN32
	if (wxWindow* win = this->getWindow())
		if (wxColourPickerCtrl* picker = dynamic_cast<wxColourPickerCtrl*>(win))
			wxGetApp().UpdateDarkUI(picker->GetPickerCtrl(), true);
#endif
}

PointCtrl::~PointCtrl()
{
    if (sizer && sizer->IsEmpty()) {
        delete sizer;
        sizer = nullptr;
    }
}

void PointCtrl::BUILD()
{
	auto temp = new wxBoxSizer(wxHORIZONTAL);

    const wxSize field_size(4 * m_em_unit, -1);

	auto default_pt = m_opt.get_default_value<ConfigOptionPoints>()->values.at(0);
	double val = default_pt(0);
	wxString X = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None);
	val = default_pt(1);
	wxString Y = val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None);

	long style = wxTE_PROCESS_ENTER;
	x_textctrl = new text_ctrl(m_parent, X, "", "", wxDefaultPosition, field_size, style);
	y_textctrl = new text_ctrl(m_parent, Y, "", "", wxDefaultPosition, field_size, style);
    if (parent_is_custom_ctrl && m_opt.height < 0)
        opt_height = (double)x_textctrl->GetSize().GetHeight() / m_em_unit;

    x_textctrl->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	if (!wxOSX) x_textctrl->SetBackgroundStyle(wxBG_STYLE_PAINT);
	y_textctrl->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	if (!wxOSX) y_textctrl->SetBackgroundStyle(wxBG_STYLE_PAINT);

    wxSize label_sz = wxSize(int(field_size.x / 2), field_size.y);
	auto static_text_x = new wxStaticText(m_parent, wxID_ANY, "x : ", wxDefaultPosition, label_sz, wxALIGN_RIGHT);
    auto static_text_y = new wxStaticText(m_parent, wxID_ANY, "y : ", wxDefaultPosition, label_sz, wxALIGN_RIGHT);
	static_text_x->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	static_text_x->SetBackgroundStyle(wxBG_STYLE_PAINT);
	static_text_y->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	static_text_y->SetBackgroundStyle(wxBG_STYLE_PAINT);

	wxGetApp().UpdateDarkUI(x_textctrl);
	wxGetApp().UpdateDarkUI(y_textctrl);
	wxGetApp().UpdateDarkUI(static_text_x, false, true);
	wxGetApp().UpdateDarkUI(static_text_y, false, true);

	temp->Add(static_text_x);
	temp->Add(x_textctrl);
	temp->Add(static_text_y);
	temp->Add(y_textctrl);

    x_textctrl->Bind(wxEVT_TEXT_ENTER, ([this](wxCommandEvent e) { propagate_value(x_textctrl); }), x_textctrl->GetId());
	y_textctrl->Bind(wxEVT_TEXT_ENTER, ([this](wxCommandEvent e) { propagate_value(y_textctrl); }), y_textctrl->GetId());

    x_textctrl->Bind(wxEVT_KILL_FOCUS, ([this](wxEvent& e) { e.Skip(); propagate_value(x_textctrl); }), x_textctrl->GetId());
    y_textctrl->Bind(wxEVT_KILL_FOCUS, ([this](wxEvent& e) { e.Skip(); propagate_value(y_textctrl); }), y_textctrl->GetId());

	// 	// recast as a wxWindow to fit the calling convention
	sizer = dynamic_cast<wxSizer*>(temp);

	x_textctrl->SetToolTip(get_tooltip_text(X+", "+Y));
	y_textctrl->SetToolTip(get_tooltip_text(X+", "+Y));
}

void PointCtrl::msw_rescale()
{
    Field::msw_rescale();

    wxSize field_size(4 * m_em_unit, -1);

    if (parent_is_custom_ctrl) {
        field_size.SetHeight(lround(opt_height * m_em_unit));
        x_textctrl->SetSize(field_size);
        y_textctrl->SetSize(field_size);
    }
    else {
        x_textctrl->SetMinSize(field_size);
        y_textctrl->SetMinSize(field_size);
    }
}

void PointCtrl::sys_color_changed()
{
#ifdef _WIN32
    for (wxSizerItem* item: sizer->GetChildren())
        if (item->IsWindow())
            wxGetApp().UpdateDarkUI(item->GetWindow());
#endif
}

bool PointCtrl::value_was_changed(text_ctrl* win)
{
	if (m_value.empty())
		return true;

	boost::any val = m_value;
	// update m_value!
	get_value();

	return boost::any_cast<Vec2d>(m_value) != boost::any_cast<Vec2d>(val);
}

void PointCtrl::propagate_value(text_ctrl* win)
{
    if (win->GetValue().empty())
        on_kill_focus();
	else if (value_was_changed(win))
        on_change_field();
}

void PointCtrl::set_value(const Vec2d& value, bool change_event)
{
	m_disable_change_event = !change_event;

	double val = value(0);
	x_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None));
	val = value(1);
	y_textctrl->SetValue(val - int(val) == 0 ? wxString::Format(_T("%i"), int(val)) : wxNumberFormatter::ToString(val, 2, wxNumberFormatter::Style_None));

	m_disable_change_event = false;
}

void PointCtrl::set_value(const boost::any& value, bool change_event)
{
	Vec2d pt(Vec2d::Zero());
	const Vec2d *ptf = boost::any_cast<Vec2d>(&value);
	if (!ptf)
	{
		ConfigOptionPoints* pts = boost::any_cast<ConfigOptionPoints*>(value);
		pt = pts->values.at(0);
	}
	else
		pt = *ptf;
	set_value(pt, change_event);
}

boost::any& PointCtrl::get_value()
{
	double x, y;
	if (!x_textctrl->GetValue().ToDouble(&x) ||
		!y_textctrl->GetValue().ToDouble(&y))
	{
		set_value(m_value.empty() ? Vec2d(0.0, 0.0) : m_value, true);
        show_error(m_parent, _L("Invalid numeric input."));
	}
	else
	if (m_opt.min > x || x > m_opt.max ||
		m_opt.min > y || y > m_opt.max)
	{
		if (m_opt.min > x) x = m_opt.min;
		if (x > m_opt.max) x = m_opt.max;
		if (m_opt.min > y) y = m_opt.min;
		if (y > m_opt.max) y = m_opt.max;
		set_value(Vec2d(x, y), true);

		show_error(m_parent, _L("Input value is out of range"));
	}

	return m_value = Vec2d(x, y);
}

void StaticText::BUILD()
{
	auto size = wxSize(wxDefaultSize);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

    const wxString legend = from_u8(m_opt.get_default_value<ConfigOptionString>()->value);
    auto temp = new wxStaticText(m_parent, wxID_ANY, legend, wxDefaultPosition, size, wxST_ELLIPSIZE_MIDDLE);
	temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
    temp->SetFont(wxGetApp().bold_font());

	wxGetApp().UpdateDarkUI(temp);

	// 	// recast as a wxWindow to fit the calling convention
	window = dynamic_cast<wxWindow*>(temp);

	temp->SetToolTip(get_tooltip_text(legend));
}

void StaticText::msw_rescale()
{
    Field::msw_rescale();

    auto size = wxSize(wxDefaultSize);
    if (m_opt.height >= 0) size.SetHeight(m_opt.height*m_em_unit);
    if (m_opt.width >= 0) size.SetWidth(m_opt.width*m_em_unit);

    if (size != wxDefaultSize)
    {
        wxStaticText* field = dynamic_cast<wxStaticText*>(window);
        field->SetSize(size);
        field->SetMinSize(size);
    }
}

void SliderCtrl::BUILD()
{
	auto size = wxSize(wxDefaultSize);
	if (m_opt.height >= 0) size.SetHeight(m_opt.height);
	if (m_opt.width >= 0) size.SetWidth(m_opt.width);

	auto temp = new wxBoxSizer(wxHORIZONTAL);

	auto def_val = m_opt.get_default_value<ConfigOptionInt>()->value;
	auto min = m_opt.min == -FLT_MAX ? 0   : (int)m_opt.min;
	auto max = m_opt.max ==  FLT_MAX ? 100 : INT_MAX;

	m_slider = new wxSlider(m_parent, wxID_ANY, def_val * m_scale,
							min * m_scale, max * m_scale,
							wxDefaultPosition, size);
	m_slider->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	m_slider->SetBackgroundStyle(wxBG_STYLE_PAINT);
 	wxSize field_size(40, -1);

	m_textctrl = new wxTextCtrl(m_parent, wxID_ANY, wxString::Format("%d", m_slider->GetValue()/m_scale),
								wxDefaultPosition, field_size);
	m_textctrl->SetFont(Slic3r::GUI::wxGetApp().normal_font());
	m_textctrl->SetBackgroundStyle(wxBG_STYLE_PAINT);

	temp->Add(m_slider, 1, wxEXPAND, 0);
	temp->Add(m_textctrl, 0, wxALIGN_CENTER_VERTICAL, 0);

	m_slider->Bind(wxEVT_SLIDER, ([this](wxCommandEvent e) {
		if (!m_disable_change_event) {
			int val = boost::any_cast<int>(get_value());
			m_textctrl->SetLabel(wxString::Format("%d", val));
			on_change_field();
		}
	}), m_slider->GetId());

	m_textctrl->Bind(wxEVT_TEXT, ([this](wxCommandEvent e) {
		std::string value = e.GetString().utf8_str().data();
		if (is_matched(value, "^-?\\d+(\\.\\d*)?$")) {
			m_disable_change_event = true;
			m_slider->SetValue(stoi(value)*m_scale);
			m_disable_change_event = false;
			on_change_field();
		}
	}), m_textctrl->GetId());

	m_sizer = dynamic_cast<wxSizer*>(temp);
}

void SliderCtrl::set_value(const boost::any& value, bool change_event)
{
	m_disable_change_event = !change_event;

	m_slider->SetValue(boost::any_cast<int>(value)*m_scale);
	int val = boost::any_cast<int>(get_value());
	m_textctrl->SetLabel(wxString::Format("%d", val));

	m_disable_change_event = false;
}

boost::any& SliderCtrl::get_value()
{
// 	int ret_val;
// 	x_textctrl->GetValue().ToDouble(&val);
	return m_value = int(m_slider->GetValue()/m_scale);
}


} // Slic3r :: GUI

