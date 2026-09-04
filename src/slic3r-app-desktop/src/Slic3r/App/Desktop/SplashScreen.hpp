#pragma once

#include <wx/splash.h>
#include <wx/font.h>

namespace Slic3r::App::Desktop {

class SplashScreen : public wxSplashScreen
{
public:
    SplashScreen(bool is_editor, wxPoint pos = wxDefaultPosition);

    void SetText(const wxString& text);
    void Decorate(wxBitmap& bmp);

private:
    struct ConstantText
    {
        wxString title;
        wxString version;
        wxString credits;

        wxFont title_font;
        wxFont version_font;
        wxFont credits_font;

        void init(const wxFont& init_font, bool is_editor, int text_banner_width);
    } m_constant_text;

    void init_constant_text();
    void set_bitmap(wxBitmap& bmp);

private:
    bool m_is_editor{true};
    wxColour m_text_color;
    wxColour m_highlighted_text_color;
    wxBitmap m_main_bitmap;
    wxFont m_action_font;
    wxRect m_state_text_rect;
    float m_scale{1.0};
};

} // namespace Slic3r::App::Desktop
