#include "Slic3r/Biz/WX/FontUtils.hpp"
#include "Slic3r/Log.hpp"
#include <wx/string.h>
#include <optional>
#include <vector>

#include "Slic3r/App/WX/I18N.hpp" // translation for font name style modifier

#ifdef _WIN32
#include <boost/crc.hpp>
#include <windows.h>
#include <wingdi.h>
#include <windef.h>
#include <WinUser.h>
#elif defined(__APPLE__)
#include "Slic3r/Utils.hpp" // ScopeGuard
#include <CoreText/CTFont.h>
#include <wx/uri.h>
#include <wx/fontutil.h> // wxNativeFontInfo
#include <wx/osx/core/cfdictionary.h>
#elif defined(__linux__)
#include "Slic3r/Biz/WX/FontConfigHelp.hpp"
#endif

using namespace Slic3r;

#ifdef __APPLE__
namespace {
bool is_valid_ttf(std::string_view file_path)
{
    if (file_path.empty())
        return false;
    auto const pos_point = file_path.find_last_of('.');
    if (pos_point == std::string_view::npos)
        return false;

    // use point only after last directory delimiter
    auto const pos_directory_delimiter = file_path.find_last_of("/\\");
    if (pos_directory_delimiter != std::string_view::npos && pos_point < pos_directory_delimiter)
        return false; // point is before directory delimiter

    // check count of extension chars
    size_t extension_size = file_path.size() - pos_point;
    if (extension_size >= 5)
        return false; // a lot of symbols for extension
    if (extension_size <= 1)
        return false; // few letters for extension

    std::string_view extension = file_path.substr(pos_point + 1, extension_size);

    // Because of MacOs - Courier, Geneva, Monaco
    if (extension == std::string_view("dfont"))
        return false;

    return true;
}

// get filepath from wxFont on Mac OsX
std::string get_file_path(const wxFont& font)
{
    const wxNativeFontInfo* info = font.GetNativeFontInfo();
    if (info == nullptr)
        return {};
    CTFontDescriptorRef descriptor = info->GetCTFontDescriptor();
    CFURLRef typeref = (CFURLRef) CTFontDescriptorCopyAttribute(descriptor, kCTFontURLAttribute);
    if (typeref == NULL)
        return {};
    ScopeGuard sg([&typeref]() { CFRelease(typeref); });
    CFStringRef url = CFURLGetString(typeref);
    if (url == NULL)
        return {};
    wxString file_uri(wxCFStringRef::AsString(url));
    wxURI uri(file_uri);
    const wxString& path    = uri.GetPath();
    wxString path_unescaped = wxURI::Unescape(path);
    std::string path_str    = path_unescaped.ToUTF8().data();
    SPDLOG_TRACE("input uri({}) convert to path({}) string({}).", file_uri.ToUTF8().data(), path.ToUTF8().data(), path_str);
    return path_str;
}
} // namespace
#endif // __APPLE__

namespace {
wxFont create_wx_font(const wxString& name, const wxFontEncoding encoding)
{
    return wxFont(wxFontInfo().FaceName(name).Encoding(encoding));
}

bool is_valid_font(
    const wxString& name,
    const std::vector<wxString>& bad,
    const wxFontEncoding encoding,
    wxFont& out_wx_font
)
{
    if (name.empty())
        return false;

    // vertical font start with @, we will filter it out
    // Not sure if it is only in Windows so filtering is on all platforms
    if (name[0] == '@')
        return false;

    // previously detected bad font
    auto it = std::lower_bound(bad.begin(), bad.end(), name);
    if (it != bad.end() && *it == name)
        return false;

    out_wx_font = create_wx_font(name, encoding);
    //*
    // Faster chech if wx_font is loadable but not 100%
    // names could contain not loadable font
    if (!Slic3r::Biz::WX::can_load(out_wx_font))
        return false;

    /*/
    // Slow copy of font files to try load font
    // After this all files are loadable
    auto font_file = Slic3r::Biz::WX::create_font_file(out_wx_font);
    if (font_file == nullptr)
        return false; // can't create font file
    // */
    return true;
}
} // namespace

