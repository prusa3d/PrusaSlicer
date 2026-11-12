#include "Slic3r/App/WX/DiffViewCtrl.hpp"
#include "Slic3r/App/WX/DiffDVCModel.hpp"
#include "Slic3r/App/WX/LongStringsDiffDialog.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/ExtraRenderers.hpp"

namespace Slic3r::App::WX {

DiffViewCtrl::DiffViewCtrl(wxWindow* parent, wxSize size) :
    wxDataViewCtrl(
        parent,
        wxID_ANY,
        wxDefaultPosition,
        size,
        wxDV_VARIABLE_LINE_HEIGHT
#ifndef __WXOSX__
            | wxDV_ROW_LINES
#endif
#ifdef _WIN32
            | wxBORDER_SIMPLE
#endif
    ),
    m_em_unit(w_config()->em_unit(/*parent*/))
{
    model = new DiffDVCModel(parent);
    this->AssociateModel(model);
    model->SetAssociatedControl(this);

    this->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &DiffViewCtrl::context_menu, this);
    this->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &DiffViewCtrl::context_menu, this);
    this->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &DiffViewCtrl::item_value_changed, this);
}

DiffViewCtrl::~DiffViewCtrl()
{
    if (model) {
        Clear();
        model->DecRef();
    }
}

void DiffViewCtrl::AppendBmpTextColumn(const wxString& label, unsigned model_column, int width, bool set_expander)
{
    m_columns_width.emplace(this->GetColumnCount(), width);
#ifdef __linux__
    wxDataViewIconTextRenderer* rd = new wxDataViewIconTextRenderer();
    rd->EnableMarkup(true);
    wxDataViewColumn* column = new wxDataViewColumn(label, rd, model_column, width * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_CELL_INERT);
#else
    wxDataViewColumn* column = new wxDataViewColumn(label, new BitmapTextRenderer(true, wxDATAVIEW_CELL_INERT), model_column, width * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE);
#endif //__linux__
    this->AppendColumn(column);
    if (set_expander)
        this->SetExpanderColumn(column);
}

void DiffViewCtrl::AppendToggleColumn_(const wxString& label, unsigned model_column, int width)
{
    m_columns_width.emplace(this->GetColumnCount(), width);
    AppendToggleColumn(label, model_column, wxDATAVIEW_CELL_ACTIVATABLE, width * m_em_unit);
}

void DiffViewCtrl::Rescale(int em)
{
    if (em > 0) {
        for (auto item : m_columns_width)
            GetColumn(item.first)->SetWidth(item.second * em);
        m_em_unit = em;
    }

    model->Rescale();
    Refresh();
}

void DiffViewCtrl::Append(const std::string& opt_key, Slic3r::Domain::Preset::PresetKind kind, const std::string& kind_name, std::string category_name, std::string group_name, std::string option_name, std::string old_value, std::string mod_value, std::string new_value, const std::string category_icon_name)
{
    ItemData item_data = {opt_key, option_name, old_value, mod_value, new_value, kind};

    std::string old_val = get_short_string(item_data.old_val);
    std::string mod_val = get_short_string(item_data.mod_val);
    std::string new_val = get_short_string(item_data.new_val);
    if (old_val != item_data.old_val || mod_val != item_data.mod_val || new_val != item_data.new_val)
        item_data.is_long = true;

    m_items_map.emplace(model->AddOption(kind, kind_name, category_name, group_name, option_name, old_val, mod_val, new_val, category_icon_name.empty() ? "question" : category_icon_name), item_data);
}

void DiffViewCtrl::Clear()
{
    model->Clear();
    m_items_map.clear();
    m_has_long_strings = false;
}

bool DiffViewCtrl::has_new_value_column()
{
    return this->GetColumnCount() == DiffDVCModel::colMax;
}

std::string DiffViewCtrl::get_short_string(const std::string& full_string)
{
    size_t max_len = 30;
    if (full_string.empty()
        || full_string.starts_with("#")
        || (full_string.find("\n") == std::string::npos && full_string.length() < max_len))
        return full_string;

    m_has_long_strings = true;

    // Break marked strings at line ends or special symbols (<, >) to parse correctly
    for (const std::string& br : std::initializer_list<std::string>{"\n", " > ", " < "}) {
        size_t n_pos = full_string.find(br);
        if (n_pos != std::string::npos && n_pos < max_len)
            max_len = n_pos;
    }
    return std::string(full_string, 0, max_len) + "..."/*dots*/;
}

void DiffViewCtrl::context_menu(wxDataViewEvent& event)
{
    if (!m_has_long_strings)
        return;

    wxDataViewItem item = event.GetItem();
    if (!item) {
        wxPoint mouse_pos     = wxGetMousePosition() - this->GetScreenPosition();
        wxDataViewColumn* col = nullptr;
        this->HitTest(mouse_pos, item, col);

        if (!item)
            item = this->GetSelection();

        if (!item)
            return;
    }

    auto it = m_items_map.find(item);
    if (it == m_items_map.end() || !it->second.is_long)
        return;

    const wxString old_value_header = this->GetColumn(DiffDVCModel::colOldValue)->GetTitle();
    const wxString mod_value_header = this->GetColumn(DiffDVCModel::colModValue)->GetTitle();
    const wxString
        new_value_header = has_new_value_column() ? this->GetColumn(DiffDVCModel::colNewValue)->GetTitle() : wxString();
    LongStringsDiffDialog(
        from_u8(it->second.opt_name),
        from_u8(it->second.old_val),
        from_u8(it->second.mod_val),
        from_u8(it->second.new_val),
        old_value_header,
        mod_value_header,
        new_value_header
    )
        .ShowModal();

#ifdef __WXOSX__
    wxWindow* parent = this->GetParent();
    if (parent && parent->IsShown()) {
        // if this dialog is shown it have to be Hide and show again to be placed on the very Top of windows
        parent->Hide();
        parent->Show();
    }
#endif // __WXOSX__
}

void DiffViewCtrl::item_value_changed(wxDataViewEvent& event)
{
    if (event.GetColumn() != DiffDVCModel::colToggle)
        return;

    wxDataViewItem item = event.GetItem();
    update_item_enabling(item);
}

void DiffViewCtrl::update_item_enabling(wxDataViewItem item)
{
    model->UpdateItemEnabling(item);
    Refresh();

    // update an enabling of the "save/move" buttons
    m_empty_selection = selected_options().empty();
}

std::vector<std::string> DiffViewCtrl::options(Slic3r::Domain::Preset::PresetKind kind, bool selected)
{
    std::vector<std::string> ret;

    for (auto item : m_items_map) {
        if (item.second.kind == kind && model->IsEnabledItem(item.first) == selected) {
            ret.emplace_back(item.second.opt_key);
        }
    }

    return ret;
}

std::vector<std::string> DiffViewCtrl::selected_options()
{
    std::vector<std::string> ret;

    for (auto item : m_items_map) {
        if (model->IsEnabledItem(item.first)) {
            ret.emplace_back(item.second.opt_key);
        }
    }

    return ret;
}

std::vector<std::string> DiffViewCtrl::unselected_options()
{
    std::vector<std::string> ret;

    for (auto item : m_items_map) {
        if (!model->IsEnabledItem(item.first)) {
            ret.emplace_back(item.second.opt_key);
        }
    }
    return ret;
}

} // namespace Slic3r::App::WX
