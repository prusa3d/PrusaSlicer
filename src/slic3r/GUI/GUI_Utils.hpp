///|/ Copyright (c) Prusa Research 2018 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_Utils_hpp_
#define slic3r_GUI_Utils_hpp_

#include <memory>
#include <string>
#include <ostream>
#include <functional>

#include <boost/optional.hpp>

#include <wx/frame.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/gdicmn.h>
#include <wx/panel.h>
#include <wx/dcclient.h>
#include <wx/debug.h>
#include <wx/settings.h>

#include <chrono>

#include "Event.hpp"

class wxCheckBox;
class wxTopLevelWindow;
class wxRect;

#define wxVERSION_EQUAL_OR_GREATER_THAN(major, minor, release) ((wxMAJOR_VERSION > major) || ((wxMAJOR_VERSION == major) && (wxMINOR_VERSION > minor)) || ((wxMAJOR_VERSION == major) && (wxMINOR_VERSION == minor) && (wxRELEASE_NUMBER >= release)))

namespace Slic3r {
namespace GUI {

#ifdef _WIN32
// USB HID attach / detach events from Windows OS.
using HIDDeviceAttachedEvent = Event<std::string>;
using HIDDeviceDetachedEvent = Event<std::string>;
wxDECLARE_EVENT(EVT_HID_DEVICE_ATTACHED, HIDDeviceAttachedEvent);
wxDECLARE_EVENT(EVT_HID_DEVICE_DETACHED, HIDDeviceDetachedEvent);

// Disk aka Volume attach / detach events from Windows OS.
using VolumeAttachedEvent = SimpleEvent;
using VolumeDetachedEvent = SimpleEvent;
wxDECLARE_EVENT(EVT_VOLUME_ATTACHED, VolumeAttachedEvent);
wxDECLARE_EVENT(EVT_VOLUME_DETACHED, VolumeDetachedEvent);
#endif /* _WIN32 */

wxTopLevelWindow* find_toplevel_parent(wxWindow *window);

enum { DPI_DEFAULT = 96 };

int get_dpi_for_window(const wxWindow *window);
wxFont get_default_font_for_dpi(const wxWindow* window, int dpi);
inline wxFont get_default_font(const wxWindow* window) { return get_default_font_for_dpi(window, get_dpi_for_window(window)); }

bool check_dark_mode();
#ifdef _WIN32
void update_dark_ui(wxWindow* window);
#endif

#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
struct DpiChangedEvent : public wxEvent {
    int dpi;
    wxRect rect;

    DpiChangedEvent(wxEventType eventType, int dpi, wxRect rect)
        : wxEvent(0, eventType), dpi(dpi), rect(rect)
    {}

    virtual wxEvent *Clone() const
    {
        return new DpiChangedEvent(*this);
    }
};

wxDECLARE_EVENT(EVT_DPI_CHANGED_SLICER, DpiChangedEvent);
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN

template<class P> class DPIAware : public P
{
public:
    DPIAware(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos=wxDefaultPosition,
        const wxSize &size=wxDefaultSize, long style=wxDEFAULT_FRAME_STYLE, const wxString &name= wxFrameNameStr, const int font_point_size = -1)
        : P(parent, id, title, pos, size, style, name)
    {
        int dpi = get_dpi_for_window(this);
        m_scale_factor = (float)dpi / (float)DPI_DEFAULT;
        m_prev_scale_factor = m_scale_factor;
		m_normal_font = get_default_font_for_dpi(this, dpi);

        if (font_point_size > 0)
            m_normal_font.SetPointSize(font_point_size);
        else if (parent)
            m_normal_font.SetPointSize(parent->GetFont().GetPointSize());

        /* Because of default window font is a primary display font, 
         * We should set correct font for window before getting em_unit value.
         */
        this->SetFont(m_normal_font);
        this->CenterOnParent();
#ifdef _WIN32
        update_dark_ui(this);
#endif

        // Linux specific issue : get_dpi_for_window(this) still doesn't responce to the Display's scale in new wxWidgets(3.1.3).
        // So, calculate the m_em_unit value from the font size, as before
#if !defined(__WXGTK__)
#ifdef _WIN32
        const double font_to_em_koef = 10./9.;// Default font point size on Windows is 9 pt
#else // ifdef __WXOSX__
        const double font_to_em_koef = 10./11.;// Default font point size on OSX is 11 pt
#endif
        m_em_unit_from_font_size = int(font_to_em_koef * m_normal_font.GetPointSize());
        m_em_unit = std::max<int>(10, int(m_scale_factor * m_em_unit_from_font_size));
#else
        // initialize default width_unit according to the width of the one symbol ("m") of the currently active font of this window.
        m_em_unit = std::max<size_t>(10, this->GetTextExtent("m").x - 1);
#endif // __WXGTK__

//        recalc_font();

#ifndef __WXOSX__
#if wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
        this->Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& evt) {
	            m_scale_factor = (float)evt.GetNewDPI().x / (float)DPI_DEFAULT;
	            m_new_font_point_size = get_default_font_for_dpi(this, evt.GetNewDPI().x).GetPointSize();
	            if (m_can_rescale && (m_force_rescale || is_new_scale_factor()))
	                rescale(wxRect());
            });
#else
        this->Bind(EVT_DPI_CHANGED_SLICER, [this](const DpiChangedEvent& evt) {
            m_scale_factor = (float)evt.dpi / (float)DPI_DEFAULT;

            m_new_font_point_size = get_default_font_for_dpi(this, evt.dpi).GetPointSize();

            if (!m_can_rescale)
                return;

            if (m_force_rescale || is_new_scale_factor())
                rescale(evt.rect);
            });
#endif // wxVERSION_EQUAL_OR_GREATER_THAN
#endif // no __WXOSX__

