// PrusaSlicer is released under the terms of the AGPLv3 or higher.
#include "I18N.hpp"
#include "CameraPanel.hpp"
#include "CameraSettings.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ComboBox.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/button.h>
#include <wx/bmpbuttn.h>
#include <wx/graphics.h>
#include <wx/statbmp.h>
#include <cmath>
#include <wx/dcbuffer.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/uri.h>
#include <algorithm>
#include <cstring>

namespace Slic3r::GUI {

// A native, keyboard-accessible button with an animated play-to-stop glyph.
class CameraPlaybackButton final : public wxBitmapButton
{
public:
    enum class State { Stopped, Connecting, Playing };
    explicit CameraPlaybackButton(wxWindow* parent)
        : wxBitmapButton(parent, wxID_ANY, wxBitmap(28, 28), wxDefaultPosition,
            wxDefaultSize, wxBU_EXACTFIT | wxBORDER_NONE), m_animation(this)
    {
        SetMinSize(FromDIP(wxSize(32, 32)));
        SetName(_L("Start stream"));
        SetToolTip(_L("Start stream"));
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
            const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - m_started).count();
            const double t = std::min(1.0, elapsed / 180.0);
            const double eased = t * t * (3.0 - 2.0 * t);
            m_position = m_from + (m_target - m_from) * eased;
            m_phase += 0.14;
            refresh_bitmap();
            if (t >= 1.0 && m_state != State::Connecting)
                m_animation.Stop();
        }, m_animation.GetId());
        refresh_bitmap();
    }
    ~CameraPlaybackButton() override { m_animation.Stop(); }

    void set_state(State state)
    {
        if (state == m_state)
            return;
        m_state = state;
        m_from = m_position;
        m_target = state == State::Stopped ? 0.0 : 1.0;
        m_started = std::chrono::steady_clock::now();
        const wxString action = state == State::Stopped ? _L("Start stream") :
            state == State::Connecting ? _L("Cancel connection") : _L("Stop stream");
        SetName(action);
        SetToolTip(action);
        m_animation.Start(16);
        refresh_bitmap();
    }
    void refresh_bitmap()
    {
        wxBitmap bitmap;
        bitmap.CreateWithDIPSize(FromDIP(wxSize(28, 28)), GetDPIScaleFactor(), 32);
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        {
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
            if (gc) {
                gc->Scale(double(FromDIP(28)) / 28.0, double(FromDIP(28)) / 28.0);
                const auto channel = [this](int a, int b) { return int(a + (b - a) * m_position); };
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(wxColour(channel(237, 116), channel(107, 121), channel(33, 129))));
                gc->DrawEllipse(4, 4, 20, 20);
                const double play[4][2] = {{-3.5, -5.5}, {6, 0}, {-3.5, 5.5}, {-3.5, -5.5}};
                const double stop[4][2] = {{-4, -4}, {4, -4}, {4, 4}, {-4, 4}};
                auto glyph = gc->CreatePath();
                for (int i = 0; i < 4; ++i) {
                    const double x = 14 + play[i][0] + (stop[i][0] - play[i][0]) * m_position;
                    const double y = 14 + play[i][1] + (stop[i][1] - play[i][1]) * m_position;
                    if (i == 0) glyph.MoveToPoint(x, y); else glyph.AddLineToPoint(x, y);
                }
                glyph.CloseSubpath();
                gc->SetBrush(*wxWHITE_BRUSH);
                gc->FillPath(glyph);
                if (m_state == State::Connecting) {
                    gc->SetPen(wxPen(wxColour(237, 107, 33), 2));
                    auto arc = gc->CreatePath();
                    arc.AddArc(14, 14, 12, m_phase, m_phase + 4.2, true);
                    gc->StrokePath(arc);
                }
            }
        }
        dc.SelectObject(wxNullBitmap);
        SetBitmap(bitmap);
    }
private:
    wxTimer m_animation;
    State m_state = State::Stopped;
    double m_position = 0;
    double m_from = 0;
    double m_target = 0;
    double m_phase = 0;
    std::chrono::steady_clock::time_point m_started;
};

