#ifndef slic3r_WxFontUtils_hpp_
#define slic3r_WxFontUtils_hpp_

#include <memory>
#include <string>
#include <string_view>
#include <boost/bimap.hpp>
#include <boost/assign/list_of.hpp>
#include <wx/font.h>
#include <wx/arrstr.h>

#include "Slic3r/Biz/Emboss/Emboss.hpp" // FontFile
#include "Slic3r/Domain/TextConfiguration.hpp"

// Helper to work with wx widget font object( wxFont )
namespace Slic3r::Biz::WX {

// check if exist file for wxFont
// return pointer on data or nullptr when can't load
bool can_load(const wxFont& font);
std::vector<wxString> validate_fonts(
    wxArrayString& facenames,
    Domain::FontList& valid,
    std::vector<wxString>& bad,
    const wxFontEncoding encoding
);

// os specific load of wxFont
std::unique_ptr<Domain::FontFile> create_font_file(const wxFont& font);

Domain::FontDescriptor::Type get_current_type();
Domain::FontDescriptor create_descriptor(const wxFont& font);
Domain::FontDescriptor create_descriptor(const wxFont& font, const std::string& name);

std::string get_human_readable_name(const wxFont& font);

// serialize / deserialize font
std::string store_wxFont(const wxFont& font);
wxFont load_wxFont(const std::string& font_descriptor);

// Try to create similar font, loaded from 3mf from different Computer
wxFont create_wxFont(const Domain::EmbossStyle& style);

/// <summary>
/// Extends the font list by style(bold/italic ...)
/// Also detect invalid fonts from list when create unique FontId
/// </summary>
/// <param name="fonts">Font list to be extended
/// NOTE: same size as @valid index into valid corespond with index into fonts.</param>
/// <param name="valid">Vector that contain valid font string to create wxFont(from enumeration).
/// NOTE: same size as @fonts index into fonts corespond with index into valid.</param>
/// <param name="bad">Vector that contain invalid font string to create wxFont(from enumeration).</param>
void extend(Domain::FontList& fonts, std::vector<wxString>& valid, std::vector<wxString>& bad);

// update font property by wxFont - without emboss depth and font size
void update_property(Domain::FontProp& font_prop, const wxFont& font);

// clang-format off
// convert wxFont types to string and vice versa
using TypeToFamily = boost::bimap<wxFontFamily, std::string_view>;
const TypeToFamily type_to_family =
    boost::assign::list_of<TypeToFamily::relation>
    (wxFONTFAMILY_DEFAULT, "default")
    (wxFONTFAMILY_DECORATIVE, "decorative")
    (wxFONTFAMILY_ROMAN, "roman")
    (wxFONTFAMILY_SCRIPT, "script")
    (wxFONTFAMILY_SWISS, "swiss")
    (wxFONTFAMILY_MODERN, "modern")
    (wxFONTFAMILY_TELETYPE, "teletype");

using TypeToStyle = boost::bimap<wxFontStyle, std::string_view>;
const TypeToStyle type_to_style =
    boost::assign::list_of<TypeToStyle::relation>
    (wxFONTSTYLE_ITALIC, "italic")
    (wxFONTSTYLE_SLANT, "slant")
    (wxFONTSTYLE_NORMAL, "normal");

using TypeToWeight = boost::bimap<wxFontWeight, std::string_view>;
const TypeToWeight type_to_weight =
    boost::assign::list_of<TypeToWeight::relation>
    (wxFONTWEIGHT_THIN, "thin")
    (wxFONTWEIGHT_EXTRALIGHT, "extraLight")
    (wxFONTWEIGHT_LIGHT, "light")
    (wxFONTWEIGHT_NORMAL, "normal")
    (wxFONTWEIGHT_MEDIUM, "medium")
    (wxFONTWEIGHT_SEMIBOLD, "semibold")
    (wxFONTWEIGHT_BOLD, "bold")
    (wxFONTWEIGHT_EXTRABOLD, "extraBold")
    (wxFONTWEIGHT_HEAVY, "heavy")
    (wxFONTWEIGHT_EXTRAHEAVY, "extraHeavy");
// clang-format on

} // namespace Slic3r::Biz::WX
#endif // slic3r_WxFontUtils_hpp_
