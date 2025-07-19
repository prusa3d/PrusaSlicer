///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966, David Kocík @kocikdav
///|/ Copyright (c) 2021 Pascal de Bruijn @pmjdebruijn
///|/ Copyright (c) 2021 Sebastian Hammerl
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "UnsavedChangesDialog.hpp"

#include <cstddef>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>

#include <wx/tokenzr.h>

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Color.hpp"
#include "slic3r/GUI/Sidebar.hpp" // IWYU pragma: keep
#include "format.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include "ExtraRenderers.hpp"
#include "wxExtensions.hpp"
#include "SavePresetDialog.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"

#include "PresetComboBoxes.hpp"

using boost::optional;

#ifdef __linux__
#define wxLinux true
#else
#define wxLinux false
#endif

namespace Slic3r {

namespace GUI {

// ----------------------------------------------------------------------------
//                  ModelNode: a node inside DiffModel
// ----------------------------------------------------------------------------

static const std::map<Preset::Type, std::string> type_icon_names = {
    {Preset::TYPE_PRINT,        "cog"           },
    {Preset::TYPE_SLA_PRINT,    "cog"           },
    {Preset::TYPE_FILAMENT,     "spool"         },
    {Preset::TYPE_SLA_MATERIAL, "resin"         },
    {Preset::TYPE_PRINTER,      "printer"       },
};

static std::string get_icon_name(Preset::Type type, PrinterTechnology pt) {
    return pt == ptSLA && type == Preset::TYPE_PRINTER ? "sla_printer" : type_icon_names.at(type);
}

static std::string def_text_color()
{
    wxColour def_colour = wxGetApp().get_label_clr_default();
    return encode_color(ColorRGB(def_colour.Red(), def_colour.Green(), def_colour.Blue()));
}
static std::string grey     = "#808080";
static std::string orange   = "#ed6b21";

static void color_string(wxString& str, const std::string& color)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = from_u8((boost::format("<span color=\"%1%\">%2%</span>") % color % into_u8(str)).str());
#endif
}

static void make_string_bold(wxString& str)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = from_u8((boost::format("<b>%1%</b>") % into_u8(str)).str());
#endif
}

// preset(root) node
ModelNode::ModelNode(Preset::Type preset_type, wxWindow* parent_win, const wxString& text, const std::string& icon_name, const wxString& new_val_column_text) :
    m_parent_win(parent_win),
    m_parent(nullptr),
    m_preset_type(preset_type),
    m_icon_name(icon_name),
    m_text(text),
    m_new_value(new_val_column_text)
{
    UpdateIcons();
}

// category node
ModelNode::ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_icon_name(icon_name),
    m_text(text)
{
    UpdateIcons();
}

// group node
ModelNode::ModelNode(ModelNode* parent, const wxString& text) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_icon_name("dot_small"),
    m_text(text)
{
    UpdateIcons();
}

#ifdef __linux__
wxIcon ModelNode::get_bitmap(const wxString& color)
#else
wxBitmap ModelNode::get_bitmap(const wxString& color)
#endif // __linux__
{
    wxBitmap bmp = get_solid_bmp_bundle(64, 16, into_u8(color))->GetBitmapFor(m_parent_win);
    if (!m_toggle)
        bmp = bmp.ConvertToDisabled();
#ifndef __linux__
    return bmp;
#else
    wxIcon icon;
    icon.CopyFromBitmap(bmp);
    return icon;
#endif // __linux__
}

// option node
ModelNode::ModelNode(ModelNode* parent, const wxString& text, const wxString& old_value, const wxString& mod_value, const wxString& new_value) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_old_color(old_value.StartsWith("#") ? old_value : ""),
    m_mod_color(mod_value.StartsWith("#") ? mod_value : ""),
    m_new_color(new_value.StartsWith("#") ? new_value : ""),
    m_icon_name("empty"),
    m_text(text),
    m_old_value(old_value),
    m_mod_value(mod_value),
    m_new_value(new_value),
    m_container(false)
{
    // check if old/new_value is color
    if (m_old_color.IsEmpty()) {
        if (!m_mod_color.IsEmpty())
            m_old_value = _L("Undef");
    }
    else {
        m_old_color_bmp = get_bitmap(m_old_color);
        m_old_value.Clear();
    }

    if (m_mod_color.IsEmpty()) {
        if (!m_old_color.IsEmpty())
            m_mod_value = _L("Undef");
    }
    else {
        m_mod_color_bmp = get_bitmap(m_mod_color);
        m_mod_value.Clear();
    }

    if (m_new_color.IsEmpty()) {
        if (!m_old_color.IsEmpty() || !m_mod_color.IsEmpty())
            m_new_value = _L("Undef");
    }
    else {
        m_new_color_bmp = get_bitmap(m_new_color);
        m_new_value.Clear();
    }

    // "color" strings
    color_string(m_old_value, def_text_color());
    color_string(m_mod_value, orange);
    color_string(m_new_value, def_text_color());

    UpdateIcons();
}

void ModelNode::UpdateEnabling()
{
    auto change_text_color = [](wxString& str, const std::string& clr_from, const std::string& clr_to)
    {
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
        std::string old_val = into_u8(str);
        boost::replace_all(old_val, clr_from, clr_to);
        str = from_u8(old_val);
#endif
    };

    if (!m_toggle) {
        change_text_color(m_text,      def_text_color(), grey);
        change_text_color(m_old_value, def_text_color(), grey);
        change_text_color(m_mod_value, orange,grey);
        change_text_color(m_new_value, def_text_color(), grey);
    }
    else {
        change_text_color(m_text,      grey, def_text_color());
        change_text_color(m_old_value, grey, def_text_color());
        change_text_color(m_mod_value, grey, orange);
        change_text_color(m_new_value, grey, def_text_color());
    }
    // update icons for the colors
    UpdateIcons();
}

void ModelNode::UpdateIcons()
{
    // update icons for the colors, if any exists
    if (!m_old_color.IsEmpty())
        m_old_color_bmp = get_bitmap(m_old_color);
    if (!m_mod_color.IsEmpty())
        m_mod_color_bmp = get_bitmap(m_mod_color);
    if (!m_new_color.IsEmpty())
        m_new_color_bmp = get_bitmap(m_new_color);

    // update main icon, if any exists
    if (m_icon_name.empty())
        return;

    wxBitmap bmp = get_bmp_bundle(m_icon_name)->GetBitmapFor(m_parent_win);
    if (!m_toggle)
        bmp = bmp.ConvertToDisabled();

#ifdef __linux__
    m_icon.CopyFromBitmap(bmp);
#else
    m_icon = bmp;
#endif //__linux__
}


// ----------------------------------------------------------------------------
//                          DiffModel
// ----------------------------------------------------------------------------

DiffModel::DiffModel(wxWindow* parent) :
    m_parent_win(parent)
{
}

wxDataViewItem DiffModel::AddPreset(Preset::Type type, wxString preset_name, PrinterTechnology pt, wxString new_preset_name/* = wxString()*/)
{
    // "color" strings
    color_string(preset_name, def_text_color());
    make_string_bold(preset_name);
    make_string_bold(new_preset_name);

    auto preset = new ModelNode(type, m_parent_win, preset_name, get_icon_name(type, pt), new_preset_name);
    m_preset_nodes.emplace_back(preset);

    wxDataViewItem child((void*)preset);
    wxDataViewItem parent(nullptr);

    ItemAdded(parent, child);
    return child;
}

ModelNode* DiffModel::AddOption(ModelNode* group_node, wxString option_name, wxString old_value, wxString mod_value, wxString new_value)
{
    group_node->Append(std::make_unique<ModelNode>(group_node, option_name, old_value, mod_value, new_value));
    ModelNode* option = group_node->GetChildren().back().get();
    wxDataViewItem group_item = wxDataViewItem((void*)group_node);
    ItemAdded(group_item, wxDataViewItem((void*)option));

    m_ctrl->Expand(group_item);
    return option;
}

ModelNode* DiffModel::AddOptionWithGroup(ModelNode* category_node, wxString group_name, wxString option_name, wxString old_value, wxString mod_value, wxString new_value)
{
    category_node->Append(std::make_unique<ModelNode>(category_node, group_name));
    ModelNode* group_node = category_node->GetChildren().back().get();
    ItemAdded(wxDataViewItem((void*)category_node), wxDataViewItem((void*)group_node));

    return AddOption(group_node, option_name, old_value, mod_value, new_value);
}

ModelNode* DiffModel::AddOptionWithGroupAndCategory(ModelNode* preset_node, wxString category_name, wxString group_name, 
                                            wxString option_name, wxString old_value, wxString mod_value, wxString new_value, const std::string category_icon_name)
{
    preset_node->Append(std::make_unique<ModelNode>(preset_node, category_name, category_icon_name));
    ModelNode* category_node = preset_node->GetChildren().back().get();
    ItemAdded(wxDataViewItem((void*)preset_node), wxDataViewItem((void*)category_node));

    return AddOptionWithGroup(category_node, group_name, option_name, old_value, mod_value, new_value);
}

wxDataViewItem DiffModel::AddOption(Preset::Type type, wxString category_name, wxString group_name, wxString option_name,
                                              wxString old_value, wxString mod_value, wxString new_value, const std::string category_icon_name)
{
    // "color" strings
    color_string(category_name, def_text_color());
    color_string(group_name,    def_text_color());
    color_string(option_name,   def_text_color());

    // "make" strings bold
    make_string_bold(category_name);
    make_string_bold(group_name);

    // add items
    for (std::unique_ptr<ModelNode>& preset : m_preset_nodes)
        if (preset->type() == type)
        {
            for (std::unique_ptr<ModelNode> &category : preset->GetChildren())
                if (category->text() == category_name)
                {
                    for (std::unique_ptr<ModelNode> &group : category->GetChildren())
                        if (group->text() == group_name)
                            return wxDataViewItem((void*)AddOption(group.get(), option_name, old_value, mod_value, new_value));
                    
                    return wxDataViewItem((void*)AddOptionWithGroup(category.get(), group_name, option_name, old_value, mod_value, new_value));
                }

            return wxDataViewItem((void*)AddOptionWithGroupAndCategory(preset.get(), category_name, group_name, option_name, old_value, mod_value, new_value, category_icon_name));
        }

    return wxDataViewItem(nullptr);    
}