// Native rendering of the spool in resources/web/other_loading.html: the same
// six-second winding cycle and 0 -> 540 -> 0 degree rotation, without a WebView.
class CameraConnectingSpool final : public wxPanel
{
public:
    explicit CameraConnectingSpool(wxWindow* parent) : wxPanel(parent), m_timer(this)
    {
        SetMinSize(FromDIP(wxSize(80, 80)));
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetName(_L("Connecting to camera"));
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Refresh(); }, m_timer.GetId());
        Bind(wxEVT_PAINT, [this](wxPaintEvent&) { paint(); });
    }
    ~CameraConnectingSpool() override { m_timer.Stop(); }
    void start()
    {
        m_started = std::chrono::steady_clock::now();
        Show();
        m_timer.Start(33);
        Refresh();
    }
    void stop() { m_timer.Stop(); Hide(); }

private:
    void paint()
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
        if (!gc) return;
        const wxSize size = GetClientSize();
        gc->Translate(size.x / 2.0, size.y / 2.0);
        const double scale = std::min(size.x, size.y) / 500.0;
        gc->Scale(scale, scale);
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - m_started).count();
        const double phase = std::fmod(seconds, 6.0) / 3.0;
        const double winding = phase <= 1.0 ? phase : 2.0 - phase;
        const double radius = 145.0 - 65.0 * winding;
        const double thickness = 130.0 * (1.0 - winding);
        gc->SetBrush(*wxTRANSPARENT_BRUSH);
        const wxColour orange(250, 104, 49);
        if (thickness > 0.01) {
            gc->SetPen(wxPen(orange, int(std::lround(thickness))));
            gc->DrawEllipse(-radius, -radius, radius * 2, radius * 2);
        }
        wxPen filament(orange, 12);
        filament.SetCap(wxCAP_ROUND);
        gc->SetPen(filament);
        const double x = 204.0 - 130.0 * winding;
        gc->StrokeLine(x, 0, x, 245);

        const wxColour spool = wxGetApp().dark_mode() ? wxColour(205, 207, 212) : wxColour(35, 31, 32);
        gc->SetPen(wxPen(spool, 12));
        gc->DrawEllipse(-244, -244, 488, 488);
        gc->SetPen(wxPen(spool, 18));
        gc->DrawEllipse(-71, -71, 142, 142);

        // Clip the rotating honeycomb to the circular spool face.
        constexpr int points = 120;
        wxPoint outer[points];
        wxPoint inner[points];
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i < points; ++i) {
            const double angle = 2 * pi * i / points;
            outer[i] = wxPoint(int(242.5 * std::cos(angle)), int(242.5 * std::sin(angle)));
            inner[i] = wxPoint(int(71.5 * std::cos(angle)), int(71.5 * std::sin(angle)));
        }
        wxRegion ring(points, outer);
        ring.Subtract(wxRegion(points, inner));
        gc->Clip(ring);
        gc->Rotate(winding * 3 * pi);
        gc->SetPen(wxPen(spool, 10));
        // The source SVG's hexagon pattern has a 50 x 87.5 unit repeat.
        auto mesh = gc->CreatePath();
        for (int row = -5; row <= 5; ++row) {
            for (int column = -7; column <= 7; ++column) {
                const double cx = column * 50.0 + (row % 2 ? 25.0 : 0.0);
                const double cy = row * 43.75;
                for (int corner = 0; corner <= 6; ++corner) {
                    const double angle = pi / 2 + corner * pi / 3;
                    const double px = cx + 28.8675 * std::cos(angle);
                    const double py = cy + 28.8675 * std::sin(angle);
                    if (corner == 0) mesh.MoveToPoint(px, py); else mesh.AddLineToPoint(px, py);
                }
            }
        }
        gc->StrokePath(mesh);
    }
    wxTimer m_timer;
    std::chrono::steady_clock::time_point m_started;
};

class CameraSettingsDialog final : public DPIDialog
{
public:
    CameraSettingsDialog(wxWindow* parent, const wxString& printer,
        const DynamicPrintConfig& config)
        : DPIDialog(parent, wxID_ANY, _L("Camera settings"), wxDefaultPosition,
            wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_config(config)
    {
        SetFont(wxGetApp().normal_font());
        auto* root = new wxBoxSizer(wxVERTICAL);
        root->Add(new wxStaticText(this, wxID_ANY, printer), 0, wxALL, FromDIP(16));
        m_settings = new CameraSettings(this, config);
        root->Add(m_settings, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));
        root->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0,
            wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));
        SetSizerAndFit(root);
        GetSizer()->SetSizeHints(this);
        CentreOnParent();
        on_sys_color_changed();
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (m_settings->apply(m_config)) EndModal(wxID_OK);
        }, wxID_OK);
    }
    const DynamicPrintConfig& config() const { return m_config; }
