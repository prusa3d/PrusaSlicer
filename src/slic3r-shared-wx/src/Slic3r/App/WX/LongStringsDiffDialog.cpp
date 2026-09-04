#include "Slic3r/App/WX/LongStringsDiffDialog.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/DiffDVCModel.hpp"

#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/tokenzr.h>
#include <set>

namespace Slic3r::App::WX {

LongStringsDiffDialog::LongStringsDiffDialog(const wxString& option_name, const wxString& old_value, const wxString& mod_value, const wxString& new_value, const wxString& old_value_header, const wxString& mod_value_header, const wxString& new_value_header) :
    wxDialog(nullptr, wxID_ANY, option_name, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->SetFont(w_config()->normal_font());

    int border                = 10;
    bool has_new_value_column = !new_value_header.IsEmpty();

    wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, this);

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2, has_new_value_column ? 3 : 2, 1, 0);
    grid_sizer->SetFlexibleDirection(wxBOTH);
    for (int col = 0; col < grid_sizer->GetCols(); col++)
        grid_sizer->AddGrowableCol(col, 1);
    grid_sizer->AddGrowableRow(1, 1);

    auto add_header = [grid_sizer, border, this](wxString label)
    {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, label);
        text->SetFont(this->GetFont().Bold());
        grid_sizer->Add(text, 0, wxALL, border);
    };

    add_header(old_value_header);
    add_header(mod_value_header);
    if (has_new_value_column)
        add_header(new_value_header);

    const wxString line_end = from_u8("\n");
    auto get_set_from_val   = [line_end](wxString str)
    {
        if (str.Find(line_end) == wxNOT_FOUND)
            str.Replace(from_u8(" "), line_end);

        std::set<wxString> str_set;

        wxStringTokenizer strings(str, line_end);
        while (strings.HasMoreTokens())
            str_set.emplace(strings.GetNextToken());

        return str_set;
    };

    std::set<wxString> old_set = get_set_from_val(old_value);
    std::set<wxString> mod_set = get_set_from_val(mod_value);
    std::set<wxString> new_set = get_set_from_val(new_value);
    std::set<wxString> old_mod_diff_set;
    std::set<wxString> mod_old_diff_set;
    std::set<wxString> new_old_diff_set;

    std::set_difference(
        old_set.begin(),
        old_set.end(),
        mod_set.begin(),
        mod_set.end(),
        std::inserter(old_mod_diff_set, old_mod_diff_set.begin())
    );
    std::set_difference(
        mod_set.begin(),
        mod_set.end(),
        old_set.begin(),
        old_set.end(),
        std::inserter(mod_old_diff_set, mod_old_diff_set.begin())
    );
    std::set_difference(
        new_set.begin(),
        new_set.end(),
        old_set.begin(),
        old_set.end(),
        std::inserter(new_old_diff_set, new_old_diff_set.begin())
    );

    auto add_value = [grid_sizer, border, this](wxString label, const std::set<wxString>& diff_set, bool is_colored = false)
    {
        wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, label, wxDefaultPosition, wxSize(400, 400), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_DEFAULT | wxTE_RICH);
        text->SetStyle(0, label.Len(), wxTextAttr(is_colored ? wxColour(from_u8(ModelNode::orange())) : wxNullColour, wxNullColour, this->GetFont()));

        for (const wxString& str : diff_set) {
            int pos = label.First(str);
            if (pos == wxNOT_FOUND)
                continue;
            text->SetStyle(
                pos,
                pos + (int) str.Len(),
                wxTextAttr(
                    is_colored ? wxColour(from_u8(ModelNode::orange())) : wxNullColour,
                    wxNullColour,
                    this->GetFont().Bold()
                )
            );
        }

        grid_sizer->Add(text, 1, wxALL | wxEXPAND, border);
    };
    add_value(old_value, old_mod_diff_set);
    add_value(mod_value, mod_old_diff_set, true);
    if (has_new_value_column)
        add_value(new_value, new_old_diff_set);

    sizer->Add(grid_sizer, 1, wxEXPAND);

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(sizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(buttons, 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

} // namespace Slic3r::App::WX
