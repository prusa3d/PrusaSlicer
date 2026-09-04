#include "Slic3r/Biz/Emboss/Emboss.hpp"

#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <Slic3r/Log.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>

#include "Slic3r/Biz/Emboss/stbtt_FlattenCurves.hpp"
#include "Slic3r/Biz/Emboss/NSVGUtils.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp" // union_ex + for boldness(polygon extend(offset))
#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygonsWithId.hpp"
#include "Slic3r/Biz/Algorithms/HealPolygon.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp" // expoly scaling
#include "Slic3r/Biz/CGAL/Algorithms/Triangulation.hpp" // CGAL project
#include "admesh/stl.h" // indexed_triangle_set
#include "Slic3r/Domain/EmbossShape.hpp"
#include "Slic3r/Domain/TextConfiguration.hpp"
#include "Slic3r/Utils.hpp" // append(ExPolygons)

#include "fmt/format.h"
#include <Slic3r/Biz/I18N/I18N.hpp> // translations

#define STB_TRUETYPE_IMPLEMENTATION
#include <imgui/imstb_truetype.h>

using namespace Slic3r::Biz;

// Experimentaly suggested ration of font ascent by multiple fonts
// to get approx center of normal text line
const double ASCENT_CENTER = 1 / 3.; // 0.5 is above small letter

// every glyph's shape point is divided by SHAPE_SCALE - increase precission of fixed point value
// stored in fonts (to be able represents curve by sequence of lines)
static constexpr double SHAPE_SCALE        = 0.001; // SCALING_FACTOR promile is fine enough
static unsigned MAX_HEAL_ITERATION_OF_TEXT = 10;

using namespace Slic3r;
using namespace Emboss;
using fontinfo_opt = std::optional<stbtt_fontinfo>;

namespace {
bool is_valid(const Domain::FontFile& font, unsigned int index);
fontinfo_opt load_font_info(const unsigned char* data, unsigned int index = 0);
std::optional<Glyph> get_glyph(const stbtt_fontinfo& font_info, int unicode_letter, float flatness);

// take glyph from cache
const Glyph* get_glyph(
    int unicode,
    const Domain::FontFile& font,
    const Domain::FontProp& font_prop,
    Glyphs& cache,
    fontinfo_opt& font_info_opt
);

// scale and convert float to int coordinate
Domain::Point to_point(const Slic3r::Biz::Emboss::stbtt__point& point);

bool is_valid(const Domain::FontFile& font, unsigned int index)
{
    if (font.data == nullptr)
        return false;
    if (font.data->empty())
        return false;
    if (index >= font.infos.size())
        return false;
    return true;
}

fontinfo_opt load_font_info(const unsigned char* data, unsigned int index)
{
    int font_offset = stbtt_GetFontOffsetForIndex(data, index);
    if (font_offset < 0) {
        assert(false);
        // "Font index(" << index << ") doesn't exist.";
        return {};
    }
    stbtt_fontinfo font_info;
    if (stbtt_InitFont(&font_info, data, font_offset) == 0) {
        // Can't initialize font.
        assert(false);
        return {};
    }
    return font_info;
}

std::optional<Glyph> get_glyph(const stbtt_fontinfo& font_info, int unicode_letter, float flatness)
{
    int glyph_index = stbtt_FindGlyphIndex(&font_info, unicode_letter);
    if (glyph_index == 0) {
        // wchar_t wchar = static_cast<wchar_t>(unicode_letter);
        //<< "Character unicode letter ("
        //<< "decimal value = " << std::dec << unicode_letter << ", "
        //<< "hexadecimal value = U+" << std::hex << unicode_letter << std::dec << ", "
        //<< "wchar value = " << wchar
        //<< ") is NOT defined inside of the font. \n";
        return {};
    }

    Glyph glyph;
    stbtt_GetGlyphHMetrics(&font_info, glyph_index, &glyph.advance_width, &glyph.left_side_bearing);

    stbtt_vertex* vertices;
    int num_verts = stbtt_GetGlyphShape(&font_info, glyph_index, &vertices);
    if (num_verts <= 0)
        return glyph; // no shape
    ScopeGuard sg1([&vertices]() { free(vertices); });

    int* contour_lengths                      = NULL;
    int num_countour_int                      = 0;
    Slic3r::Biz::Emboss::stbtt__point* points = Slic3r::Biz::Emboss::stbtt_FlattenCurves(
        vertices,
        num_verts,
        flatness,
        &contour_lengths,
        &num_countour_int,
        font_info.userdata
    );
    if (!points)
        return glyph; // no valid flattening
    ScopeGuard sg2([&contour_lengths, &points]() {
        free(contour_lengths);
        free(points);
    });

    size_t num_contour = static_cast<size_t>(num_countour_int);
    Domain::Polygons glyph_polygons;
    glyph_polygons.reserve(num_contour);
    size_t pi = 0; // point index
    for (size_t ci = 0; ci < num_contour; ++ci) {
        int length = contour_lengths[ci];
        // check minimal length for triangle
        if (length < 4) {
            // weird font
            pi += length;
            continue;
        }
        // last point is first point
        --length;
        Domain::Points pts;
        pts.reserve(length);
        for (int i = 0; i < length; ++i)
            pts.emplace_back(to_point(points[pi++]));

        // last point is first point --> closed contour
        assert(pts.front() == to_point(points[pi]));
        ++pi;

        // change outer cw to ccw and inner ccw to cw order
        std::reverse(pts.begin(), pts.end());
        glyph_polygons.emplace_back(pts);
    }
    if (!glyph_polygons.empty()) {
        unsigned max_iteration = 10;
        // TrueTypeFonts use non zero winding number
        // https://docs.microsoft.com/en-us/typography/opentype/spec/ttch01
        // https://developer.apple.com/fonts/TrueType-Reference-Manual/RM01/Chap1.html
        bool is_non_zero = true;
        glyph.shape = Algorithms::HealPolygon::heal_polygons(glyph_polygons, is_non_zero, max_iteration);
    }
    return glyph;
}

const Glyph* get_glyph(
    int unicode,
    const Domain::FontFile& font,
    const Domain::FontProp& font_prop,
    Glyphs& cache,
    fontinfo_opt& font_info_opt
)
{
    // TODO: Use resolution by printer configuration, or add it into Domain::FontProp
    const float RESOLUTION = 0.0125f; // [in mm]
    auto glyph_item        = cache.find(unicode);
    if (glyph_item != cache.end())
        return &glyph_item->second;

    unsigned int font_index = font_prop.collection_number.value_or(0);
    if (!is_valid(font, font_index))
        return nullptr;

    if (!font_info_opt.has_value()) {
        font_info_opt = load_font_info(font.data->data(), font_index);
        // can load font info?
        if (!font_info_opt.has_value())
            return nullptr;
    }

    float flatness = font.infos[font_index].unit_per_em / font_prop.size_in_mm * RESOLUTION;

    // Fix for very small flatness because it create huge amount of points from curve
    if (flatness < RESOLUTION)
        flatness = RESOLUTION;

    std::optional<Glyph> glyph_opt = get_glyph(*font_info_opt, unicode, flatness);

    // IMPROVE: multiple loadig glyph without data
    // has definition inside of font?
    if (!glyph_opt.has_value())
        return nullptr;

    Glyph& glyph = *glyph_opt;
    if (font_prop.char_gap.has_value())
        glyph.advance_width += *font_prop.char_gap;

    // scale glyph size
    glyph.advance_width     = static_cast<int>(glyph.advance_width / SHAPE_SCALE);
    glyph.left_side_bearing = static_cast<int>(glyph.left_side_bearing / SHAPE_SCALE);

    if (!glyph.shape.empty()) {
        if (font_prop.boldness.has_value()) {
            float delta = static_cast<float>(*font_prop.boldness / SHAPE_SCALE / font_prop.size_in_mm);
            glyph.shape = Algorithms::ClipperUtils::union_ex(
                Algorithms::ClipperUtils::offset_ex(glyph.shape, delta)
            );
        }
        if (font_prop.skew.has_value()) {
            double ratio = *font_prop.skew;
            auto skew    = [&ratio](Domain::Polygon& polygon) {
                for (Domain::Point& p : polygon.points)
                    p.x() += static_cast<Domain::coord_t>(std::round(p.y() * ratio));
            };
            for (Domain::ExPolygon& expolygon : glyph.shape) {
                skew(expolygon.contour);
                for (Domain::Polygon& hole : expolygon.holes)
                    skew(hole);
            }
        }
    }
    auto [it, success] = cache.try_emplace(unicode, std::move(glyph));
    assert(success);
    return &it->second;
}

Domain::Point to_point(const Slic3r::Biz::Emboss::stbtt__point& point)
{
    return Domain::Point(
        static_cast<int>(std::round(point.x / SHAPE_SCALE)),
        static_cast<int>(std::round(point.y / SHAPE_SCALE))
    );
}

} // namespace