protected:
    void on_dpi_changed(const wxRect&) override { GetSizer()->SetSizeHints(this); Fit(); }
    void on_sys_color_changed() override
    {
        wxGetApp().UpdateDlgDarkUI(this);
        m_settings->sys_color_changed();
        wxGetApp().UpdateDarkUI(FindWindowById(wxID_OK, this));
        wxGetApp().UpdateDarkUI(FindWindowById(wxID_CANCEL, this));
    }
private:
    CameraSettings* m_settings;
    DynamicPrintConfig m_config;
};

CameraPanel::CameraPanel(wxWindow* parent) : wxPanel(parent), m_timer(this)
{
    auto* root = new wxBoxSizer(wxVERTICAL);
    const int padding = FromDIP(16);
    m_preview = new wxPanel(this);
    auto* preview = new wxBoxSizer(wxVERTICAL);
    m_header = new wxPanel(m_preview);
    auto* header = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(m_header, wxID_ANY, _L("Camera"));
    header->Add(m_title, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, padding);
    m_printer_choice = new ComboBox(m_header, wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxSize(32 * wxGetApp().em_unit(), -1), 0, nullptr, wxCB_READONLY | DD_NO_CHECK_ICON);
    m_printer_choice->SetName(_L("Camera printer"));
    m_printer_choice->SetToolTip(_L("Select the physical printer whose camera you want to view. A white dot marks printers with a configured camera."));
    header->Add(m_printer_choice, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, padding);
    m_printer_choice->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) {
        const int index = m_printer_choice->GetSelection();
        if (index < 0 || size_t(index) >= m_printer_names.size()) return;
        m_view_printer = m_printer_names[index];
        refresh_printer();
    });
    header->AddStretchSpacer();
    m_settings = new ScalableButton(m_header, wxID_ANY, "cog");
    m_settings->SetName(_L("Camera settings"));
    m_settings->SetToolTip(_L("Camera settings"));
    m_settings->SetMinSize(FromDIP(wxSize(32, 32)));
    header->Add(m_settings, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, padding);
    m_header->SetSizer(header);
    m_header->SetMinSize(FromDIP(wxSize(-1, 40)));
    preview->Add(m_header, 0, wxEXPAND);

    m_video = new wxPanel(m_preview);
    m_video->SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_video->SetMinSize(FromDIP(wxSize(320, 180)));
    preview->Add(m_video, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

    m_idle_content = new wxPanel(m_video);
    auto* idle = new wxBoxSizer(wxVERTICAL);
    m_idle_logo = new wxStaticBitmap(m_idle_content, wxID_ANY,
        ScalableBitmap(m_idle_content, "PrusaSlicer", 64).bmp());
    idle->Add(m_idle_logo, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(20));
    m_connecting_spool = new CameraConnectingSpool(m_idle_content);
    idle->Add(m_connecting_spool, 0, wxALIGN_CENTER | wxBOTTOM, FromDIP(20));
    m_connecting_spool->Hide();
    m_idle_title = new wxStaticText(m_idle_content, wxID_ANY, _L("Camera stream not started"));
    m_idle_title->SetFont(wxGetApp().normal_font().Bold().Scaled(1.5));
    idle->Add(m_idle_title, 0, wxALIGN_CENTER | wxBOTTOM, 8);
    m_idle_hint = new wxStaticText(m_idle_content, wxID_ANY, _L("Start streaming to see your printer's live view."));
    idle->Add(m_idle_hint, 0, wxALIGN_CENTER | wxBOTTOM, 24);
    m_start_stream = new Button(m_idle_content, _L("Start stream"));
    m_start_stream->SetName(_L("Start stream"));
    m_start_stream->SetPaddingSize(FromDIP(wxSize(24, 12)));
    m_start_stream->SetCornerRadius(6);
    m_start_stream->SetBorderWidth(0);
    m_start_stream->SetTextColor(StateColor(*wxWHITE));
    m_start_stream->SetBackgroundColor(StateColor(
        std::make_pair(wxColour(255, 128, 55), int(StateColor::Hovered)),
        std::make_pair(wxColour(237, 107, 33), int(StateColor::Normal))));
    idle->Add(m_start_stream, 0, wxALIGN_CENTER);
    m_idle_content->SetSizer(idle);
    auto* idle_row = new wxBoxSizer(wxHORIZONTAL);
    idle_row->AddStretchSpacer();
    idle_row->Add(m_idle_content, 0, wxALIGN_CENTER_VERTICAL);
    idle_row->AddStretchSpacer();
    auto* video_layout = new wxBoxSizer(wxVERTICAL);
    video_layout->AddStretchSpacer();
    video_layout->Add(idle_row, 0, wxEXPAND);
    video_layout->AddStretchSpacer();
    m_video->SetSizer(video_layout);

    m_footer = new wxPanel(m_preview);
    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    m_playback = new CameraPlaybackButton(m_footer);
    footer->Add(m_playback, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(8));
    m_status = new wxStaticText(m_footer, wxID_ANY, _L("Camera stopped."),
        wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
    m_status->SetMinSize(wxSize(1, -1));
    footer->Add(m_status, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, padding);
    m_footer->SetSizer(footer);
    m_footer->SetMinSize(FromDIP(wxSize(-1, 40)));
    preview->Add(m_footer, 0, wxEXPAND);
    m_preview->SetSizer(preview);
    root->Add(m_preview, 1, wxEXPAND | wxALL, 8);
    SetSizer(root);

    const auto toggle_playback = [this](wxCommandEvent&) {
        if (m_timer.IsRunning()) stop_camera(); else start_camera();
    };
    m_playback->Bind(wxEVT_BUTTON, toggle_playback);
    m_start_stream->Bind(wxEVT_BUTTON, toggle_playback);
    m_settings->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        show_settings();
    });
    m_video->Bind(wxEVT_PAINT, &CameraPanel::paint_video, this);
    m_video->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) { m_video->Refresh(); event.Skip(); });
    Bind(wxEVT_TIMER, &CameraPanel::on_timer, this, m_timer.GetId());
    Bind(wxEVT_SHOW, [this](wxShowEvent& event) {
        if (!event.IsShown())
            stop_camera(false);
        else {
            refresh_printer();
            if (m_stream_requested && !m_timer.IsRunning()) start_camera();
        }
        event.Skip();
    });
    refresh_printer();
    show_idle();
    sys_color_changed();
}

