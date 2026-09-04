#include "Slic3r/App/WX/DiffDVCModel.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/ExtraRenderers.hpp"
#include "Slic3r/App/WX/I18N.hpp"

#include "Slic3r/Biz/Algorithms/Color.hpp"

#include <wx/log.h>
#include <wx/string.h>

#include <fmt/format.h>

namespace Slic3r::App::WX {

// ----------------------------------------------------------------------------
// ModelNode: a node inside DiffDVCModel
// ----------------------------------------------------------------------------

using namespace Slic3r::Domain::Preset;

#ifdef __linux__
using DataViewBitmapOrIconText = wxDataViewIconText;
#else
using DataViewBitmapOrIconText = DataViewBitmapText;
#endif

static const std::map<PresetKind, std::string> type_icon_names = {
    {PresetKind::FdmPrint, "cog"},
    {PresetKind::FdmToolPrint, "funnel"},
    {PresetKind::SlaPrint, "cog"},
    {PresetKind::SlaToolPrint, ""},
    {PresetKind::FdmMaterial, "spool"},
    {PresetKind::SlaMaterial, "resin"},
    {PresetKind::FdmPrinter, "printer"},
    {PresetKind::SlaPrinter, "sla_printer"},
};

static std::string get_icon_name(PresetKind kind)
{
    return type_icon_names.at(kind);
}

using Slic3r::Biz::Algorithms::Color::encode_color;
using Slic3r::Domain::ColorRGB;

static std::string def_text_color()
{
    wxColour def_colour = w_config()->get_label_clr_default();
    return encode_color(ColorRGB(def_colour.Red(), def_colour.Green(), def_colour.Blue()));
}

static void color_string(std::string& str, const std::string& color)
{
    str = fmt::format("<span color=\"{}\">{}</span>", color, str);
}

static void color_string(wxString& str, const std::string& color)
{
    std::string u8_str = into_u8(str);
    color_string(u8_str, color);
    str = from_u8(u8_str);
}

static void make_string_bold(std::string& str)
{
    str = fmt::format("<b>{}</b>", str);
}

static void make_string_italic(std::string& str)
{
    str = fmt::format("<i>{}</i>", str);
}

std::string ModelNode::grey()
{
    return "#808080";
}

std::string ModelNode::orange()
{
    return "#ed6b21";
}

wxBitmapOrIcon ModelNode::get_bitmap(const std::string& color)
{
    wxBitmap bmp = get_solid_bmp_bundle(64, 16, color)->GetBitmapFor(m_parent_win);
    if (!m_toggle)
        bmp = bmp.ConvertToDisabled();
#ifdef __linux__
    wxIcon icon;
    icon.CopyFromBitmap(bmp);
    return icon;
#else
    return bmp;
#endif // __linux__
}

ModelNode::ModelNode(
    Slic3r::Domain::Preset::PresetKind kind,
    wxWindow* parent_win,
    const wxString& text,
    const std::string& icon_name,
    const wxString& new_val_column_text
) :
    m_parent_win(parent_win),
    m_parent(nullptr),
    m_preset_kind(kind),
    m_icon_name(icon_name),
    m_text(text),
    m_new_value(new_val_column_text)
{
    UpdateIcons();
}

ModelNode::ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_icon_name(icon_name),
    m_text(text)
{
    UpdateIcons();
}

ModelNode::ModelNode(ModelNode* parent, const wxString& text) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_icon_name("dot_small"),
    m_text(text)
{
    UpdateIcons();
}

ModelNode::ModelNode(
    ModelNode* parent,
    const wxString& text,
    const wxString& old_value,
    const wxString& mod_value,
    const wxString& new_value
) :
    m_parent_win(parent->m_parent_win),
    m_parent(parent),
    m_old_color(old_value.StartsWith(from_u8("#")) ? into_u8(old_value) : ""),
    m_mod_color(mod_value.StartsWith(from_u8("#")) ? into_u8(mod_value) : ""),
    m_icon_name("empty"),
    m_new_color(new_value.StartsWith(from_u8("#")) ? into_u8(new_value) : ""),
    m_text(text),
    m_old_value(old_value),
    m_mod_value(mod_value),
    m_new_value(new_value),
    m_container(false)
{
    // check if old/new_value is color
    if (m_old_color.empty()) {
        if (!m_mod_color.empty())
            m_old_value = _L("Undef");
    } else {
        m_old_color_bmp = get_bitmap(m_old_color);
        m_old_value.Clear();
    }

    if (m_mod_color.empty()) {
        if (!m_old_color.empty())
            m_mod_value = _L("Undef");
    } else {
        m_mod_color_bmp = get_bitmap(m_mod_color);
        m_mod_value.Clear();
    }

    if (m_new_color.empty()) {
        if (!m_old_color.empty() || !m_mod_color.empty())
            m_new_value = _L("Undef");
    } else {
        m_new_color_bmp = get_bitmap(m_new_color);
        m_new_value.Clear();
    }

    // "color" strings
    color_string(m_old_value, def_text_color());
    color_string(m_mod_value, orange());
    color_string(m_new_value, def_text_color());

    UpdateIcons();
}

void ModelNode::UpdateEnabling()
{
    auto change_text_color =
        [](wxString& str, const std::string& clr_from, const std::string& clr_to)
    {
        std::string old_val = into_u8(str);
        boost::replace_all(old_val, clr_from, clr_to);
        str = WX::from_u8(old_val);
    };

    if (!m_toggle) {
        change_text_color(m_text, def_text_color(), grey());
        change_text_color(m_old_value, def_text_color(), grey());
        change_text_color(m_mod_value, orange(), grey());
        change_text_color(m_new_value, def_text_color(), grey());
    } else {
        change_text_color(m_text, grey(), def_text_color());
        change_text_color(m_old_value, grey(), def_text_color());
        change_text_color(m_mod_value, grey(), orange());
        change_text_color(m_new_value, grey(), def_text_color());
    }
    // update icons for the colors
    UpdateIcons();
}

void ModelNode::UpdateIcons()
{
    // update icons for the colors, if any exists
    if (!m_old_color.empty())
        m_old_color_bmp = get_bitmap(m_old_color);
    if (!m_mod_color.empty())
        m_mod_color_bmp = get_bitmap(m_mod_color);
    if (!m_new_color.empty())
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
// DiffDVCModel data model used by DiffViewCtrl, which is used by DiffDialog
// ----------------------------------------------------------------------------

ModelNode* DiffDVCModel::AddOption(
    ModelNode* group_node,
    const wxString& option_name,
    const wxString& old_value,
    const wxString& mod_value,
    const wxString& new_value
)
{
    group_node->Append(
        std::make_unique<ModelNode>(group_node, option_name, old_value, mod_value, new_value)
    );
    ModelNode* option         = group_node->GetChildren().back().get();
    wxDataViewItem group_item = wxDataViewItem((void*) group_node);
    ItemAdded(group_item, wxDataViewItem((void*) option));

    m_ctrl->Expand(group_item);
    return option;
    ;
}

ModelNode* DiffDVCModel::AddOptionWithGroup(
    ModelNode* category_node,
    const wxString& group_name,
    const wxString& option_name,
    const wxString& old_value,
    const wxString& mod_value,
    const wxString& new_value
)
{
    category_node->Append(std::make_unique<ModelNode>(category_node, group_name));
    ModelNode* group_node = category_node->GetChildren().back().get();
    ItemAdded(wxDataViewItem((void*) category_node), wxDataViewItem((void*) group_node));

    return AddOption(group_node, option_name, old_value, mod_value, new_value);
}

ModelNode* DiffDVCModel::AddOptionWithGroupAndCategory(
    ModelNode* preset_node,
    const wxString& category_name,
    const wxString& group_name,
    const wxString& option_name,
    const wxString& old_value,
    const wxString& mod_value,
    const wxString& new_value,
    const std::string category_icon_name
)
{
    preset_node->Append(
        std::make_unique<ModelNode>(preset_node, category_name, category_icon_name)
    );
    ModelNode* category_node = preset_node->GetChildren().back().get();
    ItemAdded(wxDataViewItem((void*) preset_node), wxDataViewItem((void*) category_node));

    return AddOptionWithGroup(
        category_node,
        group_name,
        option_name,
        old_value,
        mod_value,
        new_value
    );
}

DiffDVCModel::DiffDVCModel(wxWindow* parent) : m_parent_win(parent) {}

wxDataViewItem DiffDVCModel::AddPreset(
    Slic3r::Domain::Preset::PresetKind kind,
    std::string preset_name,
    std::string new_preset_name
)
{
    // "color" strings
    color_string(preset_name, def_text_color());
    make_string_bold(preset_name);
    make_string_bold(new_preset_name);

    auto preset = new ModelNode(
        kind,
        m_parent_win,
        from_u8(preset_name),
        get_icon_name(kind),
        from_u8(new_preset_name)
    );
    m_preset_nodes.emplace_back(preset);

    wxDataViewItem child((void*) preset);
    wxDataViewItem parent(nullptr);

    ItemAdded(parent, child);
    return child;
}

wxDataViewItem DiffDVCModel::AddOption(
    Slic3r::Domain::Preset::PresetKind kind,
    std::string preset_name,
    std::string category_name,
    std::string group_name,
    std::string option_name,
    std::string old_value,
    std::string mod_value,
    std::string new_value,
    const std::string category_icon_name
)
{
    // "color" strings
    color_string(category_name, def_text_color());
    color_string(group_name, def_text_color());
    color_string(option_name, def_text_color());

    // "make" strings bold
    make_string_bold(category_name);
    make_string_italic(group_name);
    make_string_bold(group_name);

    // "skin" the preset name to correct comparison
    if (kind == Slic3r::Domain::Preset::PresetKind::FdmToolPrint
        || kind == Slic3r::Domain::Preset::PresetKind::FdmMaterial)
    {
        color_string(preset_name, def_text_color());
        make_string_bold(preset_name);
    }

    // add items
    for (std::unique_ptr<ModelNode>& preset : m_preset_nodes)
        if (preset->kind() == kind) {
            if (kind == Slic3r::Domain::Preset::PresetKind::FdmToolPrint
                || kind == Slic3r::Domain::Preset::PresetKind::FdmMaterial)
            {
                // There could be several preset items for the tools.
                // So, check the node text too
                if (preset->text() != from_u8(preset_name))
                    continue;
            }

            for (std::unique_ptr<ModelNode>& category : preset->GetChildren())
                if (category->text() == from_u8(category_name)) {
                    for (std::unique_ptr<ModelNode>& group : category->GetChildren())
                        if (group->text() == from_u8(group_name))
                            return wxDataViewItem((void*) AddOption(
                                group.get(),
                                from_u8(option_name),
                                from_u8(old_value),
                                from_u8(mod_value),
                                from_u8(new_value)
                            ));

                    return wxDataViewItem((void*) AddOptionWithGroup(
                        category.get(),
                        from_u8(group_name),
                        from_u8(option_name),
                        from_u8(old_value),
                        from_u8(mod_value),
                        from_u8(new_value)
                    ));
                }

            return wxDataViewItem((void*) AddOptionWithGroupAndCategory(
                preset.get(),
                from_u8(category_name),
                from_u8(group_name),
                from_u8(option_name),
                from_u8(old_value),
                from_u8(mod_value),
                from_u8(new_value),
                category_icon_name
            ));
        }

    return wxDataViewItem(nullptr);
}

static void update_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        bool toggle = parent->IsToggled();
        for (std::unique_ptr<ModelNode>& child : parent->GetChildren()) {
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
        for (std::unique_ptr<ModelNode>& child : parent->GetChildren()) {
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

void DiffDVCModel::UpdateItemEnabling(wxDataViewItem item)
{
    assert(item.IsOk());
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    node->UpdateEnabling();

    update_children(node);
    update_parents(node);
}

bool DiffDVCModel::IsEnabledItem(const wxDataViewItem& item)
{
    assert(item.IsOk());
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    return node->IsToggled();
}

wxString DiffDVCModel::GetColumnType(unsigned int col) const
{
    switch (col) {
    case colToggle:
        return WX::from_u8("bool");
    case colIconText:
    case colOldValue:
    case colNewValue:
    default:
        return WX::from_u8("DataViewBitmapText"); //"string";
    }
}

static void rescale_children(ModelNode* parent)
{
    if (parent->IsContainer()) {
        for (std::unique_ptr<ModelNode>& child : parent->GetChildren()) {
            child->UpdateIcons();
            rescale_children(child.get());
        }
    }
}

void DiffDVCModel::Rescale()
{
    for (std::unique_ptr<ModelNode>& node : m_preset_nodes) {
        node->UpdateIcons();
        rescale_children(node.get());
    }
}

wxDataViewItem DiffDVCModel::Delete(const wxDataViewItem& item)
{
    auto ret_item   = wxDataViewItem(nullptr);
    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    if (!node) // happens if item.IsOk()==false
        return ret_item;

    // first remove the node from the parent's array of children;
    // NOTE: m_preset_nodes is only a vector of _pointers_
    // thus removing the node from it doesn't result in freeing it
    ModelNodePtrArray& children = node->GetChildren();
    // Delete all children
    while (!children.empty())
        Delete(wxDataViewItem(children.back().get()));

    auto node_parent = node->GetParent();
    wxDataViewItem parent(node_parent);

    ModelNodePtrArray& parents_children = node_parent ? node_parent->GetChildren() : m_preset_nodes;
    auto it                             = find_if(
        parents_children.begin(),
        parents_children.end(),
        [node](std::unique_ptr<ModelNode>& child) { return child.get() == node; }
    );
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

void DiffDVCModel::Clear()
{
    while (!m_preset_nodes.empty())
        Delete(wxDataViewItem(m_preset_nodes.back().get()));
}

wxDataViewItem DiffDVCModel::GetItemByName(const wxString& name)
{
    // add items
    for (std::unique_ptr<ModelNode>& preset : m_preset_nodes) {
        for (std::unique_ptr<ModelNode>& category : preset->GetChildren()) {
            if (category->text().Contains(name))
                return wxDataViewItem((void*) category.get());

            for (std::unique_ptr<ModelNode>& group : category->GetChildren()) {
                if (group->text().Contains(name))
                    return wxDataViewItem((void*) group.get());

                for (std::unique_ptr<ModelNode>& option : group->GetChildren())
                    if (option->text().Contains(name))
                        return wxDataViewItem((void*) option.get());
            }
        }
    }

    return wxDataViewItem(nullptr);
}

wxDataViewItem DiffDVCModel::GetParent(const wxDataViewItem& item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(nullptr);

    ModelNode* node = static_cast<ModelNode*>(item.GetID());

    if (node->IsRoot())
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*) node->GetParent());
}

unsigned int
DiffDVCModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    ModelNode* parent_node = (ModelNode*) parent.GetID();

    const ModelNodePtrArray& children = parent_node ? parent_node->GetChildren() : m_preset_nodes;
    for (const std::unique_ptr<ModelNode>& child : children)
        array.Add(wxDataViewItem((void*) child.get()));

    return array.Count();
}

void DiffDVCModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    switch (col) {
    case colToggle:
        variant = node->m_toggle;
        break;
    case colIconText:
        variant << DataViewBitmapOrIconText(node->m_text, node->m_icon);
        break;
    case colOldValue:
        variant << DataViewBitmapOrIconText(node->m_old_value, node->m_old_color_bmp);
        break;
    case colModValue:
        variant << DataViewBitmapOrIconText(node->m_mod_value, node->m_mod_color_bmp);
        break;
    case colNewValue:
        variant << DataViewBitmapOrIconText(node->m_new_value, node->m_new_color_bmp);
        break;
    default:
        wxLogError(from_u8("DiffModel::GetValue: wrong column %d"), col);
    }
}

static void set_icon_from_data(wxBitmapOrIcon& icon, const DataViewBitmapOrIconText& data)
{
#ifdef __linux__
    icon = data.GetIcon();
#else
    icon = data.GetBitmap();
#endif
}

bool DiffDVCModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    assert(item.IsOk());

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    switch (col) {
    case colToggle:
        node->m_toggle = variant.GetBool();
        return true;
    case colIconText: {
        DataViewBitmapOrIconText data;
        data << variant;
        set_icon_from_data(node->m_icon, data);
        node->m_text = data.GetText();
        return true;
    }
    case colOldValue: {
        DataViewBitmapOrIconText data;
        data << variant;
        set_icon_from_data(node->m_old_color_bmp, data);
        node->m_old_value = data.GetText();
        return true;
    }
    case colNewValue: {
        DataViewBitmapOrIconText data;
        data << variant;
        set_icon_from_data(node->m_new_color_bmp, data);
        node->m_new_value = data.GetText();
        return true;
    }
    default:
        wxLogError(from_u8("DiffModel::SetValue: wrong column"));
    }
    return false;
}

bool DiffDVCModel::IsEnabled(const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());
    if (col == colToggle)
        return true;

    // disable unchecked nodes
    return (static_cast<ModelNode*>(item.GetID()))->IsToggled();
}

bool DiffDVCModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisble root node can have children
    if (!item.IsOk())
        return true;

    ModelNode* node = static_cast<ModelNode*>(item.GetID());
    return node->IsContainer();
}

} // namespace Slic3r::App::WX
