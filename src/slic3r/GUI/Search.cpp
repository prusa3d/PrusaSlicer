///|/ Copyright (c) Prusa Research 2020 - 2023 Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "Search.hpp"

#include <cstddef>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/nowide/convert.hpp>

#include "wx/dataview.h"
#include "wx/numformatter.h"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "MainFrame.hpp"
#include "Tab.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "ExtraRenderers.hpp"
#include "fts_fuzzy_match.h"

#include "imgui/imconfig.h"

using boost::optional;

namespace Slic3r {

wxDEFINE_EVENT(wxCUSTOMEVT_JUMP_TO_OPTION, wxCommandEvent);

using GUI::from_u8;
using GUI::into_u8;

namespace Search {

static char marker_by_type(Preset::Type type, PrinterTechnology pt)
{
    switch(type) {
    case Preset::TYPE_PRINT:
    case Preset::TYPE_SLA_PRINT:
        return ImGui::PrintIconMarker;
    case Preset::TYPE_FILAMENT:
        return ImGui::FilamentIconMarker;
    case Preset::TYPE_SLA_MATERIAL:
        return ImGui::MaterialIconMarker;
    case Preset::TYPE_PRINTER:
        return pt == ptSLA ? ImGui::PrinterSlaIconMarker : ImGui::PrinterIconMarker;
    case Preset::TYPE_PREFERENCES:
        return ImGui::PreferencesButton;
    default:
        return ' ';
	}
}

std::string Option::opt_key() const
{
    return into_u8(key).substr(2);
}

void FoundOption::get_marked_label_and_tooltip(const char** label_, const char** tooltip_) const
{
    *label_   = marked_label.c_str();
    *tooltip_ = tooltip.c_str();
}

template<class T>
//void change_opt_key(std::string& opt_key, DynamicPrintConfig* config)
void change_opt_key(std::string& opt_key, DynamicPrintConfig* config, int& cnt)
{
    T* opt_cur = static_cast<T*>(config->option(opt_key));
    cnt = opt_cur->values.size();
    return;

    if (opt_cur->values.size() > 0)
        opt_key += "#" + std::to_string(0);
}

static std::string get_key(const std::string& opt_key, Preset::Type type)
{
    return std::to_string(int(type)) + ";" + opt_key;
}

void OptionsSearcher::append_options(DynamicPrintConfig* config, Preset::Type type)
{
    auto emplace = [this, type](std::string key, const wxString& label, int id = -1)
    {
        if (id >= 0)
            // ! It's very important to use "#". opt_key#n is a real option key used in GroupAndCategory
            key += "#" + std::to_string(id);

        const GroupAndCategory& gc = groups_and_categories[key];
        if (gc.group.IsEmpty() || gc.category.IsEmpty())
            return;

        wxString suffix;
        wxString suffix_local;
        if (gc.category == "Machine limits" || gc.category == "Material printing profile") {
            if (gc.category == "Machine limits")
                suffix = id == 1 ? L("Stealth") : L("Normal");
            else 
                suffix = id == 1 ? L("Above") : L("Below");
            suffix_local = " " + _(suffix);
            suffix = " " + suffix;
        }
        else if (gc.group == "Dynamic overhang speed" && id >= 0) {
            suffix = " " + std::to_string(id+1);
            suffix_local = suffix;
        }

        if (!label.IsEmpty())
            options.emplace_back(Option{ boost::nowide::widen(key), type,
                                        (label + suffix).ToStdWstring(), (_(label) + suffix_local).ToStdWstring(),
                                        gc.group.ToStdWstring(), _(gc.group).ToStdWstring(),
                                        gc.category.ToStdWstring(), GUI::Tab::translate_category(gc.category, type).ToStdWstring() });
    };

    for (std::string opt_key : config->keys())
    {
        const ConfigOptionDef& opt = *config->option_def(opt_key);
        if (opt.mode > mode)
            continue;

        int cnt = 0;

        if ( type != Preset::TYPE_FILAMENT && !PresetCollection::is_independent_from_extruder_number_option(opt_key))
            switch (config->option(opt_key)->type())
            {
            case coInts:	change_opt_key<ConfigOptionInts		>(opt_key, config, cnt);	break;
            case coBools:	change_opt_key<ConfigOptionBools	>(opt_key, config, cnt);	break;
            case coFloats:	change_opt_key<ConfigOptionFloats	>(opt_key, config, cnt);	break;
            case coStrings:	change_opt_key<ConfigOptionStrings	>(opt_key, config, cnt);	break;
            case coPercents:change_opt_key<ConfigOptionPercents	>(opt_key, config, cnt);	break;
            case coPoints:	change_opt_key<ConfigOptionPoints	>(opt_key, config, cnt);	break;
            case coFloatsOrPercents:	change_opt_key<ConfigOptionFloatsOrPercents	>(opt_key, config, cnt);	break;
            case coEnums:	change_opt_key<ConfigOptionEnumsGeneric>(opt_key, config, cnt);	break;

            default:		break;
            }

        wxString label = opt.full_label.empty() ? opt.label : opt.full_label;

        std::string key = get_key(opt_key, type);
        if (cnt == 0)
            emplace(key, label);
        else
            for (int i = 0; i < cnt; ++i)
                emplace(key, label, i);
    }
}

// Mark a string using ColorMarkerStart and ColorMarkerEnd symbols
static std::wstring mark_string(const std::wstring &str, const std::vector<uint16_t> &matches, Preset::Type type, PrinterTechnology pt)
{
	std::wstring out;
    out += marker_by_type(type, pt);
	if (matches.empty())
		out += str;
	else {
		out.reserve(str.size() * 2);
		if (matches.front() > 0)
			out += str.substr(0, matches.front());
		for (size_t i = 0;;) {
			// Find the longest string of successive indices.
			size_t j = i + 1;
            while (j < matches.size() && matches[j] == matches[j - 1] + 1)
                ++ j;
            out += ImGui::ColorMarkerStart;
            out += str.substr(matches[i], matches[j - 1] - matches[i] + 1);
            out += ImGui::ColorMarkerEnd;
            if (j == matches.size()) {
				out += str.substr(matches[j - 1] + 1);
				break;
			}
            out += str.substr(matches[j - 1] + 1, matches[j] - matches[j - 1] - 1);
            i = j;
		}
	}
	return out;
}

bool OptionsSearcher::search()
{
    return search(search_line, true);
}

static bool fuzzy_match(const std::wstring &search_pattern, const std::wstring &label, int& out_score, std::vector<uint16_t> &out_matches)
{
    uint16_t matches[fts::max_matches + 1]; // +1 for the stopper
    int score;
    if (fts::fuzzy_match(search_pattern.c_str(), label.c_str(), score, matches)) {
	    size_t cnt = 0;
	    for (; matches[cnt] != fts::stopper; ++cnt);
	    out_matches.assign(matches, matches + cnt);
		out_score = score;
		return true;
	} else
		return false;
}

bool OptionsSearcher::search(const std::string& search, bool force/* = false*/)
{
    if (search_line == search && !force)
        return false;

    found.clear();

    bool full_list = search.empty();
    std::wstring sep = L" : ";

    auto get_label = [this, &sep](const Option& opt, bool marked = true)
    {
        std::wstring out;
        if (marked)
            out += marker_by_type(opt.type, printer_technology);
    	const std::wstring *prev = nullptr;
    	for (const std::wstring * const s : {
	        view_params.category 	? &opt.category_local 		: nullptr,
	        &opt.group_local, &opt.label_local })
    		if (s != nullptr && (prev == nullptr || *prev != *s)) {
      			if (out.size()>2)
    				out += sep;
    			out += *s;
    			prev = s;
    		}
        return out;
    };

    auto get_label_english = [this, &sep](const Option& opt, bool marked = true)
    {
        std::wstring out;
        if (marked)
            out += marker_by_type(opt.type, printer_technology);
    	const std::wstring*prev = nullptr;
    	for (const std::wstring * const s : {
	        view_params.category 	? &opt.category 			: nullptr,
	        &opt.group, &opt.label })
    		if (s != nullptr && (prev == nullptr || *prev != *s)) {
      			if (out.size()>2)
    				out += sep;
    			out += *s;
    			prev = s;
    		}
        return out;
    };

    auto get_tooltip = [this, &sep](const Option& opt) -> wxString
    {
        return  marker_by_type(opt.type, printer_technology) +
                opt.category_local + sep +
                opt.group_local + sep + opt.label_local;
    };

    std::vector<uint16_t> matches, matches2;
    for (size_t i=0; i < options.size(); i++)
    {
        const Option &opt = options[i];
        if (full_list) {
            std::string label = into_u8(get_label(opt));
            found.emplace_back(FoundOption{ label, label, into_u8(get_tooltip(opt)), i, 0 });
            continue;
        }

        std::wstring wsearch       = boost::nowide::widen(search);
        boost::trim_left(wsearch);
        std::wstring label         = get_label(opt, false);
        std::wstring label_english = get_label_english(opt, false);
        int score = std::numeric_limits<int>::min();
        int score2;
        matches.clear();
        fuzzy_match(wsearch, label, score, matches);
        if (fuzzy_match(wsearch, opt.key, score2, matches2) && score2 > score) {
        	for (fts::pos_type &pos : matches2)
        		pos += label.size() + 1;
        	label += L"(" + opt.key + L")";
        	append(matches, matches2);
        	score = score2;
        }
        if (view_params.english && fuzzy_match(wsearch, label_english, score2, matches2) && score2 > score) {
        	label   = std::move(label_english);
        	matches = std::move(matches2);
        	score   = score2;
        }
        if (score > 90/*std::numeric_limits<int>::min()*/) {
		    label = mark_string(label, matches, opt.type, printer_technology);
            label += L"  [" + std::to_wstring(score) + L"]";// add score value
	        std::string label_u8 = into_u8(label);
	        std::string label_plain = label_u8;

#ifdef SUPPORTS_MARKUP
            boost::replace_all(label_plain, std::string(1, char(ImGui::ColorMarkerStart)), "<b>");
            boost::replace_all(label_plain, std::string(1, char(ImGui::ColorMarkerEnd)),   "</b>");
#else
            boost::erase_all(label_plain, std::string(1, char(ImGui::ColorMarkerStart)));
            boost::erase_all(label_plain, std::string(1, char(ImGui::ColorMarkerEnd)));
#endif
            found.emplace_back(FoundOption{ label_plain, label_u8, into_u8(get_tooltip(opt)), i, score });
        }
    }

    if (!full_list)
        sort_found();
 
    if (search_line != search)
        search_line = search;

    return true;
}

OptionsSearcher::OptionsSearcher()
{
    default_string = _L("Enter a search term");
}

OptionsSearcher::~OptionsSearcher()
{
}

void OptionsSearcher::check_and_update(PrinterTechnology pt_in, ConfigOptionMode mode_in, std::vector<InputInfo> input_values)
{
    if (printer_technology == pt_in && mode == mode_in)
        return;

    options.clear();

    printer_technology = pt_in;
    mode = mode_in;

    for (auto i : input_values)
        append_options(i.config, i.type);

    options.insert(options.end(), preferences_options.begin(), preferences_options.end());

    sort_options();

    search(search_line, true);
}

void OptionsSearcher::append_preferences_option(const GUI::Line& opt_line)
{
    Preset::Type type = Preset::TYPE_PREFERENCES;
    wxString label = opt_line.label;
    if (label.IsEmpty())
        return;

    std::string key = get_key(opt_line.get_options().front().opt_id, type);
    const GroupAndCategory& gc = groups_and_categories[key];
    if (gc.group.IsEmpty() || gc.category.IsEmpty())
        return;        
        
    preferences_options.emplace_back(Search::Option{ boost::nowide::widen(key), type,
                                label.ToStdWstring(), _(label).ToStdWstring(),
                                gc.group.ToStdWstring(), _(gc.group).ToStdWstring(),
                                gc.category.ToStdWstring(), _(gc.category).ToStdWstring() });
}

void OptionsSearcher::append_preferences_options(const std::vector<GUI::Line>& opt_lines)
{
    for (const GUI::Line& line : opt_lines) {
        if (line.is_separator())
            continue;
        append_preferences_option(line);
    }
}

const Option& OptionsSearcher::get_option(size_t pos_in_filter) const
{
    assert(pos_in_filter != size_t(-1) && found[pos_in_filter].option_idx != size_t(-1));
    return options[found[pos_in_filter].option_idx];
}

const Option& OptionsSearcher::get_option(const std::string& opt_key, Preset::Type type) const
{
    auto it = std::lower_bound(options.begin(), options.end(), Option({ boost::nowide::widen(get_key(opt_key, type)) }));
    assert(it != options.end());

    return options[it - options.begin()];
}

static Option create_option(const std::string& opt_key, const wxString& label, Preset::Type type, const GroupAndCategory& gc)
{
    wxString suffix;
    wxString suffix_local;
    if (gc.category == "Machine limits") {
        suffix = opt_key.back() == '1' ? L("Stealth") : L("Normal");
        suffix_local = " " + _(suffix);
        suffix = " " + suffix;
    }

    wxString category = gc.category;
    if (type == Preset::TYPE_PRINTER && category.Contains("Extruder ")) {
        std::string opt_idx = opt_key.substr(opt_key.find("#") + 1);
        category = wxString::Format("%s %d", "Extruder", atoi(opt_idx.c_str()) + 1);
    }

    return Option{ boost::nowide::widen(get_key(opt_key, type)), type,
                (label + suffix).ToStdWstring(), (_(label) + suffix_local).ToStdWstring(),
                gc.group.ToStdWstring(), _(gc.group).ToStdWstring(),
                gc.category.ToStdWstring(), GUI::Tab::translate_category(category, type).ToStdWstring() };
}

Option OptionsSearcher::get_option(const std::string& opt_key, const wxString& label, Preset::Type type) const
{
    std::string key = get_key(opt_key, type);
    auto it = std::lower_bound(options.begin(), options.end(), Option({ boost::nowide::widen(key) }));
    if(it->key == boost::nowide::widen(key))
        return options[it - options.begin()];
    if (groups_and_categories.find(key) == groups_and_categories.end()) {
        size_t pos = key.find('#');
        if (pos == std::string::npos)
            return options[it - options.begin()];

        std::string zero_opt_key = key.substr(0, pos + 1) + "0";

        if(groups_and_categories.find(zero_opt_key) == groups_and_categories.end())
            return options[it - options.begin()];

        return create_option(opt_key, label, type, groups_and_categories.at(zero_opt_key));
    }

    const GroupAndCategory& gc = groups_and_categories.at(key);
    if (gc.group.IsEmpty() || gc.category.IsEmpty())
        return options[it - options.begin()];

    return create_option(opt_key, label, type, gc);
}

static bool has_focus(wxWindow* win)
{
    if (win->HasFocus())
        return true;

    auto children = win->GetChildren();
    for (auto child : children) {
        if (has_focus(child))
            return true;
    }

    return false;
}

void OptionsSearcher::update_dialog_position()
{
    if (search_dialog) {
        wxPoint old_pos = search_dialog->GetPosition();
        wxPoint pos = search_input->GetScreenPosition() + wxPoint(-5, search_input->GetSize().y);
        if (old_pos != pos)
            search_dialog->SetPosition(pos);
    }
}

void OptionsSearcher::check_and_hide_dialog()
{
#ifdef __linux__
    // Temporary linux specific workaround:
    // has_focus(search_dialog) always returns false
    // That's why search dialog will be hidden whole the time
    return;
#endif
    if (search_dialog && search_dialog->IsShown() && !has_focus(search_dialog))
        show_dialog(false);
}

void OptionsSearcher::set_focus_to_parent()
{
    if (search_input)
        search_input->GetParent()->SetFocus();
}

void OptionsSearcher::show_dialog(bool show /*= true*/)
{
    if (search_dialog && !show) {
        search_dialog->Hide();
        return;
    }

    if (!search_dialog) {
        search_dialog = new SearchDialog(this, search_input);

        search_dialog->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e)
        {
            if (search_dialog->IsShown() && !search_input->HasFocus())
                show_dialog(false);
            e.Skip();
        });
    }
    update_dialog_position();