namespace Slic3r::Biz::WX {

bool can_load(const wxFont& font)
{
    if (!font.IsOk())
        return false;
#ifdef _WIN32
    return Emboss::can_load(font.GetHFONT()) != nullptr;
#elif defined(__APPLE__)
    return true;
    // return is_valid_ttf(get_file_path(font));
#elif defined(__linux__)
    return true;
    // font config check file path take about 4000ms for chech them all
    // std::string font_path = get_font_path(font);
    // return !font_path.empty();
#endif
    return false;
}

std::vector<wxString> validate_fonts(
    wxArrayString& facenames,
    Domain::FontList& valid,
    std::vector<wxString>& bad,
    const wxFontEncoding encoding
)
{
    // NOTE: recreate list of unopenable fonts (bad)
    // 1. filter out nonlisted bad fonts
    // 2. append new founded
    // 3. keep previously founded bad fonts which are still in list
    std::vector<wxString> bad_;
    bad_.reserve(bad.size() + 1); // one more for new installed font
    std::vector<wxString> good;
    good.reserve(facenames.size());
    valid.reserve(facenames.size());

    std::sort(facenames.begin(), facenames.end());
    for (const wxString& name : facenames) {
        wxFont wx_font;
        if (!is_valid_font(name, bad, encoding, wx_font)) {
            bad_.push_back(name);
            continue;
        }
        good.push_back(name);
        valid.push_back(Biz::WX::create_descriptor(wx_font));
    }
    assert(std::is_sorted(bad_.begin(), bad_.end()));
    bad = bad_;
    return good;
}

std::unique_ptr<Domain::FontFile> create_font_file(const wxFont& font)
{
#ifdef _WIN32
    return Emboss::create_font_file(font.GetHFONT());
#elif defined(__APPLE__)
    std::string file_path = get_file_path(font);
    if (!is_valid_ttf(file_path)) {
        SPDLOG_ERROR("Can not process font('{}'), file in path('{}') is not valid TTF.", get_human_readable_name(font), file_path);
        return nullptr;
    }
    return Emboss::create_font_file(file_path.c_str());
#elif defined(__linux__)
    std::string font_path = get_font_path(font);
    if (font_path.empty()) {
        SPDLOG_ERROR("Can not read font('{}'), file path is empty.", get_human_readable_name(font));
        return nullptr;
    }
    return Emboss::create_font_file(font_path.c_str());
#else
    // HERE is place to add implementation for another platform
    // to convert wxFont to font data as windows or font file path as linux
    return nullptr;
#endif
}

Domain::FontDescriptor::Type get_current_type()
{
#ifdef _WIN32
    return Domain::FontDescriptor::Type::wx_win_font_descr;
#elif defined(__APPLE__)
    return Domain::FontDescriptor::Type::wx_mac_font_descr;
#elif defined(__linux__)
    return Domain::FontDescriptor::Type::wx_lin_font_descr;
#else
    return Domain::FontDescriptor::Type::undefined;
#endif
}

Domain::FontDescriptor create_descriptor(const wxFont& font)
{
    return create_descriptor(font, get_human_readable_name(font));
}

Domain::FontDescriptor create_descriptor(const wxFont& font, const std::string& name)
{
    return Domain::FontDescriptor{.name = name, .path = store_wxFont(font), .type = get_current_type()};
}

// NOT working on linux GTK2
// load font used by Operating system as default GUI
// EmbossStyle get_os_font()
//{
// wxSystemFont system_font = wxSYS_DEFAULT_GUI_FONT;
// wxFont       font        = wxSystemSettings::GetFont(system_font);
// EmbossStyle  es          = create_emboss_style(font);
// es.name += std::string(" (OS default)");
// return es;
//}

std::string get_human_readable_name(const wxFont& font)
{
    if (!font.IsOk())
        return "Font is NOT ok.";
    // Face name is optional in wxFont
    wxString name = (!font.GetFaceName().empty()) ? font.GetFaceName() :
                                                    (font.GetFamilyString()
                                                     + wxString::FromUTF8(" ")
                                                     + font.GetStyleString()
                                                     + wxString::FromUTF8(" ")
                                                     + font.GetWeightString());
    return std::string(name.ToUTF8().data());
}

std::string store_wxFont(const wxFont& font)
{
    // Serialization function store also param "lfHeight" LOGFONT height,
    // which is dependent on DPI, which is different for Device context -> monitor.
    // To unify descriptors is used same point size
    // Common values are decades occasionaly houdreds. For sure is set to higher value.
    const int POINT_SIZE = 10000;
    if (font.GetPointSize() != POINT_SIZE) {
        wxFont wx_font = font;// copy
        wx_font.SetPointSize(POINT_SIZE);
        return store_wxFont(wx_font);
    }

    // wxString os = wxPlatformInfo::Get().GetOperatingSystemIdName();
    wxString font_descriptor = font.GetNativeFontInfoDesc();
    std::stringstream ss;
    ss  << "'"
        << font_descriptor
        << "' wx string get from GetNativeFontInfoDesc. wxFont "
        << "IsOk("
        << font.IsOk()
        << "), "
        << "isNull("
        << font.IsNull()
        << ")"
        <<
        // "IsFree(" << font.IsFree() << "), " << // on MacOs is no function is free
        "IsFixedWidth("
        << font.IsFixedWidth()
        << "), "
        << "IsUsingSizeInPixels("
        << font.IsUsingSizeInPixels()
        << "), "
        << "Encoding("
        << (int) font.GetEncoding()
        << "), ";
    SPDLOG_TRACE("{}", ss.str());
    return std::string(font_descriptor.ToUTF8().data());
}

wxFont load_wxFont(const std::string& font_descriptor)
{
    SPDLOG_TRACE("'{}' font descriptor string param of load_wxFont()", font_descriptor);
    wxString font_descriptor_wx = wxString::FromUTF8(font_descriptor);
    SPDLOG_TRACE("'{}' wx string descriptor", font_descriptor_wx.ToUTF8().data());
    wxFont wx_font(font_descriptor_wx);
    SPDLOG_TRACE("loaded font is '{}'.", get_human_readable_name(wx_font));
    return wx_font;
}

wxFont create_wxFont(const Domain::EmbossStyle& style)
{
    const Domain::FontProp& fp = style.prop;
    double point_size          = static_cast<double>(fp.size_in_mm);
    wxFontInfo info(point_size);
    if (fp.family.has_value()) {
        auto it = type_to_family.right.find(*fp.family);
        if (it != type_to_family.right.end())
            info.Family(it->second);
    }
    // Face names are not portable, so prefer to use Family() in portable code.
    /* if (fp.face_name.has_value()) {
        wxString face_name(*fp.face_name);
        info.FaceName(face_name);
    }*/
    if (fp.style.has_value()) {
        auto it = type_to_style.right.find(*fp.style);
        if (it != type_to_style.right.end())
            info.Style(it->second);
    }
    if (fp.weight.has_value()) {
        auto it = type_to_weight.right.find(*fp.weight);
        if (it != type_to_weight.right.end())
            info.Weight(it->second);
    }

    // Improve: load descriptor instead of store to font property to 3mf
    // switch (es.type) {
    // case Domain::FontDescriptor::Type::wx_lin_font_descr:
    // case Domain::FontDescriptor::Type::wx_win_font_descr:
    // case Domain::FontDescriptor::Type::wx_mac_font_descr:
    // case Domain::FontDescriptor::Type::file_path:
    // case Domain::FontDescriptor::Type::undefined:
    // default:
    //}

    wxFont wx_font(info);
    // Check if exist font file
    std::unique_ptr<Domain::FontFile> ff = create_font_file(wx_font);
    if (ff == nullptr)
        return {};

    return wx_font;
}

// extend fonts by styles and weight (italic + bold)
namespace {
    std::string to_string(wxFontStyle style)
    {
        switch (style) {
        case wxFontStyle::wxFONTSTYLE_NORMAL:
            // TRN It will be visible after font name in font list
            return _u8L("normal");
        case wxFontStyle::wxFONTSTYLE_ITALIC:
            // TRN It will be visible after font name in font list
            return _u8L("italic");
        case wxFontStyle::wxFONTSTYLE_SLANT:
            // TRN It will be visible after font name in font list
            return _u8L("slant");
        default:
            return "unknown style";
        }
    }