CameraPanel::~CameraPanel() { stop_camera(); }

void CameraPanel::refresh_printer()
{
    auto& printers = wxGetApp().preset_bundle->physical_printers;
    // Migrate the earlier single-camera setting once, to the selected printer.
    // Clearing it prevents assigning that camera to other printers later.
    const std::string legacy_url = wxGetApp().app_config->get("camera", "camera_url");
    if (printers.has_selection() && !legacy_url.empty()) {
        PhysicalPrinter migrated = printers.get_selected_printer();
        const wxURI uri(from_u8(legacy_url));
        if (migrated.config.opt_string("camera_type") == "none" &&
            uri.GetScheme().CmpNoCase("rtsp") == 0 && !uri.GetServer().empty() &&
            uri.GetUserInfo().empty()) {
            migrated.config.opt_string("camera_type") = "rtsp";
            migrated.config.opt_string("camera_url") = legacy_url;
            printers.save_printer(migrated);
        }
        wxGetApp().app_config->set("camera", "camera_url", "");
    }
    const std::string selected = printers.has_selection() ? printers.get_selected_printer().name : "";
    if (selected != m_selected_printer) {
        m_selected_printer = selected;
        m_view_printer = selected;
    }
    if (!printers.find_printer(m_view_printer)) m_view_printer = selected;
    m_printer_choice->Clear();
    m_printer_names.clear();
    int selection = wxNOT_FOUND;
    for (const PhysicalPrinter& candidate : printers) {
        const std::string& type = candidate.config.opt_string("camera_type");
        const bool configured = ((type == "rtsp" || type == "http") &&
            !candidate.config.opt_string("camera_url").empty()) ||
            (type == "usb" && !candidate.config.opt_string("camera_device").empty());
        m_printer_names.push_back(candidate.name);
        const int index = m_printer_choice->Append(from_u8(candidate.name) +
            (configured ? wxString::FromUTF8("  \xE2\x97\x8F") : wxString()));
        if (candidate.name == m_view_printer) selection = index;
    }
    m_printer_choice->SetSelection(selection);
    m_printer_choice->Enable(!printers.empty());
    const PhysicalPrinter* printer = printers.find_printer(m_view_printer);
    const std::string name = printer ? printer->name : "";
    const std::string type = printer ? printer->config.opt_string("camera_type") : "none";
    const std::string url = printer ? printer->config.opt_string("camera_url") : "";
    const std::string device = printer ? printer->config.opt_string("camera_device") : "";
    if (name == m_printer_name && type == m_camera_type &&
        url == into_u8(m_camera_url) && device == m_camera_device)
        return;
    stop_camera(false);
    m_printer_name = name;
    m_camera_type = type;
    m_camera_url = from_u8(url);
    m_camera_device = device;
    m_stream_requested = m_requested_printers.count(name) != 0;
    if (type == "none") {
        m_requested_printers.erase(name);
        m_stream_requested = false;
    }
    m_header->Layout();
    show_idle();
    if (IsShownOnScreen() && m_stream_requested) start_camera();
}

