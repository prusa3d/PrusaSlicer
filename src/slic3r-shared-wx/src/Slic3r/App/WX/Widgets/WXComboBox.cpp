#include "Slic3r/App/WX/Widgets/WXComboBox.hpp"
#include "Slic3r/App/WX/Widgets/UIColors.hpp"

#include <wx/wx.h>
#include <wx/dcgraph.h>

#include "Slic3r/App/WX/WidgetsConfig.hpp"

namespace Slic3r::App::WX::Widgets {

BEGIN_EVENT_TABLE(WXComboBox, TextInput)

EVT_LEFT_DOWN(WXComboBox::mouseDown)
EVT_MOUSEWHEEL(WXComboBox::mouseWheelMoved)
EVT_KEY_DOWN(WXComboBox::keyDown)

// catch paint events
END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

WXComboBox::WXComboBox(wxWindow *      parent,
                   wxWindowID      id,
                   const wxString &value,
                   const wxPoint & pos,
                   const wxSize &  size,
                   int             n,
                   const wxString  choices[],
                   long            style)
    : drop(texts, icons)
{
    text_off = style & CB_NO_TEXT;
    TextInput::Create(
        parent, {}, value,
        from_u8((style & CB_NO_DROP_ICON) ? "" : "drop_down"), pos, size,
        style | wxTE_PROCESS_ENTER
    );
    drop.Create(this, style);

    SetFont(w_config()->normal_font());
    if (style & wxCB_READONLY)
        GetTextCtrl()->Hide();
    else
        GetTextCtrl()->Bind(wxEVT_KEY_DOWN, &WXComboBox::keyDown, this);

    SetBorderColor(TextInput::GetBorderColor());
    if (parent) {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
    }

    drop.Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &e) {
        SetSelection(e.GetInt());
        e.SetEventObject(this);
        e.SetId(GetId());
        GetEventHandler()->ProcessEvent(e);
    });
    drop.Bind(EVT_DISMISS, [this](auto &) {
        drop_down = false;
        wxCommandEvent e(wxEVT_COMBOBOX_CLOSEUP);
        GetEventHandler()->ProcessEvent(e);
        if (!HasFlag(wxCB_READONLY)) {
            GetTextCtrl()->SetInsertionPointEnd();
            OnEdit();
            e.Skip();
        }
    });

#ifndef _WIN32
    this->Bind(wxEVT_SYS_COLOUR_CHANGED, [this, parent](wxSysColourChangedEvent& event) {
        event.Skip();
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
    });
#endif
    for (int i = 0; i < n; ++i) Append(choices[i]);
}

int WXComboBox::GetSelection() const
{
    return drop.GetSelection();
}

void WXComboBox::SetSelection(int n)
{
    drop.SetSelection(n);
    SetLabel(drop.GetValue());
    if (drop.selection >= 0)
        SetIcon(icons[drop.selection]);
}

void WXComboBox::Rescale()
{
    SetFont(w_config()->normal_font());

    TextInput::Rescale();
    drop.Rescale();
}

wxString WXComboBox::GetValue() const
{
    return drop.GetSelection() >= 0 ? drop.GetValue() : GetLabel();
}

void WXComboBox::SetValue(const wxString &value)
{
    drop.SetValue(value);
    SetLabel(value);
    if (drop.selection >= 0)
        SetIcon(icons[drop.selection]);
}

void WXComboBox::SetLabel(const wxString &value)
{
    if (GetTextCtrl()->IsShown() || text_off)
        GetTextCtrl()->SetValue(value);
    else
        TextInput::SetLabel(value);
}

wxString WXComboBox::GetLabel() const
{
    if (GetTextCtrl()->IsShown() || text_off)
        return GetTextCtrl()->GetValue();
    else
        return TextInput::GetLabel();
}

void WXComboBox::SetTextLabel(const wxString& label)
{
    TextInput::SetLabel(label);
}

wxString WXComboBox::GetTextLabel() const
{
    return TextInput::GetLabel();
}