static void update_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        bool toggle = parent->IsToggled();
        for (std::unique_ptr<ModelNode> &child : parent->GetChildren()) {
            child->Toggle(toggle);
            child->UpdateEnabling();
            update_children(child.get());
        }
    }
}

static void update_parents(ModelNode* node)
{
    ModelNode* parent = node->GetParent();
    if (parent) {
        bool toggle = false;
        for (std::unique_ptr<ModelNode> &child : parent->GetChildren()) {
            if (child->IsToggled()) {
                toggle = true;
                break;
            }
        }
        parent->Toggle(toggle);
        parent->UpdateEnabling();
        update_parents(parent);
    }
}

void DiffModel::UpdateItemEnabling(wxDataViewItem item)
{
    assert(item.IsOk());
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    node->UpdateEnabling();

    update_children(node);
    update_parents(node);    
}

bool DiffModel::IsEnabledItem(const wxDataViewItem& item)
{
    assert(item.IsOk());
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    return node->IsToggled();
}

void DiffModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    switch (col)
    {
    case colToggle:
        variant = node->m_toggle;
        break;
#ifdef __linux__
    case colIconText:
        variant << wxDataViewIconText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << wxDataViewIconText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colModValue:
        variant << wxDataViewIconText(node->m_mod_value, node->m_mod_color_bmp);
        break;
    case colNewValue:
        variant << wxDataViewIconText(node->m_new_value, node->m_new_color_bmp);
        break;
#else
    case colIconText:
        variant << DataViewBitmapText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << DataViewBitmapText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colModValue:
        variant << DataViewBitmapText(node->m_mod_value, node->m_mod_color_bmp);
        break;
    case colNewValue:
        variant << DataViewBitmapText(node->m_new_value, node->m_new_color_bmp);
        break;
#endif //__linux__

    default:
        wxLogError("DiffModel::GetValue: wrong column %d", col);
    }
}

bool DiffModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    assert(item.IsOk());

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    switch (col)
    {
    case colToggle:
        node->m_toggle = variant.GetBool();
        return true;
#ifdef __linux__
    case colIconText: {
        wxDataViewIconText data;
        data << variant;
        node->m_icon = data.GetIcon();
        node->m_text = data.GetText();
        return true; }
    case colOldValue: {
        wxDataViewIconText data;
        data << variant;
        node->m_old_color_bmp   = data.GetIcon();
        node->m_old_value       = data.GetText();
        return true; }
    case colNewValue: {
        wxDataViewIconText data;
        data << variant;
        node->m_new_color_bmp   = data.GetIcon();
        node->m_new_value       = data.GetText();
        return true; }
#else
    case colIconText: {
        DataViewBitmapText data;
        data << variant;
        node->m_icon = data.GetBitmap();
        node->m_text = data.GetText();
        return true; }
    case colOldValue: {
        DataViewBitmapText data;
        data << variant;
        node->m_old_color_bmp   = data.GetBitmap();
        node->m_old_value       = data.GetText();
        return true; }
    case colNewValue: {
        DataViewBitmapText data;
        data << variant;
        node->m_new_color_bmp   = data.GetBitmap();
        node->m_new_value       = data.GetText();
        return true; }
#endif //__linux__
    default:
        wxLogError("DiffModel::SetValue: wrong column");
    }
    return false;
}

bool DiffModel::IsEnabled(const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());
    if (col == colToggle)
        return true;

    // disable unchecked nodes
    return (static_cast<ModelNode*>(item.GetID()))->IsToggled();
}

wxDataViewItem DiffModel::GetParent(const wxDataViewItem& item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(nullptr);

    ModelNode* node = static_cast<ModelNode*>(item.GetID());

    if (node->IsRoot())
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*)node->GetParent());
}

bool DiffModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisble root node can have children
    if (!item.IsOk())
        return true;

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    return node->IsContainer();
}

unsigned int DiffModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    ModelNode* parent_node = (ModelNode*)parent.GetID();

    const ModelNodePtrArray& children = parent_node ? parent_node->GetChildren() : m_preset_nodes;
    for (const std::unique_ptr<ModelNode>& child : children)
        array.Add(wxDataViewItem((void*)child.get()));

    return array.Count();
}


wxString DiffModel::GetColumnType(unsigned int col) const
{
    switch (col)
    {
    case colToggle:
        return "bool";
    case colIconText:
    case colOldValue:
    case colNewValue:
    default:
        return "DataViewBitmapText";//"string";
    }
}

static void rescale_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        for (std::unique_ptr<ModelNode> &child : parent->GetChildren()) {
            child->UpdateIcons();
            rescale_children(child.get());
        }
    }
}

void DiffModel::Rescale()
{
    for (std::unique_ptr<ModelNode> &node : m_preset_nodes) {
        node->UpdateIcons();
        rescale_children(node.get());
    }
}

wxDataViewItem DiffModel::Delete(const wxDataViewItem& item)
{
    auto ret_item = wxDataViewItem(nullptr);
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    if (!node)      // happens if item.IsOk()==false
        return ret_item;

    // first remove the node from the parent's array of children;
    // NOTE: m_preset_nodes is only a vector of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    ModelNodePtrArray& children = node->GetChildren();
    // Delete all children
    while (!children.empty())
        Delete(wxDataViewItem(children.back().get()));

    auto node_parent = node->GetParent();
    wxDataViewItem parent(node_parent);

    ModelNodePtrArray& parents_children = node_parent ? node_parent->GetChildren() : m_preset_nodes;
    auto it = find_if(parents_children.begin(), parents_children.end(), 
                      [node](std::unique_ptr<ModelNode>& child) { return child.get() == node; });
    assert(it != parents_children.end());
    it = parents_children.erase(it);

    if (it != parents_children.end())
        ret_item = wxDataViewItem(it->get());

    // set m_container to FALSE if parent has no child
    if (node_parent) {
#ifndef __WXGTK__
        if (node_parent->GetChildCount() == 0)
            node_parent->m_container = false;
#endif //__WXGTK__
        ret_item = parent;
    }

    // notify control
    ItemDeleted(parent, item);
    return ret_item;
}

void DiffModel::Clear()
{
    while (!m_preset_nodes.empty())
        Delete(wxDataViewItem(m_preset_nodes.back().get()));
}


static std::string get_pure_opt_key(std::string opt_key)
{
    const int pos = opt_key.find("#");
    if (pos > 0)
        boost::erase_tail(opt_key, opt_key.size() - pos);
    return opt_key;
}    

wxDataViewItem DiffModel::GetItemByName(wxString name)
{
    // add items
    for (std::unique_ptr<ModelNode>& preset : m_preset_nodes) {
        for (std::unique_ptr<ModelNode>& category : preset->GetChildren()) {
            if (category->text().Contains(name))
                return wxDataViewItem((void*)category.get());

            for (std::unique_ptr<ModelNode>& group : category->GetChildren()) {
                if (group->text().Contains(name))
                    return wxDataViewItem((void*)group.get());

                for (std::unique_ptr<ModelNode>& option : group->GetChildren())
                    if (option->text().Contains(name))
                        return wxDataViewItem((void*)option.get());
            }
        }
    }

    return wxDataViewItem(nullptr);
}


// ----------------------------------------------------------------------------
//                  DiffViewCtrl
// ----------------------------------------------------------------------------

DiffViewCtrl::DiffViewCtrl(wxWindow* parent, wxSize size)
    : wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, size, wxDV_VARIABLE_LINE_HEIGHT | wxDV_ROW_LINES
#ifdef _WIN32
        | wxBORDER_SIMPLE
#endif
    ),
    m_em_unit(em_unit(parent))
{
    wxGetApp().UpdateDVCDarkUI(this);

    model = new DiffModel(parent);
    this->AssociateModel(model);
    model->SetAssociatedControl(this);

    this->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &DiffViewCtrl::context_menu, this);
    this->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED,    &DiffViewCtrl::context_menu, this);
    this->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &DiffViewCtrl::item_value_changed, this);
}