void CameraPanel::show_settings()
{
    if (m_settings_open) return;
    auto& printers = wxGetApp().preset_bundle->physical_printers;
    const PhysicalPrinter* viewed_printer = printers.find_printer(m_printer_name);
    if (!viewed_printer) {
        show_info(this, _L("Select or add a physical printer in the Printers tab to configure its camera."));
        return;
    }
    m_settings_open = true;
    const std::string name = viewed_printer->name;
    CameraSettingsDialog dialog(this, from_u8(name), viewed_printer->config);
    const int result = dialog.ShowModal();
    m_settings_open = false;
    if (result == wxID_OK) {
        // Look up again: a dialog must never retain a pointer into the collection.
        if (PhysicalPrinter* printer = printers.find_printer(name)) {
            PhysicalPrinter edited = *printer;
            edited.config.apply(dialog.config());
            // Saving a profile selects it internally; preserve the print destination.
            const std::string selected = printers.get_selected_full_printer_name();
            printers.save_printer(edited);
            if (selected.empty()) printers.unselect_printer();
            else printers.select_printer(selected);
            refresh_printer();
        }
    }
}

void CameraPanel::set_status(const wxString& status)
{
    if (m_status->GetLabel() != status) {
        m_status->SetLabel(status);
        m_status->SetToolTip(status);
        Layout();
    }
}

void CameraPanel::start_camera()
{
    if (m_printer_name.empty() || m_camera_type == "none") {
        show_settings();
        return;
    }
    const std::string source = m_camera_type == "usb" ? m_camera_device : into_u8(m_camera_url);
    if (source.empty()) {
        show_settings();
        return;
    }
    stop_camera(false);
    try {
        m_decoder.start(source, m_camera_type);
    } catch (const std::exception& error) {
        set_status(from_u8(error.what()));
        return;
    }
    m_requested_printers.insert(m_printer_name);
    m_stream_requested = true;
    m_playback->set_state(CameraPlaybackButton::State::Connecting);
    show_idle(true);
    m_received_frame = false;
    m_last_frame = std::chrono::steady_clock::now();
    set_status(_L("Connecting to camera..."));
    m_timer.Start(50);
}

void CameraPanel::stop_camera(bool clear_request)
{
    if (clear_request) {
        m_stream_requested = false;
        m_requested_printers.erase(m_printer_name);
    }
    m_timer.Stop();
    m_decoder.stop();
    m_frame = wxBitmap();
    m_video->Refresh();
    m_playback->set_state(CameraPlaybackButton::State::Stopped);
    show_idle();
    set_status(_L("Camera stopped."));
}