bool WXComboBox::SetFont(wxFont const& font)
{
    const bool set_drop_font = drop.SetFont(font);
    if (GetTextCtrl() && GetTextCtrl()->IsShown())
        return GetTextCtrl()->SetFont(font) && set_drop_font;
    return TextInput::SetFont(font) && set_drop_font;
}

bool WXComboBox::SetBackgroundColour(const wxColour& colour)
{
    TextInput::SetBackgroundColour(colour);

    drop.SetBackgroundColour(colour);
    StateColor selector_colors( std::make_pair(clr_background_focused,          (int)StateColor::Checked),
        w_config()->dark_mode() ?
                                std::make_pair(clr_background_disabled_dark,    (int)StateColor::Disabled) :
                                std::make_pair(clr_background_disabled_light,   (int)StateColor::Disabled),
        w_config()->dark_mode() ?
                                std::make_pair(clr_background_normal_dark,      (int)StateColor::Normal) :
                                std::make_pair(clr_background_normal_light,     (int)StateColor::Normal));
    drop.SetSelectorBackgroundColor(selector_colors);

    return true;
}

bool WXComboBox::SetForegroundColour(const wxColour& colour)
{
    TextInput::SetForegroundColour(colour);

    drop.SetTextColor(TextInput::GetTextColor());

    return true;
}

void WXComboBox::SetBorderColor(StateColor const& color)
{
    TextInput::SetBorderColor(color);
    drop.SetBorderColor(color);
    drop.SetSelectorBorderColor(color);
}

int WXComboBox::Append(const wxString &item, const wxBitmapBundle &bitmap)
{
    return Append(item, bitmap, nullptr);
}

int WXComboBox::Append(const wxString         &item,
                     const wxBitmapBundle   &bitmap,
                     void *                 clientData)
{
    texts.push_back(item);
    icons.push_back(bitmap);
    datas.push_back(clientData);
    types.push_back(wxClientData_None);
    drop.Invalidate();
    return int(texts.size()) - 1;
}

int WXComboBox::Insert(const wxString& item,
                     const wxBitmapBundle& bitmap,
                     unsigned int pos)
{
    return Insert(item, bitmap, pos, nullptr);
}

int WXComboBox::Insert(const wxString& item, const wxBitmapBundle& bitmap,
    unsigned int pos, void* clientData)
{
    const int n = wxItemContainer::Insert(item, pos, clientData);
    if (n != wxNOT_FOUND)
        icons[n] = bitmap;
    return n;
}

void WXComboBox::DoClear()
{
    texts.clear();
    icons.clear();
    datas.clear();
    types.clear();
    drop.Invalidate(true);
    if (GetTextCtrl()->IsShown() || text_off)
        GetTextCtrl()->Clear();
}

void WXComboBox::DoDeleteOneItem(unsigned int pos)
{
    if (pos >= texts.size()) return;
    texts.erase(texts.begin() + pos);
    icons.erase(icons.begin() + pos);
    datas.erase(datas.begin() + pos);
    types.erase(types.begin() + pos);
    const int selection = drop.GetSelection();
    drop.Invalidate(true);
    drop.SetSelection(selection);
}

unsigned int WXComboBox::GetCount() const { return texts.size(); }

wxString WXComboBox::GetString(unsigned int n) const
{
    return n < texts.size() ? texts[n] : wxString{};
}

void WXComboBox::SetString(unsigned int n, wxString const &value)
{
    if (n >= texts.size()) return;
    texts[n]  = value;
    drop.Invalidate();
    if (int(n) == drop.GetSelection()) SetLabel(value);
}

wxBitmap WXComboBox::GetItemBitmap(unsigned int n)
{
    return icons[n].GetBitmapFor(m_parent);
}

void WXComboBox::OnKeyDown(wxKeyEvent &event)
{
    keyDown(event);
}

int WXComboBox::DoInsertItems(const wxArrayStringsAdapter &items,
                            unsigned int                 pos,
                            void **                      clientData,
                            wxClientDataType             type)
{
    if (pos > texts.size()) return -1;
    for (size_t i = 0; i < items.GetCount(); ++i) {
        texts.insert(texts.begin() + pos, items[i]);
        icons.insert(icons.begin() + pos, wxNullBitmap);
        datas.insert(datas.begin() + pos, clientData ? clientData[i] : NULL);
        types.insert(types.begin() + pos, type);
        ++pos;
    }
    const int selection = drop.GetSelection();
    drop.Invalidate(true);
    drop.SetSelection(selection);
    return int(pos) - 1;
}