void DiffViewCtrl::AppendBmpTextColumn(const wxString& label, unsigned model_column, int width, bool set_expander/* = false*/)
{
    m_columns_width.emplace(this->GetColumnCount(), width);
#ifdef __linux__
    wxDataViewIconTextRenderer* rd = new wxDataViewIconTextRenderer();
#ifdef SUPPORTS_MARKUP
    rd->EnableMarkup(true);
#endif
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

void DiffViewCtrl::Rescale(int em /*= 0*/)
{
    if (em > 0) {
        for (auto item : m_columns_width)
            GetColumn(item.first)->SetWidth(item.second * em);
        m_em_unit = em;
    }

    model->Rescale();
    Refresh();
}


void DiffViewCtrl::Append(  const std::string& opt_key, Preset::Type type, 
                            wxString category_name, wxString group_name, wxString option_name,
                            wxString old_value, wxString mod_value, wxString new_value, const std::string category_icon_name)
{
    ItemData item_data = { opt_key, option_name, old_value, mod_value, new_value, type };

    wxString old_val = get_short_string(item_data.old_val);
    wxString mod_val = get_short_string(item_data.mod_val);
    wxString new_val = get_short_string(item_data.new_val);
    if (old_val != item_data.old_val || mod_val != item_data.mod_val || new_val != item_data.new_val)
        item_data.is_long = true;

    m_items_map.emplace(model->AddOption(type, category_name, group_name, option_name, old_val, mod_val, new_val, category_icon_name), item_data);

}

void DiffViewCtrl::Clear()
{
    model->Clear();
    m_items_map.clear();
    m_has_long_strings = false;
}

wxString DiffViewCtrl::get_short_string(wxString full_string)
{
    size_t max_len = 30;
    if (full_string.IsEmpty() || full_string.StartsWith("#") ||
        (full_string.Find("\n") == wxNOT_FOUND && full_string.Length() < max_len))
        return full_string;

    m_has_long_strings = true;

    int n_pos = full_string.Find("\n");
    if (n_pos != wxNOT_FOUND && n_pos < (int)max_len)
        max_len = n_pos;

    full_string.Truncate(max_len);
    return full_string + dots;
}

void DiffViewCtrl::context_menu(wxDataViewEvent& event)
{
    if (!m_has_long_strings)
        return;

    wxDataViewItem item = event.GetItem();
    if (!item) {
        wxPoint mouse_pos = wxGetMousePosition() - this->GetScreenPosition();
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

    const wxString old_value_header = this->GetColumn(DiffModel::colOldValue)->GetTitle();
    const wxString mod_value_header = this->GetColumn(DiffModel::colModValue)->GetTitle();
    const wxString new_value_header = has_new_value_column() ? this->GetColumn(DiffModel::colNewValue)->GetTitle() : "";
    FullCompareDialog(it->second.opt_name, it->second.old_val, it->second.mod_val, it->second.new_val,
                      old_value_header, mod_value_header, new_value_header).ShowModal();

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
    if (event.GetColumn() != DiffModel::colToggle)
        return;

    wxDataViewItem item = event.GetItem();
    update_item_enabling(item);
}

bool DiffViewCtrl::has_unselected_options()
{
    for (auto item : m_items_map)
        if (!model->IsEnabledItem(item.first))
            return true;

    return false;
}

void DiffViewCtrl::update_item_enabling(wxDataViewItem item)
{

    model->UpdateItemEnabling(item);
    Refresh();

    // update an enabling of the "save/move" buttons
    m_empty_selection = selected_options().empty();
}

std::vector<std::string> DiffViewCtrl::options(Preset::Type type, bool selected)
{
    std::vector<std::string> ret;

    for (auto item : m_items_map) {
        if (item.second.type == type && model->IsEnabledItem(item.first) == selected)
            ret.emplace_back(get_pure_opt_key(item.second.opt_key));
    }

    return ret;
}

std::vector<std::string> DiffViewCtrl::selected_options()
{
    std::vector<std::string> ret;

    for (auto item : m_items_map)
        if (model->IsEnabledItem(item.first))
            ret.emplace_back(get_pure_opt_key(item.second.opt_key));

    return ret;
}


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------

static std::string none{"none"};

UnsavedChangesDialog::UnsavedChangesDialog(const wxString& caption, const wxString& header, 
                                           const std::string& app_config_key, int act_buttons)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, caption + ": " + _L("Unsaved Changes"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    m_app_config_key(app_config_key),
    m_buttons(act_buttons)
{
    build(Preset::TYPE_INVALID, nullptr, "", header);

    const std::string& def_action = m_app_config_key.empty() ? none : wxGetApp().app_config->get(m_app_config_key);
    if (def_action == none)
        this->CenterOnScreen();
    else {
        m_exit_action = def_action == ActTransfer   ? Action::Transfer  :
                        def_action == ActSave       ? Action::Save      : Action::Discard;
        if (m_exit_action != Action::Discard)
            save(nullptr, m_exit_action == Action::Save);
    }
}

UnsavedChangesDialog::UnsavedChangesDialog(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _L("Switching Presets: Unsaved Changes"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    m_app_config_key = "default_action_on_select_preset";

    build(type, dependent_presets, new_selected_preset);

    const std::string& def_action = wxGetApp().app_config->get(m_app_config_key);
    if (def_action == none) {
        if (wxGetApp().mainframe->is_dlg_layout() && wxGetApp().mainframe->m_settings_dialog.HasFocus())
            this->SetPosition(wxGetApp().mainframe->m_settings_dialog.GetPosition());
        this->CenterOnScreen();
    }
    else {
        m_exit_action = def_action == ActTransfer   ? Action::Transfer  :
                        def_action == ActSave       ? Action::Save      : Action::Discard;
        const PresetCollection& printers = wxGetApp().preset_bundle->printers;
        if (m_exit_action == Action::Save || 
            (m_exit_action == Action::Transfer && dependent_presets && (type == dependent_presets->type() ?
            dependent_presets->get_edited_preset().printer_technology() != dependent_presets->find_preset(new_selected_preset)->printer_technology() :
            printers.get_edited_preset().printer_technology() != printers.find_preset(new_selected_preset)->printer_technology())) )
            save(dependent_presets);
    }
}

void UnsavedChangesDialog::build(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header)
{
    this->SetFont(wxGetApp().normal_font());

    int border = 10;
    int em = em_unit();

    bool add_new_value_column = !new_selected_preset.empty() && dependent_presets && dependent_presets->get_edited_preset().type == type &&
                                new_selected_preset != dependent_presets->get_edited_preset().name;
    if (add_new_value_column && dependent_presets->get_edited_preset().type == Preset::TYPE_PRINTER &&
        dependent_presets->get_edited_preset().printer_technology() != dependent_presets->find_preset(new_selected_preset)->printer_technology())
        add_new_value_column = false;

    m_action_line = new wxStaticText(this, wxID_ANY, "");
//    m_action_line->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());
    m_action_line->SetFont(wxGetApp().bold_font());

    m_tree = new DiffViewCtrl(this, wxSize(em * (add_new_value_column ? 80 : 60), em * 30));
    m_tree->AppendToggleColumn_(L"\u2714"      , DiffModel::colToggle, wxLinux ? 9 : 6);
    m_tree->AppendBmpTextColumn(""             , DiffModel::colIconText, 28);
    m_tree->AppendBmpTextColumn(_L("Original value"), DiffModel::colOldValue, 12);
    m_tree->AppendBmpTextColumn(_L("Modified value"), DiffModel::colModValue, 12);
    if (add_new_value_column)
        m_tree->AppendBmpTextColumn(_L("New value"), DiffModel::colNewValue, 12);

    // Add Buttons 
    wxFont      btn_font = this->GetFont().Scaled(1.4f);
    wxBoxSizer* buttons  = new wxBoxSizer(wxHORIZONTAL);

    auto add_btn = [this, buttons, btn_font, dependent_presets](ScalableButton** btn, int& btn_id, const std::string& icon_name, Action close_act, const wxString& label, bool process_enable = true)
    {
        *btn = new ScalableButton(this, btn_id = NewControlId(), icon_name, label, wxDefaultSize, wxDefaultPosition, wxBORDER_DEFAULT, 24);

        buttons->Add(*btn, 1, wxLEFT, 5);
        (*btn)->SetFont(btn_font);

        (*btn)->Bind(wxEVT_BUTTON, [this, close_act, dependent_presets](wxEvent&) {
            update_config(close_act);
            bool save_names_and_types = close_act == Action::Save || (close_act == Action::Transfer && ActionButtons::KEEP & m_buttons);
            if (save_names_and_types && !save(dependent_presets, close_act == Action::Save))
                return;
            close(close_act);
        });
        if (process_enable)
            (*btn)->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(m_tree->has_selection()); });
        (*btn)->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { show_info_line(Action::Undef); e.Skip(); });
    };

    // "Transfer" / "Keep" button
    if (ActionButtons::TRANSFER & m_buttons) {
        const PresetCollection* switched_presets = type == Preset::TYPE_INVALID ? nullptr : wxGetApp().get_tab(type)->get_presets();
        if (dependent_presets && switched_presets && (type == dependent_presets->type() ?
            dependent_presets->get_edited_preset().printer_technology() == dependent_presets->find_preset(new_selected_preset)->printer_technology() :
            switched_presets->get_edited_preset().printer_technology() == switched_presets->find_preset(new_selected_preset)->printer_technology()))
            add_btn(&m_transfer_btn, m_move_btn_id, "paste_menu", Action::Transfer, switched_presets->get_edited_preset().name == new_selected_preset ? _L("Keep") : _L("Transfer"));
    }
    if (!m_transfer_btn && (ActionButtons::KEEP & m_buttons))
        add_btn(&m_transfer_btn, m_move_btn_id, "paste_menu", Action::Transfer, _L("Keep"));

    { // "Don't save" / "Discard" button
        std::string btn_icon    = (ActionButtons::DONT_SAVE & m_buttons) ? "" : (dependent_presets || (ActionButtons::KEEP & m_buttons)) ? "switch_presets" : "exit";
        wxString    btn_label   = (ActionButtons::DONT_SAVE & m_buttons) ? _L("Don't save") : _L("Discard");
        add_btn(&m_discard_btn, m_continue_btn_id, btn_icon, Action::Discard, btn_label, false);
    }

    // "Save" button
    if (ActionButtons::SAVE & m_buttons) 
        add_btn(&m_save_btn, m_save_btn_id, "save", Action::Save, _L("Save"));

    ScalableButton* cancel_btn = new ScalableButton(this, wxID_CANCEL, "cross", _L("Cancel"), wxDefaultSize, wxDefaultPosition, wxBORDER_DEFAULT, 24);
    buttons->Add(cancel_btn, 1, wxLEFT|wxRIGHT, 5);
    cancel_btn->SetFont(btn_font);
    cancel_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) { this->EndModal(wxID_CANCEL); });

    m_info_line = new wxStaticText(this, wxID_ANY, "");
