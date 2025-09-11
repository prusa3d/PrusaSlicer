#include "TextInput.hpp"
#include "UIColors.hpp"

#include <wx/dcgraph.h>
#include <wx/panel.h>

#include "slic3r/GUI/GUI_App.hpp"

BEGIN_EVENT_TABLE(TextInput, wxPanel)

EVT_PAINT(TextInput::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

TextInput::TextInput()
    : label_color(std::make_pair(0x909090, (int) StateColor::Disabled),
                 std::make_pair(0x6B6B6B, (int) StateColor::Normal))
    , text_color(std::make_pair(0x909090, (int) StateColor::Disabled),
                 std::make_pair(0x262E30, (int) StateColor::Normal))
{
    if (Slic3r::GUI::wxGetApp().suppress_round_corners())
        radius = 0;
    border_width = 1;
}

TextInput::TextInput(wxWindow *     parent,
                     wxString       text,
                     wxString       label,
                     wxString       icon,
                     const wxPoint &pos,
                     const wxSize & size,
                     long           style)
    : TextInput()
{
    Create(parent, text, label, icon, pos, size, style);
}

void TextInput::Create(wxWindow *     parent,
                       wxString       text,
                       wxString       label,
                       wxString       icon,
                       const wxPoint &pos,
                       const wxSize & size,
                       long           style)
{
    text_ctrl = nullptr;
    StaticBox::Create(parent, wxID_ANY, pos, size, style);
    wxWindow::SetLabel(label);

    state_handler.attach({&label_color, &text_color});
    state_handler.update_binds();

    text_ctrl = new wxTextCtrl(this, wxID_ANY, text, {4, 4}, size, style | wxBORDER_NONE);
#ifdef __WXOSX__
    text_ctrl->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
    text_ctrl->SetInitialSize(text_ctrl->GetBestSize());
    if (parent) {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
    }
    state_handler.attach_child(text_ctrl);

    text_ctrl->Bind(wxEVT_KILL_FOCUS, [this](auto &e) {
        OnEdit();
        e.SetId(GetId());
        ProcessEventLocally(e);
        e.Skip();
    });
    text_ctrl->Bind(wxEVT_TEXT_ENTER, [this](auto &e) {
        OnEdit();
        e.SetId(GetId());
        ProcessEventLocally(e);
    });
    text_ctrl->Bind(wxEVT_TEXT, [this](auto &e) {
        e.SetId(GetId());
        ProcessEventLocally(e);
    });
    text_ctrl->Bind(wxEVT_RIGHT_DOWN, [](auto &e) {}); // disable context menu

    if (!icon.IsEmpty()) {
        this->drop_down_icon = ScalableBitmap(this, icon.ToStdString(), 16);
        this->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            const wxPoint pos = event.GetLogicalPosition(wxClientDC(this));
            if (OnClickDropDownIcon && dd_icon_rect.Contains(pos))
                OnClickDropDownIcon();
            event.Skip();
        });
    }
    messureSize();
}

void TextInput::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

bool TextInput::SetBackgroundColour(const wxColour& colour)
{
    const int clr_background_disabled = Slic3r::GUI::wxGetApp().dark_mode() ? clr_background_disabled_dark : clr_background_disabled_light;
    const StateColor clr_state( std::make_pair(clr_background_disabled,    (int)StateColor::Disabled),
                                std::make_pair(clr_background_focused,     (int)StateColor::Checked),
                                std::make_pair(colour,                     (int)StateColor::Focused),
                                std::make_pair(colour,                     (int)StateColor::Normal));

    SetBackgroundColor(clr_state);
    if (text_ctrl)
        text_ctrl->SetBackgroundColour(colour);

    return true;
}

bool TextInput::SetForegroundColour(const wxColour& colour)
{
    const StateColor clr_state( std::make_pair(clr_foreground_disabled,    (int)StateColor::Disabled),
                                std::make_pair(colour,                     (int)StateColor::Normal));

    SetLabelColor(clr_state);
    SetTextColor (clr_state);

    return true;
}

void TextInput::SetValue(const wxString& value)
{
    if (text_ctrl)
        text_ctrl->SetValue(value);
}

wxString TextInput::GetValue()
{
    if (text_ctrl)
        return text_ctrl->GetValue();
    return wxEmptyString;
}

void TextInput::SetSelection(long from, long to)
{
    if (text_ctrl)
        text_ctrl->SetSelection(from, to);
}

void TextInput::SysColorsChanged()
{
    if (auto parent = this->GetParent()) {
        SetBackgroundColour(parent->GetBackgroundColour());
        SetForegroundColour(parent->GetForegroundColour());
        if (this->drop_down_icon.bmp().IsOk())
            this->drop_down_icon.sys_color_changed();
    }
}

void TextInput::SetIcon(const wxBitmapBundle& icon_in)
{
    icon = icon_in;
}

void TextInput::SetLabelColor(StateColor const &color)
{
    label_color = color;
    state_handler.update_binds();
}

void TextInput::SetTextColor(StateColor const& color)
{
    text_color = color;
    state_handler.update_binds();
    if (text_ctrl)
        text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
}

