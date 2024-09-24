#ifndef slic3r_GUI_DropDown_hpp_
#define slic3r_GUI_DropDown_hpp_

#include <wx/stattext.h>
#include <wx/popupwin.h>

#include <boost/date_time/posix_time/posix_time.hpp>

#include "../wxExtensions.hpp"
#include "StateHandler.hpp"

#define DD_NO_CHECK_ICON    0x0001
#define DD_NO_DROP_ICON     0x0002
#define DD_NO_TEXT          0x0004
#define DD_STYLE_MASK       0x0008

wxDECLARE_EVENT(EVT_DISMISS, wxCommandEvent);

class DropDown : public wxPopupTransientWindow
{
    std::vector<wxString> &       texts;
    std::vector<wxBitmapBundle> & icons;
    bool                          need_sync  = false;
    int                         selection = -1;
    int                         hover_item = -1;

    double radius;
    bool   use_content_width = false;
    bool   align_icon        = false;
    bool   text_off          = false;

    wxSize textSize;
    wxSize iconSize;
    wxSize rowSize;

    StateHandler state_handler;
    StateColor   text_color;
    StateColor   border_color;
    StateColor   selector_border_color;
    StateColor   selector_background_color;
    ScalableBitmap check_bitmap;

    bool pressedDown = false;
    bool slider_grabbed = false;
    boost::posix_time::ptime dismissTime;
    wxPoint                  offset; // x not used
    wxPoint                  dragStart;

public:
    DropDown(std::vector<wxString> &texts,
             std::vector<wxBitmapBundle> &icons);
    
    DropDown(wxWindow *     parent,
             std::vector<wxString> &texts,
             std::vector<wxBitmapBundle> &icons,
             long           style     = 0);
    
    void Create(wxWindow *     parent,
             long           style     = 0);
    
    void Invalidate(bool clear = false);

    int GetSelection() const { return selection; }

    void SetSelection(int n);

    wxString GetValue() const;
    void     SetValue(const wxString &value);

    void SetCornerRadius(double radius);

    void SetBorderColor(StateColor const & color);

    void SetSelectorBorderColor(StateColor const & color);

    void SetTextColor(StateColor const &color);

    void SetSelectorBackgroundColor(StateColor const &color);

    void SetUseContentWidth(bool use);

    void SetAlignIcon(bool align);
    
    void Rescale();

    bool HasDismissLongTime();

    static void SetTransparentBG(wxDC& dc, wxWindow* win);

    void CallDismissAndNotify() { DismissAndNotify(); }
    
protected:
    void OnDismiss() override;

private:
    void paintEvent(wxPaintEvent& evt);
    void paintNow();

    void render(wxDC& dc);

    friend class ComboBox;
    void messureSize();
    void autoPosition();

    // some useful events
    void mouseDown(wxMouseEvent& event);
    void mouseReleased(wxMouseEvent &event);
    void mouseCaptureLost(wxMouseCaptureLostEvent &event);
    void mouseMove(wxMouseEvent &event);
    void mouseWheelMoved(wxMouseEvent &event);

    void sendDropDownEvent();


    DECLARE_EVENT_TABLE()
};

#endif // !slic3r_GUI_DropDown_hpp_