//    m_info_line->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold());
    m_info_line->SetFont(wxGetApp().bold_font());
    m_info_line->Hide();

    if (!m_app_config_key.empty()) {
        m_remember_choice = new wxCheckBox(this, wxID_ANY, _L("Remember my choice"));
        m_remember_choice->SetValue(wxGetApp().app_config->get(m_app_config_key) != none);
        m_remember_choice->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& evt)
        {
            if (!evt.IsChecked())
                return;
            wxString preferences_item = m_app_config_key == "default_action_on_new_project"     ? _L("Ask for unsaved changes in presets when creating new project") :
                                        m_app_config_key == "default_action_on_select_preset"   ? _L("Ask for unsaved changes in presets when selecting new preset") :
                                                                                                  _L("Ask to save unsaved changes in presets when closing the application or when loading a new project") ;
            wxString action = m_app_config_key == "default_action_on_new_project"   ? _L("You will not be asked about the unsaved changes in presets the next time you create new project") : 
                              m_app_config_key == "default_action_on_select_preset" ? _L("You will not be asked about the unsaved changes in presets the next time you switch a preset") :
                                                                                      _L("You will not be asked about the unsaved changes in presets the next time you: \n"
						                                                                    "- Closing PrusaSlicer while some presets are modified,\n"
						                                                                    "- Loading a new project while some presets are modified") ;
            wxString msg = _L("PrusaSlicer will remember your action.") + "\n\n" + action + "\n\n" +
                           format_wxstr(_L("Visit \"Preferences\" and check \"%1%\"\nto be asked about unsaved changes again."), preferences_item);
    
            MessageDialog dialog(nullptr, msg, _L("PrusaSlicer: Don't ask me again"), wxOK | wxCANCEL | wxICON_INFORMATION);
            if (dialog.ShowModal() == wxID_CANCEL)
                m_remember_choice->SetValue(false);
        });
    }

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(m_action_line,0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_tree,       1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_info_line,  0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 2*border);
    topSizer->Add(buttons,      0, wxEXPAND | wxALL, border);
    if (m_remember_choice)
        topSizer->Add(m_remember_choice, 0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, border);

    update(type, dependent_presets, new_selected_preset, header);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    show_info_line(Action::Undef);
}

void UnsavedChangesDialog::show_info_line(Action action, std::string preset_name)
{
    if (action == Action::Undef && !m_tree->has_long_strings())
        m_info_line->Hide();
    else {
        wxString text;
        if (action == Action::Undef)
            text = _L("Some fields are too long to fit. Right mouse click reveals the full text.");
        else if (action == Action::Discard)
            text = ActionButtons::DONT_SAVE & m_buttons ? _L("All settings changes will not be saved") :_L("All settings changes will be discarded.");
        else {
            if (preset_name.empty())
                text = action == Action::Save           ? _L("Save the selected options.") : 
                       ActionButtons::KEEP & m_buttons  ? _L("Keep the selected settings.") :
                                                          _L("Transfer the selected settings to the newly selected preset.");
            else
                text = format_wxstr(
                    action == Action::Save ?
                        _L("Save the selected options to preset \"%1%\".") :
                        _L("Transfer the selected options to the newly selected preset \"%1%\"."),
                    preset_name);
            //text += "\n" + _L("Unselected options will be reverted.");
        }
        m_info_line->SetLabel(text);
        m_info_line->Show();
    }

    Layout();
    Refresh();
}

void UnsavedChangesDialog::update_config(Action action)
{
    if (!m_remember_choice || !m_remember_choice->GetValue())
        return;

    std::string act = action == Action::Transfer ? ActTransfer :
                      action == Action::Discard  ? ActDiscard   : ActSave;
    wxGetApp().app_config->set(m_app_config_key, act);
}

void UnsavedChangesDialog::close(Action action)
{
    m_exit_action = action;
    this->EndModal(wxID_CLOSE);
}

bool UnsavedChangesDialog::save(PresetCollection* dependent_presets, bool show_save_preset_dialog/* = true*/)
{
    names_and_types.clear();

    // save one preset
    if (dependent_presets) {
        const Preset& preset = dependent_presets->get_edited_preset();
        std::string name = preset.name;

        // for system/default/external presets we should take an edited name
        if (preset.is_system || preset.is_default || preset.is_external) {
            SavePresetDialog save_dlg(this, { preset.type });
            if (save_dlg.ShowModal() != wxID_OK) {
                m_exit_action = Action::Discard;
                return false;
            }
            name = save_dlg.get_name();
        }

        names_and_types.emplace_back(make_pair(name, preset.type));
    }
    // save all presets 
    else
    {
        std::vector<Preset::Type> types_for_save;

        PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();

        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty()) {
                const Preset& preset = tab->get_presets()->get_edited_preset();
                if (preset.is_system || preset.is_default || preset.is_external)
                    types_for_save.emplace_back(preset.type);

                names_and_types.emplace_back(make_pair(preset.name, preset.type));
            }


        if (show_save_preset_dialog && !types_for_save.empty()) {
            SavePresetDialog save_dlg(this, types_for_save);
            if (save_dlg.ShowModal() != wxID_OK) {
                m_exit_action = Action::Discard;
                return false;
            }

            for (std::pair<std::string, Preset::Type>& nt : names_and_types) {
                const std::string& name = save_dlg.get_name(nt.second);
                if (!name.empty())
                    nt.first = name;
            }
        }
    }
    return true;
}

static size_t get_id_from_opt_key(std::string opt_key)
{
    int pos = opt_key.find("#");
    if (pos > 0) {
        boost::erase_head(opt_key, pos + 1);
        return static_cast<size_t>(atoi(opt_key.c_str()));
    }
    return 0;
}

static wxString get_full_label(std::string opt_key, const DynamicPrintConfig& config)
{
    opt_key = get_pure_opt_key(opt_key);

    if (config.option(opt_key)->is_nil())
        return _L("N/A");

    const ConfigOptionDef* opt = config.def()->get(opt_key);
    return opt->full_label.empty() ? opt->label : opt->full_label;
}

static wxString get_string_value(std::string opt_key, const DynamicPrintConfig& config)
{
    size_t opt_idx = get_id_from_opt_key(opt_key);
    opt_key = get_pure_opt_key(opt_key);

    if (config.option(opt_key)->is_nil())
        return _L("N/A");

    wxString out;

    const ConfigOptionDef* opt = config.def()->get(opt_key);
    bool is_nullable = opt->nullable;

    switch (opt->type) {
    case coInt:
        return from_u8((boost::format("%1%") % config.option(opt_key)->getInt()).str());
    case coInts: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionIntsNullable>(opt_key);
            if (opt_idx < values->size())
                return from_u8((boost::format("%1%") % values->get_at(opt_idx)).str());
        }
        else {
            auto values = config.opt<ConfigOptionInts>(opt_key);
            if (opt_idx < values->size())
                return from_u8((boost::format("%1%") % values->get_at(opt_idx)).str());
        }
        return _L("Undef");
    }
    case coBool:
        return config.opt_bool(opt_key) ? "true" : "false";
    case coBools: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionBoolsNullable>(opt_key);
            if (opt_idx < values->size())
                return values->get_at(opt_idx) ? "true" : "false";
        }
        else {
            auto values = config.opt<ConfigOptionBools>(opt_key);
            if (opt_idx < values->size())
                return values->get_at(opt_idx) ? "true" : "false";
        }
        return _L("Undef");
    }
    case coPercent:
        return from_u8((boost::format("%1%%%") % int(config.optptr(opt_key)->getFloat())).str());
    case coPercents: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionPercentsNullable>(opt_key);
            if (opt_idx < values->size())
                return from_u8((boost::format("%1%%%") % values->get_at(opt_idx)).str());
        }
        else {
            auto values = config.opt<ConfigOptionPercents>(opt_key);
            if (opt_idx < values->size())
                return from_u8((boost::format("%1%%%") % values->get_at(opt_idx)).str());
        }
        return _L("Undef");
    }
    case coFloat:
        return double_to_string(config.option(opt_key)->getFloat());
    case coFloats: {
        if (is_nullable) {
            auto values = config.opt<ConfigOptionFloatsNullable>(opt_key);
            if (opt_idx < values->size())
                return double_to_string(values->get_at(opt_idx));
        }
        else {
            auto values = config.opt<ConfigOptionFloats>(opt_key);
            if (opt_idx < values->size())
                return double_to_string(values->get_at(opt_idx));
        }
        return _L("Undef");
    }
    case coString:
        return from_u8(config.opt_string(opt_key));
    case coStrings: {
        const ConfigOptionStrings* strings = config.opt<ConfigOptionStrings>(opt_key);
        if (strings) {
            if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
                if (strings->empty())
                    return _L("All"); 
                for (size_t id = 0; id < strings->size(); id++)
                    out += from_u8(strings->get_at(id)) + "\n";
                out.RemoveLast(1);
                return out;
            }
            if (opt_key == "gcode_substitutions") {
                if (!strings->empty())
                    for (size_t id = 0; id < strings->size(); id += 4)
                        out +=  from_u8(strings->get_at(id))     + ";\t" + 
                                from_u8(strings->get_at(id + 1)) + ";\t" + 
                                from_u8(strings->get_at(id + 2)) + ";\t" +
                                from_u8(strings->get_at(id + 3)) + ";\n";
                return out;
            }
            if (!strings->empty() && opt_idx < strings->values.size())
                return from_u8(strings->get_at(opt_idx));
        }
        break;
        }
    case coFloatOrPercent: {
        const ConfigOptionFloatOrPercent* opt = config.opt<ConfigOptionFloatOrPercent>(opt_key);
        if (opt)
            out = double_to_string(opt->value) + (opt->percent ? "%" : "");
        return out;
    }
    case coFloatsOrPercents: {
        const ConfigOptionFloatsOrPercents* opt = config.opt<ConfigOptionFloatsOrPercents>(opt_key);
        if (opt) {
            const auto val = opt->get_at(opt_idx);
            out = double_to_string(val.value) + (val.percent ? "%" : "");
        }
        return out;
    }
    case coEnum: {
        auto opt = config.option_def(opt_key)->enum_def->enum_to_label(config.option(opt_key)->getInt());
        return opt.has_value() ? _(from_u8(*opt)) : _L("Undef");
    }
    case coEnums: {
        auto values = config.option(opt_key)->getInts();
        if (opt_idx < values.size()) {
            auto opt = config.option_def(opt_key)->enum_def->enum_to_label(values[opt_idx]);
            return opt.has_value() ? _(from_u8(*opt)) : _L("Undef");
        }
        return _L("Undef");
    }
    case coPoints: {
        if (opt_key == "bed_shape") {
            BedShape shape(*config.option<ConfigOptionPoints>(opt_key));
            return shape.get_full_name_with_params();
        }

        Vec2d val = config.opt<ConfigOptionPoints>(opt_key)->get_at(opt_idx);
        return from_u8((boost::format("[%1%]") % ConfigOptionPoint(val).serialize()).str());
    }
    default:
        break;
    }
    return out;
}