    std::string to_string(wxFontWeight weight)
    {
        switch (weight) {
        case wxFontWeight::wxFONTWEIGHT_THIN:
            // TRN It will be visible after font name in font list
            return _u8L("thin");
        case wxFontWeight::wxFONTWEIGHT_EXTRALIGHT:
            // TRN It will be visible after font name in font list
            return _u8L("extralight");
        case wxFontWeight::wxFONTWEIGHT_LIGHT:
            // TRN It will be visible after font name in font list
            return _u8L("light");
        case wxFontWeight::wxFONTWEIGHT_NORMAL:
            // TRN It will be visible after font name in font list
            return _u8L("normal");
        case wxFontWeight::wxFONTWEIGHT_MEDIUM:
            // TRN It will be visible after font name in font list
            return _u8L("medium");
        case wxFontWeight::wxFONTWEIGHT_SEMIBOLD:
            // TRN It will be visible after font name in font list
            return _u8L("semibold");
        case wxFontWeight::wxFONTWEIGHT_BOLD:
            // TRN It will be visible after font name in font list
            return _u8L("bold");
        case wxFontWeight::wxFONTWEIGHT_EXTRABOLD:
            // TRN It will be visible after font name in font list
            return _u8L("extrabold");
        case wxFontWeight::wxFONTWEIGHT_HEAVY:
            // TRN It will be visible after font name in font list
            return _u8L("heavy");
        case wxFontWeight::wxFONTWEIGHT_EXTRAHEAVY:
            // TRN It will be visible after font name in font list
            return _u8L("extraheavy");
        default:
            return "unknown weight";
        }
    }

