#include "Slic3r/Biz/WX/FontManager.hpp"
#include "Slic3r/Biz/WX/FontUtils.hpp"
#include "Slic3r/Biz/Emboss/Emboss.hpp"
#include "Slic3r/Biz/CerealUtils.hpp"
#include "Slic3r/App/WX/I18N.hpp" // translation for name of favorit fonts
#include "Slic3r/Log.hpp"

#include <wx/fontenum.h>
#include <boost/functional/hash.hpp>
#include <boost/nowide/fstream.hpp>

// cache font list by cereal
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <cereal/archives/binary.hpp>

using namespace Slic3r;

template <class Archive>
void save(Archive& archive, wxString const& d)
{
    std::string s(d.ToUTF8().data());
    archive(s);
}

template <class Archive>
void load(Archive& archive, wxString& d)
{
    std::string s;
    archive(s);
    d = wxString::FromUTF8(s);
}

namespace {
// increase number when change struct FacenamesSerializer
constexpr std::uint32_t FACENAMES_VERSION = 1;

struct FacenamesSerializer
{
    // hash number for unsorted vector of installed font into system
    size_t hash = 0;

    // Descriptor of openable fonts
    // NOTE: same size as good
    Domain::FontList fonts;

    // assumption that is loadable
    std::vector<wxString> good;
    // Can't load for some reason
    std::vector<wxString> bad;
};

template <class Archive>
void serialize(Archive& ar, FacenamesSerializer& t, const std::uint32_t version)
{
    // When performing a load, the version associated with the class
    // is whatever it was when that data was originally serialized
    // When we save, we'll use the version that is defined in the macro
    if (version != FACENAMES_VERSION)
        return;
    ar(t.hash, t.fonts, t.good, t.bad);
}

bool store(
    const boost::filesystem::path& path,
    const Domain::FontList& fonts,
    const std::vector<wxString>& good,
    const std::vector<wxString>& bad,
    size_t hash
)
{
    assert(std::is_sorted(bad.begin(), bad.end()));
    assert(good.size() == fonts.size()); // when found unopenable font I need initial string
    boost::nowide::ofstream file(path, std::ios::binary);
    ::cereal::BinaryOutputArchive archive(file);
    FacenamesSerializer data{.hash = hash, .fonts = fonts, .good = good, .bad = bad};
    try {
        archive(data);
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Failed to write fontlist cache - {}{}", path.string(), ex.what());
        return false;
    }
    return true;
}

bool load(
    const boost::filesystem::path& path,
    Domain::FontList& openable,
    std::vector<wxString>& bad,
    std::vector<wxString>& valid,
    size_t& hash,
    const wxFontEncoding encoding
)
{
    if (!boost::filesystem::exists(path)) {
        SPDLOG_WARN("Fontlist cache - '{}' does not exists.", path.string());
        return false;
    }
    boost::nowide::ifstream file(path, std::ios::binary);
    cereal::BinaryInputArchive archive(file);

    FacenamesSerializer data;
    try {
        archive(data);
    } catch (const std::exception& ex) {
        SPDLOG_ERROR("Failed to load fontlist cache - '{}'. Exception: {}", path.string(), ex.what());
        return false;
    }

    if (!std::is_sorted(data.bad.begin(), data.bad.end()))
        return false;
    if (!std::is_sorted(data.good.begin(), data.good.end()))
        return false;

    hash     = data.hash;
    bad      = data.bad;
    valid    = data.good;
    openable = data.fonts;
    return true;
}
} // namespace

///////////
// global definition
/////////
CEREAL_CLASS_VERSION(::FacenamesSerializer, FACENAMES_VERSION); // register class version

static std::size_t hash_value(wxString const& s)
{
    boost::hash<std::string> hasher;
    return hasher(std::string(s.ToUTF8()));
}