void UnsavedChangesDialog::update(Preset::Type type, PresetCollection* dependent_presets, const std::string& new_selected_preset, const wxString& header)
{
    PresetCollection* presets = dependent_presets;

    // activate buttons and labels
    if (m_save_btn)
        m_save_btn    ->Bind(wxEVT_ENTER_WINDOW, [this, presets]                           (wxMouseEvent& e) { show_info_line(Action::Save, presets ? presets->get_selected_preset().name : ""); e.Skip(); });
    if (m_transfer_btn) {
        bool is_empty_name = dependent_presets && type != dependent_presets->type();
        m_transfer_btn->Bind(wxEVT_ENTER_WINDOW, [this, new_selected_preset, is_empty_name](wxMouseEvent& e) { show_info_line(Action::Transfer, is_empty_name ? "" : new_selected_preset); e.Skip(); });
    }
    if (m_discard_btn)
        m_discard_btn ->Bind(wxEVT_ENTER_WINDOW, [this]                                    (wxMouseEvent& e) { show_info_line(Action::Discard); e.Skip(); });

    if (type == Preset::TYPE_INVALID) {
        PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
        int presets_cnt = 0;
        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                presets_cnt++;
        m_action_line->SetLabel((header.IsEmpty() ? "" : header + "\n\n") + 
                                _L_PLURAL("The following preset was modified",
                                          "The following presets were modified", presets_cnt));
    }
    else {
        wxString action_msg;
        if (dependent_presets && type == dependent_presets->type()) {
            action_msg = format_wxstr(_L("Preset \"%1%\" has the following unsaved changes:"), presets->get_edited_preset().name);
        }
        else {
            action_msg = format_wxstr(type == Preset::TYPE_PRINTER ?
                _L("Preset \"%1%\" is not compatible with the new printer profile and it has the following unsaved changes:") :
                _L("Preset \"%1%\" is not compatible with the new print profile and it has the following unsaved changes:"),
                presets->get_edited_preset().name);
        }
        m_action_line->SetLabel(action_msg);
    }

    update_tree(type, presets, new_selected_preset);
}

void UnsavedChangesDialog::update_tree(Preset::Type type, PresetCollection* presets_, const std::string& new_selected_preset)
{
    // update searcher befofre update of tree
    wxGetApp().check_and_update_searcher();
    Search::OptionsSearcher& searcher = wxGetApp().searcher();
    searcher.sort_options_by_key();

    // list of the presets with unsaved changes
    std::vector<PresetCollection*> presets_list;
    if (type == Preset::TYPE_INVALID)
    {
        PrinterTechnology printer_technology = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();

        for (Tab* tab : wxGetApp().tabs_list)
            if (tab->supports_printer_technology(printer_technology) && tab->current_preset_is_dirty())
                presets_list.emplace_back(tab->get_presets());
    }
    else
        presets_list.emplace_back(presets_);

    // Display a dialog showing the dirty options in a human readable form.
    for (PresetCollection* presets : presets_list)
    {
        const DynamicPrintConfig& old_config = presets->get_selected_preset().config;
        const PrinterTechnology&  old_pt     = presets->get_selected_preset().printer_technology();
        const DynamicPrintConfig& mod_config = presets->get_edited_preset().config;
        const DynamicPrintConfig& new_config = m_tree->has_new_value_column() ? presets->find_preset(new_selected_preset, false, false)->config : mod_config;
        type = presets->type();

        const std::map<wxString, std::string>& category_icon_map = wxGetApp().get_tab(type)->get_category_icon_map();

        m_tree->model->AddPreset(type, from_u8(presets->get_edited_preset().name), old_pt, from_u8(new_selected_preset));

        // Collect dirty options.
        const bool deep_compare = type != Preset::TYPE_FILAMENT;
        auto dirty_options = presets->current_dirty_options(deep_compare);

        // process changes of extruders count
        if (type == Preset::TYPE_PRINTER && old_pt == ptFFF &&
            old_config.opt<ConfigOptionStrings>("extruder_colour")->values.size() != mod_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()) {
            wxString local_label = _L("Extruders count");
            wxString old_val = from_u8((boost::format("%1%") % old_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());
            wxString mod_val = from_u8((boost::format("%1%") % mod_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());
            wxString new_val = !m_tree->has_new_value_column() ? "" : from_u8((boost::format("%1%") % new_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());

            m_tree->Append("extruders_count", type, _L("General"), _L("Capabilities"), local_label, old_val, mod_val, new_val, category_icon_map.at("General"));
        }

        wxString custom_gcode_local_name = wxEmptyString;

        for (const std::string& opt_key : dirty_options) {
            const Search::Option& option = searcher.get_option(opt_key, type);
            if (option.opt_key() != opt_key) {
                // When founded option isn't the correct one.
                // It can be for dirty_options: "default_print_profile", "printer_model", "printer_settings_id",
                // because of they don't exist in searcher
                continue;
            }

            if (custom_gcode_local_name.IsEmpty() && boost::nowide::narrow(option.category) == "Custom G-code")
                custom_gcode_local_name = option.category_local;

            m_tree->Append(opt_key, type, option.category_local, option.group_local, option.label_local,
                get_string_value(opt_key, old_config), get_string_value(opt_key, mod_config), 
                m_tree->has_new_value_column() ? get_string_value(opt_key, new_config) : "", category_icon_map.at(option.category));
        }

        // Unselect all Custom-Gcodes
        if (!custom_gcode_local_name.IsEmpty()) {
            wxDataViewItem item = m_tree->model->GetItemByName(custom_gcode_local_name);
            if (item.IsOk()) {
                wxVariant variant = false;
                m_tree->model->SetValue(variant, item, DiffModel::colToggle);
                m_tree->update_item_enabling(item);
            }
        }
    }

    // Revert sort of searcher back
    searcher.sort_options_by_label();
}

wxString UnsavedChangesDialog::msg_success_saved_modifications(size_t saved_presets_cnt)
{
    return _L_PLURAL("The preset modifications are successfully saved",
                     "The presets modifications are successfully saved", static_cast<unsigned int>(saved_presets_cnt));
}

void UnsavedChangesDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    int em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL, m_save_btn_id, m_move_btn_id, m_continue_btn_id }, 1.5);

    const wxSize& size = wxSize(70 * em, 30 * em);
    SetMinSize(size);

    m_tree->Rescale(em);

    Fit();
    Refresh();
}

void UnsavedChangesDialog::on_sys_color_changed()
{
    for (auto btn : { m_save_btn, m_transfer_btn, m_discard_btn } )
        btn->sys_color_changed();
    // msw_rescale updates just icons, so use it
    m_tree->Rescale();

    Refresh();
}


//------------------------------------------
//          FullCompareDialog
//------------------------------------------