        this->Bind(wxEVT_MOVE_START, [this](wxMoveEvent& event)
        {
            event.Skip();

            // Suppress application rescaling, when a MainFrame moving is not ended
            m_can_rescale = false;
        });

        this->Bind(wxEVT_MOVE_END, [this](wxMoveEvent& event)
        {
            event.Skip();

            m_can_rescale = is_new_scale_factor();

            // If scale factor is different after moving of MainFrame ...
            if (m_can_rescale)
                // ... rescale application
                rescale(event.GetRect());
            else
            // set value to _true_ in purpose of possibility of a display dpi changing from System Settings
                m_can_rescale = true;
        });

        this->Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event)
        {
            event.Skip();
            on_sys_color_changed();
        });
    }

    virtual ~DPIAware() {}

    float   scale_factor() const        { return m_scale_factor; }
    float   prev_scale_factor() const   { return m_prev_scale_factor; }

    int     em_unit() const             { return m_em_unit; }
    const wxFont& normal_font() const   { return m_normal_font; }
    void enable_force_rescale()         { m_force_rescale = true; }

#ifdef _WIN32
    void force_color_changed()
    {
        update_dark_ui(this);
        on_sys_color_changed();
    }
#endif

protected:
    virtual void on_dpi_changed(const wxRect &suggested_rect) = 0;
    virtual void on_sys_color_changed() {};

private:
    float m_scale_factor;
    int m_em_unit;
    int m_em_unit_from_font_size {10};

    wxFont m_normal_font;
    float m_prev_scale_factor;
    bool  m_can_rescale{ true };
    bool m_force_rescale{ false };

    int   m_new_font_point_size;

    // check if new scale is differ from previous
    bool    is_new_scale_factor() const { return fabs(m_scale_factor - m_prev_scale_factor) > 0.001; }

    // function for a font scaling of the window
    void    scale_win_font(wxWindow *window, const int font_point_size)
    {
        wxFont new_font(window->GetFont());
        new_font.SetPointSize(font_point_size);
        window->SetFont(new_font);
    }

    // recursive function for scaling fonts for all controls in Window
    void    scale_controls_fonts(wxWindow *window, const int font_point_size)
    {
        auto children = window->GetChildren();

        for (auto child : children) {
            scale_controls_fonts(child, font_point_size);
            scale_win_font(child, font_point_size);
        }

        window->Layout();
    }

    void    rescale(const wxRect &suggested_rect)
    {
        this->Freeze();

        m_force_rescale = false;
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
        // rescale fonts of all controls
        scale_controls_fonts(this, m_new_font_point_size);
        // rescale current window font
        scale_win_font(this, m_new_font_point_size);
#endif // wxVERSION_EQUAL_OR_GREATER_THAN

        // set normal application font as a current window font
        m_normal_font = this->GetFont();

        // update em_unit value for new window font
        m_em_unit = std::max<int>(10, int(m_scale_factor * m_em_unit_from_font_size));

        // rescale missed controls sizes and images
        on_dpi_changed(suggested_rect);

        this->Layout();
        this->Thaw();

        // reset previous scale factor from current scale factor value
        m_prev_scale_factor = m_scale_factor;
    }