    search_string();
    search_input->SetSelection(-1,-1);

    search_dialog->Popup();
    if (!search_input->HasFocus())
        search_input->SetFocus();
}

void OptionsSearcher::dlg_sys_color_changed()
{
    if (search_dialog)
        search_dialog->on_sys_color_changed();
}

void OptionsSearcher::dlg_msw_rescale()
{
    if (search_dialog)
        search_dialog->msw_rescale();
}

void OptionsSearcher::edit_search_input()
{
    if (!search_input)
        return;

    if (search_dialog) {
        search_dialog->input_text(search_input->GetValue());
        if (!search_dialog->IsShown())
            search_dialog->Popup();
    }
    else
        GUI::wxGetApp().show_search_dialog();
}

void OptionsSearcher::process_key_down_from_input(wxKeyEvent& e)
{
    int key = e.GetKeyCode();
    if (key == WXK_ESCAPE)
        search_dialog->Hide();
    else if (search_dialog && (key == WXK_UP || key == WXK_DOWN || key == WXK_NUMPAD_ENTER || key == WXK_RETURN)) {
        search_dialog->KeyDown(e);
#ifdef __linux__
        search_dialog->SetFocus();
#endif // __linux__
    }
}

void OptionsSearcher::set_search_input(TextInput* input_ctrl)
{
    search_input = input_ctrl;
    update_dialog_position();
}