#ifdef _WIN32
#include <windows.h>
#include <wingdi.h>
#include <windef.h>
#include <WinUser.h>

namespace {
Domain::EmbossStyle create_style(const std::wstring& name, const std::wstring& path)
{
    return Domain::EmbossStyle{
        .descriptor =
            Domain::FontDescriptor{
                .name = boost::nowide::narrow(name.c_str()),
                .path = boost::nowide::narrow(path.c_str()),
                .type = Domain::FontDescriptor::Type::file_path
            },
        .prop = Domain::FontProp()
    };
}
} // namespace

namespace Slic3r::Biz::Emboss {
// Get system font file path
std::optional<std::wstring> get_font_path(const std::wstring& font_face_name)
{
    // static const LPWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    static const LPCWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    HKEY hKey;
    LONG result;

    // Open Windows font registry key
    result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, fontRegistryPath, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
        return {};

    DWORD maxValueNameSize, maxValueDataSize;
    result = RegQueryInfoKey(hKey, 0, 0, 0, 0, 0, 0, 0, &maxValueNameSize, &maxValueDataSize, 0, 0);
    if (result != ERROR_SUCCESS)
        return {};

    DWORD valueIndex = 0;
    LPWSTR valueName = new WCHAR[maxValueNameSize];
    LPBYTE valueData = new BYTE[maxValueDataSize];
    DWORD valueNameSize, valueDataSize, valueType;
    std::wstring wsFontFile;

    // Look for a matching font name
    do {
        wsFontFile.clear();
        valueDataSize = maxValueDataSize;
        valueNameSize = maxValueNameSize;

        result = RegEnumValue(
            hKey,
            valueIndex,
            valueName,
            &valueNameSize,
            0,
            &valueType,
            valueData,
            &valueDataSize
        );

        valueIndex++;
        if (result != ERROR_SUCCESS || valueType != REG_SZ) {
            continue;
        }

        std::wstring wsValueName(valueName, valueNameSize);

        // Found a match
        if (_wcsnicmp(font_face_name.c_str(), wsValueName.c_str(), font_face_name.length()) == 0) {
            wsFontFile.assign((LPWSTR) valueData, valueDataSize);
            break;
        }
    } while (result != ERROR_NO_MORE_ITEMS);

    delete[] valueName;
    delete[] valueData;

    RegCloseKey(hKey);

    if (wsFontFile.empty())
        return {};

    // Build full font file path
    WCHAR winDir[MAX_PATH];
    GetWindowsDirectory(winDir, MAX_PATH);

    std::wstringstream ss;
    ss << winDir << "\\Fonts\\" << wsFontFile;
    wsFontFile = ss.str();

    return wsFontFile;
}

Domain::EmbossStyles get_font_list()
{
    // Domain::EmbossStyles list1 = get_font_list_by_enumeration();
    // Domain::EmbossStyles list2 = get_font_list_by_register();
    // Domain::EmbossStyles list3 = get_font_list_by_folder();
    return get_font_list_by_register();
}

Domain::EmbossStyles get_font_list_by_register()
{
    // static const LPWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    static const LPCWSTR fontRegistryPath = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    HKEY hKey;
    LONG result;

    // Open Windows font registry key
    result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, fontRegistryPath, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        assert(false);
        // std::wcerr << L"Can not Open register key (" << fontRegistryPath << ")"
        // << L", function 'RegOpenKeyEx' return code: " << result <<  std::endl;
        return {};
    }

    DWORD maxValueNameSize, maxValueDataSize;
    result = RegQueryInfoKey(hKey, 0, 0, 0, 0, 0, 0, 0, &maxValueNameSize, &maxValueDataSize, 0, 0);
    if (result != ERROR_SUCCESS) {
        assert(false);
        // Can not earn query key, function 'RegQueryInfoKey' return code: result
        return {};
    }

    // Build full font file path
    WCHAR winDir[MAX_PATH];
    GetWindowsDirectory(winDir, MAX_PATH);
    std::wstring font_path = std::wstring(winDir) + L"\\Fonts\\";

    Domain::EmbossStyles font_list;
    DWORD valueIndex = 0;
    // Look for a matching font name
    LPWSTR font_name    = new WCHAR[maxValueNameSize];
    LPBYTE fileTTF_name = new BYTE[maxValueDataSize];
    DWORD font_name_size, fileTTF_name_size, valueType;
    do {
        fileTTF_name_size = maxValueDataSize;
        font_name_size    = maxValueNameSize;

        result = RegEnumValue(
            hKey,
            valueIndex,
            font_name,
            &font_name_size,
            0,
            &valueType,
            fileTTF_name,
            &fileTTF_name_size
        );
        valueIndex++;
        if (result != ERROR_SUCCESS || valueType != REG_SZ)
            continue;
        std::wstring font_name_w(font_name, font_name_size);
        std::wstring file_name_w((LPWSTR) fileTTF_name, fileTTF_name_size);
        std::wstring path_w = font_path + file_name_w;

        // filtrate .fon from lists
        size_t pos = font_name_w.rfind(L" (TrueType)");
        if (pos >= font_name_w.size())
            continue;
        // remove TrueType text from name
        font_name_w = std::wstring(font_name_w, 0, pos);
        font_list.emplace_back(create_style(font_name_w, path_w));
    } while (result != ERROR_NO_MORE_ITEMS);
    delete[] font_name;
    delete[] fileTTF_name;

    RegCloseKey(hKey);
    return font_list;
}
} // namespace Slic3r::Biz::Emboss

namespace {
bool CALLBACK EnumFamCallBack(LPLOGFONT lplf, LPNEWTEXTMETRIC lpntm, DWORD FontType, LPVOID aFontList)
{
    std::vector<std::wstring>* fontList = (std::vector<std::wstring>*) (aFontList);
    if (FontType & TRUETYPE_FONTTYPE) {
        std::wstring name = lplf->lfFaceName;
        fontList->push_back(name);
    }
    return true;
    // UNREFERENCED_PARAMETER(lplf);
    UNREFERENCED_PARAMETER(lpntm);
}
} // namespace

namespace Slic3r::Biz::Emboss {
Domain::EmbossStyles get_font_list_by_enumeration()
{
    HDC hDC = GetDC(NULL);
    std::vector<std::wstring> font_names;
    EnumFontFamilies(hDC, (LPCTSTR) NULL, (FONTENUMPROC) EnumFamCallBack, (LPARAM) &font_names);

    Domain::EmbossStyles font_list;
    for (const std::wstring& font_name : font_names) {
        font_list.emplace_back(create_style(font_name, L""));
    }
    return font_list;
}

Domain::EmbossStyles get_font_list_by_folder()
{
    Domain::EmbossStyles result;
    WCHAR winDir[MAX_PATH];
    UINT winDir_size        = GetWindowsDirectory(winDir, MAX_PATH);
    std::wstring search_dir = std::wstring(winDir, winDir_size) + L"\\Fonts\\";
    WIN32_FIND_DATA fd;
    HANDLE hFind;
    // By https://en.wikipedia.org/wiki/TrueType has also suffix .tte
    std::vector<std::wstring> suffixes = {L"*.ttf", L"*.ttc", L"*.tte"};
    for (const std::wstring& suffix : suffixes) {
        hFind = ::FindFirstFile((search_dir + suffix).c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE)
            continue;
        do {
            // skip folder . and ..
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::wstring file_name(fd.cFileName);
            // TODO: find font name instead of filename
            result.emplace_back(create_style(file_name, search_dir + file_name));
        } while (::FindNextFile(hFind, &fd));
        ::FindClose(hFind);
    }
    return result;
}
} // namespace Slic3r::Biz::Emboss
#else
namespace Slic3r::Biz::Emboss {
Domain::EmbossStyles get_font_list()
{
    // not implemented
    return {};
}

std::optional<std::wstring> get_font_path(const std::wstring& font_face_name)
{
    // not implemented
    return {};
}
} // namespace Slic3r::Biz::Emboss
#endif