FullCompareDialog::FullCompareDialog(const wxString& option_name, const wxString& old_value, const wxString& mod_value, const wxString& new_value,
                                     const wxString& old_value_header, const wxString& mod_value_header, const wxString& new_value_header)
    : wxDialog(nullptr, wxID_ANY, option_name, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxGetApp().UpdateDarkUI(this);
    this->SetFont(wxGetApp().normal_font());

    int border = 10;
    bool has_new_value_column = !new_value_header.IsEmpty();

    wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, this);

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2, has_new_value_column ? 3 : 2, 1, 0);
    grid_sizer->SetFlexibleDirection(wxBOTH);
    for (int col = 0 ; col < grid_sizer->GetCols(); col++)
        grid_sizer->AddGrowableCol(col, 1);
    grid_sizer->AddGrowableRow(1,1);

    auto add_header = [grid_sizer, border, this](wxString label) {
        wxStaticText* text = new wxStaticText(this, wxID_ANY, label);
        text->SetFont(this->GetFont().Bold());
        grid_sizer->Add(text, 0, wxALL, border);
    };

    add_header(old_value_header);
    add_header(mod_value_header);
    if (has_new_value_column)
        add_header(new_value_header);

    auto get_set_from_val = [](wxString str) {
        if (str.Find("\n") == wxNOT_FOUND)
            str.Replace(" ", "\n");

        std::set<wxString> str_set;

        wxStringTokenizer strings(str, "\n");
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

    std::set_difference(old_set.begin(), old_set.end(), mod_set.begin(), mod_set.end(), std::inserter(old_mod_diff_set, old_mod_diff_set.begin()));
    std::set_difference(mod_set.begin(), mod_set.end(), old_set.begin(), old_set.end(), std::inserter(mod_old_diff_set, mod_old_diff_set.begin()));
    std::set_difference(new_set.begin(), new_set.end(), old_set.begin(), old_set.end(), std::inserter(new_old_diff_set, new_old_diff_set.begin()));

    auto add_value = [grid_sizer, border, this](wxString label, const std::set<wxString>& diff_set, bool is_colored = false) {
        wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, label, wxDefaultPosition, wxSize(400, 400), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_DEFAULT | wxTE_RICH);
        wxGetApp().UpdateDarkUI(text);
        text->SetStyle(0, label.Len(), wxTextAttr(is_colored ? wxColour(orange) : wxNullColour, wxNullColour, this->GetFont()));

        for (const wxString& str : diff_set) {
            int pos = label.First(str);
            if (pos == wxNOT_FOUND)
                continue;
            text->SetStyle(pos, pos + (int)str.Len(), wxTextAttr(is_colored ? wxColour(orange) : wxNullColour, wxNullColour, this->GetFont().Bold()));
        }

        grid_sizer->Add(text, 1, wxALL | wxEXPAND, border);
    };
    add_value(old_value, old_mod_diff_set);
    add_value(mod_value, mod_old_diff_set, true);
    if (has_new_value_column)
        add_value(new_value, new_old_diff_set);

    sizer->Add(grid_sizer, 1, wxEXPAND);

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);
    wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_OK, this)), true);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(sizer,   1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(buttons, 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}


static PresetCollection* get_preset_collection(Preset::Type type, PresetBundle* preset_bundle = nullptr) {
    if (!preset_bundle)
        preset_bundle = wxGetApp().preset_bundle;
    return  type == Preset::Type::TYPE_PRINT        ? &preset_bundle->prints :
            type == Preset::Type::TYPE_SLA_PRINT    ? &preset_bundle->sla_prints :
            type == Preset::Type::TYPE_FILAMENT     ? &preset_bundle->filaments :
            type == Preset::Type::TYPE_SLA_MATERIAL ? &preset_bundle->sla_materials :
            type == Preset::Type::TYPE_PRINTER      ? &preset_bundle->printers :
            nullptr;
}

//------------------------------------------
//          DiffPresetDialog
//------------------------------------------
static std::string get_selection(PresetComboBox* preset_combo)
{
    return into_u8(preset_combo->GetString(preset_combo->GetSelection()));
}

void DiffPresetDialog::create_presets_sizer()
{
    m_presets_sizer = new wxBoxSizer(wxVERTICAL);

    for (auto new_type : { Preset::TYPE_PRINT, Preset::TYPE_SLA_PRINT, Preset::TYPE_FILAMENT, Preset::TYPE_SLA_MATERIAL, Preset::TYPE_PRINTER })
    {
        const PresetCollection* collection = get_preset_collection(new_type);
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        PresetComboBox* presets_left;
        PresetComboBox* presets_right;
        ScalableButton* equal_bmp = new ScalableButton(this, wxID_ANY, "equal");

        auto add_preset_combobox = [collection, sizer, new_type, this](PresetComboBox** cb_, PresetBundle* preset_bundle) {
            *cb_ = new PresetComboBox(this, new_type, wxSize(em_unit() * 35, -1), preset_bundle);
            PresetComboBox*cb = (*cb_);
            cb->SetFont(this->GetFont());
            cb->show_modif_preset_separately();
            cb->set_selection_changed_function([this, new_type, preset_bundle, cb](int selection) {
                std::string preset_name = Preset::remove_suffix_modified(cb->GetString(selection).ToUTF8().data());
                if (m_view_type == Preset::TYPE_INVALID)
                    update_compatibility(preset_name, new_type, preset_bundle);
                // update selection inside of related presets
                preset_bundle->get_presets(new_type).select_preset_by_name(preset_name, true);
                update_tree(); 
            });
            if (collection->get_selected_idx() != (size_t)-1)
                cb->update(collection->get_selected_preset().name);

            sizer->Add(cb, 1);
            cb->Show(new_type == Preset::TYPE_PRINTER);
        };
        add_preset_combobox(&presets_left, &m_preset_bundle_left);
        sizer->Add(equal_bmp, 0, wxRIGHT | wxLEFT | wxALIGN_CENTER_VERTICAL, 5);
        add_preset_combobox(&presets_right, &m_preset_bundle_right);
        m_presets_sizer->Add(sizer, 1, wxTOP, 5);
        equal_bmp->Show(new_type == Preset::TYPE_PRINTER);

        m_preset_combos.push_back({ presets_left, equal_bmp, presets_right });

        equal_bmp->Bind(wxEVT_BUTTON, [presets_left, presets_right, this](wxEvent&) {
            std::string preset_name = get_selection(presets_left);
            presets_right->update(preset_name); 
            if (m_view_type == Preset::TYPE_INVALID)
                update_compatibility(preset_name, presets_right->get_type(), &m_preset_bundle_right);
            update_tree();
        });
    }
}

void DiffPresetDialog::create_show_all_presets_chb()
{
    m_show_all_presets = new wxCheckBox(this, wxID_ANY, _L("Show all presets (including incompatible)"));
    m_show_all_presets->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        bool show_all = m_show_all_presets->GetValue();
        for (auto preset_combos : m_preset_combos) {
            if (preset_combos.presets_left->get_type() == Preset::TYPE_PRINTER)
                continue;
            preset_combos.presets_left->show_all(show_all);
            preset_combos.presets_right->show_all(show_all);
        }
        if (m_view_type == Preset::TYPE_INVALID)
            update_tree();
    });
}

void DiffPresetDialog::create_info_lines()
{
//    const wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
    const wxFont font = GetFont().Bold();

    m_top_info_line = new wxStaticText(this, wxID_ANY, _L("Select presets to compare"));
    m_top_info_line->SetFont(font);

    m_bottom_info_line = new wxStaticText(this, wxID_ANY, "");
    m_bottom_info_line->SetFont(font);
}

void DiffPresetDialog::create_tree()
{
    m_tree = new DiffViewCtrl(this, wxSize(em_unit() * 65, em_unit() * 40));
    m_tree->SetFont(this->GetFont());
    m_tree->AppendToggleColumn_(L"\u2714", DiffModel::colToggle, wxLinux ? 9 : 6);
    m_tree->AppendBmpTextColumn("",                      DiffModel::colIconText, 35);
    m_tree->AppendBmpTextColumn(_L("Left Preset Value"), DiffModel::colOldValue, 15);
    m_tree->AppendBmpTextColumn(_L("Right Preset Value"),DiffModel::colModValue, 15);
    m_tree->Hide();
    m_tree->GetColumn(DiffModel::colToggle)->SetHidden(true);
}

std::array<Preset::Type, 3> DiffPresetDialog::types_list() const
{
    return PresetBundle::types_list(m_pr_technology);
}

void DiffPresetDialog::create_buttons()
{
    wxFont font = this->GetFont().Scaled(1.4f);
    m_buttons   = new wxBoxSizer(wxHORIZONTAL);

    auto show_in_bottom_info = [this](const wxString& ext_line, wxMouseEvent& e) {
        m_bottom_info_line->SetLabel(ext_line);
        m_bottom_info_line->Show(true);
        Layout();
        e.Skip();
    };

    // Transfer 
    m_transfer_btn = new ScalableButton(this, wxID_ANY, "paste_menu", _L("Transfer"), wxDefaultSize, wxDefaultPosition, wxBORDER_DEFAULT, 24);
    m_transfer_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) { button_event(Action::Transfer);});


    auto enable_transfer = [this](const Preset::Type& type) {
        const Preset& main_edited_preset = get_preset_collection(type, wxGetApp().preset_bundle)->get_edited_preset();
        if (main_edited_preset.is_dirty)
            return main_edited_preset.name == get_right_preset_name(type);
        return true;
    };
    m_transfer_btn->Bind(wxEVT_UPDATE_UI, [this, enable_transfer](wxUpdateUIEvent& evt) {
        bool enable = m_tree->has_selection();
        if (enable) {
            if (m_view_type == Preset::TYPE_INVALID) {
                for (const Preset::Type& type : types_list())
                    if (!enable_transfer(type)) {
                        enable = false;
                        break;
                    }
            }
            else
                enable = enable_transfer(m_view_type);
        }
        evt.Enable(enable);
    });
    m_transfer_btn->Bind(wxEVT_ENTER_WINDOW, [show_in_bottom_info](wxMouseEvent& e) {
        show_in_bottom_info(_L("Transfer the selected options from left preset to the right.\n"
                            "Note: New modified presets will be selected in settings tabs after close this dialog."), e); });

    // Save
    m_save_btn = new ScalableButton(this, wxID_ANY, "save", _L("Save"), wxDefaultSize, wxDefaultPosition, wxBORDER_DEFAULT, 24);
    m_save_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) { button_event(Action::Save); });
    m_save_btn->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(m_tree->has_selection()); });
    m_save_btn->Bind(wxEVT_ENTER_WINDOW, [show_in_bottom_info](wxMouseEvent& e) {
        show_in_bottom_info(_L("Save the selected options from left preset to the right."), e); });

    // Cancel
    m_cancel_btn = new ScalableButton(this, wxID_CANCEL, "cross", _L("Cancel"), wxDefaultSize, wxDefaultPosition, wxBORDER_DEFAULT, 24);
    m_cancel_btn->Bind(wxEVT_BUTTON, [this](wxEvent&) { button_event(Action::Discard);});

    for (ScalableButton* btn : { m_transfer_btn, m_save_btn, m_cancel_btn }) {
        btn->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) { update_bottom_info(); Layout(); e.Skip(); });
        m_buttons->Add(btn, 1, wxLEFT, 5);
        btn->SetFont(font);
    }

    m_buttons->Show(false);
}

