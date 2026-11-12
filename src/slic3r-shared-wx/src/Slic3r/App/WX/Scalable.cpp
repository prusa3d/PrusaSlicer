#include "Slic3r/App/WX/Scalable.hpp"
#include "Slic3r/App/WX/BitmapGetters.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/MacUtils.hpp"
#include "Slic3r/Log.hpp"

#include <wx/display.h>

#include <set>


namespace Slic3r::App::WX {

// ----------------------------------------------------------------------------
// ScalableBitmap
// ----------------------------------------------------------------------------

ScalableBitmap ScalableBitmap::create_from_svg(
    wxWindow* parent,
    boost::filesystem::path& icon_path,
    const wxSize icon_size
)
{
    return ScalableBitmap(parent, icon_path, icon_size);
}

ScalableBitmap ScalableBitmap::create_from_png_or_jpg(
    wxWindow* parent,
    boost::filesystem::path& icon_path,
    const wxSize icon_size,
    bool as_circle
)
{
    return ScalableBitmap(parent, icon_path, icon_size, as_circle);
}

ScalableBitmap::ScalableBitmap( wxWindow *parent, 
                                const std::string& icon_name,
                                const int  width/* = 16*/,
                                const int  height/* = -1*/,
                                const bool grayscale/* = false*/):
    m_parent(parent), m_icon_name(icon_name),
    m_bmp_width(width), m_bmp_height(height)
{
    m_bmp = *get_bmp_bundle(icon_name, width, height);
    m_bitmap = m_bmp.GetBitmapFor(m_parent);
}

ScalableBitmap::ScalableBitmap( wxWindow*           parent,
                                const std::string&  icon_name,
                                const wxSize        icon_size,
                                const bool          grayscale/* = false*/) :
ScalableBitmap(parent, icon_name, icon_size.x, icon_size.y, grayscale)
{
}


ScalableBitmap::ScalableBitmap(wxWindow* parent,
    boost::filesystem::path& icon_path,
    const  wxSize               icon_size) :
    m_parent(parent),
    m_bmp_width(icon_size.x),
    m_bmp_height(icon_size.y)
{
    wxString path = from_u8(icon_path.string());
    if (icon_path.extension().string() != "svg") {
        SPDLOG_ERROR("Failed to load bitmap {}. Expected SVG file for loading.", into_u8(path));
        return;
    }

    m_bmp = wxBitmapBundle::FromSVGFile(path, icon_size);
}

// Create a circular avatar using per-pixel alpha for a smooth edge.
static wxBitmap create_circular_bitmap_anti_aliased(const wxBitmap& bmp_src, int size)
{
    if (!bmp_src.IsOk()) return wxNullBitmap;

    // Scale source to target size (wxImage scaling)
    wxImage img_src = bmp_src.ConvertToImage();
    wxImage img_out = img_src.Scale(size, size, wxIMAGE_QUALITY_HIGH);

    // Ensure alpha channel exists (we'll overwrite it)
    img_out.InitAlpha();

    const double cx = size / 2.0;
    const double cy = size / 2.0;
    const double radius = size / 2.0;
    // Use pixel-center sampling and linear ramp over ~1 pixel for smoother edge
    const double ramp = 1.0; // width of linear ramp (pixels) for AA

    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            // compute distance from pixel center to circle center
            double px = x + 0.5;
            double py = y + 0.5;
            double dx = px - cx;
            double dy = py - cy;
            double d = sqrt(dx * dx + dy * dy);

            unsigned char a;
            if (d <= radius - ramp * 0.5)
                a = 255;
            else if (d >= radius + ramp * 0.5)
                a = 0;
            else
            {
                // linear interpolation across the ramp => smooth alpha
                double t = (radius + ramp * 0.5 - d) / ramp; // 0..1
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                a = static_cast<unsigned char>(t * 255.0 + 0.5);
            }
            img_out.SetAlpha(x, y, a);
        }
    }

    // Convert to wxBitmap (preserves alpha)
    return wxBitmap(img_out);
}

ScalableBitmap::ScalableBitmap(
    wxWindow* parent,
    boost::filesystem::path& icon_path,
    const wxSize icon_size,
    bool as_circle
) :
    m_parent(parent),
    m_bmp_width(icon_size.x),
    m_bmp_height(icon_size.y)
{
    wxString path = from_u8(icon_path.string());
    wxBitmap bitmap;
    const std::string ext = icon_path.extension().string();

    if (ext == ".png" || ext == ".jpg") {
        if (!bitmap.LoadFile(path, ext == ".png" ? wxBITMAP_TYPE_PNG : wxBITMAP_TYPE_JPEG)) {
            SPDLOG_ERROR("Failed to load bitmap {}", into_u8(path));
            return;
        }

        // check if the bitmap has a square shape
        if (wxSize sz = bitmap.GetSize(); sz.x != sz.y) {
            const int bmp_side = std::min(sz.GetWidth(), sz.GetHeight());

            wxRect rc = sz.GetWidth() > sz.GetHeight() ?
                wxRect(int(0.5 * (sz.x - sz.y)), 0, bmp_side, bmp_side) :
                wxRect(0, int(0.5 * (sz.y - sz.x)), bmp_side, bmp_side);

            // crop bitmap for square
            bitmap = bitmap.GetSubBitmap(rc);
        }
        assert(bitmap.IsOk());

        // get allowed scale factors

        std::set<double> scales = {1.0};
#ifdef __APPLE__
        scales.emplace(mac_max_scaling_factor());
#elif _WIN32
        size_t disp_cnt = wxDisplay::GetCount();
        for (size_t disp = 0; disp < disp_cnt; ++disp)
            scales.emplace(wxDisplay(disp).GetScaleFactor());
#endif

        // create bitmaps for bundle

        wxVector<wxBitmap> bmps;
        for (double scale : scales) {
            int size = icon_size.x * scale;
            if (as_circle) {
                bmps.push_back(create_circular_bitmap_anti_aliased(bitmap, size));
            }
            else {
                // Scale source to target size using wxImage scaling with high quality
                wxImage img = bitmap.ConvertToImage().Scale(size, size, wxIMAGE_QUALITY_HIGH);
                bmps.push_back(wxBitmap(img));
            }
        }
        m_bmp = wxBitmapBundle::FromBitmaps(bmps);
    }
    else {
        SPDLOG_ERROR("Failed to load bitmap {}. Expected PNG or JPG file for loading.", into_u8(path));
    }
}