namespace Slic3r::Biz::Emboss {
std::unique_ptr<Domain::FontFile> create_font_file_from_data(
    std::unique_ptr<std::vector<unsigned char>> data
)
{
    int collection_size = stbtt_GetNumberOfFonts(data->data());
    // at least one font must be inside collection
    if (collection_size < 1) {
        assert(false);
        // There is no font collection inside font data
        return nullptr;
    }

    unsigned int c_size = static_cast<unsigned int>(collection_size);
    std::vector<Domain::FontFile::Info> infos;
    infos.reserve(c_size);
    for (unsigned int i = 0; i < c_size; ++i) {
        auto font_info = load_font_info(data->data(), i);
        if (!font_info.has_value())
            return nullptr;

        const stbtt_fontinfo* info = &(*font_info);
        // load information about line gap
        int ascent, descent, linegap;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &linegap);

        float pixels    = 1000.; // value is irelevant
        float em_pixels = stbtt_ScaleForMappingEmToPixels(info, pixels);
        int unit_per_em = static_cast<int>(std::round(pixels / em_pixels));
        infos.emplace_back(Domain::FontFile::Info{ascent, descent, linegap, unit_per_em});
    }
    return std::make_unique<Domain::FontFile>(std::move(data), std::move(infos));
}

std::unique_ptr<Domain::FontFile> create_font_file(const char* file_path)
{
    FILE* file = boost::nowide::fopen(file_path, "rb");
    if (file == nullptr) {
        assert(false);
        SPDLOG_ERROR("Couldn't open {} for reading.", file_path);
        return nullptr;
    }
    ScopeGuard sg([&file]() { std::fclose(file); });

    // find size of file
    if (fseek(file, 0L, SEEK_END) != 0) {
        assert(false);
        SPDLOG_ERROR("Couldn't fseek file {} for size measure.", file_path);
        return nullptr;
    }
    size_t size = ftell(file);
    if (size == 0) {
        assert(false);
        SPDLOG_ERROR("Size of font file is zero. Can't read.");
        return nullptr;
    }
    rewind(file);
    auto buffer               = std::make_unique<std::vector<unsigned char>>(size);
    size_t count_loaded_bytes = fread((void*) &buffer->front(), 1, size, file);
    if (count_loaded_bytes != size) {
        assert(false);
        SPDLOG_ERROR("Different loaded(from file) data size.");
        return nullptr;
    }
    return create_font_file_from_data(std::move(buffer));
}
} // namespace Slic3r::Biz::Emboss

#ifdef _WIN32
namespace {
bool load_hfont(void* hfont, DWORD& dwTable, DWORD& dwOffset, size_t& size, HDC hdc = nullptr)
{
    bool del_hdc = false;
    if (hdc == nullptr) {
        del_hdc = true;
        hdc     = ::CreateCompatibleDC(NULL);
        if (hdc == NULL)
            return false;
    }

    // To retrieve the data from the beginning of the file for TrueType
    // Collection files specify 'ttcf' (0x66637474).
    dwTable  = 0x66637474;
    dwOffset = 0;

    ::SelectObject(hdc, hfont);
    size = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
    if (size == GDI_ERROR) {
        // HFONT is NOT TTC(collection)
        dwTable = 0;
        size    = ::GetFontData(hdc, dwTable, dwOffset, NULL, 0);
    }

    if (size == 0 || size == GDI_ERROR) {
        if (del_hdc)
            ::DeleteDC(hdc);
        return false;
    }
    return true;
}
} // namespace

namespace Slic3r::Biz::Emboss {
void* can_load(void* hfont)
{
    DWORD dwTable = 0, dwOffset = 0;
    size_t size = 0;
    if (!load_hfont(hfont, dwTable, dwOffset, size))
        return nullptr;
    return hfont;
}

std::unique_ptr<Domain::FontFile> create_font_file(void* hfont)
{
    HDC hdc = ::CreateCompatibleDC(NULL);
    if (hdc == NULL) {
        assert(false);
        SPDLOG_ERROR("Can't create HDC by CreateCompatibleDC(NULL).");
        return nullptr;
    }

    DWORD dwTable = 0, dwOffset = 0;
    size_t size;
    if (!load_hfont(hfont, dwTable, dwOffset, size, hdc)) {
        ::DeleteDC(hdc);
        return nullptr;
    }
    auto buffer        = std::make_unique<std::vector<unsigned char>>(size);
    size_t loaded_size = ::GetFontData(hdc, dwTable, dwOffset, buffer->data(), size);
    ::DeleteDC(hdc);
    if (size != loaded_size) {
        assert(false);
        SPDLOG_ERROR("Different loaded(from HFONT) data size.");
        return nullptr;
    }
    return create_font_file_from_data(std::move(buffer));
}
} // namespace Slic3r::Biz::Emboss
#endif // _WIN32

namespace Slic3r::Biz::Emboss {
std::optional<Glyph> letter2glyph(
    const Domain::FontFile& font,
    unsigned int font_index,
    int letter,
    float flatness
)
{
    if (!is_valid(font, font_index))
        return {};
    auto font_info_opt = load_font_info(font.data->data(), font_index);
    if (!font_info_opt.has_value())
        return {};
    return get_glyph(*font_info_opt, letter, flatness);
}

const Domain::FontFile::Info& get_font_info(const Domain::FontFile& font, const Domain::FontProp& prop)
{
    unsigned int font_index = prop.collection_number.value_or(0);
    assert(is_valid(font, font_index));
    return font.infos[font_index];
}

int get_line_height(const Domain::FontFile& font, const Domain::FontProp& prop)
{
    const Domain::FontFile::Info& info = get_font_info(font, prop);
    int line_height                    = info.ascent - info.descent + info.linegap;
    line_height += prop.line_gap.value_or(0);
    return static_cast<int>(line_height / SHAPE_SCALE);
}
} // namespace Slic3r::Biz::Emboss

namespace {
Domain::ExPolygons letter2shapes(
    wchar_t letter,
    Domain::Point& cursor,
    const Domain::FontFile& font,
    Glyphs& cache,
    const Domain::FontProp& font_prop,
    fontinfo_opt& font_info_cache
)
{
    if (letter == '\n') {
        cursor.x() = 0;
        // 2d shape has opposit direction of y
        cursor.y() -= get_line_height(font, font_prop);
        return {};
    }
    if (letter == '\t') {
        // '\t' = 4*space => same as imgui
        const int count_spaces = 4;
        const Glyph* space     = get_glyph(int(' '), font, font_prop, cache, font_info_cache);
        if (space == nullptr)
            return {};
        cursor.x() += count_spaces * space->advance_width;
        return {};
    }
    if (letter == '\r')
        return {};

    int unicode = static_cast<int>(letter);
    auto it     = cache.find(unicode);

    // Create glyph from font file and cache it
    const Glyph* glyph_ptr = (it != cache.end()) ?
        &it->second :
        get_glyph(unicode, font, font_prop, cache, font_info_cache);
    if (glyph_ptr == nullptr)
        return {};

    // move glyph to cursor position
    Domain::ExPolygons expolygons = glyph_ptr->shape; // copy
    for (Domain::ExPolygon& expolygon : expolygons)
        expolygon.translate(cursor);

    cursor.x() += glyph_ptr->advance_width;
    return expolygons;
}

// Check cancel every X letters in text
// Lower number - too much checks(slows down)
// Higher number - slows down response on cancelation
const int CANCEL_CHECK = 10;

Domain::HealedExPolygons union_with_delta(
    const Domain::ExPolygonsWithIds& shapes,
    float delta,
    unsigned max_heal_iteration
)
{
    // unify to one expolygons
    Domain::ExPolygons expolygons;
    for (const Domain::ExPolygonsWithId& shape : shapes) {
        if (shape.expoly.empty())
            continue;
        append(expolygons, Algorithms::ClipperUtils::offset_ex(shape.expoly, delta));
    }
    Domain::ExPolygons result = Algorithms::ClipperUtils::union_ex(expolygons);
    result                    = Algorithms::ClipperUtils::offset_ex(result, -delta);
    bool is_healed = Algorithms::HealPolygon::heal_expolygons(result, max_heal_iteration);
    return {result, is_healed};
}
} // namespace

