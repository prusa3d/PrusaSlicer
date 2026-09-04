#pragma once

#include "Slic3r/Domain/Preset/Types.hpp"

#include <wx/dataview.h>
#include <string>

namespace Slic3r::App::WX {
// ----------------------------------------------------------------------------
// DiffViewCtrl
// ----------------------------------------------------------------------------

class DiffDVCModel;

class DiffViewCtrl : public wxDataViewCtrl
{
    bool m_has_long_strings{false};
    bool m_empty_selection{false};
    int m_em_unit;

    struct ItemData
    {
        std::string opt_key;
        std::string opt_name;
        std::string old_val;
        std::string mod_val;
        std::string new_val;
        Slic3r::Domain::Preset::PresetKind kind;
        bool is_long{false};
    };

    // tree items related to the options
    std::map<wxDataViewItem, ItemData> m_items_map;
    std::map<unsigned int, int> m_columns_width;

public:
    DiffViewCtrl(wxWindow* parent, wxSize size);
    ~DiffViewCtrl() override;

    DiffDVCModel* model{nullptr};

    void AppendBmpTextColumn(const wxString& label, unsigned model_column, int width, bool set_expander = false);
    void AppendToggleColumn_(const wxString& label, unsigned model_column, int width);
    void Rescale(int em = 0);
    void
    Append(const std::string& opt_key, Slic3r::Domain::Preset::PresetKind kind, const std::string& kind_name, std::string category_name, std::string group_name, std::string option_name, std::string old_value, std::string mod_value, std::string new_value, const std::string category_icon_name);
    void Clear();

    bool has_selection()
    {
        return !m_empty_selection;
    }

    void set_em_unit(int em)
    {
        m_em_unit = em;
    }

    bool has_long_strings()
    {
        return m_has_long_strings;
    }

    bool has_new_value_column();

    std::string get_short_string(const std::string& full_string);

    void context_menu(wxDataViewEvent& event);
    void item_value_changed(wxDataViewEvent& event);

    void update_item_enabling(wxDataViewItem item);

    std::vector<std::string> options(Slic3r::Domain::Preset::PresetKind kind, bool selected);
    std::vector<std::string> selected_options();
    std::vector<std::string> unselected_options();
};
} // namespace Slic3r::App::WX