void OptionsSearcher::add_key(const std::string& opt_key, Preset::Type type, const wxString& group, const wxString& category)
{
    groups_and_categories[get_key(opt_key, type)] = GroupAndCategory{group, category};
}


//------------------------------------------
//          SearchDialog
//------------------------------------------

static const std::map<const char, int> icon_idxs = {
    {ImGui::PrintIconMarker     , 0},
    {ImGui::PrinterIconMarker   , 1},
    {ImGui::PrinterSlaIconMarker, 2},
    {ImGui::FilamentIconMarker  , 3},
    {ImGui::MaterialIconMarker  , 4},
    {ImGui::PreferencesButton   , 5},
};

SearchDialog::SearchDialog(OptionsSearcher* searcher, wxWindow* parent)
    : GUI::DPIDialog(parent ? parent : GUI::wxGetApp().tab_panel(), wxID_ANY, _L("Search"), wxDefaultPosition, wxDefaultSize, wxSTAY_ON_TOP | wxRESIZE_BORDER),
    searcher(searcher)
{
    SetFont(GUI::wxGetApp().normal_font());
#if _WIN32
    GUI::wxGetApp().UpdateDarkUI(this);
#elif __WXGTK__
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

    int border = 10;
    int em = em_unit();

    search_list = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(em * 40, em * 30), wxDV_NO_HEADER | wxDV_SINGLE
#ifdef _WIN32
        | wxBORDER_SIMPLE
#endif
    );
    GUI::wxGetApp().UpdateDarkUI(search_list);
    search_list_model = new SearchListModel(this);
    search_list->AssociateModel(search_list_model);

