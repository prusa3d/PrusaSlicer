#include "Slic3r/App/WX/BitmapGetters.hpp"
#include "Slic3r/App/WX/BitmapCache.hpp"
#include "Slic3r/App/WX/WidgetsConfig.hpp"
#include "Slic3r/App/WX/StringConversions.hpp"
#include "Slic3r/App/WX/I18N.hpp"

#include <stdexcept>
#include <boost/algorithm/string/replace.hpp>
#include <boost/format.hpp>

namespace Slic3r::App::WX {

#ifdef __WXGTK2__
static int scale() 
{
    return int(em_unit(nullptr) * 0.1f + 0.5f);
}
#endif // __WXGTK2__

wxBitmapBundle* get_bmp_bundle(const std::string& bmp_name_in, int width/* = 16*/, int height/* = -1*/, const std::string& new_color/* = std::string()*/)
{
#ifdef __WXGTK2__
    width *= scale();
    if (height > 0)
        height *= scale();
#endif // __WXGTK2__

    static BitmapCache cache;

    std::string bmp_name = bmp_name_in;
    boost::replace_last(bmp_name, ".png", "");

    if (height < 0)
        height = width;

    // Try loading an SVG first, then PNG if SVG is not found:
    wxBitmapBundle* bmp = cache.from_svg(bmp_name, width, height, w_config()->dark_mode(), new_color);
    if (bmp == nullptr) {
        bmp = cache.from_png(bmp_name, width, height);
        if (!bmp)
            // Neither SVG nor PNG has been found, raise error
            throw std::runtime_error("Could not load bitmap: " + bmp_name); //! Slic3r::RuntimeError("Could not load bitmap: " + bmp_name);
    }
    return bmp;
}

wxBitmapBundle* get_bmp_bundle_for_mac_menu(const std::string& bmp_name)
{
    int size = 16;
#ifdef __WXGTK2__
    size *= scale();
#endif // __WXGTK2__

    static BitmapCache cache;

    // Try loading an SVG first, then PNG if SVG is not found:
    wxBitmapBundle* bmp = cache.from_svg_for_mac_menu(bmp_name, size, size);
    if (!bmp) {
        // No SVG has been found, raise error
        throw std::runtime_error("Could not load bitmap: " + bmp_name);
    }
    return bmp;
}

wxBitmapBundle* get_empty_bmp_bundle(int width, int height)
{
    static BitmapCache cache;
#ifdef __WXGTK2__
    return cache.mkclear_bndl(width * scale(), height * scale());
#else
    return cache.mkclear_bndl(width, height);
#endif // __WXGTK2__
}

wxBitmapBundle* get_solid_bmp_bundle(int width, int height, const std::string& color )
{
    static BitmapCache cache;
#ifdef __WXGTK2__
    return cache.mksolid_bndl(width * scale(), height * scale(), color, 1, dark_mode());
#else
    return cache.mksolid_bndl(width, height, color, 1, w_config()->dark_mode());
#endif // __WXGTK2__
}


std::vector<wxBitmapBundle*> get_extruder_color_icons(bool thin_icon/* = false*/)
{
    // Create the bitmap with color bars.
    std::vector<wxBitmapBundle*> bmps;
    std::vector<std::string> colors;// = Slic3r::GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();

    if (colors.empty())
        return bmps;

    for (const std::string& color : colors)
        bmps.emplace_back(get_solid_bmp_bundle(thin_icon ? 16 : 32, 16, color));

    return bmps;
}


void apply_extruder_selector(Widgets::BitmapComboBox**   ctrl,
                             wxWindow*          parent,
                             bool               use_full_item_name  /* = false*/,
                             const std::string& first_item          /* = ""*/,
                             wxPoint            pos                 /* = wxDefaultPosition*/,
                             wxSize             size                /* = wxDefaultSize*/,
                             bool               use_thin_icon       /* = false*/)
{
    std::vector<wxBitmapBundle*> icons = get_extruder_color_icons(use_thin_icon);

    if (!*ctrl) {
        *ctrl = new Widgets::BitmapComboBox(parent, wxID_ANY, wxEmptyString, pos, size, 0, nullptr, wxCB_READONLY);
    }
    else
    {
        (*ctrl)->SetPosition(pos);
        (*ctrl)->SetMinSize(size);
        (*ctrl)->SetSize(size);
        (*ctrl)->Clear();
    }
    if (first_item.empty())
        (*ctrl)->Hide();    // to avoid unwanted rendering before layout (ExtruderSequenceDialog)

    if (icons.empty() && !first_item.empty()) {
        (*ctrl)->Append(_(first_item), wxNullBitmap);
        return;
    }

    int i = 0;
    wxString str = _L("Extruder");
    for (wxBitmapBundle* bmp : icons) {
        if (i == 0) {
            if (!first_item.empty())
                (*ctrl)->Append(_(first_item), *bmp);
            ++i;
        }

        (*ctrl)->Append(use_full_item_name
                        ? from_u8((boost::format("%1% %2%") % str % i).str())
                        : wxString::Format(from_u8("%d"), i), *bmp);
        ++i;
    }
    (*ctrl)->SetSelection(0);
}


}