void TextInput::SetBGColor(StateColor const& color)
{
    background_color = color;
    state_handler.update_binds();
}

void TextInput::SetCtrlSize(wxSize const& size)
{
    StaticBox::SetInitialSize(size);
    Rescale();
}

void TextInput::Rescale()
{
    if (text_ctrl)
        text_ctrl->SetInitialSize(text_ctrl->GetBestSize());

    messureSize();
    Refresh();
}

bool TextInput::SetFont(const wxFont& font)
{
    bool ret = StaticBox::SetFont(font);
    if (text_ctrl)
        return ret && text_ctrl->SetFont(font);
    return ret;
}

bool TextInput::Enable(bool enable)
{
    bool result = text_ctrl->Enable(enable) && wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
        text_ctrl->SetBackgroundColour(background_color.colorForStates(state_handler.states()));
        text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
    }
    return result;
}

void TextInput::SetMinSize(const wxSize& size)
{
    wxSize size2 = size;
    if (size2.y < 0) {
#ifdef __WXMAC__
        if (GetPeer()) // peer is not ready in Create on mac
#endif
        size2.y = GetSize().y;
    }
    wxWindow::SetMinSize(size2);
}

void TextInput::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING) return;
    wxSize size = GetSize();
    wxPoint textPos = {5, 0};
    if (this->icon.IsOk()) {
        wxSize szIcon = get_preferred_size(icon, m_parent);
        textPos.x += szIcon.x;
    }
    wxSize dd_icon_size = wxSize(0,0);
    if (this->drop_down_icon.bmp().IsOk())
        dd_icon_size = this->drop_down_icon.GetSize();

    bool align_right = GetWindowStyle() & wxRIGHT;
    if (align_right)
        textPos.x += labelSize.x;
    if (text_ctrl) {
        wxSize textSize = text_ctrl->GetBestSize();
        if (textSize.y > size.y) {
            // Don't allow to set internal control height more, then its initial height
            textSize.y = text_ctrl->GetSize().y;
        }
        wxClientDC dc(this);
        const int r_shift = int(dd_icon_size.x == 0 ? (3. * dc.GetContentScaleFactor()) : ((size.y - dd_icon_size.y) / 2));
        textSize.x = size.x - textPos.x - labelSize.x - dd_icon_size.x - r_shift;
        if (textSize.x < -1) textSize.x = -1;
        text_ctrl->SetSize(textSize);
        text_ctrl->SetPosition({textPos.x, (size.y - textSize.y) / 2});
    }
}

void TextInput::DoSetToolTipText(wxString const &tip)
{
    wxWindow::DoSetToolTipText(tip);
    text_ctrl->SetToolTip(tip);
}

void TextInput::paintEvent(wxPaintEvent &evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void TextInput::render(wxDC& dc)
{
    StaticBox::render(dc);
    int states = state_handler.states();
    wxSize size = GetSize();
    bool   align_right = GetWindowStyle() & wxRIGHT;
    // start draw
    wxPoint pt = { 5 + text_ctrl->GetMargins().x, 0};
    if (icon.IsOk()) {
        wxSize szIcon = get_preferred_size(icon, m_parent);
        pt.y = (size.y - szIcon.y) / 2;
#ifdef __WXGTK3__
        dc.DrawBitmap(icon.GetBitmap(szIcon), pt);
#else
        dc.DrawBitmap(icon.GetBitmapFor(m_parent), pt);
#endif
        pt.x += szIcon.x + 5;
    }

    // drop_down_icon draw
    wxPoint pt_r = {size.x, 0};
    if (drop_down_icon.bmp().IsOk()) {
        wxSize szIcon = drop_down_icon.GetSize();
        pt_r.y = (size.y - szIcon.y) / 2;
        pt_r.x -= szIcon.x + pt_r.y;
        dd_icon_rect = wxRect(pt_r, szIcon);
        dc.DrawBitmap(drop_down_icon.get_bitmap(), pt_r);
        pt_r.x -= 5;
    }

    auto text = wxWindow::GetLabel();
    if (!text_ctrl->IsShown() && !text.IsEmpty()) {
        wxSize textSize = text_ctrl->GetSize();
        if (align_right) {
            pt.x += textSize.x;
            pt.y = (size.y + textSize.y) / 2 - labelSize.y;
        } else {
            if (pt.x + labelSize.x > pt_r.x)
                text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, pt_r.x - pt.x);
            pt.y = (size.y - labelSize.y) / 2;
        }
        dc.SetTextForeground(label_color.colorForStates(states));
        dc.SetFont(GetFont());
        dc.DrawText(text, pt);
    }
}

void TextInput::messureSize()
{
    wxSize size = GetSize();
    wxClientDC dc(this);
    labelSize = dc.GetTextExtent(wxWindow::GetLabel());

    const wxSize textSize = text_ctrl->GetSize();
    const wxSize iconSize = drop_down_icon.bmp().IsOk() ? drop_down_icon.GetSize() : wxSize(0, 0);
    size.y = ((textSize.y > iconSize.y) ? textSize.y : iconSize.y) + 8;

    wxSize minSize = size;
    minSize.x = GetMinWidth();
    SetMinSize(minSize);
    SetSize(size);
}