#ifdef __WXMSW__
    search_list->AppendColumn(new wxDataViewColumn("", new BitmapTextRenderer(true, wxDATAVIEW_CELL_INERT), SearchListModel::colIconMarkedText, wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT));
    search_list->GetColumn(SearchListModel::colIconMarkedText)->SetWidth(48  * em_unit());
#else
    search_list->AppendBitmapColumn("", SearchListModel::colIcon);

    wxDataViewTextRenderer* const markupRenderer = new wxDataViewTextRenderer();

#ifdef SUPPORTS_MARKUP
    markupRenderer->EnableMarkup();
#endif

    search_list->AppendColumn(new wxDataViewColumn("", markupRenderer, SearchListModel::colMarkedText, wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT));

    search_list->GetColumn(SearchListModel::colIcon      )->SetWidth(3  * em_unit());
    search_list->GetColumn(SearchListModel::colMarkedText)->SetWidth(40 * em_unit());
#endif

    wxBoxSizer* check_sizer = new wxBoxSizer(wxHORIZONTAL);

    check_category  = new ::CheckBox(this, _L("Category"));
    if (GUI::wxGetApp().is_localized())
        check_english   = new ::CheckBox(this, _L("Search in English"));

    wxStdDialogButtonSizer* cancel_btn = this->CreateStdDialogButtonSizer(wxCANCEL);
    GUI::wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)));

    check_sizer->Add(new wxStaticText(this, wxID_ANY, _L("Use for search") + ":"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->Add(check_category, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    if (check_english)
        check_sizer->Add(check_english,  0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);
    check_sizer->AddStretchSpacer(border);
    check_sizer->Add(cancel_btn,     0, wxALIGN_CENTER_VERTICAL);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(search_list, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(check_sizer, 0, wxEXPAND | wxALL, border);

    search_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &SearchDialog::OnSelect,    this);
    search_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,    &SearchDialog::OnActivate,  this);
#ifdef __WXMSW__
    search_list->GetMainWindow()->Bind(wxEVT_MOTION,    &SearchDialog::OnMotion,    this);
    search_list->GetMainWindow()->Bind(wxEVT_LEFT_DOWN, &SearchDialog::OnLeftDown, this);
#endif //__WXMSW__

    // Under OSX mouse and key states didn't fill after wxEVT_DATAVIEW_SELECTION_CHANGED call
    // As a result, we can't to identify what kind of actions was done
    // So, under OSX is used OnKeyDown function to navigate inside the list
#ifdef __APPLE__
    search_list->Bind(wxEVT_KEY_DOWN, &SearchDialog::OnKeyDown, this);
#endif

    check_category->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);
    if (check_english)
        check_english ->Bind(wxEVT_CHECKBOX, &SearchDialog::OnCheck, this);

//    Bind(wxEVT_MOTION, &SearchDialog::OnMotion, this);
//    Bind(wxEVT_LEFT_DOWN, &SearchDialog::OnLeftDown, this);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

SearchDialog::~SearchDialog()
{
    if (search_list_model)
        search_list_model->DecRef();
}

void SearchDialog::Popup(wxPoint position /*= wxDefaultPosition*/)
{
    update_list();

    const OptionViewParameters& params = searcher->view_params;
    check_category->SetValue(params.category);
    if (check_english)
        check_english->SetValue(params.english);

    if (position != wxDefaultPosition)
        this->SetPosition(position);
#ifdef __APPLE__
    this->ShowWithoutActivating();
#else
    this->Show();
#endif
}

void SearchDialog::ProcessSelection(wxDataViewItem selection)
{
    if (!selection.IsOk())
        return;
    this->Hide();

    // If call GUI::wxGetApp().sidebar.jump_to_option() directly from here,
    // then mainframe will not have focus and found option will not be "active" (have cursor) as a result
    // SearchDialog have to be closed and have to lose a focus
    // and only after that jump_to_option() function can be called
    // So, post event to plater: 
    wxCommandEvent event(wxCUSTOMEVT_JUMP_TO_OPTION);
    event.SetInt(search_list_model->GetRow(selection));
    wxPostEvent(GUI::wxGetApp().mainframe, event);
}

void SearchDialog::input_text(wxString input_string)
{
    if (input_string == searcher->default_string)
        input_string.Clear();

    searcher->search(into_u8(input_string));

    update_list();
}

void SearchDialog::OnKeyDown(wxKeyEvent& event)
{
    int key = event.GetKeyCode();

    // change selected item in the list
    if (key == WXK_UP || key == WXK_DOWN)
    {
        // So, for the next correct navigation, set focus on the search_list
 //       search_list->SetFocus(); // #ys_delete_after_test -> Looks like no need anymore

        auto item = search_list->GetSelection();

        if (item.IsOk()) {
            unsigned selection = search_list_model->GetRow(item);

            if (key == WXK_UP && selection > 0)
                selection--;
            if (key == WXK_DOWN && selection < unsigned(search_list_model->GetCount() - 1))
                selection++;

            prevent_list_events = true;
            search_list->Select(search_list_model->GetItem(selection));
            prevent_list_events = false;
        }
    }
    // process "Enter" pressed
    else if (key == WXK_NUMPAD_ENTER || key == WXK_RETURN)
        ProcessSelection(search_list->GetSelection());
    else
        event.Skip(); // !Needed to have EVT_CHAR generated as well
}

void SearchDialog::OnActivate(wxDataViewEvent& event)
{
    ProcessSelection(event.GetItem());
}

void SearchDialog::OnSelect(wxDataViewEvent& event)
{
    // To avoid selection update from Select() under osx
    if (prevent_list_events)
        return;    

    // Under OSX mouse and key states didn't fill after wxEVT_DATAVIEW_SELECTION_CHANGED call
    // As a result, we can't to identify what kind of actions was done
    // So, under OSX is used OnKeyDown function to navigate inside the list
#ifndef __APPLE__
    // wxEVT_DATAVIEW_SELECTION_CHANGED is processed, when selection is changed after mouse click or press the Up/Down arrows
    // But this two cases should be processed in different way:
    // Up/Down arrows   -> leave it as it is (just a navigation)
    // LeftMouseClick   -> call the ProcessSelection function  
    if (wxGetMouseState().LeftIsDown())
#endif //__APPLE__
        ProcessSelection(search_list->GetSelection());
}

void SearchDialog::update_list()
{
    // Under OSX model->Clear invoke wxEVT_DATAVIEW_SELECTION_CHANGED, so
    // set prevent_list_events to true already here 
    prevent_list_events = true;
    search_list_model->Clear();

    const std::vector<FoundOption>& filters = searcher->found_options();
    for (const FoundOption& item : filters)
        search_list_model->Prepend(item.label);

    // select first item, if search_list
    if (search_list_model->GetCount() > 0)
        search_list->Select(search_list_model->GetItem(0));
    prevent_list_events = false;
}

void SearchDialog::OnCheck(wxCommandEvent& event)
{
    OptionViewParameters& params = searcher->view_params;
    if (check_english)
        params.english  = check_english->GetValue();
    params.category = check_category->GetValue();

    searcher->search();
    update_list();
}

void SearchDialog::OnMotion(wxMouseEvent& event)
{
    wxDataViewItem    item;
    wxDataViewColumn* col;
    wxWindow* win = this;
#ifdef __WXMSW__
    win = search_list;
#endif
    search_list->HitTest(wxGetMousePosition() - win->GetScreenPosition(), item, col);
    search_list->Select(item);

    event.Skip();
}

void SearchDialog::OnLeftDown(wxMouseEvent& event)
{
    ProcessSelection(search_list->GetSelection());
}

void SearchDialog::msw_rescale()
{
    const int& em = em_unit();
#ifdef __WXMSW__
    search_list->GetColumn(SearchListModel::colIconMarkedText)->SetWidth(48  * em);
#else
    search_list->GetColumn(SearchListModel::colIcon      )->SetWidth(3  * em);
    search_list->GetColumn(SearchListModel::colMarkedText)->SetWidth(45 * em);
#endif
    const wxSize& size = wxSize(40 * em, 30 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void SearchDialog::on_sys_color_changed()
{
#ifdef _WIN32
    GUI::wxGetApp().UpdateAllStaticTextDarkUI(this);
    GUI::wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)), true);
    for (wxWindow* win : std::vector<wxWindow*> {search_list, check_category, check_english})
        if (win) GUI::wxGetApp().UpdateDarkUI(win);
#endif

    // msw_rescale updates just icons, so use it
    search_list_model->sys_color_changed();

    Refresh();
}