#if 0 //#ifdef _WIN32  // #ysDarkMSW - Allow it when we deside to support the sustem colors for application 
    bool HandleSettingChange(WXWPARAM wParam, WXLPARAM lParam) override
    {
        update_dark_ui(this);
        on_sys_color_changed();

        // let the system handle it
        return false;
    }
#endif

};

typedef DPIAware<wxFrame> DPIFrame;
typedef DPIAware<wxDialog> DPIDialog;


class EventGuard
{
    // This is a RAII-style smart-ptr-like guard that will bind any event to any event handler
    // and unbind it as soon as it goes out of scope or unbind() is called.
    // This can be used to solve the annoying problem of wx events being delivered to freed objects.

private:
    // This is a way to type-erase both the event type as well as the handler:

    struct EventStorageBase {
        virtual ~EventStorageBase() {}
    };

    template<class EvTag, class Fun>
    struct EventStorageFun : EventStorageBase {
        wxEvtHandler *emitter;
        EvTag tag;
        Fun fun;

        EventStorageFun(wxEvtHandler *emitter, const EvTag &tag, Fun fun)
            : emitter(emitter)
            , tag(tag)
            , fun(std::move(fun))
        {
            emitter->Bind(this->tag, this->fun);
        }

        virtual ~EventStorageFun() { emitter->Unbind(tag, fun); }
    };

    template<typename EvTag, typename Class, typename EvArg, typename EvHandler>
    struct EventStorageMethod : EventStorageBase {
        typedef void(Class::* MethodPtr)(EvArg &);

        wxEvtHandler *emitter;
        EvTag tag;
        MethodPtr method;
        EvHandler *handler;

        EventStorageMethod(wxEvtHandler *emitter, const EvTag &tag, MethodPtr method, EvHandler *handler)
            : emitter(emitter)
            , tag(tag)
            , method(method)
            , handler(handler)
        {
            emitter->Bind(tag, method, handler);
        }

        virtual ~EventStorageMethod() { emitter->Unbind(tag, method, handler); }
    };

    std::unique_ptr<EventStorageBase> event_storage;
public:
    EventGuard() {}
    EventGuard(const EventGuard&) = delete;
    EventGuard(EventGuard &&other) : event_storage(std::move(other.event_storage)) {}

    template<class EvTag, class Fun>
    EventGuard(wxEvtHandler *emitter, const EvTag &tag, Fun fun)
        :event_storage(new EventStorageFun<EvTag, Fun>(emitter, tag, std::move(fun)))
    {}

    template<typename EvTag, typename Class, typename EvArg, typename EvHandler>
    EventGuard(wxEvtHandler *emitter, const EvTag &tag, void(Class::* method)(EvArg &), EvHandler *handler)
        :event_storage(new EventStorageMethod<EvTag, Class, EvArg, EvHandler>(emitter, tag, method, handler))
    {}

    EventGuard& operator=(const EventGuard&) = delete;
    EventGuard& operator=(EventGuard &&other)
    {
        event_storage = std::move(other.event_storage);
        return *this;
    }

    void unbind() { event_storage.reset(nullptr); }
    explicit operator bool() const { return !!event_storage; }
};


class CheckboxFileDialog : public wxFileDialog
{
public:
    CheckboxFileDialog(wxWindow *parent,
        const wxString &checkbox_label,
        bool checkbox_value,
        const wxString &message = wxFileSelectorPromptStr,
        const wxString &default_dir = wxEmptyString,
        const wxString &default_file = wxEmptyString,
        const wxString &wildcard = wxFileSelectorDefaultWildcardStr,
        long style = wxFD_DEFAULT_STYLE,
        const wxPoint &pos = wxDefaultPosition,
        const wxSize &size = wxDefaultSize,
        const wxString &name = wxFileDialogNameStr
    );

    bool get_checkbox_value() const;

private:
    struct ExtraPanel : public wxPanel
    {
        wxCheckBox *cbox;

        ExtraPanel(wxWindow *parent);
        static wxWindow* ctor(wxWindow *parent);
    };

    wxString checkbox_label;
};

//std::ostream& operator<<(std::ostream &os, const WindowMetrics& metrics);

class TaskTimer
{
    std::chrono::milliseconds   start_timer;
    std::string                 task_name;
public:
    TaskTimer(std::string task_name);

    ~TaskTimer();
};

class KeyAutoRepeatFilter
{
    size_t m_count{ 0 };

public:
    void increase_count() { ++m_count; }
    void reset_count() { m_count = 0; }
    bool is_first() const { return m_count == 0; }
};

}}

#endif