    void set_property(wxFont& wx_font, wxFontStyle style)
    {
        wx_font.SetStyle(style);
    }

    void set_property(wxFont& wx_font, wxFontWeight weight)
    {
        wx_font.SetWeight(weight);
    }

#ifdef _WIN32
    // Identifier must load font file at least partialy
    struct FontIdentifier
    {
        // font file size
        size_t size = std::numeric_limits<size_t>::max();

        // calculated crc for font file
        size_t crc = std::numeric_limits<size_t>::max();

        bool operator==(const FontIdentifier& o) const
        {
            return size == o.size && crc == o.crc;
        }

        bool operator<(const FontIdentifier& o) const
        {
            return size < o.size || (size == o.size && crc < o.crc);
        }
    };

    // global data
    HDC g_hdc = NULL;
    std::vector<unsigned char> g_buffer = std::vector<unsigned char>(500);

    void end_creation_font_ids()
    {
        ::DeleteDC(g_hdc);
        g_buffer.clear();
    }

    // Copied from Emboss.hpp
    bool load_hfont(void* hfont, DWORD& dwTable, DWORD& dwOffset, size_t& size, HDC hdc)
    {
        // To retrieve the data from the beginning of the file for TrueType
        // Collection files specify 'ttcf' (0x66637474).
        dwTable = 0x66637474;
        dwOffset = 0;

        ::SelectObject(hdc, hfont);
        size = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
        if (size == GDI_ERROR) {
            // HFONT is NOT TTC(collection)
            dwTable = 0;
            size = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
        }

        if (size == 0 || size == GDI_ERROR) {
            return false;
        }
        return true;
    }

    std::optional<FontIdentifier> create_font_identifier(const wxFont& font)
    {
        if (g_hdc == NULL) {
            g_hdc = ::CreateCompatibleDC(NULL);
            if (g_hdc == NULL) {
                assert(false);
                SPDLOG_ERROR("Can't create HDC by CreateCompatibleDC(NULL).");
                return {};
            }
        }

        DWORD dwTable = 0, dwOffset = 0;
        size_t size;
        if (!load_hfont(font.GetHFONT(), dwTable, dwOffset, size, g_hdc)) {
            return {};
        }
        size_t crc_size = std::min(g_buffer.size(), size);
        size_t loaded_size = ::GetFontData(g_hdc, dwTable, dwOffset, g_buffer.data(), crc_size);
        if (loaded_size != crc_size)
            return {};

        boost::crc_32_type result;
        result.process_bytes(g_buffer.data(), crc_size);
        size_t crc = result.checksum();
        return FontIdentifier{ .size = size, .crc = crc };
    }
#else
    using FontIdentifier = std::string;