void DiffPresetDialog::create_edit_sizer()
{
    // Add check box for the edit mode
    m_use_for_transfer = new wxCheckBox(this, wxID_ANY, _L("Transfer values from left to right"));
    m_use_for_transfer->SetToolTip(_L("If checked, this dialog can be used for transferring selected values from the preset on the left to the preset on the right."));
    m_use_for_transfer->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        bool use = m_use_for_transfer->GetValue();
        m_tree->GetColumn(DiffModel::colToggle)->SetHidden(!use);
        if (m_tree->IsShown()) {
            m_buttons->Show(use);
            Fit();
            Refresh();
        }
        else
            this->Layout();
    });

    // Add Buttons 
    create_buttons();

    // Create and fill edit sizer
    m_edit_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_edit_sizer->Add(m_use_for_transfer, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);
    m_edit_sizer->AddSpacer(em_unit() * 10);
    m_edit_sizer->Add(m_buttons, 1, wxLEFT, 5);
    m_edit_sizer->Show(false);
}

void DiffPresetDialog::complete_dialog_creation()
{
    wxBoxSizer*topSizer = new wxBoxSizer(wxVERTICAL);

    int border = 10;
    topSizer->Add(m_top_info_line,      0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 2 * border);
    topSizer->Add(m_presets_sizer,      0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_show_all_presets,   0, wxEXPAND | wxALL, border);
    topSizer->Add(m_tree,               1, wxEXPAND | wxALL, border);
    topSizer->Add(m_bottom_info_line,   0, wxEXPAND | wxALL, 2 * border);
    topSizer->Add(m_edit_sizer,         0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, 2 * border);

    this->SetMinSize(wxSize(80 * em_unit(), 30 * em_unit()));
    this->SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

DiffPresetDialog::DiffPresetDialog(MainFrame* mainframe)
    : DPIDialog(mainframe, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER, "diff_presets_dialog", mainframe->normal_font().GetPointSize()),
    m_pr_technology(wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology())
{    
    // Init bundles

    assert(wxGetApp().preset_bundle);

    m_preset_bundle_left  = *wxGetApp().preset_bundle;
    m_preset_bundle_right = *wxGetApp().preset_bundle;

    // Create UI items

    create_info_lines();

    create_presets_sizer();

    create_show_all_presets_chb();

    create_tree();

    create_edit_sizer();

    complete_dialog_creation();
}

void DiffPresetDialog::update_controls_visibility(Preset::Type type /* = Preset::TYPE_INVALID*/)
{
    for (auto preset_combos : m_preset_combos) {
        Preset::Type cb_type = preset_combos.presets_left->get_type();
        bool show = type != Preset::TYPE_INVALID    ? type == cb_type :
                    cb_type == Preset::TYPE_PRINTER ? true : 
                    m_pr_technology == ptFFF        ? cb_type == Preset::TYPE_PRINT || cb_type == Preset::TYPE_FILAMENT :
                                                      cb_type == Preset::TYPE_SLA_PRINT || cb_type == Preset::TYPE_SLA_MATERIAL;
        preset_combos.presets_left->Show(show);
        preset_combos.equal_bmp->Show(show);
        preset_combos.presets_right->Show(show);

        if (show) {
            if (cb_type == Preset::TYPE_FILAMENT) {
                int extruder_id = 0;
                if (type == Preset::TYPE_FILAMENT)
                    extruder_id = dynamic_cast<TabFilament*>(wxGetApp().get_tab(Preset::TYPE_FILAMENT))->get_active_extruder();
                preset_combos.presets_left->set_extruder_idx(extruder_id);
                preset_combos.presets_right->set_extruder_idx(extruder_id);
            }
            preset_combos.presets_left->update_from_bundle();
            preset_combos.presets_right->update_from_bundle();
        }
    }

    m_show_all_presets->Show(type != Preset::TYPE_PRINTER);
}

void DiffPresetDialog::update_bundles_from_app()
{
    m_preset_bundle_left  = *wxGetApp().preset_bundle;
    m_preset_bundle_right = *wxGetApp().preset_bundle;

    m_pr_technology = m_preset_bundle_left.printers.get_edited_preset().printer_technology();

    for (auto preset_combos : m_preset_combos) {
        if (m_view_type == Preset::TYPE_INVALID || m_view_type == preset_combos.presets_left->get_type()) {
            preset_combos.presets_left->init_from_bundle(&m_preset_bundle_left);
            preset_combos.presets_right->init_from_bundle(&m_preset_bundle_right);
        }
    }
}

void DiffPresetDialog::show(Preset::Type type /* = Preset::TYPE_INVALID*/)
{
    this->SetTitle(_L("Compare Presets"));
    m_view_type = type;

    update_bundles_from_app();
    update_controls_visibility(type);
    if (type == Preset::TYPE_INVALID)
        Fit();

    update_tree();

    // if this dialog is shown it have to be Hide and show again to be placed on the very Top of windows
    if (IsShown())
        Hide();
    Show();
}

void DiffPresetDialog::update_presets(Preset::Type type, bool update_preset_bundles_from_app/* = true */)
{
    if (update_preset_bundles_from_app)
        update_bundles_from_app();
    update_controls_visibility(type);

    if (type == Preset::TYPE_INVALID)
        for (auto preset_combos : m_preset_combos) {
            if (preset_combos.presets_left->get_type() == Preset::TYPE_PRINTER) {
                preset_combos.presets_left->update_from_bundle ();
                preset_combos.presets_right->update_from_bundle();
                break;
            }
        }
    else 
        for (auto preset_combos : m_preset_combos) {
            if (preset_combos.presets_left->get_type() == type) {
                preset_combos.presets_left->update();
                preset_combos.presets_right->update();
                break;
            }
        }

    update_tree();
}

void DiffPresetDialog::update_bottom_info(wxString bottom_info)
{
    if (m_tree->has_long_strings())
        bottom_info = _L("Some fields are too long to fit. Right mouse click reveals the full text.");

    const bool show_bottom_info = !m_tree->IsShown() || m_tree->has_long_strings();
    if (show_bottom_info)
        m_bottom_info_line->SetLabel(bottom_info);
    m_bottom_info_line->Show(show_bottom_info);
}

void DiffPresetDialog::update_tree()
{
    // update searcher before update of tree
    wxGetApp().check_and_update_searcher();
    Search::OptionsSearcher& searcher = wxGetApp().searcher();
    searcher.sort_options_by_key();

    m_tree->Clear();
    wxString bottom_info = "";
    bool show_tree = false;

    for (auto preset_combos : m_preset_combos)
    {
        if (!preset_combos.presets_left->IsShown())
            continue;
        Preset::Type type = preset_combos.presets_left->get_type();

        const PresetCollection* presets = get_preset_collection(type);

        std::string preset_name_full = get_selection(preset_combos.presets_left);
        std::string preset_name = Preset::remove_suffix_modified(preset_name_full);
        const Preset* left_preset  = presets->find_preset(preset_name, false, preset_name_full.length() != preset_name.length());
        preset_name_full = get_selection(preset_combos.presets_right);
        preset_name = Preset::remove_suffix_modified(preset_name_full);
        const Preset* right_preset = presets->find_preset(preset_name, false, preset_name_full.length() != preset_name.length());

        if (!left_preset || !right_preset) {
            bottom_info = _L("One of the presets doesn't found");
            preset_combos.equal_bmp->SetBitmap_(ScalableBitmap(this, "question"));
            preset_combos.equal_bmp->SetToolTip(bottom_info);
            continue;
        }

        const DynamicPrintConfig& left_config   = left_preset->config;
        const PrinterTechnology&  left_pt       = left_preset->printer_technology();
        const DynamicPrintConfig& right_congig  = right_preset->config;

        if (left_pt != right_preset->printer_technology()) {
            bottom_info = _L("Compared presets has different printer technology");
            preset_combos.equal_bmp->SetBitmap_(ScalableBitmap(this, "question"));
            preset_combos.equal_bmp->SetToolTip(bottom_info);
            continue;
        }

        // Collect dirty options.
        const bool deep_compare = type != Preset::TYPE_FILAMENT;
        auto dirty_options = type == Preset::TYPE_PRINTER && left_pt == ptFFF &&
                             left_config.opt<ConfigOptionStrings>("extruder_colour")->values.size() < right_congig.opt<ConfigOptionStrings>("extruder_colour")->values.size() ?
                             presets->dirty_options(right_preset, left_preset, deep_compare) :
                             presets->dirty_options(left_preset, right_preset, deep_compare);

        if (dirty_options.empty()) {
            bottom_info = _L("Presets are the same");
            preset_combos.equal_bmp->SetBitmap_(ScalableBitmap(this, "equal"));
            preset_combos.equal_bmp->SetToolTip(bottom_info);
            continue;
        }

        show_tree = true;
        preset_combos.equal_bmp->SetBitmap_(ScalableBitmap(this, "not_equal"));
        preset_combos.equal_bmp->SetToolTip(_L("Presets are different.\n"
                                               "Click this button to select the same preset for the right and left preset."));

        m_tree->model->AddPreset(type, "\"" + from_u8(left_preset->name) + "\" vs \"" + from_u8(right_preset->name) + "\"", left_pt);

        const std::map<wxString, std::string>& category_icon_map = wxGetApp().get_tab(type)->get_category_icon_map();

        // process changes of extruders count
        if (type == Preset::TYPE_PRINTER && left_pt == ptFFF &&
            left_config.opt<ConfigOptionStrings>("extruder_colour")->values.size() != right_congig.opt<ConfigOptionStrings>("extruder_colour")->values.size()) {
            wxString local_label = _L("Extruders count");
            wxString left_val = from_u8((boost::format("%1%") % left_config.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());
            wxString right_val = from_u8((boost::format("%1%") % right_congig.opt<ConfigOptionStrings>("extruder_colour")->values.size()).str());

            m_tree->Append("extruders_count", type, _L("General"), _L("Capabilities"), local_label, left_val, right_val, "", category_icon_map.at("General"));
        }

        for (const std::string& opt_key : dirty_options) {
            wxString left_val = get_string_value(opt_key, left_config);
            wxString right_val = get_string_value(opt_key, right_congig);

            Search::Option option = searcher.get_option(opt_key, get_full_label(opt_key, left_config), type);
            if (option.opt_key() != opt_key) {
#ifdef _DEBUG
                // temporary solution, just for testing
                m_tree->Append(opt_key, type, _L("Undef category"), _L("Undef group"), opt_key, left_val, right_val, "", "question");
#endif
                // When founded option isn't the correct one.
                // It can be for dirty_options: "default_print_profile", "printer_model", "printer_settings_id",
                // because of they don't exist in searcher
                continue;
            }
            m_tree->Append(opt_key, type, option.category_local, option.group_local, option.label_local,
                left_val, right_val, "", category_icon_map.at(option.category));
        }
    }

    bool tree_was_shown = m_tree->IsShown();
    m_tree->Show(show_tree);

    bool can_transfer_options = m_view_type == Preset::TYPE_INVALID || get_left_preset_name(m_view_type) != get_right_preset_name(m_view_type);
    m_edit_sizer->Show(show_tree && can_transfer_options);
    m_buttons->Show(m_edit_sizer->IsShown(size_t(0)) && m_use_for_transfer->GetValue());
   
    update_bottom_info(bottom_info);

    if (tree_was_shown == m_tree->IsShown())
        Layout();
    else {
        Fit();
        Refresh();
    }

    // Revert sort of searcher back
    searcher.sort_options_by_label();
}

void DiffPresetDialog::on_dpi_changed(const wxRect&)
{
    int em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL});

    const wxSize& size = wxSize(80 * em, 30 * em);
    SetMinSize(size);

    auto rescale = [em](PresetComboBox* pcb) {
        pcb->msw_rescale();
        wxSize sz = wxSize(35 * em, -1);
        pcb->SetMinSize(sz);
        pcb->SetSize(sz);
    };

    for (auto preset_combos : m_preset_combos) {
        rescale(preset_combos.presets_left);
        rescale(preset_combos.presets_right);
    }

    m_tree->Rescale(em);

    Fit();
    Refresh();
}