void *WXComboBox::DoGetItemClientData(unsigned int n) const { return n < texts.size() ? datas[n] : NULL; }

void WXComboBox::DoSetItemClientData(unsigned int n, void *data)
{
    if (n < texts.size())
        datas[n] = data;
}

void WXComboBox::mouseDown(wxMouseEvent &event)
{
    SetFocus();
    if (drop_down) {
        drop.Hide();
    } else if (drop.HasDismissLongTime()) {
        drop.autoPosition();
        drop_down = true;
        drop.Popup();
        wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
        GetEventHandler()->ProcessEvent(e);
    }
}

void WXComboBox::mouseWheelMoved(wxMouseEvent &event)
{
    if (drop_down) return;
    auto delta = ((event.GetWheelRotation() < 0) == event.IsWheelInverted()) ? -1 : 1;
    unsigned int n = GetSelection() + delta;
    if (n < GetCount()) {
        SetSelection((int) n);
        sendComboBoxEvent();
    }
}

void WXComboBox::keyDown(wxKeyEvent& event)
{
    int key_code = event.GetKeyCode();
    switch (key_code) {
        case WXK_RETURN: {
            if (drop_down) {
                drop.DismissAndNotify();
                sendComboBoxEvent();
            }
            else if (drop.HasDismissLongTime()) {
                drop.autoPosition();
                drop_down = true;
                drop.Popup();
                wxCommandEvent e(wxEVT_COMBOBOX_DROPDOWN);
                GetEventHandler()->ProcessEvent(e);
            }
            break;
        }
        case WXK_UP: {
            if (GetSelection() > 0)
                SetSelection(GetSelection() - 1);
            if (!drop.IsShown())
                sendComboBoxEvent();
            break;
        }
        case WXK_DOWN: {
            if (GetSelection() + 1 < int(texts.size()))
                SetSelection(GetSelection() + 1);
            if (!drop.IsShown())
                sendComboBoxEvent();
            break;
        }
        case WXK_LEFT: {
            if (HasFlag(wxCB_READONLY)) {
                if(GetSelection() > 0)
                    SetSelection(GetSelection() - 1);
                break;
            }
            const auto pos = GetTextCtrl()->GetInsertionPoint();
            if(pos > 0)
                GetTextCtrl()->SetInsertionPoint(pos - 1);
            break;
        }
        case WXK_RIGHT: {
            if (HasFlag(wxCB_READONLY)) {
                if (GetSelection() + 1 < int(texts.size()))
                    SetSelection(GetSelection() + 1);
                break;
            }
            const size_t pos = size_t(GetTextCtrl()->GetInsertionPoint());
            if (pos < GetLabel().Length())
                GetTextCtrl()->SetInsertionPoint(pos + 1);
            break;
        }
        case WXK_TAB:
            HandleAsNavigationKey(event);
            break;
        default: {
            if (drop.IsShown() && HasFlag(wxCB_READONLY)) {
                for (size_t n = 0; n < texts.size(); n++) {
                    if (texts[n].StartsWith(wxString(static_cast<char>(key_code)))) {
                        SetSelection(int(n));
                        break;
                    }
                }
            }
            event.Skip();
            break;
        }
    }
}

void WXComboBox::OnEdit()
{
    if (GetTextCtrl()) {
        auto value = GetTextCtrl()->GetValue();
        SetValue(value);
    }
}

#ifdef __WIN32__

WXLRESULT WXComboBox::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_GETDLGCODE) {
        return DLGC_WANTALLKEYS;
    }
    return TextInput::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

void WXComboBox::sendComboBoxEvent()
{
    wxCommandEvent event(wxEVT_COMBOBOX, GetId());
    event.SetEventObject(this);
    event.SetInt(drop.GetSelection());
    event.SetString(drop.GetValue());
    GetEventHandler()->ProcessEvent(event);
}

}