    void end_creation_font_ids() {}
#endif

    std::optional<FontIdentifier> get_font_id(const wxFont& font)
    {
#ifdef _WIN32
        return create_font_identifier(font);
#elif defined(__APPLE__)
        return get_file_path(font);
#elif defined(__linux__)
        return Biz::WX::get_font_path(font);
#else
        return font.GetFamilyString().ToStdString();
#endif
    }

    struct FontDescEx
    {
        Domain::FontDescriptor descriptor;
        FontIdentifier id;
    };

    using FontDescExs = std::vector<FontDescEx>;

    template <typename Property>
    FontDescExs extend_by_properties(
        const wxFont& wx_font_start,
        const FontDescEx& font_ex,
        const std::vector<Property>& properties
    )
    {
        FontDescExs result;
        const Domain::FontDescriptor& orig_descr = font_ex.descriptor;
        for (Property property : properties) {
            wxFont wx_font{ wx_font_start };
            set_property(wx_font, property);

            auto font_id_opt = get_font_id(wx_font);
            if (!font_id_opt.has_value())
                continue;

            const FontIdentifier& font_id = *font_id_opt;
            if (font_ex.id == font_id)
                continue;

            auto it = std::find_if(result.begin(), result.end(), [&font_id](const FontDescEx& r) {
                return r.id == font_id;
                });
            if (it != result.end())
                continue; // already exist in current extension

            if (!Biz::WX::can_load(wx_font))
                continue;

            Domain::FontDescriptor descr = Biz::WX::create_descriptor(wx_font);
            if (descr.path == orig_descr.path)
                continue;

            descr.name = orig_descr.name + " " + to_string(property);
            result.push_back(FontDescEx{ .descriptor = descr, .id = font_id });
        }
        return result;
    }

} // namespace

void extend(Domain::FontList& fonts, std::vector<wxString>& valid, std::vector<wxString>& bad)
{
    // NOTE: order define priority of used property when create same font
    static std::vector<wxFontStyle> italic_styles = {
        wxFontStyle::wxFONTSTYLE_ITALIC,
        wxFontStyle::wxFONTSTYLE_SLANT,
        wxFontStyle::wxFONTSTYLE_NORMAL
    };
    static std::vector<wxFontWeight> bold_weight = {
        wxFontWeight::wxFONTWEIGHT_BOLD,
        wxFontWeight::wxFONTWEIGHT_THIN,
        wxFontWeight::wxFONTWEIGHT_LIGHT,
        wxFontWeight::wxFONTWEIGHT_MEDIUM,
        wxFontWeight::wxFONTWEIGHT_HEAVY,
        wxFontWeight::wxFONTWEIGHT_EXTRAHEAVY,
        wxFontWeight::wxFONTWEIGHT_EXTRALIGHT,
        wxFontWeight::wxFONTWEIGHT_SEMIBOLD,
        wxFontWeight::wxFONTWEIGHT_EXTRABOLD,
        wxFontWeight::wxFONTWEIGHT_NORMAL
    };

    struct FontDescEx2
    {
        FontDescEx descriptor_ex;
        std::optional<size_t> index;

        FontDescEx2(const FontDescEx& descriptor_ex) : descriptor_ex(descriptor_ex) {}

        FontDescEx2(const FontDescEx& descriptor_ex, size_t index) :
            descriptor_ex(descriptor_ex),
            index(index)
        {
        }

        operator FontDescEx& ()
        {
            return descriptor_ex;
        }
    };

    std::vector<FontDescEx2> result;
    for (const Domain::FontDescriptor& font : fonts) {
        wxFont wx_font = Biz::WX::load_wxFont(font.path);
        auto font_id_opt = get_font_id(wx_font);
        if (!font_id_opt.has_value())
            continue;
        FontDescEx font_ex{ .descriptor = font, .id = *font_id_opt };
        result.push_back(FontDescEx2(font_ex, &font - &fonts.front()));
        FontDescExs ext_style = extend_by_properties(wx_font, font_ex, italic_styles);
        result.insert(result.end(), ext_style.begin(), ext_style.end());
        ext_style.push_back(font_ex); // add original style
        for (const FontDescEx& font_ex : ext_style) {
            wxFont wx_font = Biz::WX::load_wxFont(font_ex.descriptor.path);
            FontDescExs ext_weight = extend_by_properties(wx_font, font_ex, bold_weight);
            result.insert(result.end(), ext_weight.begin(), ext_weight.end());
        }
    }
    end_creation_font_ids();
    // clean duplicit use of same file
    auto pred_id_sort = [](const FontDescEx& f1, const FontDescEx& f2) {
        return f1.id < f2.id;
        };
    std::sort(result.begin(), result.end(), pred_id_sort);
    auto pred_id_unique = [](const FontDescEx& f1, const FontDescEx& f2) {
        return f1.id == f2.id;
        };
    auto id_it = std::unique(result.begin(), result.end(), pred_id_unique);
    result.erase(id_it, result.end());

    // clean duplicit paths - for sure - can't be loaded both
    auto pred_path_sort = [](const FontDescEx& f1, const FontDescEx& f2) {
        return f1.descriptor.path < f2.descriptor.path;
        };
    std::sort(result.begin(), result.end(), pred_path_sort);
    auto pred_path_unique = [](const FontDescEx& f1, const FontDescEx& f2) {
        return f1.descriptor.path == f2.descriptor.path;
        };
    auto path_it = std::unique(result.begin(), result.end(), pred_path_unique);
    result.erase(path_it, result.end());

    // prepare sorted result
    auto pred_name_sort = [](const FontDescEx& f1, const FontDescEx& f2) {
        return f1.descriptor.name < f2.descriptor.name;
        };
    std::sort(result.begin(), result.end(), pred_name_sort);
    Domain::FontList fonts_new;
    fonts_new.reserve(result.size());
    std::vector<wxString> valid_new;
    valid_new.reserve(valid.size());
    std::vector<bool> valid_used(valid.size(), { false });
    for (const FontDescEx2& r : result) {
        fonts_new.push_back(r.descriptor_ex.descriptor);
        if (r.index.has_value()) {
            valid_new.push_back(valid[*r.index]);
            valid_used[*r.index] = true;
        }
        else {
            valid_new.push_back(wxString()); // unknown name for filtration
        }
    }

    // update bad list
    for (size_t i = 0; i < valid.size(); ++i)
        if (!valid_used[i])
            bad.push_back(valid[i]);

    std::sort(bad.begin(), bad.end());
    fonts = fonts_new;
    valid = valid_new;
}

void update_property(Domain::FontProp& font_prop, const wxFont& font)
{
    wxString wx_face_name = font.GetFaceName();
    std::string face_name((const char*) wx_face_name.ToUTF8());
    if (!face_name.empty())
        font_prop.face_name = face_name;

    wxFontFamily wx_family = font.GetFamily();
    if (wx_family != wxFONTFAMILY_DEFAULT) {
        auto it = type_to_family.left.find(wx_family);
        if (it != type_to_family.left.end())
            font_prop.family = it->second;
    }

    wxFontStyle wx_style = font.GetStyle();
    if (wx_style != wxFONTSTYLE_NORMAL) {
        auto it = type_to_style.left.find(wx_style);
        if (it != type_to_style.left.end())
            font_prop.style = it->second;
    }

    wxFontWeight wx_weight = font.GetWeight();
    if (wx_weight != wxFONTWEIGHT_NORMAL) {
        auto it = type_to_weight.left.find(wx_weight);
        if (it != type_to_weight.left.end())
            font_prop.weight = it->second;
    }
}

} // namespace Slic3r::Biz::WX