void DiffPresetDialog::on_sys_color_changed()
{
#ifdef _WIN32
    wxGetApp().UpdateAllStaticTextDarkUI(this);
    wxGetApp().UpdateDarkUI(m_show_all_presets);
    wxGetApp().UpdateDVCDarkUI(m_tree);
#endif

    for (auto preset_combos : m_preset_combos) {
        preset_combos.presets_left->sys_color_changed();
        preset_combos.equal_bmp->sys_color_changed();
        preset_combos.presets_right->sys_color_changed();
    }

    for (ScalableButton* btn : { m_transfer_btn, m_save_btn, m_cancel_btn })
        btn->sys_color_changed();

    // msw_rescale updates just icons, so use it
    m_tree->Rescale();
    Refresh();
}

void DiffPresetDialog::update_compatibility(const std::string& preset_name, Preset::Type type, PresetBundle* preset_bundle)
{
    PresetCollection* presets = get_preset_collection(type, preset_bundle);

    bool print_tab = type == Preset::TYPE_PRINT || type == Preset::TYPE_SLA_PRINT;
    bool printer_tab = type == Preset::TYPE_PRINTER;
    bool technology_changed = false;

    if (printer_tab) {
        const Preset& new_printer_preset = *presets->find_preset(preset_name, true);
        PrinterTechnology    old_printer_technology = presets->get_selected_preset().printer_technology();
        PrinterTechnology    new_printer_technology = new_printer_preset.printer_technology();

        technology_changed = old_printer_technology != new_printer_technology;
    }

    // select preset 
    presets->select_preset_by_name(preset_name, false);

    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // The following method should not discard changes of current print or filament presets on change of a printer profile,
    // if they are compatible with the current printer.
    auto update_compatible_type = [](bool technology_changed, bool on_page, bool show_incompatible_presets) {
        return  technology_changed ? PresetSelectCompatibleType::Always :
            on_page ? PresetSelectCompatibleType::Never :
            show_incompatible_presets ? PresetSelectCompatibleType::OnlyIfWasCompatible : PresetSelectCompatibleType::Always;
    };
    if (print_tab || printer_tab)
        preset_bundle->update_compatible(
            update_compatible_type(technology_changed, print_tab, true),
            update_compatible_type(technology_changed, false, true));

    bool is_left_presets = preset_bundle == &m_preset_bundle_left;
    PrinterTechnology pr_tech = preset_bundle->printers.get_selected_preset().printer_technology();

    // update preset comboboxes
    for (auto preset_combos : m_preset_combos)
    {
        PresetComboBox* cb = is_left_presets ? preset_combos.presets_left : preset_combos.presets_right;
        Preset::Type presets_type = cb->get_type();
        if ((print_tab && (
                (pr_tech == ptFFF && presets_type == Preset::TYPE_FILAMENT) ||
                (pr_tech == ptSLA && presets_type == Preset::TYPE_SLA_MATERIAL) )) ||
            (printer_tab && (
                (pr_tech == ptFFF && (presets_type == Preset::TYPE_PRINT || presets_type == Preset::TYPE_FILAMENT) ) ||
                (pr_tech == ptSLA && (presets_type == Preset::TYPE_SLA_PRINT || presets_type == Preset::TYPE_SLA_MATERIAL) )) ))
            cb->update();
    }

    if (technology_changed &&
        m_preset_bundle_left.printers.get_selected_preset().printer_technology() ==
        m_preset_bundle_right.printers.get_selected_preset().printer_technology())
    {
        m_pr_technology = m_preset_bundle_left.printers.get_edited_preset().printer_technology();
        update_controls_visibility();
    }
}

bool DiffPresetDialog::is_save_confirmed()
{
    presets_to_save.clear();

    std::vector<Preset::Type> types_for_save;

    for (const Preset::Type& type : types_list()) {
        if (!m_tree->options(type, true).empty()) {
            types_for_save.emplace_back(type);
            presets_to_save.emplace_back(PresetToSave{ type, get_left_preset_name(type), get_right_preset_name(type), get_right_preset_name(type) });
        }
    }

    if (!types_for_save.empty()) {
        SavePresetDialog save_dlg(this, types_for_save, _u8L("Modified"), false, &m_preset_bundle_right);
        if (save_dlg.ShowModal() != wxID_OK)
            return false;

        for (auto& preset : presets_to_save) {
            const std::string& name = save_dlg.get_name(preset.type);
            if (!name.empty())
                preset.new_name = name;
        }
    }
    return true;
}

std::vector<std::string> DiffPresetDialog::get_options_to_save(Preset::Type type)
{
    auto options = m_tree->options(type, true);

    // erase "inherits" option from the list if it exists there
    if (const auto it = std::find(options.begin(), options.end(), "inherits"); it != options.end())
        options.erase(it);

    if (type == Preset::TYPE_PRINTER) {
        // erase "extruders_count" option from the list if it exists there
        if (const auto it = std::find(options.begin(), options.end(), "extruders_count"); it != options.end())
            options.erase(it);
    }
    return options;
}

void DiffPresetDialog::button_event(Action act)
{
    if (act == Action::Save) {
        if (is_save_confirmed()) {
            size_t saved_cnt = 0;
            for (const auto& preset : presets_to_save)
                if (wxGetApp().preset_bundle->transfer_and_save(preset.type, preset.from_name, preset.to_name, preset.new_name, get_options_to_save(preset.type)))
                    saved_cnt++;

            if (saved_cnt > 0) {
                MessageDialog(this, UnsavedChangesDialog::msg_success_saved_modifications(saved_cnt)).ShowModal();
                update_bundles_from_app();
                for (const auto& preset : presets_to_save) {
                    m_preset_bundle_left.get_presets(preset.type).select_preset_by_name(preset.from_name, true);
                    m_preset_bundle_right.get_presets(preset.type).select_preset_by_name(preset.new_name, true);
                }
                update_presets(m_view_type, false);
            }
        }
    }
    else {
        Hide();

        if (act == Action::Transfer)
            process_options([this](Preset::Type type) {
                if (Tab* tab = wxGetApp().get_tab(type))
                    tab->transfer_options(get_left_preset_name(type),
                                          get_right_preset_name(type),
                                          get_selected_options(type));
            });
        else if (!presets_to_save.empty())
            process_options([](Preset::Type type) {
                if (Tab* tab = wxGetApp().get_tab(type)) {
                    tab->update_preset_choice();
                    wxGetApp().sidebar().update_presets(type);
                }
            });
    }
}

void DiffPresetDialog::process_options(std::function<void(Preset::Type)> process)
{
    if (m_view_type == Preset::TYPE_INVALID) {
        for (const Preset::Type& type : types_list())
            process(type);
    }
    else
        process(m_view_type);
};

std::string DiffPresetDialog::get_left_preset_name(Preset::Type type)
{
    PresetComboBox* cb = m_preset_combos[int(type - Preset::TYPE_PRINT)].presets_left;
    return Preset::remove_suffix_modified(get_selection(cb));
}

std::string DiffPresetDialog::get_right_preset_name(Preset::Type type)
{
    PresetComboBox* cb = m_preset_combos[int(type - Preset::TYPE_PRINT)].presets_right;
    return Preset::remove_suffix_modified(get_selection(cb));
}

}

}    // namespace Slic3r::GUI
