#ifndef slic3r_GUI_TextInput_hpp_
#define slic3r_GUI_TextInput_hpp_

#include <wx/textctrl.h>
#include "StaticBox.hpp"

class TextInput : public wxNavigationEnabled<StaticBox>
{

    wxSize labelSize;
    wxBitmapBundle icon;
    ScalableBitmap drop_down_icon;
    StateColor     label_color;
    StateColor     text_color;
    wxTextCtrl*    text_ctrl{nullptr};

    static const int TextInputWidth = 200;
    static const int TextInputHeight = 50;

    wxRect                  dd_icon_rect;
    std::function<void()>   OnClickDropDownIcon{ nullptr };

public:
    TextInput();

    TextInput(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              wxString       icon  = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0);

public:
    void Create(wxWindow *     parent,
              wxString       text,
              wxString       label = "",
              wxString       icon  = "",
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0);

    void SetLabel(const wxString& label) wxOVERRIDE;

    void SetIcon(const wxBitmapBundle& icon);

    void SetLabelColor(StateColor const &color);

    void SetBGColor(StateColor const &color);

    void SetTextColor(StateColor const &color);

    void SetCtrlSize(wxSize const& size);

    virtual void Rescale();

    bool SetFont(const wxFont &font) override;

    virtual bool Enable(bool enable = true) override;

    virtual void SetMinSize(const wxSize& size) override;

    bool SetBackgroundColour(const wxColour &colour) override;

    bool SetForegroundColour(const wxColour &colour) override;

    wxTextCtrl *GetTextCtrl() { return text_ctrl; }

    wxTextCtrl const *GetTextCtrl() const { return text_ctrl; }

    void SetValue(const wxString& value);

    wxString GetValue();

    void SetSelection(long from, long to);

    void SysColorsChanged();

    void SetOnDropDownIcon(std::function<void()> click_drop_down_icon_fn) { OnClickDropDownIcon = click_drop_down_icon_fn; }

protected:
    virtual void OnEdit() {}

    void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) wxOVERRIDE;

    void DoSetToolTipText(wxString const &tip) override;

    StateColor GetTextColor()   const { return text_color; }
    StateColor GetBorderColor() const { return border_color; }

private:
    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_TextInput_hpp_