// ----------------------------------------------------------------------------
// SearchListModel
// ----------------------------------------------------------------------------

SearchListModel::SearchListModel(wxWindow* parent) : wxDataViewVirtualListModel(0)
{
    int icon_id = 0;
    for (const std::string& icon : { "cog", "printer", "sla_printer", "spool", "resin", "notification_preferences" })
        m_icon[icon_id++] = ScalableBitmap(parent, icon);    
}

void SearchListModel::Clear()
{
    m_values.clear();
    Reset(0);
}

void SearchListModel::Prepend(const std::string& label)
{
    const char icon_c = label.at(0);
    int icon_idx = icon_idxs.at(icon_c);
    wxString str = from_u8(label).Remove(0, 1);

    m_values.emplace_back(str, icon_idx);

    RowPrepended();
}

void SearchListModel::sys_color_changed()
{
    for (ScalableBitmap& bmp : m_icon)
        bmp.sys_color_changed();
}

wxString SearchListModel::GetColumnType(unsigned int col) const 
{
#ifdef __WXMSW__
    if (col == colIconMarkedText)
        return "DataViewBitmapText";
#else
    if (col == colIcon)
        return "wxBitmap";
#endif
    return "string";
}

void SearchListModel::GetValueByRow(wxVariant& variant,
    unsigned int row, unsigned int col) const
{
    switch (col)
    {
#ifdef __WXMSW__
    case colIconMarkedText: {
        const ScalableBitmap& icon = m_icon[m_values[row].second];
        variant << DataViewBitmapText(m_values[row].first, icon.bmp().GetBitmapFor(icon.parent()));
        break;
    }
#else
    case colIcon: 
        variant << m_icon[m_values[row].second].bmp().GetBitmapFor(m_icon[m_values[row].second].parent());
        break;
    case colMarkedText:
        variant = m_values[row].first;
        break;
#endif
    case colMax:
        wxFAIL_MSG("invalid column");
    default:
        break;
    }
}


}

}    // namespace Slic3r::GUI
