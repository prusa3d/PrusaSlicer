#include "SplashScreen.hpp"
#include "MainFrame.hpp"

#include <Slic3r/App/WX/BitmapCache.hpp>
#include <Slic3r/App/WX/WidgetsConfig.hpp>
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include <Slic3r/App/WX/I18N.hpp>

#include "Slic3r/Directories.hpp"

#include "Slic3r/Version.hpp"

#include <wx/font.h>
#include <wx/dcmemory.h>
#include <wx/dcclient.h>
#include <wx/display.h>
#include <wx/fontutil.h>

namespace Slic3r::App::Desktop {

namespace {
// Helper function to determine the scale factor of the display where the splash screen will appear.
// This needs to be a free function so it can be called before the constructor's initializer list.
float get_display_scale_factor(const wxPoint& pos)
{
    int display_idx = wxDisplay::GetFromPoint(pos == wxDefaultPosition ? wxGetMousePosition() : pos);
    wxDisplay display(display_idx != wxNOT_FOUND ? display_idx : 0);
    double scale = display.GetScaleFactor();

    // Ensure scale is at least 1.0 as a fallback.
    if (scale < 1.0) {
        scale = 1.0;
    }
    return scale;
}

// This function is responsible for creating a bitmap correctly scaled for the target display.
static wxBitmap make_bitmap(bool is_editor, double scale)
{
    // 1. Load the base 1x resolution bitmap from the JPEG file.
    wxBitmap bmp_1x = wxBitmap(WX::from_u8(var(is_editor ? "splashscreen.jpg" : "splashscreen-gcodepreview.jpg")), wxBITMAP_TYPE_JPEG);

    if (!bmp_1x.IsOk()) {
        // Fallback to a bundled bitmap if the JPEG fails to load.
        wxBitmapBundle* bmp_bndl = WX::get_bmp_bundle("PrusaSlicer", 400);
        bmp_1x = bmp_bndl->GetBitmap(bmp_bndl->GetPreferredBitmapSizeAtScale(1.0));
        // It's critical to have a valid bitmap here.
        if (!bmp_1x.IsOk()) return wxBitmap();
    }

    // 2. Calculate logical and scaled dimensions for the final composite bitmap.
    // The final bitmap has a dark grey banner on the left, making it wider.
    const wxSize image_part_logical_size = bmp_1x.GetSize();
    const int final_logical_width  = lround((double) 3 / 2 * image_part_logical_size.GetWidth());
    const int final_logical_height = image_part_logical_size.GetHeight();

    const int border{1};

    wxSize final_scaled_size(
        lround(final_logical_width * scale),
        lround(final_logical_height * scale)
    );
    final_scaled_size.IncBy(2 * border);

    // 3. Create a wxImage to build the hi-res splash screen.
    wxImage final_image(final_scaled_size);
    if (!final_image.IsOk()) return wxBitmap();

    // 4.1 Render the border
    wxRect rc(final_scaled_size);
    final_image.SetRGB(rc, 100, 100, 100);
    rc.Deflate(border);
    // 4.2 Fill the entire image with the dark grey background color.
    final_image.SetRGB(rc, 26, 26, 26);

    // 5. Rescale the original JPEG image part to the target resolution.
    wxImage image_part_scaled = bmp_1x.ConvertToImage().Rescale(lround(image_part_logical_size.GetWidth() * scale),
                                                               lround(image_part_logical_size.GetHeight() * scale),
                                                               wxIMAGE_QUALITY_HIGH);

    // 6. Paste the scaled image part onto the grey background at the correct position.
    const int image_part_x_offset =
        lround((final_logical_width - image_part_logical_size.GetWidth()) * scale) + border;
    final_image.Paste(image_part_scaled, image_part_x_offset, border);

    // 7. Convert the final wxImage to a wxBitmap.
    wxBitmap final_bitmap(final_image);

#ifndef __WXMSW__
    // 8. This is the crucial step for non-Windows platforms: set the bitmap's scale factor.
    // This tells wxWidgets that this large bitmap should occupy a smaller logical area.
    if (final_bitmap.IsOk()) {
        final_bitmap.SetScaleFactor(scale);
    }
#endif

    return final_bitmap;
}

} // namespace

SplashScreen::SplashScreen(bool is_editor, wxPoint pos) :
    // The base class constructor is called here. We generate the scaled, but undecorated,
    // bitmap by calling our static helper functions directly in the initializer list.
    wxSplashScreen(make_bitmap(is_editor, get_display_scale_factor(pos)),
    wxSPLASH_CENTRE_ON_SCREEN | wxSPLASH_NO_TIMEOUT,
    0, nullptr, wxID_ANY, pos, wxDefaultSize,
    #ifdef NDEBUG // splashcreen on top is annoying in debug mode, when the app starts slowly
        wxSTAY_ON_TOP |
    #endif
    wxNO_BORDER | wxFRAME_NO_TASKBAR),
    m_is_editor(is_editor),
    m_text_color(wxColour(255, 255, 255)),
    m_highlighted_text_color(wxColour(237, 107, 33)),
    m_scale(get_display_scale_factor(pos)) // Initialize member variable for scale factor.
{
#ifndef __WXMSW__
    // For non-Windows platforms, we need to decrease m_scale based on the content scale factor
    m_scale /= this->GetContentScaleFactor();
#endif
    // The base class now has the undecorated bitmap.
    // We get a reference to it, decorate it, and then set it back.
    m_main_bitmap = m_window->GetBitmap();
    if (!m_main_bitmap.IsOk()) {
        return;
    }

    // By default, the splash screen is always shown on the main display,
    // so we need to set its position manually.
    this->SetPosition(pos);
    // The splash screen size may change after being moved to another display,
    // so update it based on the bitmap size.
    this->SetClientSize(m_main_bitmap.GetLogicalWidth(), m_main_bitmap.GetLogicalHeight());
    this->CenterOnScreen();

    // Initialize constant texts and scale fonts using the determined scale factor.
    init_constant_text();

    // This font will be used for the action string.
    m_action_font = m_constant_text.credits_font.Bold();

    // Draw the logo and constant info text onto our hi-res bitmap.
    Decorate(m_main_bitmap);

    // Update the splash screen window with the newly decorated bitmap.
    set_bitmap(m_main_bitmap);
}

void SplashScreen::SetText(const wxString& text)
{
    // It crashes here when the splash is about to be hidden but the SetText is called
    // The m_main_bitmap is already in invalid state, so prevent setting text in such case
    if (!m_main_bitmap.IsOk())
        return;
    // This method creates a temporary copy of the main bitmap to draw on.
    wxBitmap bitmap(m_main_bitmap);
    if (!text.empty()) {
        wxMemoryDC memDC;
        memDC.SelectObject(bitmap);

        memDC.SetFont(m_action_font);
        memDC.SetTextForeground(m_highlighted_text_color);

        // Draw text at a center of the remained banner place.
        memDC.DrawLabel(text, m_state_text_rect, wxALIGN_CENTER_HORIZONTAL);

        memDC.SelectObject(wxNullBitmap);
    }
    // Update the splash screen with the (potentially modified) bitmap.
    set_bitmap(bitmap);

#ifdef __WXOSX__
    // This yield helps ensure the splash screen updates promptly on macOS.
    wxYield();
#endif
}

void SplashScreen::Decorate(wxBitmap& bmp)
{
    if (!bmp.IsOk())
        return;

    // draw text to the box at the left of the splashscreen.
    // this box will be 2/5 of the weight of the bitmap, and be at the left.
    int width = lround(bmp.GetLogicalWidth() * 0.33);

    // load bitmap for logo
    WX::BitmapCache bmp_cache;
    int logo_size = lround(width * 0.25);
    wxBitmap* logo_bmp_ptr = bmp_cache.load_svg(m_is_editor ? "PrusaSlicer" : "PrusaSlicer-gcodeviewer", logo_size, logo_size);
    if (logo_bmp_ptr == nullptr)
        return;

    wxCoord margin = 20 * m_scale;

    // use a memory DC to draw directly onto the bitmap
    wxMemoryDC memDc(bmp);

    // draw logo
    int logo_pos_x = static_cast<int>(0.5 * (width - logo_size));
    memDc.DrawBitmap(*logo_bmp_ptr, logo_pos_x, 2 * margin, true);

    wxRect banner_rect(wxPoint(0, logo_size + margin), wxPoint(width, bmp.GetLogicalHeight()));
    banner_rect.Deflate(margin, 2 * margin);

    wxString strPrusa  = WX::from_u8("Prusa");
    wxString strSlicer = WX::from_u8("Slicer");

    memDc.SetFont(m_constant_text.title_font);
    int widthPrusa    = memDc.GetTextExtent(strPrusa).GetWidth();
    int widthSlicer   = memDc.GetTextExtent(strSlicer).GetWidth();
    int widthCombined = memDc.GetTextExtent(strPrusa + strSlicer).GetWidth();
    int kerning       = widthCombined - (widthPrusa + widthSlicer);

    int banner_h_center = static_cast<int>((width - kerning) * 0.5);
    wxRect banner_rect_Prusa(
        banner_rect.GetLeftTop(),
        wxPoint(banner_h_center, banner_rect.GetBottom())
    );
    banner_h_center += 1 + kerning;
    wxRect banner_rect_Slicer(
        wxPoint(banner_h_center, banner_rect.GetTop()),
        banner_rect.GetRightBottom()
    );

    // draw the (white) labels inside of our black box (at the left of the splashscreen)
    memDc.SetTextForeground(m_text_color);

    memDc.DrawLabel(strPrusa, banner_rect_Prusa, wxALIGN_TOP | wxALIGN_RIGHT);
    memDc.SetTextForeground(m_highlighted_text_color);
    memDc.DrawLabel(strSlicer, banner_rect_Slicer, wxALIGN_TOP | wxALIGN_LEFT);
    memDc.SetTextForeground(m_text_color);

    int title_height = memDc.GetTextExtent(m_constant_text.title).GetY();
    banner_rect.SetTop(banner_rect.GetTop() + title_height + margin);

    memDc.SetFont(m_constant_text.version_font);
    memDc.DrawLabel(m_constant_text.version, banner_rect, wxALIGN_TOP | wxALIGN_CENTER_HORIZONTAL);
    int version_height = memDc.GetTextExtent(m_constant_text.version).GetY();
    banner_rect.SetTop(banner_rect.GetTop() + version_height + margin);

    // save remained place for the text with application state
    m_state_text_rect = banner_rect;

    banner_rect.SetBottom(banner_rect_Prusa.GetBottom());
    memDc.SetFont(m_constant_text.credits_font);
    memDc.DrawLabel(
        m_constant_text.credits,
        banner_rect,
        wxALIGN_BOTTOM | wxALIGN_CENTER_HORIZONTAL
    );
}

void SplashScreen::init_constant_text()
{
    // The text banner width in logical pixels.
    int text_banner_width = lround(0.33 * m_main_bitmap.GetLogicalWidth()) - roundl(m_scale * 80);

    m_constant_text.init(WX::w_config()->normal_font(), m_is_editor, text_banner_width);
}

void SplashScreen::set_bitmap(wxBitmap& bmp)
{
    m_window->SetBitmap(bmp);
    m_window->Refresh();
    m_window->Update();
}

void SplashScreen::ConstantText::init(const wxFont& init_font, bool is_editor, int text_banner_width)
{
    using namespace WX;

    // title
    title = from_u8(SLIC3R_APP_NAME); // is_editor ? SLIC3R_APP_NAME : GCODEVIEWER_APP_NAME;

    // dynamically get the version to display
    // TRN: {} is a placeholder for version number
    version = from_u8(fmt::format(fmt::runtime(Biz::_u8L("Version {}")), SLIC3R_VERSION));

    // credits information
    std::string credits_str = fmt::format(
        "{}\n\n{}\n\n{}",
        fmt::format(
            fmt::runtime(
                // TRN: {} is a placeholder for the application name (e.g., "PrusaSlicer").
                // Please preserve the "\n" newline characters to maintain the 3-line format of this phrase.
                Biz::_u8L(
                    "{} is based on Slic3r\n"
                    "by Alessandro Ranellucci\n"
                    "and the RepRap community."
                )
            ),
            SLIC3R_APP_NAME
        ),
        Biz::_u8L("Developed by Prusa Research."),
        Biz::_u8L("Licensed under GNU AGPLv3.")
    );
    credits = from_u8(credits_str);

    // Use a temporary DC to measure text extents with the correct fonts.
    wxMemoryDC memDC;
    memDC.SetFont(init_font.Bold());
    int title_width        = memDC.GetTextExtent(title).GetX();
    float title_font_scale = static_cast<float>(text_banner_width) / title_width;
    title_font             = init_font.Scaled(title_font_scale);

    credits_font = version_font = title_font.Scaled(0.3f);
    title_font = title_font.Bold();
}

} // namespace Slic3r::App::Desktop
