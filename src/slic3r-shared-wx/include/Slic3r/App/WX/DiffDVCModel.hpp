#pragma once

#include "Slic3r/Domain/Preset/Types.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"

#include <wx/dataview.h>

namespace Slic3r::App::WX {

// ----------------------------------------------------------------------------
// ModelNode: a node inside DiffDVCModel
// ----------------------------------------------------------------------------

class ModelNode;
using ModelNodePtrArray = std::vector<std::unique_ptr<ModelNode>>;
class DiffDVCModel;

#ifdef __linux__
using wxBitmapOrIcon = wxIcon;
#else
using wxBitmapOrIcon = wxBitmap;
#endif

// On all of 3 different platforms Bitmap+Text icon column looks different
// because of Markup text is missed or not implemented.
// As a temporary workaround, we will use:
// MSW - DataViewBitmapText (our custom renderer wxBitmap + std::string, supported Markup text)
// OSX - -//-, but Markup text is not implemented right now
// GTK - wxDataViewIconText (wxWidgets for GTK renderer wxIcon + std::string, supported Markup text)
class ModelNode
{
public:
    // preset(root) node
    ModelNode(
        Slic3r::Domain::Preset::PresetKind kind,
        wxWindow* parent_win,
        const wxString& text,
        const std::string& icon_name,
        const wxString& new_val_column_text
    );

    // category node
    ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name);

    // group node
    ModelNode(ModelNode* parent, const wxString& text);

    // option node
    ModelNode(
        ModelNode* parent,
        const wxString& text,
        const wxString& old_value,
        const wxString& mod_value,
        const wxString& new_value
    );

    bool IsContainer() const
    {
        return m_container;
    }

    bool IsToggled() const
    {
        return m_toggle;
    }

    void Toggle(bool toggle = true)
    {
        m_toggle = toggle;
    }

    bool IsRoot() const
    {
        return m_parent == nullptr;
    }

    Slic3r::Domain::Preset::PresetKind kind() const
    {
        return m_preset_kind;
    }

    const wxString text() const
    {
        return m_text;
    }

    ModelNode* GetParent()
    {
        return m_parent;
    }

    ModelNodePtrArray& GetChildren()
    {
        return m_children;
    }

    ModelNode* GetNthChild(unsigned int n)
    {
        return m_children[n].get();
    }

    unsigned int GetChildCount() const
    {
        return (unsigned int) (m_children.size());
    }

    void Append(std::unique_ptr<ModelNode> child)
    {
        m_children.emplace_back(std::move(child));
    }

    void UpdateEnabling();
    void UpdateIcons();

    static std::string grey();
    static std::string orange();

private:
    wxWindow* m_parent_win{nullptr};

    ModelNode* m_parent;
    ModelNodePtrArray m_children;
    wxBitmap m_empty_bmp;
    Slic3r::Domain::Preset::PresetKind m_preset_kind;

    std::string m_icon_name;
    // saved values for colors if they exist
    std::string m_old_color;
    std::string m_mod_color;
    std::string m_new_color;

    wxBitmapOrIcon get_bitmap(const std::string& color);

protected:
    bool m_toggle{true};
    wxBitmapOrIcon m_icon;
    wxBitmapOrIcon m_old_color_bmp;
    wxBitmapOrIcon m_mod_color_bmp;
    wxBitmapOrIcon m_new_color_bmp;

    wxString m_text;
    wxString m_old_value;
    wxString m_mod_value;
    wxString m_new_value;

    // TODO/FIXME:
    // the GTK version of wxDVC (in particular wxDataViewCtrlInternal::ItemAdded)
    // needs to know in advance if a node is or _will be_ a container.
    // Thus implementing:
    // bool IsContainer() const
    // { return m_children.size()>0; }
    // doesn't work with wxGTK when DiffDVCModel::AddToClassical is called
    // AND the classical node was removed (a new node temporary without children
    // would be added to the control)
    bool m_container{true};

    friend class DiffDVCModel;
};

// ----------------------------------------------------------------------------
// DiffDVCModel data model used by DiffViewCtrl, which is used by DiffDialog
// ----------------------------------------------------------------------------

class DiffDVCModel : public wxDataViewModel
{
    wxWindow* m_parent_win{nullptr};
    ModelNodePtrArray m_preset_nodes;

    wxDataViewCtrl* m_ctrl{nullptr};

    ModelNode* AddOption(
        ModelNode* group_node,
        const wxString& option_name,
        const wxString& old_value,
        const wxString& mod_value,
        const wxString& new_value
    );
    ModelNode* AddOptionWithGroup(
        ModelNode* category_node,
        const wxString& group_name,
        const wxString& option_name,
        const wxString& old_value,
        const wxString& mod_value,
        const wxString& new_value
    );
    ModelNode* AddOptionWithGroupAndCategory(
        ModelNode* preset_node,
        const wxString& category_name,
        const wxString& group_name,
        const wxString& option_name,
        const wxString& old_value,
        const wxString& mod_value,
        const wxString& new_value,
        const std::string category_icon_name
    );

public:
    enum
    {
        colToggle,
        colIconText,
        colOldValue,
        colModValue,
        colNewValue,
        colMax
    };

    DiffDVCModel(wxWindow* parent);
    ~DiffDVCModel() override = default;

    void SetAssociatedControl(wxDataViewCtrl* ctrl)
    {
        m_ctrl = ctrl;
    }

    wxDataViewItem AddPreset(
        Slic3r::Domain::Preset::PresetKind kind,
        std::string preset_name,
        std::string new_preset_name = std::string()
    );
    wxDataViewItem AddOption(
        Slic3r::Domain::Preset::PresetKind kind,
        std::string kind_name,
        std::string category_name,
        std::string group_name,
        std::string option_name,
        std::string old_value,
        std::string mod_value,
        std::string new_value,
        const std::string category_icon_name
    );

    void UpdateItemEnabling(wxDataViewItem item);
    bool IsEnabledItem(const wxDataViewItem& item);

    unsigned int GetColumnCount() const override
    {
        return colMax;
    }

    wxString GetColumnType(unsigned int col) const override;
    void Rescale();

    wxDataViewItem Delete(const wxDataViewItem& item);
    void Clear();
    wxDataViewItem GetItemByName(const wxString& name);

    wxDataViewItem GetParent(const wxDataViewItem& item) const override;
    unsigned int
    GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override;

    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override;
    bool IsContainer(const wxDataViewItem& item) const override;

    // Is the container just a header or an item with all columns
    // In our case it is an item with all columns
    bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override
    {
        return true;
    }
};
} // namespace Slic3r::App::WX