wxSize ScalableBitmap::GetSize() const 
{ 
    return get_preferred_size(m_bmp, m_parent); 
}

void ScalableBitmap::sys_color_changed()
{
    m_bmp = *get_bmp_bundle(m_icon_name, m_bmp_width, m_bmp_height);
}

// ----------------------------------------------------------------------------
// PrusaButton
// ----------------------------------------------------------------------------

ScalableButton::ScalableButton( wxWindow *          parent,
                                wxWindowID          id,
                                const std::string&  icon_name /*= ""*/,
                                const wxString&     label /* = wxEmptyString*/,
                                const wxSize&       size /* = wxDefaultSize*/,
                                const wxPoint&      pos /* = wxDefaultPosition*/,
                                long                style /*= wxBU_EXACTFIT | wxNO_BORDER*/,
                                int                 width/* = 16*/, 
                                int                 height/* = -1*/) :
    m_parent(parent),
    m_current_icon_name(icon_name),
    m_bmp_width(width),
    m_bmp_height(height),
    m_has_border(!(style & wxNO_BORDER))
{
    Create(parent, id, label, pos, size, style);

    if (!icon_name.empty()) {
        SetBitmap(*get_bmp_bundle(icon_name, width, height));
        if (!label.empty())
            SetBitmapMargins(int(0.5* w_config()->em_unit(parent)), 0);
    }

    if (size != wxDefaultSize)
    {
        const int em = w_config()->em_unit(parent);
        m_width = size.x/em;
        m_height= size.y/em;
    }
}


ScalableButton::ScalableButton( wxWindow *          parent, 
                                wxWindowID          id,
                                const ScalableBitmap&  bitmap,
                                const wxString&     label /*= wxEmptyString*/, 
                                long                style /*= wxBU_EXACTFIT | wxNO_BORDER*/) :
    m_parent(parent),
    m_current_icon_name(bitmap.name()),
    m_bmp_width(bitmap.px_size().x),
    m_bmp_height(bitmap.px_size().y),
    m_has_border(!(style& wxNO_BORDER))
{
    Create(parent, id, label, wxDefaultPosition, wxDefaultSize, style);

    SetBitmap(bitmap.bmp());
}

void ScalableButton::SetBitmap_(const ScalableBitmap& bmp)
{
    SetBitmap(bmp.bmp());
    m_current_icon_name = bmp.name();
}

bool ScalableButton::SetBitmap_(const std::string& bmp_name)
{
    m_current_icon_name = bmp_name;
    if (m_current_icon_name.empty())
        return false;

    wxBitmapBundle bmp = *get_bmp_bundle(m_current_icon_name, m_bmp_width, m_bmp_height);
    SetBitmap(bmp);
    SetBitmapCurrent(bmp);
    SetBitmapPressed(bmp);
    SetBitmapFocus(bmp);
    SetBitmapDisabled(bmp);
    return true;
}

void ScalableButton::SetBitmapDisabled_(const ScalableBitmap& bmp)
{
    SetBitmapDisabled(bmp.bmp());
    m_disabled_icon_name = bmp.name();
}

int ScalableButton::GetBitmapHeight()
{
#ifdef __APPLE__
    return GetBitmap().GetScaledHeight();
#else
    return GetBitmap().GetHeight();
#endif
}

wxSize ScalableButton::GetBitmapSize()
{
#ifdef __APPLE__
    return wxSize(GetBitmap().GetScaledWidth(), GetBitmap().GetScaledHeight());
#else
    return wxSize(GetBitmap().GetWidth(), GetBitmap().GetHeight());
#endif
}

void ScalableButton::sys_color_changed()
{
    if (m_current_icon_name.empty())
        return;
    wxBitmapBundle bmp = *get_bmp_bundle(m_current_icon_name, m_bmp_width, m_bmp_height);
    SetBitmap(bmp);
    SetBitmapCurrent(bmp);
    SetBitmapPressed(bmp);
    SetBitmapFocus(bmp);
    if (!m_disabled_icon_name.empty())
        SetBitmapDisabled(*get_bmp_bundle(m_disabled_icon_name, m_bmp_width, m_bmp_height));
    if (!GetLabelText().IsEmpty())
        SetBitmapMargins(int(0.5 * w_config()->em_unit(m_parent)), 0);
}

}


