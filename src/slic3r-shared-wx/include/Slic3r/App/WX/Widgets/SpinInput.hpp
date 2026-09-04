#pragma once

#include <wx/containr.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include "StaticBox.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"

namespace Slic3r::App::WX::Widgets {

class Button;

class SpinInputBase : public wxNavigationEnabled<StaticBox>
{
protected:
    wxSize          labelSize;
    StateColor      label_color;
    StateColor      text_color;
    wxTextCtrl *    text_ctrl{nullptr};
    Button *        button_inc {nullptr};
    Button *        button_dec {nullptr};
    wxTimer         timer;

    static const int SpinInputWidth = 200;
    static const int SpinInputHeight = 50;

    enum class ButtonId
    {
        btnIncrease,
        btnDecrease
    };

public:
    SpinInputBase();

    void SetCornerRadius(double radius);

    void SetLabel(const wxString &label) wxOVERRIDE;

    void SetLabelColor(StateColor const &color);

    void SetTextColor(StateColor const &color);

    void SetSize(wxSize const &size);

    void Rescale();

    virtual bool Enable(bool enable = true) wxOVERRIDE;

    wxTextCtrl * GetText() { return text_ctrl; }

    virtual void SetValue(const wxString &text) = 0;

    wxString GetTextValue() const;

    bool SetFont(wxFont const& font) override;

    bool SetBackgroundColour(const wxColour& colour) override;
    bool SetForegroundColour(const wxColour& colour) override;
    void SetBorderColor(StateColor const& color);
    void SetSelection(long from, long to);

protected:
    void DoSetToolTipText(wxString const &tip) override;

    void paintEvent(wxPaintEvent& evt);

    void render(wxDC& dc);

    void messureSize();

    Button *create_button(ButtonId id);
    virtual void bind_inc_dec_button(Button *btn, ButtonId id) = 0;

    // some useful events
    virtual void mouseWheelMoved(wxMouseEvent& event) = 0;
    virtual void keyPressed(wxKeyEvent& event) = 0;
    virtual void onTimer(wxTimerEvent &evnet) = 0;
    virtual void onTextLostFocus(wxEvent &event) = 0;
    virtual void onTextEnter(wxCommandEvent &event) = 0;

    void onText(wxCommandEvent &event);

    void sendSpinEvent();

    bool          is_completed{ false };

    DECLARE_EVENT_TABLE()
};

class SpinInput : public SpinInputBase
{
    int val;
    int min;
    int max;
    int delta;

public:
    SpinInput(wxWindow *     parent,
              wxString       text,
              wxString       label = {},
              const wxPoint &pos   = wxDefaultPosition,
              const wxSize & size  = wxDefaultSize,
              long           style = 0,
              int min = 0, int max = 100, int initial = 0);

    void Create(wxWindow *     parent,
              wxString       text,
              wxString       label   = {},
              const wxPoint &pos     = wxDefaultPosition,
              const wxSize & size    = wxDefaultSize,
              long           style   = 0,
              int            min     = 0,
              int            max     = 100,
              int            initial = 0);

    void SetValue(const wxString &text) override;

    void SetValue (int value);
    int  GetValue () const;

    void SetRange(int min, int max);

    int GetMin() const { return this->min; }
    int GetMax() const { return this->max; }

protected:
    void bind_inc_dec_button(Button* btn, ButtonId id) override;
    // some useful events
    void mouseWheelMoved(wxMouseEvent& event) override;
    void keyPressed(wxKeyEvent& event) override;
    void onTimer(wxTimerEvent& evnet) override;
    void onTextLostFocus(wxEvent& event) override;
    void onTextEnter(wxCommandEvent& event) override;
};

class SpinInputDouble : public SpinInputBase
{
    double val;
    double min;
    double max;
    double inc;
    double delta;
    int digits {-1};

public:

    SpinInputDouble() : SpinInputBase() {}

    SpinInputDouble(wxWindow* parent,
        wxString        text,
        wxString        label = {},
        const wxPoint&  pos = wxDefaultPosition,
        const wxSize&   size = wxDefaultSize,
        long            style = 0,
        double          min = 0.,
        double          max = 100.,
        double          initial = 0.,
        double          inc = 1.);

    void Create(wxWindow* parent,
        wxString        text,
        wxString        label = {},
        const wxPoint&  pos = wxDefaultPosition,
        const wxSize&   size = wxDefaultSize,
        long            style = 0,
        double          min = 0.,
        double          max = 100.,
        double          initial = 0.,
        double          inc = 1.);

    void    SetValue(const wxString& text) override;

    void    SetValue(double value);
    double  GetValue() const;

    //wxString GetTextValue() const override;

    void SetRange(double min, double max);
    void SetIncrement(double inc);
    void SetDigits(unsigned digits);

    double GetMin() const { return this->min; }
    double GetMax() const { return this->max; }

protected:
    void bind_inc_dec_button(Button* btn, ButtonId id) override;
    // some useful events
    void mouseWheelMoved(wxMouseEvent& event) override;
    void keyPressed(wxKeyEvent& event) override;
    void onTimer(wxTimerEvent& evnet) override;
    void onTextLostFocus(wxEvent& event) override;
    void onTextEnter(wxCommandEvent& event) override;
};

}