namespace Slic3r::Biz::Emboss {
Domain::ExPolygons union_with_delta(Domain::EmbossShape& shape, float delta, unsigned max_heal_iteration)
{
    if (!shape.final_shape.expolygons.empty())
        return shape.final_shape.expolygons;

    shape.final_shape = ::union_with_delta(shape.shapes_with_ids, delta, max_heal_iteration);
    for (const Domain::ExPolygonsWithId& e : shape.shapes_with_ids)
        if (!e.is_healed)
            shape.final_shape.is_healed = false;
    return shape.final_shape.expolygons;
}

namespace {
constexpr double get_tesselation_tolerance(
    const std::optional<double>& scale_x,
    const std::optional<double>& scale_y) {
    double scale = std::max(scale_x.value_or(1.), scale_y.value_or(1.));
    constexpr double tesselation_tolerance_in_mm = .1; //8e-2;
    using Algorithms::Scaling::SCALING_FACTOR;
    constexpr double tesselation_tolerance_scaled =
        (tesselation_tolerance_in_mm * tesselation_tolerance_in_mm)
        / SCALING_FACTOR / SCALING_FACTOR;
    return tesselation_tolerance_scaled / scale / scale;
}
}

ReadShapeResult read_shape_from_file(Domain::EmbossShape& shape,
    const std::optional<double>& volume_scale_x,
    const std::optional<double>& volume_scale_y) {
    ASSERT(shape.svg_file.has_value());
    Domain::EmbossShape::SvgFile& svg_file = *shape.svg_file;
    if (svg_file.file_data == nullptr) {
        svg_file.file_data = read_from_disk(svg_file.path);
        if (svg_file.file_data == nullptr)
            return ReadShapeResult::file_inaccessible;
        svg_file.image = nullptr;
    }
    if (svg_file.image == nullptr) {
        ASSERT(svg_file.file_data != nullptr);
        // init svg image
        svg_file.image = nsvgParse(*svg_file.file_data);
        if (svg_file.image.get() == NULL)
            return ReadShapeResult::nsvg_issue;
        ASSERT(svg_file.image != nullptr);
        shape.shapes_with_ids = {}; // clear shapes with ids        
    }
    ASSERT(shape.shapes_with_ids.empty());
    NSVGLineParams params{ get_tesselation_tolerance(volume_scale_x, volume_scale_y) };
    shape.shapes_with_ids = create_shape_with_ids(*svg_file.image, params);
    if (shape.shapes_with_ids.empty())
        return ReadShapeResult::no_shape;

    shape.final_shape = {}; // clear final shape
    union_with_delta(shape, UNION_DELTA, UNION_MAX_ITERATIN);
    if (!shape.final_shape.is_healed)
        // cant remove selfintersection and double points -> imposssible triangulation
        return ReadShapeResult::cant_heal;
    if (shape.final_shape.expolygons.empty())
        // appear when shapes with id are soo small that during healing it disapear.
        return ReadShapeResult::no_shape; 

    return ReadShapeResult::success;
}
std::string to_string(ReadShapeResult issue, const std::string& svg_file_path) {
    auto add_file = [&svg_file_path](const std::string& message) {
        return fmt::format(fmt::runtime(message), svg_file_path);};
    switch (issue) {
    case ReadShapeResult::file_inaccessible: 
        return add_file(_u8L("SVG file(\"{}\") is not accessible. Check application rights."));
    case ReadShapeResult::nsvg_issue:
        return add_file(_u8L("SVG file(\"{}\") can't be processed by NanoSVG."));
    case ReadShapeResult::no_shape:
        return add_file(_u8L("SVG file(\"{}\") do not contain embossabled path."));
    case ReadShapeResult::cant_heal:
        return add_file(_u8L("SVG file(\"{}\") contain path with unrepairabled shape for emboss."));
    case ReadShapeResult::success:
        return "sucessfull load of the svg file";
    default: 
        return "unknown issue";
    }    
}


Domain::HealedExPolygons text2shapes(
    FontFileWithCache& font_with_cache,
    const char* text,
    const Domain::FontProp& font_prop,
    const std::function<bool()>& was_canceled
)
{
    std::wstring text_w = boost::nowide::widen(text);
    Domain::ExPolygonsWithIds vshapes = text2vshapes(font_with_cache, text_w, font_prop, was_canceled);

    float delta = static_cast<float>(1. / SHAPE_SCALE);
    return ::union_with_delta(vshapes, delta, MAX_HEAL_ITERATION_OF_TEXT);
}

bool has_reflection(const Domain::Transform3d& transform){ 
    return transform.linear().determinant() < 0; 
}

// for creation volume
Domain::ModelVolumePtrs prepare_volumes_to_slice(const Domain::ModelObject& mo)
{
    const Domain::ModelVolumePtrs& volumes = mo.volumes;
    Domain::ModelVolumePtrs result;
    result.reserve(volumes.size());
    for (Domain::ModelVolume* volume : volumes) {
        // only part could be surface for volumes
        if (!volume->is_model_part())
            continue;

        result.push_back(volume);
    }
    return result;
}

Domain::ModelVolumePtrs prepare_volumes_to_slice(const Domain::ModelVolume& mv)
{
    const Domain::ModelVolumePtrs& volumes = mv.get_object()->volumes;
    Domain::ModelVolumePtrs result;
    result.reserve(volumes.size());
    for (Domain::ModelVolume* volume : volumes) {
        // only part could be surface for volumes
        if (!volume->is_model_part())
            continue;

        // is selected volume
        if (mv.id() == volume->id())
            continue;

        result.push_back(volume);
    }
    return result;
}

} // namespace Slic3r::Biz::Emboss

namespace {
/// <summary>
/// Align shape against pivot
/// </summary>
/// <param name="shapes">Shapes to align
/// Prerequisities: shapes are aligned left top</param>
/// <param name="text">To detect end of lines - to be able horizontal center the line</param>
/// <param name="prop">Containe Horizontal and vertical alignment</param>
/// <param name="font">Needed for scale and font size</param>
void align_shape(
    Domain::ExPolygonsWithIds& shapes,
    const std::wstring& text,
    const Domain::FontProp& prop,
    const Domain::FontFile& font
);
} // namespace