namespace Slic3r::Biz::WX {

FontManager::FontManager(const std::string& data_dir) :
    m_cache_path(boost::filesystem::path(data_dir) / "fonts.cereal"),
    m_data_dir(data_dir)
{}

const Domain::FontList& FontManager::get_fonts()
{
    wxFontEnumerator::InvalidateCache();

    // Configuration of font encoding
    const wxFontEncoding encoding = wxFontEncoding::wxFONTENCODING_SYSTEM;
    wxArrayString facenames       = wxFontEnumerator::GetFacenames(encoding);

    // check if it is same as last time
    size_t hash = boost::hash_range(facenames.begin(), facenames.end());
    if (!m_hash.has_value()) {
        // Application first call need load data from permanent cache
        size_t cache_hash;
        if (load(m_cache_path, m_openable, m_bad, m_valid, cache_hash, encoding)) {
            m_hash = cache_hash;
        }
    }
    if (m_hash.has_value() && *m_hash == hash) {
        // no new installed font -> skip validation
        return m_openable;
    }
    SPDLOG_INFO("Changed fontlist detected(prev_hash={}, curr_hash={}).", m_hash.value_or(0), hash);

    // extend fonts
    m_hash  = hash;
    m_valid = validate_fonts(facenames, m_openable, m_bad, encoding);

    // extend openable list by style(italic, slant) and weight(bold, heavy, ...)
    extend(m_openable, m_valid, m_bad);
    store(m_cache_path, m_openable, m_valid, m_bad, hash);
    return m_openable;
}

std::unique_ptr<const Domain::FontFile> FontManager::open(const Domain::FontDescriptor& descriptor)
{
    if (descriptor.type == Domain::FontDescriptor::Type::file_path) {
        return Biz::Emboss::create_font_file(descriptor.path.c_str());
    }

    if (descriptor.type != get_current_type()) // TODO: try find similar font?
        return nullptr;
    auto result = create_font_file(load_wxFont(descriptor.path));
    if (m_hash.has_value() && result == nullptr) {
        // extend m_bad and store it
        auto openable_it = std::find_if(
            m_openable.begin(),
            m_openable.end(),
            [&path = descriptor.path](const Domain::FontDescriptor& d) {
                return d.path.compare(path) == 0;
            }
        );
        if (openable_it == m_openable.end())
            return nullptr;

        size_t index  = openable_it - m_openable.begin();
        auto valid_it = m_valid.begin() + index;
        if (valid_it->empty())
            return nullptr; // NOTE: extension of font by italic and bold add empty string
        m_bad.push_back(*valid_it);
        std::sort(m_bad.begin(), m_bad.end());
        m_openable.erase(openable_it);
        m_valid.erase(valid_it);
        store(m_cache_path, m_openable, m_valid, m_bad, *m_hash);
        return nullptr;
    }
    return result;
}

Domain::FontDescriptor::Type FontManager::get_current_type() const
{
    return WX::get_current_type();
}

namespace {
Domain::FontDescriptor create_descriptor(
    const wxFont& wx_font,
    const std::string& name,
    const Domain::FontList& fonts
)
{
    for (const Domain::FontDescriptor& font : fonts) {
        wxFont wx_font_ = load_wxFont(font.path);
        if (wx_font.GetFaceName() != wx_font_.GetFaceName() ||
            wx_font.GetStyle()    != wx_font_.GetStyle()    ||
            wx_font.GetWeight()   != wx_font_.GetWeight()   )
            continue;
        Domain::FontDescriptor result = font; // copy
        result.name                   = name;
        return result;
    }
    // wxFont is not found in fonts
    return {};
}
} // namespace

FontManager::Descriptor FontManager::get_current_descriptor(const Domain::FontDescriptor& descriptor)
{
    if (descriptor.type == Domain::FontDescriptor::Type::file_path)
        return descriptor;
    if (descriptor.type != get_current_type())
        return tl::unexpected{ ConversionError::AnotherOS };
    wxFont wx_font = load_wxFont(descriptor.path);
    if (!wx_font.IsOk())
        return tl::unexpected{ ConversionError::NotOkWxFont };

    if (m_openable.empty())
        get_fonts(); // fill m_openable
    return create_descriptor(wx_font, descriptor.name, m_openable);
}

Domain::FontList FontManager::create_favorit()
{
    wxFont wx_font_normal = *wxNORMAL_FONT;
    if (m_openable.empty())
        get_fonts(); // not const - fill m_openable

#ifdef __APPLE__
    // Set normal font to helvetica when possible    
    for (const Domain::FontDescriptor& font : m_openable) {
        if (font.name == "Helvetica") {
            wx_font_normal = load_wxFont(font.path);
            break;
        }
    }
#endif // __APPLE__

    // https://docs.wxwidgets.org/3.0/classwx_font.html
    // Predefined objects/pointers: wxNullFont, wxNORMAL_FONT, wxSMALL_FONT, wxITALIC_FONT, wxSWISS_FONT
    Domain::FontList favorits = {
        create_descriptor(wx_font_normal, _u8L("NORMAL"), m_openable), // wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT)
        create_descriptor(
            *wxSMALL_FONT,
            _u8L("SMALL"),
            m_openable
        ), // A font using the wxFONTFAMILY_SWISS family and 2 points smaller than wxNORMAL_FONT.
        create_descriptor(
            *wxITALIC_FONT,
            _u8L("ITALIC"),
            m_openable
        ), // A font using the wxFONTFAMILY_ROMAN family and wxFONTSTYLE_ITALIC style and of the same size of wxNORMAL_FONT.
        create_descriptor(
            *wxSWISS_FONT,
            _u8L("SWISS"),
            m_openable
        ), // A font identic to wxNORMAL_FONT except for the family used which is wxFONTFAMILY_SWISS.
        create_descriptor(
            wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD),
            _u8L("MODERN"),
            m_openable
        ),
    };

    auto is_invalid = [](const Domain::FontDescriptor& descriptor) {
        // Check that exsit valid TrueType Font for wx font
        return create_font_file(load_wxFont(descriptor.path)) == nullptr;
    };

    // Not all predefined font for wx must be valid TTF, but at least one style must be loadable
    favorits.erase(std::remove_if(favorits.begin(), favorits.end(), is_invalid), favorits.end());

    // exist some valid style?
    if (!favorits.empty())
        return favorits;

    // No valid style in defult list
    // at least one style must contain loadable font
    for (const Domain::FontDescriptor& font : m_openable) {
        if (!is_invalid(font)) {
            Domain::FontDescriptor descriptor = font; // copy
            descriptor.name                   = _u8L("First font");
            favorits.push_back(descriptor);
            break;
        }
    }
    if (favorits.empty()) {
        // On current OS is not installed any correct TTF font
        // use font packed with Slic3r
        favorits.push_back(
            Domain::FontDescriptor{
                .name = _u8L("Default font"),
                .path = m_data_dir + "/fonts/NotoSans-Regular.ttf",
                .type = Domain::FontDescriptor::Type::file_path
            }
        );
    }
    return favorits;
}

} // namespace Slic3r::Biz::WX
