// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#ifndef slic3r_CameraPanel_hpp_
#define slic3r_CameraPanel_hpp_

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/bitmap.h>
#include <chrono>
#include <set>
#include <vector>
#include "slic3r/Utils/VideoStreamDecoder.hpp"

class wxStaticText;
class wxButton;
class ScalableButton;
class Button;
class ComboBox;

namespace Slic3r::GUI {
class CameraPlaybackButton;
class CameraConnectingSpool;

class CameraPanel : public wxPanel
{
public:
    explicit CameraPanel(wxWindow* parent);
    ~CameraPanel() override;
    void stop_camera(bool clear_request = true);
    void sys_color_changed();
    void refresh_printer();

private:
    void start_camera();
    void on_timer(wxTimerEvent&);
    void paint_video(wxPaintEvent&);
    void set_status(const wxString& status);
    void show_settings();
    void show_idle(bool connecting = false);

    wxString m_camera_url;
    std::string m_printer_name;
    std::string m_view_printer;
    std::string m_selected_printer;
    ComboBox* m_printer_choice;
    std::vector<std::string> m_printer_names;
    std::string m_camera_type = "none";
    std::string m_camera_device;
    std::set<std::string> m_requested_printers;
    wxStaticText* m_status;
    CameraPlaybackButton* m_playback;
    CameraConnectingSpool* m_connecting_spool;
    wxWindow* m_idle_logo;
    Button* m_start_stream;
    wxPanel* m_preview;
    wxPanel* m_header;
    wxPanel* m_footer;
    wxPanel* m_idle_content;
    wxStaticText* m_title;
    wxStaticText* m_idle_title;
    wxStaticText* m_idle_hint;
    ScalableButton* m_settings;
    wxPanel* m_video;
    wxBitmap m_frame;
    wxTimer m_timer;
    VideoStreamDecoder m_decoder;
    bool m_received_frame = false;
    // Playback intent is session-only; hiding the tab releases the connection.
    bool m_stream_requested = false;
    bool m_settings_open = false;
    std::chrono::steady_clock::time_point m_last_frame;
};
} // namespace Slic3r::GUI
#endif