void CameraPanel::on_timer(wxTimerEvent&)
{
    std::vector<unsigned char> rgb;
    if (m_decoder.take_frame(rgb)) {
        wxImage image(VideoStreamDecoder::width, VideoStreamDecoder::height);
        std::memcpy(image.GetData(), rgb.data(), rgb.size());
        m_frame = wxBitmap(image);
        if (!m_received_frame) {
            m_playback->set_state(CameraPlaybackButton::State::Playing);
            m_connecting_spool->stop();
            m_idle_content->Hide();
            m_video->Layout();
        }
        m_received_frame = true;
        m_last_frame = std::chrono::steady_clock::now();
        set_status(_L("Camera live"));
        m_video->Refresh();
    }
    const auto state = m_decoder.state();
    if (state == VideoStreamDecoder::State::Error || state == VideoStreamDecoder::State::Ended ||
        state == VideoStreamDecoder::State::Stopped) {
        const wxString error = from_u8(m_decoder.last_error());
        stop_camera(false);
        set_status(error.empty() ? _L("Camera disconnected. Press Play to reconnect.") : error);
    } else if (std::chrono::steady_clock::now() - m_last_frame > std::chrono::seconds(20)) {
        const bool had_frame = m_received_frame;
        stop_camera(false);
        set_status(had_frame ? _L("Camera stream stalled. Press Play to reconnect.") :
            _L("Camera timed out. Check the camera IP, local network connection, and RTSP setting."));
    }
}

void CameraPanel::paint_video(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(m_video);
    dc.SetBackground(wxBrush(wxGetApp().dark_mode() ? wxColour(27, 28, 32) : wxColour(245, 245, 247)));
    dc.Clear();
    if (!m_frame.IsOk())
        return;
    const wxSize size = m_video->GetClientSize();
    const double scale = std::min(double(size.x) / m_frame.GetWidth(), double(size.y) / m_frame.GetHeight());
    const int width = int(m_frame.GetWidth() * scale), height = int(m_frame.GetHeight() * scale);
    if (width > 0 && height > 0)
        dc.DrawBitmap(wxBitmap(m_frame.ConvertToImage().Scale(width, height, wxIMAGE_QUALITY_NORMAL)),
            (size.x - width) / 2, (size.y - height) / 2);
}

void CameraPanel::show_idle(bool connecting)
{
    m_idle_logo->Show(!connecting);
    if (connecting) m_connecting_spool->start(); else m_connecting_spool->stop();
    m_idle_title->SetLabel(connecting ? _L("Connecting to camera...") : _L("Camera stream not started"));
    m_idle_hint->SetLabel(connecting ? _L("Opening your printer's live view.") :
        m_printer_name.empty() ? _L("Select a physical printer to configure its camera.") :
        m_camera_type == "none" ? _L("Add a camera in this physical printer's settings.") :
        _L("Start streaming to see your printer's live view."));
    m_start_stream->SetLabel(connecting ? _L("Cancel") : _L("Start stream"));
    m_start_stream->SetName(connecting ? _L("Cancel connection") : _L("Start stream"));
    m_idle_content->Show();
    m_idle_content->Layout();
    m_video->Layout();
    m_video->Refresh();
}

void CameraPanel::sys_color_changed()
{
    wxGetApp().UpdateDarkUI(this);
    const bool dark = wxGetApp().dark_mode();
    const wxColour bars = dark ? wxColour(47, 48, 53) : wxColour(230, 231, 234);
    const wxColour background = dark ? wxColour(27, 28, 32) : wxColour(245, 245, 247);
    const wxColour muted = dark ? wxColour(174, 177, 184) : wxColour(84, 87, 94);
    m_preview->SetBackgroundColour(dark ? wxColour(65, 66, 73) : wxColour(211, 213, 219));
    m_header->SetBackgroundColour(bars);
    m_footer->SetBackgroundColour(bars);
    m_idle_content->SetBackgroundColour(background);
    m_idle_title->SetBackgroundColour(background);
    m_idle_hint->SetBackgroundColour(background);
    m_idle_title->SetForegroundColour(dark ? *wxWHITE : wxColour(38, 39, 44));
    m_idle_hint->SetForegroundColour(muted);
    m_title->SetBackgroundColour(bars);
    m_title->SetForegroundColour(muted);
    m_status->SetBackgroundColour(bars);
    m_status->SetForegroundColour(muted);
    wxGetApp().UpdateDarkUI(m_printer_choice);
    m_settings->sys_color_changed();
    m_playback->refresh_bitmap();
    m_connecting_spool->Refresh();
    m_video->Refresh();
}

} // namespace Slic3r::GUI