namespace Slic3r::Biz::Emboss {
Domain::ExPolygonsWithIds text2vshapes(
    FontFileWithCache& font_with_cache,
    const std::wstring& text,
    const Domain::FontProp& font_prop,
    const std::function<bool()>& was_canceled
)
{
    assert(font_with_cache.has_value());
    if (!font_with_cache.has_value())
        return {};

    const Domain::FontFile& font = *font_with_cache.font_file;
    unsigned int font_index      = font_prop.collection_number.value_or(0);
    if (!is_valid(font, font_index))
        return {};

    std::shared_ptr<Glyphs> cache = font_with_cache.cache; // copy pointer
    unsigned counter              = CANCEL_CHECK - 1; // it is needed to validate using of cache
    Domain::Point cursor(0, 0);

    fontinfo_opt font_info_cache;
    Domain::ExPolygonsWithIds result;
    result.reserve(text.size());
    for (wchar_t letter : text) {
        if (++counter == CANCEL_CHECK) {
            counter = 0;
            if (was_canceled())
                return {};
        }
        unsigned id = static_cast<unsigned>(letter);
        result.push_back({id, letter2shapes(letter, cursor, font, *cache, font_prop, font_info_cache)});
    }

    align_shape(result, text, font_prop, font);
    return result;
}

unsigned get_count_lines(const std::wstring& ws)
{
    unsigned count = 1;
    for (wchar_t wc : ws)
        if (wc == '\n')
            ++count;
    return count;
}

unsigned get_count_lines(const std::string& text)
{
    std::wstring ws = boost::nowide::widen(text.c_str());
    return get_count_lines(ws);
}

unsigned get_count_lines(const Domain::ExPolygonsWithIds& shapes)
{
    if (shapes.empty())
        return 0; // no glyphs
    unsigned result = 1; // one line is minimum
    for (const Domain::ExPolygonsWithId& shape_id : shapes)
        if (shape_id.id == ENTER_UNICODE)
            ++result;
    return result;
}

void apply_transformation(
    const std::optional<float>& angle,
    const std::optional<float>& distance,
    Domain::Transform3d& transformation
)
{
    if (angle.has_value()) {
        double angle_z = *angle;
        transformation *= Eigen::AngleAxisd(angle_z, Domain::Vec3d::UnitZ());
    }
    if (distance.has_value()) {
        Domain::Vec3d translate = Domain::Vec3d::UnitZ() * (*distance);
        transformation.translate(translate);
    }
}

bool is_italic(const Domain::FontFile& font, unsigned int font_index)
{
    if (font_index >= font.infos.size())
        return false;
    fontinfo_opt font_info_opt = load_font_info(font.data->data(), font_index);

    if (!font_info_opt.has_value())
        return false;
    stbtt_fontinfo* info = &(*font_info_opt);

    // https://docs.microsoft.com/cs-cz/typography/opentype/spec/name
    // https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6name.html
    // 2 ==> Style / Subfamily name
    int name_id = 2;
    int length;
    const char* value = stbtt_GetFontNameString(
        info,
        &length,
        STBTT_PLATFORM_ID_MICROSOFT,
        STBTT_MS_EID_UNICODE_BMP,
        STBTT_MS_LANG_ENGLISH,
        name_id
    );

    // value is big endian utf-16 i need extract only normal chars
    std::string value_str;
    value_str.reserve(length / 2);
    for (int i = 1; i < length; i += 2)
        value_str.push_back(value[i]);

    // lower case
    std::transform(value_str.begin(), value_str.end(), value_str.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    const std::vector<std::string> italics({"italic", "oblique"});
    for (const std::string& it : italics) {
        if (value_str.find(it) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string create_range_text(
    const std::string& text,
    const Domain::FontFile& font,
    unsigned int font_index,
    bool* exist_unknown
)
{
    if (!is_valid(font, font_index))
        return {};

    std::wstring ws = boost::nowide::widen(text);

    // need remove symbols not contained in font
    std::sort(ws.begin(), ws.end());

    auto font_info_opt = load_font_info(font.data->data(), 0);
    if (!font_info_opt.has_value())
        return {};
    const stbtt_fontinfo* font_info = &(*font_info_opt);

    if (exist_unknown != nullptr)
        *exist_unknown = false;
    int prev_unicode = -1;
    ws.erase(
        std::remove_if(
            ws.begin(),
            ws.end(),
            [&prev_unicode, font_info, exist_unknown](wchar_t wc) -> bool {
                int unicode = static_cast<int>(wc);

                // skip white spaces
                if (unicode == '\n' || unicode == '\r' || unicode == '\t')
                    return true;

                // is duplicit?
                if (prev_unicode == unicode)
                    return true;
                prev_unicode = unicode;

                // can find in font?
                bool is_unknown = !stbtt_FindGlyphIndex(font_info, unicode);
                if (is_unknown && exist_unknown != nullptr)
                    *exist_unknown = true;
                return is_unknown;
            }
        ),
        ws.end()
    );

    return boost::nowide::narrow(ws);
}

double get_text_shape_scale(const Domain::FontProp& fp, const Domain::FontFile& ff)
{
    const Domain::FontFile::Info& info = get_font_info(ff, fp);
    double scale                       = fp.size_in_mm / (double) info.unit_per_em;
    // Shape is scaled for store point coordinate as integer
    return scale * SHAPE_SCALE;
}
} // namespace Slic3r::Biz::Emboss

namespace {

void add_quad(uint32_t i1, uint32_t i2, indexed_triangle_set& result, uint32_t count_point)
{
    // bottom indices
    uint32_t i1_ = i1 + count_point;
    uint32_t i2_ = i2 + count_point;
    result.indices.push_back(
        Domain::Index3{static_cast<int>(i2), static_cast<int>(i2_), static_cast<int>(i1)}
    );
    result.indices.push_back(
        Domain::Index3{static_cast<int>(i1_), static_cast<int>(i1), static_cast<int>(i2_)}
    );
};

indexed_triangle_set polygons2model_unique(
    const Domain::ExPolygons& shape2d,
    const Algorithms::IProjection& projection,
    const Domain::Points& points
)
{
    // CW order of triangle indices
    std::vector<Domain::Index3> shape_triangles = CGAL::Algorithms::Triangulation::triangulate(
        shape2d,
        points
    );
    uint32_t count_point = points.size();

    indexed_triangle_set result;
    result.vertices.reserve(2 * count_point);
    std::vector<Domain::Vec3f>& front_points = result.vertices; // alias
    std::vector<Domain::Vec3f> back_points;
    back_points.reserve(count_point);

    for (const Domain::Point& p : points) {
        auto p2 = projection.create_front_back(p);
        front_points.push_back(p2.first.cast<float>());
        back_points.push_back(p2.second.cast<float>());
    }

    // insert back points, front are already in
    result.vertices.insert(
        result.vertices.end(),
        std::make_move_iterator(back_points.begin()),
        std::make_move_iterator(back_points.end())
    );
    result.indices.reserve(shape_triangles.size() * 2 + points.size() * 2);
    // top triangles - change to CCW
    for (const Domain::Index3& t : shape_triangles)
        result.indices.push_back(Domain::Index3{t[0], t[2], t[1]});
    // bottom triangles - use CW
    for (const Domain::Index3& t : shape_triangles)
        result.indices.push_back(
            Domain::Index3{
                static_cast<int>(t[0] + count_point),
                static_cast<int>(t[1] + count_point),
                static_cast<int>(t[2] + count_point)
            }
        );

    // quads around - zig zag by triangles
    size_t polygon_offset = 0;
    auto add_quads = [&polygon_offset, &result, &count_point](const Domain::Polygon& polygon) {
        uint32_t polygon_points = polygon.points.size();
        // previous index
        uint32_t prev = polygon_offset + polygon_points - 1;
        for (uint32_t p = 0; p < polygon_points; ++p) {
            uint32_t index = polygon_offset + p;
            add_quad(prev, index, result, count_point);
            prev = index;
        }
        polygon_offset += polygon_points;
    };

    for (const Domain::ExPolygon& expolygon : shape2d) {
        add_quads(expolygon.contour);
        for (const Domain::Polygon& hole : expolygon.holes)
            add_quads(hole);
    }

    return result;
}

indexed_triangle_set polygons2model_duplicit(
    const Domain::ExPolygons& shape2d,
    const Algorithms::IProjection& projection,
    const Domain::Points& points,
    const Domain::Points& duplicits
)
{
    // CW order of triangle indices
    std::vector<uint32_t> changes = CGAL::Algorithms::Triangulation::create_changes(points, duplicits);
    std::vector<Domain::Index3> shape_triangles = CGAL::Algorithms::Triangulation::triangulate(
        shape2d,
        points,
        changes
    );
    uint32_t count_point = *std::max_element(changes.begin(), changes.end()) + 1;

    indexed_triangle_set result;
    result.vertices.reserve(2 * count_point);
    std::vector<Domain::Vec3f>& front_points = result.vertices;
    std::vector<Domain::Vec3f> back_points;
    back_points.reserve(count_point);

    uint32_t max_index = std::numeric_limits<uint32_t>::max();
    for (uint32_t i = 0; i < changes.size(); ++i) {
        uint32_t index = changes[i];
        if (max_index != std::numeric_limits<uint32_t>::max() && index <= max_index)
            continue; // duplicit point
        assert(index == max_index + 1);
        assert(front_points.size() == index);
        assert(back_points.size() == index);
        max_index              = index;
        const Domain::Point& p = points[i];
        auto p2                = projection.create_front_back(p);
        front_points.push_back(p2.first.cast<float>());
        back_points.push_back(p2.second.cast<float>());
    }
    assert(max_index + 1 == count_point);

    // insert back points, front are already in
    result.vertices.insert(
        result.vertices.end(),
        std::make_move_iterator(back_points.begin()),
        std::make_move_iterator(back_points.end())
    );

    result.indices.reserve(shape_triangles.size() * 2 + points.size() * 2);
    // top triangles - change to CCW
    for (const Domain::Index3& t : shape_triangles)
        result.indices.push_back(Domain::Index3{t[0], t[2], t[1]});
    // bottom triangles - use CW
    for (const Domain::Index3& t : shape_triangles)
        result.indices.push_back(
            Domain::Index3{
                static_cast<int>(t[0] + count_point),
                static_cast<int>(t[1] + count_point),
                static_cast<int>(t[2] + count_point)
            }
        );

    // quads around - zig zag by triangles
    size_t polygon_offset = 0;
    auto add_quads = [&polygon_offset, &result, count_point, &changes](const Domain::Polygon& polygon) {
        uint32_t polygon_points = polygon.points.size();
        // previous index
        uint32_t prev = changes[polygon_offset + polygon_points - 1];
        for (uint32_t p = 0; p < polygon_points; ++p) {
            uint32_t index = changes[polygon_offset + p];
            if (prev == index)
                continue;
            add_quad(prev, index, result, count_point);
            prev = index;
        }
        polygon_offset += polygon_points;
    };

    for (const Domain::ExPolygon& expolygon : shape2d) {
        add_quads(expolygon.contour);
        for (const Domain::Polygon& hole : expolygon.holes)
            add_quads(hole);
    }
    return result;
}
} // namespace

namespace Slic3r::Biz::Emboss {
indexed_triangle_set polygons2model(
    const Domain::ExPolygons& shape2d,
    const Algorithms::IProjection& projection
)
{
    Domain::Points points    = Algorithms::ExPolygon::to_points(shape2d);
    Domain::Points duplicits = Algorithms::Point::collect_duplicates(points);
    return (duplicits.empty()) ? polygons2model_unique(shape2d, projection, points) :
                                 polygons2model_duplicit(shape2d, projection, points, duplicits);
}

std::pair<Domain::Vec3d, Domain::Vec3d> ProjectZ::create_front_back(const Domain::Point& p) const
{
    Domain::Vec3d front(p.x(), p.y(), 0.);
    return std::make_pair(front, project(front));
}

Domain::Vec3d ProjectZ::project(const Domain::Vec3d& point) const
{
    Domain::Vec3d res = point; // copy
    res.z()           = m_depth;
    return res;
}

std::optional<Domain::Vec2d> ProjectZ::unproject(const Domain::Vec3d& p, double* depth) const
{
    return Domain::Vec2d(p.x(), p.y());
}

Domain::Vec3d suggest_up(const Domain::Vec3d normal, double up_limit)
{
    // Normal must be 1
    assert(Domain::fuzzy_compare(normal.squaredNorm(), 1.));

    // wanted up direction of result
    Domain::Vec3d wanted_up_side = (std::fabs(normal.z()) > up_limit) ? Domain::Vec3d::UnitY() :
                                                                        Domain::Vec3d::UnitZ();

    // create perpendicular unit vector to surface triangle normal vector
    // lay on surface of triangle and define up vector for text
    Domain::Vec3d wanted_up_dir = normal.cross(wanted_up_side).cross(normal);
    // normal3d is NOT perpendicular to normal_up_dir
    wanted_up_dir.normalize();

    return wanted_up_dir;
}

std::optional<float> calc_up(const Domain::Transform3d& tr, double up_limit)
{
    auto tr_linear = tr.linear();
    // z base of transformation ( tr * UnitZ )
    Domain::Vec3d normal = tr_linear.col(2);
    // scaled matrix has base with different size
    normal.normalize();
    Domain::Vec3d suggested = suggest_up(normal, up_limit);
    assert(Domain::fuzzy_compare(suggested.squaredNorm(), 1.));

    Domain::Vec3d up = tr_linear.col(1); // tr * UnitY()
    up.normalize();
    Domain::SquareMatrix3d m;
    m.row(0)   = up;
    m.row(1)   = suggested;
    m.row(2)   = normal;
    double det = m.determinant();
    double dot = suggested.dot(up);
    double res = -atan2(det, dot);
    if (Domain::fuzzy_compare(res, 0.))
        return {};
    return res;
}

Domain::Transform3d create_transformation_onto_surface(
    const Domain::Vec3d& position,
    const Domain::Vec3d& normal,
    double up_limit
)
{
    // is normalized ?
    assert(Domain::fuzzy_compare(normal.squaredNorm(), 1.));

    // up and emboss direction for generated model
    Domain::Vec3d up_dir     = Domain::Vec3d::UnitY();
    Domain::Vec3d emboss_dir = Domain::Vec3d::UnitZ();

    // after cast from float it needs to be normalized again
    Domain::Vec3d wanted_up_dir = suggest_up(normal, up_limit);

    // perpendicular to emboss vector of text and normal
    Domain::Vec3d axis_view;
    double angle_view;
    if (normal == -Domain::Vec3d::UnitZ()) {
        // text_emboss_dir has opposit direction to wanted_emboss_dir
        axis_view  = Domain::Vec3d::UnitY();
        angle_view = M_PI;
    } else {
        axis_view  = emboss_dir.cross(normal);
        angle_view = std::acos(emboss_dir.dot(normal)); // in rad
        axis_view.normalize();
    }

    Eigen::AngleAxis view_rot(angle_view, axis_view);
    Domain::Vec3d wanterd_up_rotated = view_rot.matrix().inverse() * wanted_up_dir;
    wanterd_up_rotated.normalize();
    double angle_up = std::acos(up_dir.dot(wanterd_up_rotated));

    Domain::Vec3d text_view = up_dir.cross(wanterd_up_rotated);
    Domain::Vec3d diff_view = emboss_dir - text_view;
    if (std::fabs(diff_view.x()) > 1.
        || std::fabs(diff_view.y()) > 1.
        || std::fabs(diff_view.z()) > 1.) // oposit direction
        angle_up *= -1.;

    Eigen::AngleAxis up_rot(angle_up, emboss_dir);

    Domain::Transform3d transform = Domain::Transform3d::Identity();
    transform.translate(position);
    transform.rotate(view_rot);
    transform.rotate(up_rot);
    return transform;
}

// OrthoProject

std::pair<Domain::Vec3d, Domain::Vec3d> OrthoProject::create_front_back(const Domain::Point& p) const
{
    Domain::Vec3d front(p.x(), p.y(), 0.);
    Domain::Vec3d front_tr = m_matrix * front;
    return std::make_pair(front_tr, project(front_tr));
}

Domain::Vec3d OrthoProject::project(const Domain::Vec3d& point) const
{
    return point + m_direction;
}

std::optional<Domain::Vec2d> OrthoProject::unproject(const Domain::Vec3d& p, double* depth) const
{
    Domain::Vec3d pp = m_matrix_inv * p;
    if (depth != nullptr)
        *depth = pp.z();
    return Domain::Vec2d(pp.x(), pp.y());
}
} // namespace Slic3r::Biz::Emboss

// sample slice
namespace {
using Slic3r::Biz::Emboss::PolygonPoint;
// using coor2 = int64_t;
using Coord2 = double;
using P2     = Eigen::Matrix<Coord2, 2, 1, Eigen::DontAlign>;

bool point_in_distance(
    const Coord2& distance_sq,
    PolygonPoint& polygon_point,
    const size_t& i,
    const Slic3r::Domain::Polygon& polygon,
    bool is_first,
    bool is_reverse = false
)
{
    size_t s  = polygon.size();
    size_t ii = (i + polygon_point.index) % s;

    // second point of line
    const Domain::Point& p = polygon[ii];
    Domain::Point p_d      = p - polygon_point.point;

    P2 p_d2              = p_d.cast<Coord2>();
    Coord2 p_distance_sq = p_d2.squaredNorm();
    if (p_distance_sq < distance_sq)
        return false;

    // found line
    if (is_first) {
        // on same line
        // center also lay on line
        // new point is distance moved from point by direction
        polygon_point.point += p_d * sqrt(distance_sq / p_distance_sq);
        return true;
    }

    // line cross circle

    // start point of line
    size_t ii2              = (is_reverse) ? (ii + 1) % s : (ii + s - 1) % s;
    polygon_point.index     = (is_reverse) ? ii : ii2;
    const Domain::Point& p2 = polygon[ii2];

    Domain::Point line_dir = p2 - p;
    P2 line_dir2           = line_dir.cast<Coord2>();

    Coord2 a = line_dir2.dot(line_dir2);
    Coord2 b = 2 * p_d2.dot(line_dir2);
    Coord2 c = p_d2.dot(p_d2) - distance_sq;

    double discriminant = b * b - 4 * a * c;
    if (discriminant < 0) {
        assert(false);
        // no intersection
        polygon_point.point = p;
        return true;
    }

    // ray didn't totally miss sphere,
    // so there is a solution to
    // the equation.
    discriminant = sqrt(discriminant);

    // either solution may be on or off the ray so need to test both
    // t1 is always the smaller value, because BOTH discriminant and
    // a are nonnegative.
    double t1 = (-b - discriminant) / (2 * a);
    double t2 = (-b + discriminant) / (2 * a);

    double t = std::min(t1, t2);
    if (t < 0. || t > 1.) {
        // Bad intersection
        assert(false);
        polygon_point.point = p;
        return true;
    }

    polygon_point.point = p + (t * line_dir2).cast<Domain::coord_t>();
    return true;
}

void point_in_distance(int32_t distance, PolygonPoint& p, const Domain::Polygon& polygon)
{
    Coord2 distance_sq = static_cast<Coord2>(distance) * distance;
    bool is_first      = true;
    for (size_t i = 1; i < polygon.size(); ++i) {
        if (point_in_distance(distance_sq, p, i, polygon, is_first))
            return;
        is_first = false;
    }
    // There is not point on polygon with this distance
}

void point_in_reverse_distance(int32_t distance, PolygonPoint& p, const Domain::Polygon& polygon)
{
    Coord2 distance_sq = static_cast<Coord2>(distance) * distance;
    bool is_first      = true;
    bool is_reverse    = true;
    for (size_t i = polygon.size(); i > 0; --i) {
        if (point_in_distance(distance_sq, p, i, polygon, is_first, is_reverse))
            return;
        is_first = false;
    }
    // There is not point on polygon with this distance
}
} // namespace

namespace Slic3r::Biz::Emboss {
// calculate rotation, need copy of polygon point
double calculate_angle(int32_t distance, PolygonPoint polygon_point, const Domain::Polygon& polygon)
{
    PolygonPoint polygon_point2 = polygon_point; // copy
    point_in_distance(distance, polygon_point, polygon);
    point_in_reverse_distance(distance, polygon_point2, polygon);

    Domain::Point surface_dir = polygon_point2.point - polygon_point.point;
    Domain::Point norm(-surface_dir.y(), surface_dir.x());
    Domain::Vec2d norm_d = norm.cast<double>();
    // norm_d.normalize();
    return std::atan2(norm_d.y(), norm_d.x());
}

std::vector<double> calculate_angles(
    const Domain::BoundingBoxes2crd& glyph_sizes,
    const PolygonPoints& polygon_points,
    const Domain::Polygon& polygon
)
{
    const int32_t default_distance = static_cast<int32_t>(std::round(scale_(5.)));
    const int32_t min_distance     = static_cast<int32_t>(std::round(scale_(.1)));

    std::vector<double> result;
    result.reserve(polygon_points.size());
    assert(glyph_sizes.size() == polygon_points.size());
    if (glyph_sizes.size() != polygon_points.size()) {
        // only backup solution should not be used
        for (const PolygonPoint& pp : polygon_points)
            result.emplace_back(calculate_angle(default_distance, pp, polygon));
        return result;
    }

    for (size_t i = 0; i < polygon_points.size(); i++) {
        int32_t distance = Algorithms::BoundingBox::sizes(glyph_sizes[i]).x() / 2;
        if (distance < min_distance) // too small could lead to false angle
            distance = default_distance;
        result.emplace_back(calculate_angle(distance, polygon_points[i], polygon));
    }
    return result;
}

PolygonPoints sample_slice(const TextLine& slice, const Domain::BoundingBoxes2crd& bbs, double scale)
{
    // find BB in center of line
    size_t first_right_index = 0;
    for (const Domain::BoundingBox2crd& bb : bbs)
        if (!bb.defined) // white char do not have bb
            continue;
        else if (bb.min.x() < 0)
            ++first_right_index;
        else
            break;

    PolygonPoints samples(bbs.size());
    int32_t shapes_x_cursor = 0;

    PolygonPoint cursor = slice.start; // copy

    auto create_sample =
        [&] // polygon_cursor, &polygon_line_index, &line_bbs, &shapes_x_cursor, &shape_scale, &em_2_polygon, &line, &offsets]
        (const Domain::BoundingBox2crd& bb, bool is_reverse) {
            if (!bb.defined)
                return cursor;
            Domain::Point letter_center = Algorithms::BoundingBox::center(bb);
            int32_t shape_distance      = shapes_x_cursor - letter_center.x();
            shapes_x_cursor             = letter_center.x();
            double distance_mm          = shape_distance * scale;
            int32_t distance_polygon    = static_cast<int32_t>(std::round(scale_(distance_mm)));
            if (is_reverse)
                point_in_distance(distance_polygon, cursor, slice.polygon);
            else
                point_in_reverse_distance(distance_polygon, cursor, slice.polygon);
            return cursor;
        };

    // calc transformation for letters on the Right side from center
    bool is_reverse = true;
    for (size_t index = first_right_index; index < bbs.size(); ++index)
        samples[index] = create_sample(bbs[index], is_reverse);

    // calc transformation for letters on the Left side from center
    if (first_right_index < bbs.size()) {
        shapes_x_cursor = Algorithms::BoundingBox::center(bbs[first_right_index]).x();
        cursor          = samples[first_right_index];
    } else {
        // only left side exists
        shapes_x_cursor = 0;
        cursor          = slice.start; // copy
    }
    is_reverse = false;
    for (size_t index_plus_one = first_right_index; index_plus_one > 0; --index_plus_one) {
        size_t index   = index_plus_one - 1;
        samples[index] = create_sample(bbs[index], is_reverse);
    }
    return samples;
}
} // namespace Slic3r::Biz::Emboss

namespace {
float get_align_y_offset(
    Domain::FontProp::VerticalAlign align,
    unsigned count_lines,
    const Domain::FontFile& ff,
    const Domain::FontProp& fp
)
{
    assert(count_lines != 0);
    int line_height   = get_line_height(ff, fp);
    int ascent        = get_font_info(ff, fp).ascent / SHAPE_SCALE;
    float line_center = static_cast<float>(std::round(ascent * ASCENT_CENTER));

    // direction of Y in 2d is from top to bottom
    // zero is on base line of first line
    switch (align) {
    case Domain::FontProp::VerticalAlign::bottom:
        return line_height * (count_lines - 1);
    case Domain::FontProp::VerticalAlign::top:
        return -ascent;
    case Domain::FontProp::VerticalAlign::center:
    default:
        return -line_center + line_height * (count_lines - 1) / 2.;
    }
}

int32_t get_align_x_offset(
    Domain::FontProp::HorizontalAlign align,
    const Domain::BoundingBox2crd& shape_bb,
    const Domain::BoundingBox2crd& line_bb
)
{
    using Algorithms::BoundingBox::center;
    using Algorithms::BoundingBox::sizes;
    switch (align) {
    case Domain::FontProp::HorizontalAlign::right:
        return -shape_bb.max.x() + (sizes(shape_bb).x() - sizes(line_bb).x());
    case Domain::FontProp::HorizontalAlign::center:
        return -center(shape_bb).x() + (sizes(shape_bb).x() - sizes(line_bb).x()) / 2;
    case Domain::FontProp::HorizontalAlign::left: // no change
    default:
        break;
    }
    return 0;
}

void align_shape(
    Domain::ExPolygonsWithIds& shapes,
    const std::wstring& text,
    const Domain::FontProp& prop,
    const Domain::FontFile& font
)
{
    // Shapes have to match letters in text
    assert(shapes.size() == text.length());

    unsigned count_lines = get_count_lines(text);
    int y_offset         = get_align_y_offset(prop.align.vertical, count_lines, font, prop);

    // Speed up for left aligned text
    if (prop.align.horizontal == Domain::FontProp::HorizontalAlign::left) {
        // already horizontaly aligned
        for (Domain::ExPolygonsWithId& shape : shapes)
            for (Domain::ExPolygon& s : shape.expoly)
                s.translate(Domain::Point(0, y_offset));
        return;
    }

    Domain::BoundingBox2crd shape_bb = Algorithms::ExPolygonsWithId::get_extents(shapes);
    auto get_line_bb                 = [&](size_t j) {
        Domain::BoundingBox2crd line_bb;
        for (; j < text.length() && text[j] != '\n'; ++j)
            line_bb = Algorithms::BoundingBox::merge(
                line_bb,
                Algorithms::ExPolygon::get_extents(shapes[j].expoly)
            );
        return line_bb;
    };

    // Align x line by line
    Domain::Point offset(get_align_x_offset(prop.align.horizontal, shape_bb, get_line_bb(0)), y_offset);
    for (size_t i = 0; i < shapes.size(); ++i) {
        wchar_t letter = text[i];
        if (letter == '\n') {
            offset.x() = get_align_x_offset(prop.align.horizontal, shape_bb, get_line_bb(i + 1));
            continue;
        }
        Domain::ExPolygons& shape = shapes[i].expoly;
        for (Domain::ExPolygon& s : shape)
            s.translate(offset);
    }
}
} // namespace

namespace Slic3r::Biz::Emboss {
double get_align_y_offset_in_mm(
    Domain::FontProp::VerticalAlign align,
    unsigned count_lines,
    const Domain::FontFile& ff,
    const Domain::FontProp& fp
)
{
    float offset_in_font_point = get_align_y_offset(align, count_lines, ff, fp);
    double scale               = get_text_shape_scale(fp, ff);
    return scale * offset_in_font_point;
}

#ifdef REMOVE_SPIKES
#include <Geometry.hpp>

void remove_spikes(Domain::Polygon& polygon, const SpikeDesc& spike_desc)
{
    enum class Type
    {
        add, // Move with point B on A-side and add new point on C-side
        move, // Only move with point B
        erase // left only points A and C without move
    };

    struct SpikeHeal
    {
        Type type;
        size_t index;
        Domain::Point b;
        Domain::Point add;
    };

    using SpikeHeals = std::vector<SpikeHeal>;
    SpikeHeals heals;

    size_t count = polygon.size();
    if (count < 3)
        return;

    const Domain::Point* ptr_a = &polygon[count - 2];
    const Domain::Point* ptr_b = &polygon[count - 1];
    for (const Domain::Point& c : polygon) {
        const Domain::Point& a = *ptr_a;
        const Domain::Point& b = *ptr_b;
        ScopeGuard sg([&ptr_a, &ptr_b, &c]() {
            // prepare for next loop
            ptr_a = ptr_b;
            ptr_b = &c;
        });

        // calc sides
        Domain::Point ba = a - b;
        Domain::Point bc = c - b;

        Domain::Vec2d ba_f = ba.cast<double>();
        Domain::Vec2d bc_f = bc.cast<double>();
        double dot_product = ba_f.dot(bc_f);

        // sqrt together after multiplication save one sqrt
        double ba_size_sq = ba_f.squaredNorm();
        double bc_size_sq = bc_f.squaredNorm();
        double norm       = sqrt(ba_size_sq * bc_size_sq);
        double cos_angle  = dot_product / norm;

        // small angle are around 1 --> cos(0) = 1
        if (cos_angle < spike_desc.cos_angle)
            continue;

        SpikeHeal heal;
        heal.index = &b - &polygon.points.front();

        // has to be in range <-1, 1>
        // Due to preccission of floating point number could be sligtly out of range
        if (cos_angle > 1.)
            cos_angle = 1.;
        if (cos_angle < -1.)
            cos_angle = -1.;

        // Current Spike angle
        double angle          = acos(cos_angle);
        double wanted_size    = spike_desc.half_bevel / cos(angle / 2.);
        double wanted_size_sq = wanted_size * wanted_size;

        bool is_ba_short = ba_size_sq < wanted_size_sq;
        bool is_bc_short = bc_size_sq < wanted_size_sq;
        auto a_side      = [&b, &ba_f, &ba_size_sq, &wanted_size]() {
            Domain::Vec2d ba_norm = ba_f / sqrt(ba_size_sq);
            return b + (wanted_size * ba_norm).cast<Domain::coord_t>();
        };
        auto c_side = [&b, &bc_f, &bc_size_sq, &wanted_size]() {
            Domain::Vec2d bc_norm = bc_f / sqrt(bc_size_sq);
            return b + (wanted_size * bc_norm).cast<Domain::coord_t>();
        };
        if (is_ba_short && is_bc_short) {
            // remove short spike
            heal.type = Type::erase;
        } else if (is_ba_short) {
            // move point B on C-side
            heal.type = Type::move;
            heal.b    = c_side();
        } else if (is_bc_short) {
            // move point B on A-side
            heal.type = Type::move;
            heal.b    = a_side();
        } else {
            // move point B on A-side and add point on C-side
            heal.type = Type::add;
            heal.b    = a_side();
            heal.add  = c_side();
        }
        heals.push_back(heal);
    }

    if (heals.empty())
        return;

    // sort index from high to low
    if (heals.front().index == (count - 1))
        std::rotate(heals.begin(), heals.begin() + 1, heals.end());
    std::reverse(heals.begin(), heals.end());

    int extend      = 0;
    int curr_extend = 0;
    for (const SpikeHeal& h : heals)
        switch (h.type) {
        case Type::add:
            ++curr_extend;
            if (extend < curr_extend)
                extend = curr_extend;
            break;
        case Type::erase:
            --curr_extend;
        }

    Domain::Points& pts = polygon.points;
    if (extend > 0)
        pts.reserve(pts.size() + extend);

    for (const SpikeHeal& h : heals) {
        switch (h.type) {
        case Type::add:
            pts[h.index] = h.b;
            pts.insert(pts.begin() + h.index + 1, h.add);
            break;
        case Type::erase:
            pts.erase(pts.begin() + h.index);
            break;
        case Type::move:
            pts[h.index] = h.b;
            break;
        default:
            break;
        }
    }
}

void remove_spikes(Domain::Polygons& polygons, const SpikeDesc& spike_desc)
{
    for (Domain::Polygon& polygon : polygons)
        remove_spikes(polygon, spike_desc);
    remove_bad(polygons);
}

void remove_spikes(Domain::ExPolygons& expolygons, const SpikeDesc& spike_desc)
{
    for (Domain::ExPolygon& expolygon : expolygons) {
        remove_spikes(expolygon.contour, spike_desc);
        remove_spikes(expolygon.holes, spike_desc);
    }
    remove_bad(expolygons);
}

#endif // REMOVE_SPIKES

} // namespace Slic3r::Biz::Emboss
