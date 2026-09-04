#include "PrusaFile.hpp"
#include <string_view>
#include <set>
#include <type_traits> // enable_if
#include <boost/assign.hpp>
#include <boost/bimap.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>


#include "Slic3r/Log.hpp"
#include "Slic3r/Biz/Format/ProjectFileConstants.hpp"
#include "Slic3r/Biz/Format/VirtualExtruder.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/ProjectMetadataJson.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Biz/Config/ConfigLoad.hpp"
#include "Slic3r/Biz/Config/SelectedPresetJson.hpp"
#include "Slic3r/Biz/Emboss/NSVGUtils.hpp" // open content of svg file
#include "Slic3r/Domain/ConfigBoxesFDM.hpp"
#include "Slic3r/Domain/FullConfigFDM.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/Types.hpp"

using Slic3r::Biz::_u8L;
using Slic3r::Domain::FacetsAnnotation;
using Slic3r::Domain::Vec3d;
using Slic3r::Domain::TriangleSelector::TriangleBitStreamMapping;
using Slic3r::Domain::TriangleSelector::TriangleSplittingData;
using Slic3r::Domain::TriangleSelector::TriangleStateType;

using ModelObject = Slic3r::Domain::ModelObject;
using ModelVolume = Slic3r::Domain::ModelVolume;
using ModelInstance = Slic3r::Domain::ModelInstance;
using Model = Slic3r::Domain::Model;
using ModelVolumeType = Slic3r::Domain::ModelVolumeType;
using ModelVolumePtrs = Slic3r::Domain::ModelVolumePtrs;

using json = nlohmann::ordered_json;
using namespace Slic3r;

using EmbossProjection = Domain::EmbossProjection;
using EmbossShape = Domain::EmbossShape;
using EmbossStyle = Domain::EmbossStyle;
using Domain::FontDescriptor;
using FontProp = Domain::FontProp;
using TextConfiguration = Domain::TextConfiguration;

namespace{
using NamesType = const std::vector<std::string_view>;
using RT = Read3mfIssueType; // shorting name

void write_file(mz_zip_archive &archive, const json &data, const char *filepath) {
    if (data.empty())
        return; // Do not store empty files

    int indentation = 2; // two spaces indentation
    std::string data_str = Biz::beautify_json(data, indentation);
    if (!mz_zip_writer_add_mem(&archive, filepath, 
        (const void *) data_str.data(), data_str.length(), MZ_DEFAULT_COMPRESSION))
        throw boost::filesystem::filesystem_error( std::string() +
            "Unable to add \"" + filepath + "\" file into archive.", {});
}

void write_svg_file(mz_zip_archive &archive, const std::string &filepath, const std::string &file_data_str) {
    if(!mz_zip_writer_add_mem(&archive, filepath.c_str(), 
        (const void *) file_data_str.c_str(), file_data_str.size(), MZ_DEFAULT_COMPRESSION))
        throw boost::filesystem::filesystem_error("Unable to add svg file to archive.", {});
}

/**
 * Add only non empty data.
 * Returns true when the data was stored (empty data is not stored).
 */
bool add(json& result, std::string_view name, json &&data) {
    if (data.empty()) {
        return false;
    }

    result[name] = std::move(data);
    return true;
}

/// <summary>
/// Hepl function to convert json data type
/// </summary>
/// <typeparam name="T">Type of data handled by json library</typeparam>
/// <param name="data_json">json keeping value data</param>
/// <param name="value">value to convert into</param>
/// <param name="result">keep list of issues</param>
/// <param name="issue">Current issue type to add when issue appear</param>
/// <returns>True when json contain data and it was converted into value otherwise FALSE</returns>
template<typename T, std::enable_if_t<
       std::is_same_v<T, json::array_t>
    || std::is_same_v<T, json::object_t>
    || std::is_same_v<T, json::string_t>
    || std::is_same_v<T, json::boolean_t>
    || std::is_same_v<T, json::number_integer_t>
    || std::is_same_v<T, json::number_unsigned_t>
    || std::is_same_v<T, json::number_float_t> 
    || std::is_same_v<T, json::binary_t> 
    , bool> = true > 
bool get_value(const json &data_json, T &value, Read3mfIssues& collected_issues, RT issue) {
    assert(data_json.is_primitive());
    assert(!data_json.is_structured());
    assert(!data_json.is_null());
    // Get pointer on data stored in json object
    // No-throw guarantee: this function never throws exceptions. (cite from documentation)
    const T *value_ptr = data_json.get_ptr<const T*>();
    if (value_ptr == nullptr) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("Can't get value"), data_json.dump()));
        return false;
    }
    value = *value_ptr; // copy value    
    return true;
}

/// <summary>
/// Function to convert values from json with adding result issue when data is not same as saved
/// Prefer call of from_json
/// </summary>
/// <typeparam name="T"></typeparam>
/// <param name="data_json"></param>
/// <param name="value"></param>
/// <param name="r"></param>
/// <param name="issue"></param>
/// <returns></returns>
template<typename T>
bool value_from_json(const json &data_json, T &value, Read3mfIssues& collected_issues, RT issue) {
    // Do not know the way to template specify only when type alias differ
    // Get an error template function ambiguity
    // NOTE: on R-PI is same type: unsigned = size_t = json::number_unsigned_t
    if constexpr (std::is_floating_point_v<T>){ // float + double
        if (!data_json.is_number()) {
            collected_issues.add_issue(Read3mfIssue(issue, std::string("Not a number"), data_json.dump()));
            return false;
        }
        json::number_float_t value_dbl;
        if (!value_from_json(data_json, value_dbl, collected_issues, issue))
            return false;
        value = static_cast<T>(value_dbl);
        return true;
    } else if constexpr (std::is_signed_v<T>) { // int
        if (!data_json.is_number_integer()) {
            collected_issues.add_issue(Read3mfIssue(issue, std::string("Not a number integer"), data_json.dump()));
            return false;
        }
        json::number_integer_t value_int64;
        if (!value_from_json(data_json, value_int64, collected_issues, issue))
            return false;
        value = static_cast<T>(value_int64);
        return true; 
    } else if constexpr (std::is_unsigned_v<T>) { // unsigned + size_t
        if (!data_json.is_number_unsigned()) {
            collected_issues.add_issue(Read3mfIssue(issue, std::string("Not an unsigned number"), data_json.dump()));
            return false;
        }
        json::number_unsigned_t value_uint64;
        if (!value_from_json(data_json, value_uint64, collected_issues, issue))
            return false;
        value = static_cast<T>(value_uint64);
        return true;
    }
    PANIC();// template specialization is not implemented
    return false;
}

template<> bool value_from_json(const json &data_json, json::number_unsigned_t &value, Read3mfIssues& collected_issues, RT issue)
{
    if (!data_json.is_number_unsigned()) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("Not an unsigned number"), data_json.dump()));
        return false;
    }
    return get_value(data_json, value, collected_issues, issue);
}
template<> bool value_from_json(const json &data_json, json::number_integer_t &value, Read3mfIssues& collected_issues, RT issue)
{
    if (!data_json.is_number()) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("Not an integer number"), data_json.dump()));
        return false;
    }
    return get_value(data_json, value, collected_issues, issue);
}
template<> bool value_from_json(const json &data_json, json::string_t &value, Read3mfIssues& collected_issues, RT issue)
{
    if (!data_json.is_string()) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("Not a string"), data_json.dump()));
        return false;
    }
    return get_value(data_json, value, collected_issues, issue);
}
template<> bool value_from_json(const json &data_json, json::boolean_t &value, Read3mfIssues& collected_issues, RT issue)
{
    if (!data_json.is_boolean()) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("Not a bool"), data_json.dump()));
        return false;
    }
    return get_value(data_json, value, collected_issues, issue);
}
template<> bool value_from_json(const json &data_json, json::number_float_t &value, Read3mfIssues& collected_issues, RT issue)
{
    if (data_json.is_number_float()) return get_value(data_json, value, collected_issues, issue);
    // Load int value into floating point value without issue
    if (!data_json.is_number_integer()) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("Not a number"), data_json.dump()));
        return false;
    }
    json::number_integer_t value_int;
    if (!value_from_json(data_json, value_int, collected_issues, issue))
        return false;
    value = static_cast<json::number_float_t>(value_int);
    return true;
}

template<typename T>
json to_json(const Domain::Advanced::Vec<T, 3>& v)
{
    return json{v.x(), v.y(), v.z()};
}

template<typename T>
bool value_from_json(const json &v_json, Domain::Advanced::Vec<T, 3>& v, Read3mfIssues& collected_issues, RT issue)
{
    if (!v_json.is_array()) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("not an array"), v_json.dump()));
        return false;
    }
    size_t max_i = 3;
    if (v_json.size() != 3) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("bad amount of numbers"), std::to_string(v_json.size()) + v_json.dump()));
        max_i = std::min(size_t(3), v_json.size());        
    }
    for (size_t i = 0; i < max_i; i++)
        value_from_json(v_json[i], v[i], collected_issues, issue);
    return (max_i == 3);
}

// optional as value
template<typename T> bool value_from_json(const json &data_json, std::optional<T> &value, Read3mfIssues& collected_issues, RT issue)
{
    T value_;
    if (!value_from_json(data_json, value_, collected_issues, issue))
        return false;
    value = value_;
    return true;
}

/// <summary>
/// Get value by name from parent json object 
/// </summary>
/// <typeparam name="T">Specify loaded data</typeparam>
/// <param name="parent_json">MUST be json object(NOTE: check is done in function is_valid())</param>
/// <param name="name">Json object Property name - in JSON file is between symbols "(appostrofs)</param>
/// <param name="value">[output] value to fill by data from json</param>
/// <param name="result">Keep list of issues collected during load</param>|
/// <param name="issue">Specify issue genereted when JSON data do not match T datatype</param>
/// <param name="log_missing">When true than generate issue missing when parent object do not contain name property</param>
/// <returns>TRUE when succesfully read value from json otherwise FALSE</returns>
template<typename T>
inline bool from_json(const json &parent_json, std::string_view name, T& value, Read3mfIssues& collected_issues,
    RT issue = RT::unknown, bool log_missing = false){
    auto json_ptr = parent_json.find(name);
    if (json_ptr == parent_json.end()) {
        if (log_missing)
            collected_issues.add_issue(Read3mfIssue(issue, std::string("missing")));
        return false;
    }
    return value_from_json(*json_ptr, value, collected_issues, issue);
}

// Convert enum to json by bidirectional map
template<typename ENUM>
json to_json(ENUM value, const boost::bimap<ENUM, std::string_view> &bmap){
    const auto &map = bmap.left;
    auto found_item = map.find(value);

    // not found, add key to enum!!
    // To fix it uses first value in map. Should not appear in released version
    assert(found_item != map.end()); 
    if (found_item == map.end())
        found_item = map.begin();

    return json(found_item->second);
}

template<typename ENUM>
bool from_json(const json &parent_json, std::string_view name, ENUM &value, const boost::bimap<ENUM, std::string_view> &bmap,
    Read3mfIssues& collected_issues, RT issue, bool log_missing = false ) {
    auto json_ptr = parent_json.find(name);
    if (json_ptr == parent_json.end()) {
        if (log_missing)
            collected_issues.add_issue(Read3mfIssue(issue, std::string("missing")));
        return false;
    }
    const json &data_json = *json_ptr;
    std::string value_str;
    if (!value_from_json(data_json, value_str, collected_issues, issue))
        return false;

    const auto &map = bmap.right;
    auto found_item = map.find(value_str);

    // not known string
    if (found_item == map.end()) {
        collected_issues.add_issue(Read3mfIssue(issue, std::string("unknown enum value"), value_str));
        return false;
    }

    value = found_item->second;
    return true;
}

/// <summary>
/// Write issue when object contain not known names
/// Check whether json is object
/// </summary>
/// <param name="object_json">json object</param>
/// <param name="known_properties">list of known property names in object</param>
/// <param name="result">List of issues</param>
/// <param name="issue">Type of issue added when unknown property name appear</param>
/// <returns>False when object_json is not an object otherwise True</returns>
bool is_valid(const json &object_json, NamesType &known_properties, 
    Read3mfIssues& collected_issues, RT issue) {
    if (!object_json.is_object()){
        collected_issues.add_issue(Read3mfIssue(issue, std::string("Not an object"), object_json.dump()));
        return false;
    }
    for (const auto &[key, value] : object_json.items()) {
        if (std::any_of(known_properties.begin(), known_properties.end(),
            [&kk = key](std::string_view k) {return k==kk;}))
            continue;
        collected_issues.add_issue(Read3mfIssue(issue, key, value.dump()));
    }
    return true;
}

namespace TextConfigurationSerialization {
// Store / load of TextConfiguration
constexpr std::string_view TEXT = "text";
// TextConfiguration::EmbossStyle
constexpr std::string_view STYLE_NAME = "style_name";
constexpr std::string_view FONT_DESCRIPTOR = "font_descriptor";
constexpr std::string_view FONT_DESCRIPTOR_TYPE = "font_descriptor_type";

// TextConfiguration::FontProperty
constexpr std::string_view CHAR_GAP = "char_gap";
constexpr std::string_view LINE_GAP = "line_gap";
constexpr std::string_view LINE_HEIGHT = "line_height";
constexpr std::string_view BOLDNESS = "boldness";
constexpr std::string_view SKEW = "skew";
constexpr std::string_view PER_GLYPH = "per_glyph";
constexpr std::string_view HORIZONTAL_ALIGN = "horizontal";
constexpr std::string_view VERTICAL_ALIGN = "vertical";
constexpr std::string_view COLLECTION_NUMBER = "collection";

constexpr std::string_view FONT_FAMILY = "family";
constexpr std::string_view FONT_FACE_NAME = "face_name";
constexpr std::string_view FONT_STYLE = "style";
constexpr std::string_view FONT_WEIGHT = "weight";

const NamesType NAMES = {{TEXT, STYLE_NAME, FONT_DESCRIPTOR, FONT_DESCRIPTOR_TYPE, 
CHAR_GAP, LINE_GAP, LINE_HEIGHT, BOLDNESS, SKEW, PER_GLYPH, HORIZONTAL_ALIGN, VERTICAL_ALIGN, COLLECTION_NUMBER,
FONT_FAMILY, FONT_FACE_NAME, FONT_STYLE, FONT_WEIGHT}};

using TypeToName = boost::bimap<FontDescriptor::Type, std::string_view>;
const TypeToName type_to_name = 
    boost::assign::list_of<TypeToName::relation>
    (FontDescriptor::Type::file_path, "file_name")
    (FontDescriptor::Type::wx_win_font_descr, "wxFontDescriptor_Windows")
    (FontDescriptor::Type::wx_lin_font_descr, "wxFontDescriptor_Linux")
    (FontDescriptor::Type::wx_mac_font_descr, "wxFontDescriptor_MacOsX");

using HorizontalAlignToName = boost::bimap<FontProp::HorizontalAlign, std::string_view>;
const HorizontalAlignToName horizontal_align_to_name = 
    boost::assign::list_of<HorizontalAlignToName::relation>
    (FontProp::HorizontalAlign::left, "left")
    (FontProp::HorizontalAlign::center, "center")
    (FontProp::HorizontalAlign::right, "right");

using VerticalAlignToName = boost::bimap<FontProp::VerticalAlign, std::string_view>;
const VerticalAlignToName vertical_align_to_name = 
    boost::assign::list_of<VerticalAlignToName::relation>
    (FontProp::VerticalAlign::top, "top")
    (FontProp::VerticalAlign::center, "middle")
    (FontProp::VerticalAlign::bottom, "bottom");

json to_json(const TextConfiguration &tc) {
    json result = json::object();
    result[TEXT] = tc.text;
    // font item
    const EmbossStyle &style = tc.style;
    result[STYLE_NAME] = style.descriptor.name;
    result[FONT_DESCRIPTOR] = style.descriptor.path;
    result[FONT_DESCRIPTOR_TYPE] = ::to_json(style.descriptor.type, type_to_name);

    // font property
    const FontProp &fp = tc.style.prop;
    if (fp.char_gap.has_value()) result[CHAR_GAP] = *fp.char_gap;
    if (fp.line_gap.has_value()) result[LINE_GAP] = *fp.line_gap;

    result[LINE_HEIGHT] = fp.size_in_mm;
    if (fp.boldness.has_value()) result[BOLDNESS] = *fp.boldness;
    if (fp.skew.has_value())     result[SKEW] = *fp.skew;
    if (fp.per_glyph)            result[PER_GLYPH] = true;
    result[HORIZONTAL_ALIGN] = ::to_json(fp.align.horizontal, horizontal_align_to_name);
    result[VERTICAL_ALIGN] = ::to_json(fp.align.vertical, vertical_align_to_name);
    if (fp.collection_number.has_value()) result[COLLECTION_NUMBER] = *fp.collection_number;
    // font descriptor
    if (fp.family.has_value())   result[FONT_FAMILY] = *fp.family;
    if (fp.face_name.has_value())result[FONT_FACE_NAME] = *fp.face_name;
    if (fp.style.has_value())    result[FONT_STYLE] = *fp.style;
    if (fp.weight.has_value())   result[FONT_WEIGHT] = *fp.weight;
    return result;
}

void load(const json &tc_json, TextConfiguration &tc, Read3mfIssues& collected_issues) {
    if(!is_valid(tc_json, NAMES, collected_issues, RT::project_text_configuration_unknown_property))
        return;
    from_json(tc_json, TEXT, tc.text, collected_issues, RT::project_text_configuration_text_issue, true);
    EmbossStyle &style = tc.style;
    from_json(tc_json, STYLE_NAME,           style.descriptor.name, collected_issues, RT::project_text_configuration_style_name_issue, true);
    from_json(tc_json, FONT_DESCRIPTOR,      style.descriptor.path, collected_issues, RT::project_text_configuration_font_descriptor_issue, true);
    from_json(tc_json, FONT_DESCRIPTOR_TYPE, style.descriptor.type, type_to_name, collected_issues, RT::project_text_configuration_font_descriptor_type_issue, true);
    FontProp &fp = style.prop;
    from_json(tc_json, CHAR_GAP,    fp.char_gap  , collected_issues, RT::project_text_configuration_char_gap_issue);
    from_json(tc_json, LINE_GAP,    fp.line_gap  , collected_issues, RT::project_text_configuration_line_gap_issue);
    from_json(tc_json, LINE_HEIGHT, fp.size_in_mm, collected_issues, RT::project_text_configuration_line_height_issue, true);
    from_json(tc_json, BOLDNESS,    fp.boldness  , collected_issues, RT::project_text_configuration_boldness_issue);
    from_json(tc_json, SKEW,        fp.skew      , collected_issues, RT::project_text_configuration_skew_issue);
    from_json(tc_json, PER_GLYPH,   fp.per_glyph , collected_issues, RT::project_text_configuration_per_glyph_issue);
    from_json(tc_json, HORIZONTAL_ALIGN , fp.align.horizontal , horizontal_align_to_name, collected_issues, RT::project_text_configuration_horizontal_align_issue, true);
    from_json(tc_json, VERTICAL_ALIGN   , fp.align.vertical, vertical_align_to_name  , collected_issues, RT::project_text_configuration_vertical_align_issue, true);
    from_json(tc_json, COLLECTION_NUMBER, fp.collection_number, collected_issues, RT::project_text_configuration_collection_number_issue);
    from_json(tc_json, FONT_FAMILY      , fp.family           , collected_issues, RT::project_text_configuration_font_family_issue);
    from_json(tc_json, FONT_FACE_NAME   , fp.face_name        , collected_issues, RT::project_text_configuration_font_face_name_issue);
    from_json(tc_json, FONT_STYLE       , fp.style            , collected_issues, RT::project_text_configuration_font_face_style_issue);
    from_json(tc_json, FONT_WEIGHT      , fp.weight           , collected_issues, RT::project_text_configuration_font_weight_issue);        
}
} // namespace TextConfigurationSerialization

namespace EmbossShapeSerialization {
// Store / load of EmbossShape
constexpr std::string_view SHAPE_SCALE = "scale";
constexpr std::string_view UNHEALED = "unhealed";
constexpr std::string_view SVG_FILE_PATH = "filepath";
constexpr std::string_view SVG_FILE_PATH_IN_3MF = "filepath3mf";

// EmbossProjection
constexpr std::string_view DEPTH = "depth";
constexpr std::string_view USE_SURFACE = "use_surface";

const NamesType NAMES = {{SHAPE_SCALE, UNHEALED, SVG_FILE_PATH, SVG_FILE_PATH_IN_3MF, DEPTH, USE_SURFACE}};

void write_svg_files(mz_zip_archive &archive, const Domain::Model &model) {
    // write only first appear of svg
    std::set<std::string> paths;
    for (const ModelObject *mo_ptr : model.objects) {
        if (mo_ptr == nullptr)
            continue;
        for (const ModelVolume *mv_ptr : mo_ptr->volumes) {
            if (mv_ptr == nullptr)
                continue;
            if (!mv_ptr->emboss_shape.has_value())
                continue;
            const EmbossShape &es = *mv_ptr->emboss_shape;
            if (!es.svg_file.has_value())
                continue;
            const EmbossShape::SvgFile &svg = *es.svg_file;
            std::shared_ptr<std::string> file_data = svg.file_data; // copy pointer
            if (file_data == nullptr && !svg.path.empty()) {
                file_data = Biz::Emboss::read_from_disk(svg.path);
                if (file_data == nullptr)
                    SPDLOG_WARN("Can't load svg file from path: {}", svg.path);
            }
            if (file_data == nullptr)
                continue; // Text configuration do not have svg file

            // This can appear by pressing [ctr]+[c] continued [ctrl]+[v] of loaded svg from 3mf
            if (!paths.insert(svg.path_in_3mf).second)
                continue; // file is already stored in 3mf
            write_svg_file(archive, svg.path_in_3mf, *file_data);
        }
    }
}

json to_json(const EmbossShape &es) {
    json result = json::object();
    // path_in_3mf .. empty mean shape is EmbossedText OR unwanted store .svg file into .3mf
    // (protection of copyRight)
    if (es.svg_file.has_value() && !es.svg_file->path_in_3mf.empty()) {
        const EmbossShape::SvgFile &svg = *es.svg_file;
        if (!svg.path.empty())
            result[SVG_FILE_PATH] = boost::filesystem::path(svg.path).generic_string();
        result[SVG_FILE_PATH_IN_3MF] = boost::filesystem::path(svg.path_in_3mf).generic_string();
        // INFO: svg file content is stored at begining by function write_svg_files
    }

    result[SHAPE_SCALE] = es.scale;
    if (!es.final_shape.is_healed)
        result[UNHEALED] = true;

    // projection
    const EmbossProjection &p = es.projection;
    result[DEPTH] = p.depth;
    if (p.use_surface)
        result[USE_SURFACE] = true;
    return result;
}
void load(const json &es_json, EmbossShape &es, Read3mfIssues& collected_issues) {
    if (!is_valid(es_json, NAMES, collected_issues, RT::project_emboss_shape_unknown_property))
        return;

    EmbossShape::SvgFile svg;
    bool is_source = from_json(es_json, SVG_FILE_PATH, svg.path, collected_issues, RT::project_emboss_shape_svg_file_path_issue);
    is_source |= from_json(es_json, SVG_FILE_PATH_IN_3MF, svg.path_in_3mf, collected_issues, RT::project_emboss_shape_svg_file_path_in_3mf_issue);
    if (is_source)
        es.svg_file = std::move(svg);

    from_json(es_json, SHAPE_SCALE, es.scale, collected_issues, RT::project_emboss_shape_scale_issue, true);
    
    if(bool is_unhealed = false;
        from_json(es_json, UNHEALED, is_unhealed, collected_issues, RT::project_emboss_shape_is_unhealed_issue)) {
        assert(is_unhealed);
        es.final_shape.is_healed = false;
    }

    EmbossProjection &p = es.projection;
    from_json(es_json, DEPTH, p.depth, collected_issues, RT::project_emboss_shape_depth_issue, true);
    from_json(es_json, USE_SURFACE, p.use_surface, collected_issues, RT::project_emboss_shape_use_surface_issue);
}
} // namespace EmbossShapeSerialization

namespace SourceSerialization {

constexpr std::string_view FILEPATH = "filepath";
constexpr std::string_view OFFSET = "offset";
constexpr std::string_view IS_FROM_INCH = "isFromInch";
constexpr std::string_view IS_FROM_METERS = "isFromMeters";
constexpr std::string_view OBJECT_INDEX = "objectIdx"; // index into ModelObjects loaded from 3mf
constexpr std::string_view VOLUME_INDEX = "volumeIdx"; // index into ModelVolumes loaded from 3mf

constexpr std::string_view REPAIR = "repair";
constexpr std::string_view EDGE_FIXED = "edgeFixed";
constexpr std::string_view DEGENERATE_FACETS = "degenerateFacets";
constexpr std::string_view FACETS_REMOVED = "facetsRemoved";
constexpr std::string_view FACETS_REVERSED = "facetsReversed";
constexpr std::string_view BACKWARDED_EDGES = "backwardsEdges";

const NamesType SOURCE_NAMES = {{FILEPATH, OFFSET, IS_FROM_INCH, IS_FROM_METERS, OBJECT_INDEX, VOLUME_INDEX, REPAIR}};
const NamesType REPAIR_NAMES = {{EDGE_FIXED, DEGENERATE_FACETS, FACETS_REMOVED, FACETS_REVERSED, BACKWARDED_EDGES}};
json to_json(const Domain::RepairedMeshErrors &rme) {
    json r;
    if (rme.edges_fixed != 0)
        r[EDGE_FIXED] = rme.edges_fixed;
    if (rme.degenerate_facets != 0)
        r[DEGENERATE_FACETS] = rme.degenerate_facets;
    if (rme.facets_removed != 0)
        r[FACETS_REMOVED] = rme.facets_removed;
    if (rme.facets_reversed != 0)
        r[FACETS_REVERSED] = rme.facets_reversed;
    if (rme.backwards_edges != 0)
        r[BACKWARDED_EDGES] = rme.backwards_edges;
    return r;
}

json to_json(const ModelVolume::Source &source, const Domain::TriangleMeshStats &stats) {
    json r;
    if (!source.input_file.empty())
        // unify direction of slashes
        r[FILEPATH] = boost::filesystem::path(source.input_file).generic_string();
    
    if (!source.mesh_offset.isApprox(Vec3d::Zero()))
        r[OFFSET] = ::to_json(source.mesh_offset);

    // Adress into multi source(like 3mf) model
    r[OBJECT_INDEX] = source.object_idx;
    r[VOLUME_INDEX] = source.volume_idx;

    // can't be both but data allowed it.
    // TODO: fix it inside source to one variable model unit
    assert(!(source.is_converted_from_inches && source.is_converted_from_meters));
    if (source.is_converted_from_inches) {
        r[IS_FROM_INCH] = true;
    } else if (source.is_converted_from_meters) {
        r[IS_FROM_METERS] = true;
    }
    add(r, REPAIR, to_json(stats.repaired_errors));
    return r;
}

void load(const json &source_json, ModelVolume::Source &source, Domain::TriangleMeshStats &stats, Read3mfIssues& collected_issues) {
    if (!is_valid(source_json, SOURCE_NAMES, collected_issues, RT::project_source_unknown_property))
        return;

    from_json(source_json, FILEPATH,     source.input_file,  collected_issues, RT::project_source_filepath_issue);
    from_json(source_json, OFFSET,       source.mesh_offset, collected_issues, RT::project_source_offset_issue);
    from_json(source_json, OBJECT_INDEX, source.object_idx,  collected_issues, RT::project_source_object_idx_issue);
    from_json(source_json, VOLUME_INDEX, source.volume_idx,  collected_issues, RT::project_source_volume_idx_issue);
    if(!from_json(source_json, IS_FROM_INCH,   source.is_converted_from_inches, collected_issues, RT::project_source_is_from_inch_issue))
        from_json(source_json, IS_FROM_METERS, source.is_converted_from_meters, collected_issues, RT::project_source_is_from_meters_issue);

    if (auto repair_json_it = source_json.find(REPAIR); repair_json_it != source_json.end()) {
        const json &repair_json = *repair_json_it;
        if(!is_valid(repair_json, REPAIR_NAMES, collected_issues, RT::project_source_repair_issue))
            return;

        Domain::RepairedMeshErrors &rme = stats.repaired_errors;
        from_json(repair_json, EDGE_FIXED,        rme.edges_fixed,       collected_issues, RT::project_repair_edge_fixed_issue);
        from_json(repair_json, DEGENERATE_FACETS, rme.degenerate_facets, collected_issues, RT::project_repair_degenerate_facets_issue);
        from_json(repair_json, FACETS_REMOVED,    rme.facets_removed,    collected_issues, RT::project_repair_facets_removed_issue);
        from_json(repair_json, FACETS_REVERSED,   rme.facets_reversed,   collected_issues, RT::project_repair_facets_reversed_issue);
        from_json(repair_json, BACKWARDED_EDGES,  rme.backwards_edges,   collected_issues, RT::project_repair_backwards_edges_issue);
    }
}
} // namespace SourceSerialization

namespace FacetsAnnotationSerialization {
constexpr const char* FACETS_ANNOTATION_FILE = "Metadata/Slic3r_facets_annotation.json";
constexpr std::string_view ID                = "id";

constexpr std::string_view MM_SEGMENTATION_FACETS             = "mmSegmentationFacets";
constexpr std::string_view MM_SEGMENTATION_FACETS_VERSION_KEY = "mmSegmentationFacetsVersion";
constexpr std::string_view SUPPORT_FACETS                     = "supportedFacets";
constexpr std::string_view SUPPORT_FACETS_VERSION_KEY         = "supportedFacetsVersion";
constexpr std::string_view SEAM_FACETS                        = "seamFacets";
constexpr std::string_view SEAM_FACETS_VERSION_KEY            = "seamFacetsVersion";
constexpr std::string_view FUZZY_SKIN_FACETS                  = "fuzzySkinFacets";
constexpr std::string_view FUZZY_SKIN_FACETS_VERSION_KEY      = "fuzzySkinFacetsVersion";

NamesType FACETS_NAMES{
    {ID,
     MM_SEGMENTATION_FACETS,
     SUPPORT_FACETS,
     SEAM_FACETS,
     FUZZY_SKIN_FACETS,
     MM_SEGMENTATION_FACETS_VERSION_KEY,
     SUPPORT_FACETS_VERSION_KEY,
     SEAM_FACETS_VERSION_KEY,
     FUZZY_SKIN_FACETS_VERSION_KEY}
};

/**
 * Painting data version, stored next to each facet set.
 * Version 1 : Initial version (triangle states 0-16).
 * Version 2 : Extended support up to 255 triangle states.
 *
 * These constants are the maximum painting versions this slicer is able to read. The version
 * written into the file is not these constants but the minimum version required by the actual
 * data (see minimum_required_painting_version()), so files stay readable by older slicers
 * whenever possible.
 */
constexpr unsigned int MM_SEGMENTATION_FACETS_VERSION = 2;
constexpr unsigned int SUPPORT_FACETS_VERSION         = 2;
constexpr unsigned int SEAM_FACETS_VERSION            = 2;
constexpr unsigned int FUZZY_SKIN_FACETS_VERSION      = 2;

constexpr std::string_view TRIANGLE = "triangle"; // index into mesh(specifiead by ID) triangles
constexpr std::string_view DIVIDING = "dividing";
NamesType FACET_NAMES{{TRIANGLE, DIVIDING}};
void write(mz_zip_archive &archive, const Domain::Model &model, const VolumeToObjectid &v2id) {
    auto facets_to_json = [](const Domain::FacetsAnnotation &facets, int triangle_count) {
        if (facets.empty())
            return json{};

        json result = json::array();
        // Should be unique in archive
        for (int i = 0; i < triangle_count; ++i) {
            std::string data = facets.get_triangle_as_string(i);
            if (data.empty())
                continue;
            json t;
            t[TRIANGLE] = i;
            t[DIVIDING] = data;
            result.push_back(std::move(t));
        }
        return result;
    };

    /**
     * Store the painting together with its version, but only when the painting is not empty.
     * The version is derived from the data itself, so paintings that use only triangle states
     * representable by an older version stay readable by older slicers.
     */
    auto add_painting = [&facets_to_json](
                            json& facet_json,
                            const FacetsAnnotation& facets,
                            std::string_view name,
                            std::string_view version_key,
                            int triangle_count
                        )
    {
        if (add(facet_json, name, facets_to_json(facets, triangle_count))) {
            facet_json[version_key] = facets.get_data().minimum_required_painting_version();
        }
    };

    json facets_json = json::array();
    for (const ModelObject *mo : model.objects) {
        for (const ModelVolume *mv : mo->volumes) {
            auto it = v2id.find(mv);
            if (it == v2id.end())
                continue;
            unsigned id = it->second;
            int triangle_count = static_cast<int>(mv->mesh().its.indices.size());
            json facet_json;
            add_painting(
                facet_json,
                mv->mm_segmentation_facets,
                MM_SEGMENTATION_FACETS,
                MM_SEGMENTATION_FACETS_VERSION_KEY,
                triangle_count
            );
            add_painting(
                facet_json,
                mv->supported_facets,
                SUPPORT_FACETS,
                SUPPORT_FACETS_VERSION_KEY,
                triangle_count
            );
            add_painting(
                facet_json,
                mv->seam_facets,
                SEAM_FACETS,
                SEAM_FACETS_VERSION_KEY,
                triangle_count
            );
            add_painting(
                facet_json,
                mv->fuzzy_skin_facets,
                FUZZY_SKIN_FACETS,
                FUZZY_SKIN_FACETS_VERSION_KEY,
                triangle_count
            );

            if (facet_json.empty())
                continue;
            facet_json[ID] = id;
            facets_json.push_back(std::move(facet_json));
        }
    }
    write_file(archive, facets_json, FACETS_ANNOTATION_FILE);
}

/**
 * @brief Returns whether the painting's stored version can be read.
 *
 * A missing version key defaults to version 1 because older 3MF files were mistakenly written
 * without it.
 *
 * @param version_key Key under which the painting's version is stored.
 * @param max_supported_version Maximum painting version this slicer is able to read.
 */
bool is_painting_version_supported(
    const json& volume_facets,
    std::string_view version_key,
    unsigned int max_supported_version
)
{
    return volume_facets.value(version_key, 1u) <= max_supported_version;
}

void load(const json &facets_json_arr, const VolumeMap &volume_map, Read3mfIssues& collected_issues) {
    if (!facets_json_arr.is_array()) {
        collected_issues.add_issue(Read3mfIssue(RT::facets_must_be_array));
        return;
    }

    auto json_to_facets = [&collected_issues](
                              const json& facets_json,
                              FacetsAnnotation& facets,
                              const size_t mesh_facets_count
                          )
    {
        if (!facets_json.is_array())
            return;

        facets.reserve(static_cast<int>(facets_json.size()));
        for (const auto& facet_json: facets_json){
            if(!is_valid(facet_json, FACET_NAMES, collected_issues, RT::facets_unknown_facet_key))
                continue;

            unsigned triangle_index = 0;
            if (!from_json(facet_json, TRIANGLE, triangle_index, collected_issues, RT::facets_triangle_id_issue, true))
                continue;
            std::string data;
            if (!from_json(facet_json, DIVIDING, data, collected_issues, RT::facets_dividing_data_issue, true))
                continue;

            // TODO: check setting invalid data !!!
            facets.set_triangle_from_string(triangle_index, data);
        }
        facets.shrink_to_fit();

        TriangleSplittingData& splitting_data = facets.triangle_splitting_data;
        const std::vector<TriangleBitStreamMapping>& triangles_to_split =
            splitting_data.triangles_to_split;

        // Unpainted facets aren't serialized, so fewer deserialized triangles_to_split than mesh
        // facets means that some facet keeps the default (NONE) state.
        if (!triangles_to_split.empty() && triangles_to_split.size() < mesh_facets_count) {
            splitting_data.used_states[static_cast<size_t>(TriangleStateType::NONE)] = true;
        }
    };

    // Localized names of paintings skipped because of an unsupported version, shown to the user in the warning dialog.
    std::set<std::string> skipped_painting_names;

    /**
     * Load one painting of all volumes sharing the mesh, or record its name into
     * skipped_painting_names when its stored version is unsupported.
     */
    auto load_painting = [&json_to_facets, &skipped_painting_names](
                             const json& volume_facets,
                             const ModelVolumePtrs& volumes,
                             std::string_view facets_key,
                             std::string_view version_key,
                             unsigned int max_supported_version,
                             const std::string& name,
                             FacetsAnnotation ModelVolume::* facets_member
                         )
    {
        if (!volume_facets.contains(facets_key)) {
            return;
        }

        if (!is_painting_version_supported(volume_facets, version_key, max_supported_version)) {
            skipped_painting_names.insert(name);
            return;
        }

        for (ModelVolume* mv : volumes) {
            json_to_facets(volume_facets[facets_key], mv->*facets_member, mv->mesh().facets_count());
        }
    };

    for (const auto &volume_facets : facets_json_arr) {
        if (!is_valid(volume_facets, FACETS_NAMES, collected_issues, RT::facets_unknown_type))
            continue;
        int id = volume_facets.value(ID, -1);
        if (id < 0) {
            collected_issues.add_issue(Read3mfIssue(RT::facets_cant_identify_source, volume_facets.dump()));
            continue;
        }
        PathId path_id{static_cast<format_3MF::ST_ResourceID>(id)};
        auto it = volume_map.find(path_id);
        if (it == volume_map.end()) {
            collected_issues.add_issue(Read3mfIssue(RT::facets_bad_id, std::to_string(id)));
            continue;
        }

        const ModelVolumePtrs& volumes = it->second;
        load_painting(
            volume_facets,
            volumes,
            MM_SEGMENTATION_FACETS,
            MM_SEGMENTATION_FACETS_VERSION_KEY,
            MM_SEGMENTATION_FACETS_VERSION,
            _u8L("multi-material"),
            &ModelVolume::mm_segmentation_facets
        );
        load_painting(
            volume_facets,
            volumes,
            SUPPORT_FACETS,
            SUPPORT_FACETS_VERSION_KEY,
            SUPPORT_FACETS_VERSION,
            _u8L("FDM supports"),
            &ModelVolume::supported_facets
        );
        load_painting(
            volume_facets,
            volumes,
            SEAM_FACETS,
            SEAM_FACETS_VERSION_KEY,
            SEAM_FACETS_VERSION,
            _u8L("seam"),
            &ModelVolume::seam_facets
        );
        load_painting(
            volume_facets,
            volumes,
            FUZZY_SKIN_FACETS,
            FUZZY_SKIN_FACETS_VERSION_KEY,
            FUZZY_SKIN_FACETS_VERSION,
            _u8L("fuzzy skin"),
            &ModelVolume::fuzzy_skin_facets
        );
    }

    for (const std::string& painting_name : skipped_painting_names) {
        collected_issues.add_issue(
            Read3mfIssue(RT::facets_newer_version_skipped, std::nullopt, painting_name)
        );
    }
}
} // namespace FacetsAnnotationSerialization

// copy _3MF_Exporter::_add_layer_height_profile_file_to_archive
namespace LayerHeightProfileSerialization {
json to_json(const Domain::ZHeightPairs& layer_height_profile)
{
    if (layer_height_profile.empty())
        return {}; // Not used for this object

    assert(layer_height_profile.size() >= 2);
    if (layer_height_profile.size() < 2) {
        return {}; // bad layer height
    }

    // layer_height_profile is list of pair<z, layer_height>
    json z_height_pairs = json::array();
    for (const Domain::ZHeightPair& z_height_pair : layer_height_profile) {
        z_height_pairs.push_back({z_height_pair.z, z_height_pair.layer_height});
    }

    return z_height_pairs;
}

Domain::ZHeightPairs load(const json& layer_heights_json, Read3mfIssues& collected_issues)
{
    if (!layer_heights_json.is_array()) {
        collected_issues.add_issue(Read3mfIssue(RT::layer_heights_must_be_array));
        return {};
    }

    Domain::ZHeightPairs layer_heights;
    layer_heights.reserve(layer_heights_json.size());
    for (const json& z_height_pair : layer_heights_json) {
        if (!z_height_pair.is_array()) {
            collected_issues.add_issue(
                Read3mfIssue(RT::layer_heights_must_be_array_of_pairs, "no array")
            );
            return {};
        }

        if (z_height_pair.size() != 2) {
            collected_issues.add_issue(Read3mfIssue(
                RT::layer_heights_must_be_array_of_pairs,
                std::to_string(z_height_pair.size())
            ));
            return {};
        }

        layer_heights.push_back({z_height_pair[0].get<double>(), z_height_pair[1].get<double>()});
    }

    return layer_heights;
}
} // namespace LayerHeightProfileSerialization

namespace CutSerialization {

using CutConnectorType = Domain::CutConnectorType;
using CutId = Domain::CutId;

constexpr std::string_view CUT_TYPE = "type";
constexpr std::string_view R_TOLERANCE = "rTolerance";
constexpr std::string_view H_TOLERANCE = "hTolerance";
const NamesType NAMES = {{CUT_TYPE, R_TOLERANCE, H_TOLERANCE}};
using CutConnectorTypeToName = boost::bimap<CutConnectorType, std::string_view>;
const CutConnectorTypeToName cut_type_to_name = 
    boost::assign::list_of<CutConnectorTypeToName::relation>
    (CutConnectorType::Plug, "plug")
    (CutConnectorType::Dowel, "dowel")
    (CutConnectorType::Snap, "snap")
    (CutConnectorType::Undef, "undef"); // TODO: why undef is needed?

json cut_to_json(const ModelVolume::CutInfo &cut_info) {
    json cut_json;
    cut_json[CUT_TYPE] = to_json(cut_info.connector_type, cut_type_to_name);
    cut_json[R_TOLERANCE] = cut_info.radius_tolerance;
    cut_json[H_TOLERANCE] = cut_info.height_tolerance;
    return cut_json;
}

void load(const json &cut_info_json, ModelVolume::CutInfo &cut_info, Read3mfIssues& collected_issues) {
    if (!is_valid(cut_info_json, NAMES, collected_issues, RT::project_cut_info_unknown_property))
        return;
    from_json(cut_info_json, CUT_TYPE, cut_info.connector_type, cut_type_to_name, collected_issues, RT::project_cut_info_type_issue, true);
    from_json(cut_info_json, R_TOLERANCE, cut_info.radius_tolerance, collected_issues, RT::project_cut_info_radius_tolerance_issue, true);
    from_json(cut_info_json, H_TOLERANCE, cut_info.height_tolerance, collected_issues, RT::project_cut_info_height_tolerance_issue, true);
}

constexpr std::string_view CUT_ID = "cutId";
constexpr std::string_view CHECK_SUM = "checkSum";
constexpr std::string_view CONNECTORS_CNT = "connectorsCnt";
const NamesType NAMES_ID = {{CUT_TYPE, R_TOLERANCE, H_TOLERANCE}};

json cut_to_json(const CutId& cut) {
    if (!cut.valid())
        return {};

    json cut_json;
    cut_json[CUT_ID] = cut.id();
    cut_json[CHECK_SUM] = cut.check_sum();
    cut_json[CONNECTORS_CNT] = cut.connectors_cnt();
    return cut_json;
}

void load(const json &cut_object_json, CutId &cut, Read3mfIssues& collected_issues){
    if (!is_valid(cut_object_json, NAMES_ID, collected_issues, RT::project_cut_info_unknown_property))
        return;

    size_t id, checksum, cnt;
    if(!from_json(cut_object_json, CUT_ID        , id      , collected_issues, RT::project_cut_object_id_issue, true)) return;
    if(!from_json(cut_object_json, CHECK_SUM     , checksum, collected_issues, RT::project_cut_object_checksum_issue, true)) return;
    if(!from_json(cut_object_json, CONNECTORS_CNT, cnt     , collected_issues, RT::project_cut_object_connector_count_issue, true)) return;
    cut = CutId(id, checksum, cnt);
}
} // namespace CutSerialization

namespace VolumeSerialization {
constexpr std::string_view ID                 = "id";                // Object_id from 3mf
constexpr std::string_view VOLUME_UUID        = "object_uuid";       // 3mf Object uuid
constexpr std::string_view VOLUME_TYPE        = "type";              // Slic3r specification of volume type
constexpr std::string_view VOLUME_SETTINGS    = "volume_settings";   // volume specific configuration
constexpr std::string_view TEXT_CONFIGURATION = "textConfiguration"; // more in TextConfigurationSerialization
constexpr std::string_view SHAPE              = "shape";             // more in EmbossShapeSerialization
constexpr std::string_view SOURCE             = "source";            // more in SourceSerialization
constexpr std::string_view CUT_INFO           = "cutInfo";           // more in CutSerialization

NamesType VOLUME_NAMES{{ID, VOLUME_UUID, VOLUME_TYPE, VOLUME_SETTINGS, SOURCE, 
                        TEXT_CONFIGURATION, SHAPE, CUT_INFO}};

using VolumeTypeToName = boost::bimap<ModelVolumeType, std::string_view>;
const VolumeTypeToName volume_type_to_name = boost::assign::list_of<VolumeTypeToName::relation>
    (ModelVolumeType::MODEL_PART,         "ModelPart") // default value
    (ModelVolumeType::NEGATIVE_VOLUME,    "NegativeVolume")
    (ModelVolumeType::PARAMETER_MODIFIER, "ParameterModifier")
    (ModelVolumeType::SUPPORT_BLOCKER,    "SupportBlocker")
    (ModelVolumeType::SUPPORT_ENFORCER,   "SupportEnforcer");

json volumes_to_json(const ModelVolumePtrs &volumes, const VolumeToObjectid &v2id) {
    json volumes_json = json::array();
    for (const ModelVolume *volume_ptr : volumes) {
        const ModelVolume &volume = *volume_ptr;
        json volume_json;
        { // write object id for volume
            auto it = v2id.find(volume_ptr);
            // id is created during writing .model file into 3mf
            assert(it != v2id.end());
            if (it == v2id.end())
                continue;
            volume_json[ID] = it->second;
        }
        //if (auto volume_uuid_it = find_by_id(volumes_uuid, volume.id().id);
        //    volume_uuid_it != volumes_uuid.cend())
        //    volume_json[VOLUME_UUID] = volume_uuid_it->object_uuid;

        volume_json[VOLUME_TYPE] = to_json(volume.type(), volume_type_to_name);        
        if (volume.text_configuration.has_value())
            add(volume_json, TEXT_CONFIGURATION,
                TextConfigurationSerialization::to_json(*volume.text_configuration));
        if (volume.emboss_shape.has_value())
            add(volume_json, SHAPE, EmbossShapeSerialization::to_json(*volume.emboss_shape));
        add(volume_json, SOURCE, SourceSerialization::to_json(volume.source, volume.mesh().stats()));
        add(volume_json, VOLUME_SETTINGS, nlohmann::ordered_json(volume.volume_settings));
        if (volume.is_cut_connector())
            add(volume_json, CUT_INFO, CutSerialization::cut_to_json(volume.cut_info));
        if (!volume_json.empty())
            volumes_json.push_back(std::move(volume_json));
    }
    return volumes_json;
}

void load_volume(const json &volume_json, const VolumeMap &volume_map, Read3mfIssues& collected_issues) {
    if(!is_valid(volume_json, VOLUME_NAMES, collected_issues, RT::project_volume_unknown_property))
        return;

    int id = volume_json.value(ID, -1);
    if (id < 0) {
        collected_issues.add_issue(Read3mfIssue(RT::project_volume_missing_id, volume_json.dump()));
        return;
    }
    PathId path_id{static_cast<format_3MF::ST_ResourceID>(id)};
    auto it = volume_map.find(path_id);
    if (it == volume_map.end()) {
        collected_issues.add_issue(Read3mfIssue(RT::project_volume_bad_id, std::to_string(id)));
        return;
    }

    ModelVolumePtrs mvs = it->second;
    if (auto volume_type_it = volume_json.find(VOLUME_TYPE);
        volume_type_it != volume_json.end()){
        std::string volume_type_str = static_cast<std::string>(*volume_type_it);
        const auto &m = volume_type_to_name.right;
        auto type_it = m.find(volume_type_str);
        // type must be defined inside map
        assert(type_it != m.end());
        if (type_it == m.end()) {
            // use default value
            collected_issues.add_issue(Read3mfIssue(RT::project_volume_unknown_type, volume_type_str));
        } else {
            ModelVolumeType type = type_it->second;
            for (ModelVolume *mv : mvs)
                mv->set_type(type);
        }
    }

    
    for (ModelVolume* mv : mvs) {
        Domain::VolumeSettings volume_settings;
        auto issues = Biz::Config::load_box(volume_json[VOLUME_SETTINGS], volume_settings);
        mv->volume_settings = volume_settings;
        if (! issues.empty()) {
            // TODO Handle errors.
            // RT issue = RT::project_volume_config_issue;
        }
    }
  
    if (auto source_json_it = volume_json.find(SOURCE);
        source_json_it != volume_json.end()){
        // Volumes in mvs may share the same underlying TriangleMesh via shared_ptr.
        // Group by raw mesh pointer so geometry is copied at most once per unique mesh.
        std::unordered_map<const Domain::TriangleMesh*, std::shared_ptr<const Domain::TriangleMesh>> mesh_cache;
        for (ModelVolume *mv : mvs) {
            Domain::TriangleMeshStats stats = mv->mesh().stats();
            SourceSerialization::load(*source_json_it, mv->source, stats, collected_issues);
            const Domain::TriangleMesh* old_ptr = &mv->mesh();
            auto it = mesh_cache.find(old_ptr);
            if (it == mesh_cache.end()) {
                it = mesh_cache.emplace(old_ptr,
                    std::make_shared<const Domain::TriangleMesh>(old_ptr->its, std::move(stats))
                ).first;
            }
            mv->set_mesh(it->second);
        }
        // Propagate the new meshes to every other volume in the full volume_map that
        // still points to one of the old TriangleMesh objects. This preserves sharing:
        // volumes not covered by PrusaFile end up pointing to the same new mesh as their
        // PrusaFile-covered counterparts, so the file does not grow on re-save.
        for (const auto& [pid, vols] : volume_map) {
            for (ModelVolume* v : vols) {
                if (auto cache_it = mesh_cache.find(&v->mesh()); cache_it != mesh_cache.end())
                    v->set_mesh(cache_it->second);
            }
        }
    }

    if (auto tc_json_it = volume_json.find(TEXT_CONFIGURATION);
        tc_json_it != volume_json.end()){
        Domain::TextConfiguration tc;
        TextConfigurationSerialization::load(*tc_json_it, tc, collected_issues);
        for (ModelVolume *mv : mvs)
            mv->text_configuration = tc;
    }

    if (auto shape_json_it = volume_json.find(SHAPE);
        shape_json_it != volume_json.end()) {
        Domain::EmbossShape es;
        EmbossShapeSerialization::load(*shape_json_it, es, collected_issues);
        for (ModelVolume *mv : mvs)
            mv->emboss_shape = es;
    }

    if (auto cut_json_it = volume_json.find(CUT_INFO);
        cut_json_it != volume_json.end()) {
        ModelVolume::CutInfo cut_info;
        CutSerialization::load(*cut_json_it, cut_info, collected_issues);
        for (ModelVolume *mv : mvs)
            mv->cut_info = cut_info;
    }
}
} // namespace VolumeSerialization

namespace SlaSupportPointsSerialization {

constexpr std::string_view POSITION = "p";          // position of support point on Object
constexpr std::string_view HEAD_FRONT_RADIUS = "r"; // head front radius
constexpr std::string_view IS_NEW_ISLAND = "island";        // is new island
NamesType NAMES{{POSITION, HEAD_FRONT_RADIUS, IS_NEW_ISLAND}};

json to_json(const Domain::SLA::SupportPoints &points) {
    json r = json::array();
    for (const Domain::SLA::SupportPoint &p : points) {
        json p_json;
        p_json[POSITION] = ::to_json(p.pos);
        p_json[HEAD_FRONT_RADIUS] = p.head_front_radius;
        if (p.is_island())
            p_json[IS_NEW_ISLAND] = true;
        r.push_back(std::move(p_json));
    }
    return r;
}

void load(const json &pts_json, Domain::SLA::SupportPoints &pts, Read3mfIssues& collected_issues) {
    if (!pts_json.is_array()) {
        collected_issues.add_issue(Read3mfIssue(RT::project_sla_support_points_must_be_array));
        return;
    }
    pts.reserve(pts_json.size());
    for (const json &pt_json : pts_json) {
        if(!is_valid(pt_json, NAMES, collected_issues, RT::project_sla_support_point_unknown_property))
            continue;
        Domain::SLA::SupportPoint pt;
        bool is_island = pt.is_island();
        from_json(pt_json, POSITION,          pt.pos,               collected_issues, RT::project_sla_support_point_position_issue, true);
        from_json(pt_json, HEAD_FRONT_RADIUS, pt.head_front_radius, collected_issues, RT::project_sla_support_point_radius_issue, true);
        from_json(pt_json, IS_NEW_ISLAND,     is_island,            collected_issues, RT::project_sla_support_point_is_new_island_issue);
        if (is_island)
            pt.type = Domain::SLA::SupportPointType::island;
        pts.push_back(pt);
    }
}
} // namespace SlaSupportPointsSerialization

namespace SlaDrainHolesSerialization {
constexpr std::string_view POSITION = "position";
constexpr std::string_view NORMAL = "normal";
constexpr std::string_view RADIUS = "radius";
constexpr std::string_view HEIGHT = "height";
NamesType NAMES{{POSITION, NORMAL, RADIUS, HEIGHT}};
json to_json(const Domain::SLA::DrainHoles &holes) {
    json r = json::array();
    for (const Domain::SLA::DrainHole &h : holes) {
        json h_json;
        h_json[POSITION] = ::to_json(h.pos);
        h_json[NORMAL]   = ::to_json(h.normal);
        h_json[RADIUS]   = h.radius;
        h_json[HEIGHT]   = h.height;
        r.push_back(std::move(h_json));
    }
    return r;
}
void from_json(const json &holes_json, Domain::SLA::DrainHoles &holes, Read3mfIssues& collected_issues) {
    if (!holes_json.is_array()) {
        collected_issues.add_issue(Read3mfIssue(RT::project_sla_drain_holes_must_be_array));
        return;
    }
    holes.reserve(holes_json.size());
    for (const json &hole_json : holes_json) {
        if(!is_valid(hole_json, NAMES, collected_issues, RT::project_sla_drain_hole_unknown_property))
            continue;
        Domain::SLA::DrainHole hole;
        ::from_json(hole_json, POSITION, hole.pos,    collected_issues, RT::project_sla_drain_hole_position_issue, true);
        ::from_json(hole_json, NORMAL,   hole.normal, collected_issues, RT::project_sla_drain_hole_normal_issue, true);
        ::from_json(hole_json, RADIUS,   hole.radius, collected_issues, RT::project_sla_drain_hole_radius_issue, true);
        ::from_json(hole_json, HEIGHT,   hole.height, collected_issues, RT::project_sla_drain_hole_height_issue, true);
        holes.push_back(hole);
    }
}
} // namespace SlaDrainHolesSerialization

namespace RangesSerialization {

constexpr std::string_view Z_RANGE       = "zRange";        // Range of z values [from, to]
constexpr std::string_view CONFIGURATION = "configuration"; // Range configuration
NamesType RANGES_NAMES{{Z_RANGE, CONFIGURATION}};

json ranges_to_json(const Domain::LayerConfigRanges &ranges) {
    if (ranges.empty())
        return json{};

    json result = json::array();
    for (const auto &[range, config] : ranges) {
        ASSERT(range.first < range.second);
        json config_json = nlohmann::ordered_json(config);
        assert(!config_json.empty());
        if (config_json.empty())
            continue;

        json range_json;
        range_json[Z_RANGE] = range; //{range.first, range.second};
        range_json[CONFIGURATION] = config_json;
        result.push_back(std::move(range_json));
    }
    return result;
}

void ranges_from_json(const json &ranges_json, Domain::LayerConfigRanges &ranges,
    Read3mfIssues& collected_issues) {
    if (!ranges_json.is_array()) {
        collected_issues.add_issue(Read3mfIssue(RT::project_object_ranges_must_be_array));
        return;
    }
    if (ranges_json.empty()){
        collected_issues.add_issue(Read3mfIssue(RT::project_object_ranges_must_not_be_empty));
        return;
    }
    
    for (const json& range_json : ranges_json) {
        if(!is_valid(range_json, RANGES_NAMES, collected_issues, RT::project_object_range_unknown_property))
            continue;
        const json& z_range_json = range_json[Z_RANGE];
        if (!z_range_json.is_array() || z_range_json.size() != 2 ) {
            collected_issues.add_issue(Read3mfIssue(RT::project_object_range_bad_z1, z_range_json.dump()));
            continue;
        }
        Domain::LayerHeightRange z_range = range_json.value(Z_RANGE, Domain::LayerHeightRange(-1, -1));
        if (z_range.first > z_range.second || z_range.first < 0) {
            collected_issues.add_issue(Read3mfIssue(RT::project_object_range_bad_z2, z_range_json.dump()));
            continue;
        }

        ASSERT(range_json.contains(CONFIGURATION));
        Domain::VolumeSettings volume_settings;
        auto issues = Biz::Config::load_box(range_json[CONFIGURATION], volume_settings);
        ranges[z_range] = volume_settings;
        if (! issues.empty()) {
            // TODO Handle errors.
            // RT issue = RT::project_object_range_config_issue;
        }
    }
}
} // namespace RangesSerialization

namespace InstanceSerialization{
// identify instance --> .3mf/3D/3dmodel.model/model/build/item
// zero started indexed order of <item> tag inside <build>
constexpr std::string_view ORD = "ord";
constexpr std::string_view ITEM_UUID = "item_uuid"; 
constexpr std::string_view PRINTABLE = "printable"; // true is default and it is not written into 3mf
const NamesType NAMES{{ORD, ITEM_UUID, PRINTABLE}};

json instance_to_json(
    const ModelInstance &instance,
    const InstanceToBuildOrder &instances_map
    //const ItemsWithUUID &items_uuid
) {
    // fast skip til the instance keep only printability state
    if (instance.is_printable())
        return {};
    auto it = instances_map.find(instance.id().id);
    assert(it != instances_map.end()); // must be known 
    if (it == instances_map.end())
        return {};

    json instances_json = json::object();
    instances_json[ORD] = it->second;
    //if (auto item_uuid_it = find_by_id(items_uuid, instance.id().id);
    //    item_uuid_it != items_uuid.cend())
    //    instances_json[ITEM_UUID] = item_uuid_it->item_uuid;
    assert(!instance.is_printable());
    instances_json[PRINTABLE] = false;
    return instances_json;
}

void load_instance(const json &instance_json, const InstanceMap& instances, Read3mfIssues& collected_issues) {
    if (!is_valid(instance_json, NAMES, collected_issues, RT::project_instance_unknown_property))
        return;

    size_t ord;
    if (!from_json(instance_json, ORD, ord, collected_issues, RT::project_instance_order_issue, true))
        return;

    if (ord >= instances.size()) {
        collected_issues.add_issue(Read3mfIssue(RT::project_instance_order_out_of_range_issue, std::to_string(ord)));
        return;
    }

    ModelInstance *mi = instances[ord];
    from_json(instance_json, PRINTABLE, mi->printable, collected_issues, RT::project_instance_order_issue);
}

json instances_to_json(
    const Domain::ModelInstancePtrs &instances,
    const InstanceToBuildOrder &instances_map
    //const ItemsWithUUID& items_uuid
) {
    json instances_json = json::array();
    for (const ModelInstance *instance_ptr : instances) {
        assert(instance_ptr != nullptr);
        if (instance_ptr == nullptr)
            continue;
        json instance_json = instance_to_json(*instance_ptr, instances_map/*, items_uuid*/);
        if (!instance_json.empty())
            instances_json.push_back(std::move(instance_json));
    }
    return instances_json;
}
} // namespace InstanceSerialization

namespace ObjectsSerialization {
constexpr std::string_view ID                 = "id";             // Object_id from 3mf
constexpr std::string_view OBJECT_UUID        = "object_uuid";    // Universally Unique Identifier of object
constexpr std::string_view VOLUMES            = "volumes";        // List of volume settings
constexpr std::string_view INSTANCES          = "instances";      
constexpr std::string_view OBJECT_SETTINGS    = "object_settings";
constexpr std::string_view LAYER_HEIGHT_PROFILE="layerHeightProfile";
constexpr std::string_view RANGES             = "ranges";         // layer ranges configurations
constexpr std::string_view CUT_OBJECT_ID      = "cutId";          // more in CutSerialization
constexpr std::string_view SLA_SUPPORT_POINTS = "slaSupportPoints";
constexpr std::string_view SLA_DRAIN_HOLES    = "slaDrainHoles";

const NamesType OBJECT_NAMES{{ID, OBJECT_UUID, VOLUMES, INSTANCES, RANGES, CUT_OBJECT_ID, OBJECT_SETTINGS, 
                              LAYER_HEIGHT_PROFILE, SLA_SUPPORT_POINTS, SLA_DRAIN_HOLES}};

json object_to_json(const ModelObject &object, const StoredStructure &stored_structure) {
    const ObjectToObjectid &o2id = stored_structure.objects;
    auto it = o2id.find(object.id().id);
    if (it == o2id.end())
        return {};

    json object_json = json::object();
    object_json[ID] = it->second;

    // UUIDs are commented out for now.
    //const ObjectsWithUUID &objects_uuid = persist.objects_uuid;    
    //if (auto object_uuid_it = find_by_id(objects_uuid, object.id().id);
    //    object_uuid_it != objects_uuid.cend())
    //    object_json[OBJECT_UUID] = object_uuid_it->object_uuid;
    add(object_json, VOLUMES, VolumeSerialization::volumes_to_json(object.volumes, stored_structure.volumes/*, persist.volumes_uuid*/));
    add(object_json, INSTANCES, InstanceSerialization::instances_to_json(object.instances, stored_structure.instances/*, persist.items_uuid*/));
    if (!object.layer_config_ranges.empty())
        add(object_json, RANGES, RangesSerialization::ranges_to_json(object.layer_config_ranges));
    if (object.is_cut())
        add(object_json, CUT_OBJECT_ID, CutSerialization::cut_to_json(object.cut_id));
    add(object_json, OBJECT_SETTINGS, object.object_settings);
    if (const Domain::ZHeightPairs &layer_height_profile = object.layer_height_profile.get();
        !layer_height_profile.empty())
        add(object_json, LAYER_HEIGHT_PROFILE, LayerHeightProfileSerialization::to_json(layer_height_profile));
    if (!object.sla_support_points.empty())
        add(object_json, SLA_SUPPORT_POINTS, SlaSupportPointsSerialization::to_json(object.sla_support_points));
    if (!object.sla_drain_holes.empty())
        add(object_json, SLA_DRAIN_HOLES, SlaDrainHolesSerialization::to_json(object.sla_drain_holes));
    return object_json;
}

json objects_to_json(const Model &model, const StoredStructure &stored_structure)
{
    json objects_json = json::array();
    for (const ModelObject *object_ptr : model.objects) {
        assert(object_ptr != nullptr);
        if (object_ptr == nullptr)
            continue;
        json object_json = object_to_json(*object_ptr, stored_structure);
        if (!object_json.empty())
            objects_json.push_back(std::move(object_json));
    }
    return objects_json;
}

void load_objects(
    const json &parent_json,
    std::string_view name,
    const ModelMap &model_map,
    Read3mfIssues& collected_issues) {
    auto object_json_it = parent_json.find(name);
    if (object_json_it == parent_json.end())
        return; // no objects in json

    const json &objects_json = *object_json_it;
    if (!objects_json.is_array())
        collected_issues.add_issue(Read3mfIssue(RT::project_objects_must_be_array));

    const BuildMap &object_map = model_map.build;
    for (const json &object_json : objects_json) {
        if(!is_valid(object_json, OBJECT_NAMES, collected_issues, RT::project_object_unknown_property))
            continue;

        if(auto volumes_json = object_json.find(VOLUMES);
            volumes_json != object_json.end() &&
            !volumes_json->empty() &&
            volumes_json->is_array())
            for (const json& volume_json: *volumes_json)
                VolumeSerialization::load_volume(volume_json, model_map.volumes, collected_issues);

        if(auto instances_json = object_json.find(INSTANCES);
            instances_json != object_json.end() &&
            !instances_json->empty() &&
            instances_json->is_array())
            for (const json &instance_json : *instances_json)
                InstanceSerialization::load_instance(instance_json, model_map.instances, collected_issues);

        int id = object_json.value(ID, -1);
        if (id < 0) {
            collected_issues.add_issue(Read3mfIssue(RT::project_object_missing_id, object_json.dump()));
            continue;
        }
        PathId path_id{static_cast<format_3MF::ST_ResourceID>(id)};
        auto it = object_map.find(path_id);
        if (it == object_map.end()) {
            collected_issues.add_issue(Read3mfIssue(RT::project_object_bad_id, std::to_string(id)));
            continue;
        }
        Domain::ModelObjectPtrs mos = it->second;
        assert(!mos.empty());
        if (mos.empty())
            continue;        

        for (ModelObject* mo_ptr : mos) {
            if (! object_json.contains(OBJECT_SETTINGS) || ! object_json[OBJECT_SETTINGS].is_object())
                continue;
            Domain::ObjectSettings object_settings;
            auto issues = Biz::Config::load_box(object_json[OBJECT_SETTINGS], object_settings);
            mo_ptr->object_settings = object_settings;
            if (! issues.empty()) {
                // TODO Handle errors.
                // RT issue = RT::project_object_configuration_issue;
            }
        }

        ModelObject &mo = *mos.front(); // mos is not empty it is checked before
        Domain::ModelObjectPtrs mos_(mos.begin() + 1, mos.end()); // without first model object
        if (auto ranges_json = object_json.find(RANGES);
            ranges_json != object_json.end()) {
            RangesSerialization::ranges_from_json(*ranges_json, mo.layer_config_ranges, collected_issues);
            for (auto mo_ : mos_) // copy into other objects
                mo_->layer_config_ranges = mo.layer_config_ranges;
        }
        if (auto layer_height_profile_json = object_json.find(LAYER_HEIGHT_PROFILE);
            layer_height_profile_json != object_json.end()) {
            mo.layer_height_profile.set(LayerHeightProfileSerialization::load(*layer_height_profile_json, collected_issues));
            for (auto mo_ : mos_) // copy into other objects
                mo_->layer_height_profile.set(mo.layer_height_profile.get());
        }
        if (auto sla_points_json = object_json.find(SLA_SUPPORT_POINTS);
            sla_points_json != object_json.end()) {
            SlaSupportPointsSerialization::load(*sla_points_json, mo.sla_support_points, collected_issues);
            for (auto mo_ : mos_) // copy into other objects
                mo_->sla_support_points = mo.sla_support_points;
        }
        if (auto sla_holes_json = object_json.find(SLA_DRAIN_HOLES);
            sla_holes_json != object_json.end()) {
            SlaDrainHolesSerialization::from_json(*sla_holes_json, mo.sla_drain_holes, collected_issues);
            for (auto mo_ : mos_) // copy into other objects
                mo_->sla_drain_holes = mo.sla_drain_holes;
        }
        if (auto cut_object_json = object_json.find(CUT_OBJECT_ID);
            cut_object_json != object_json.end()) {
            CutSerialization::load(*cut_object_json, mo.cut_id, collected_issues);
            for (auto mo_ : mos_) // copy into other objects
                mo_->cut_id = mo.cut_id;
        }
    }
}
} // namespace ObjectsSerialization

struct ResultLoadJson {
    // index to file in archive to detect unprocessed files
    int file_index = -1;
    json parsed_json;
};

tl::expected<ResultLoadJson, Read3mfIssue> load_json(mz_zip_archive &archive, const char *filename, RT issue_type) {
    int facets_file_index = mz_zip_reader_locate_file(&archive, filename, nullptr, 0);
    if (facets_file_index < 0)
        return {}; // No facets stored in 3mf

    auto create_issue = [&](const std::string &name) -> Read3mfIssue {
        return Read3mfIssue(issue_type, name, std::string(filename), facets_file_index);
    };

    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive, facets_file_index, &stat))
        return tl::make_unexpected(create_issue("mz_zip_reader_file_stat"));

    if (stat.m_uncomp_size == 0)
        return tl::make_unexpected(create_issue("stat.m_uncomp_size == 0"));

    size_t uncomp_size = static_cast<size_t>(stat.m_uncomp_size);
    std::unique_ptr<char[]> buffer(new char[uncomp_size+1]);
    if (mz_zip_reader_extract_to_mem(&archive, facets_file_index, buffer.get(), uncomp_size, 0) !=
        MZ_TRUE)
        return tl::make_unexpected(create_issue("mz_zip_reader_extract_to_mem"));

    // json must be null terminated
    buffer[uncomp_size] = '\0';

    // Exceptions:
    //      Throws parse_error.101 in case of an unexpected token.
    //      Throws parse_error.102 if to_unicode fails or surrogate error.
    //      Throws parse_error.103 if to_unicode fails.
    // Notes: A UTF-8 byte order mark is silently ignored.
    ResultLoadJson result;
    result.file_index = facets_file_index;
    try {
        result.parsed_json = json::parse(buffer.get());
    } catch (const json::parse_error &e) {
        return tl::make_unexpected(create_issue(e.what()));
    }
    return result;
}

namespace ProjectFileSerialization {
using namespace Slic3r::Biz::Format::ProjectFileConstants;

NamesType PROJECT_NAMES{{PROJECT_METADATA, CONFIGURATION, PRESET_METADATA, OBJECTS}};

void write(
    mz_zip_archive &archive,
    const Model &model,
    const Domain::ProjectMetadata& project_metadata,
    const Domain::Project::ConfigContainerList& config_containers,
    const StoredStructure &stored_structure
) {
    json project_json = json::object();
    add(project_json, OBJECTS, ObjectsSerialization::objects_to_json(model, stored_structure));
    project_json[PROJECT_METADATA] = project_metadata;

    std::vector<nlohmann::json> all_containers_json;
    for (const auto& config_container : config_containers) {
        nlohmann::json cc_json;
        std::vector<nlohmann::json> beds_json;

        bool has_wipe_tower = false;
        const Domain::ConfigPack config_pack{config_container->build_print_config()};
        if (auto config_pack_fdm{std::get_if<Domain::ConfigPackFDM>(&config_pack)}) {
            const bool enabled{config_pack_fdm->print.items.opt("wipe_tower").get<bool>()};
            has_wipe_tower =
                config_container->selected_preset().hw_config.material_slot_count() > 1 && enabled;
        }

        for (const auto& bed_instance : config_container->bed_instances()) {
            const Vec3d& offset = bed_instance->transformation.get_offset();
            beds_json.emplace_back();
            beds_json.back()["position_x"] = offset.x();
            beds_json.back()["position_y"] = offset.y();

            if (has_wipe_tower) {
                beds_json.back()["wipe_tower"]["x"] = bed_instance->wipe_tower.position.x();
                beds_json.back()["wipe_tower"]["y"] = bed_instance->wipe_tower.position.y();
                beds_json.back()["wipe_tower"]["rotation_angle"] = bed_instance->wipe_tower.rotation;
            } else
                beds_json.back()["wipe_tower"] = nullptr;

            if (bed_instance->custom_gcode && !bed_instance->custom_gcode.value().gcodes.empty()) {
                const Domain::CustomGCode::Info& custom_gcode = bed_instance->custom_gcode.value();
                beds_json.back()["custom_gcode"]["mode"] = static_cast<int>(custom_gcode.mode);
                std::vector<nlohmann::json> gcodes_json;
                for (const Domain::CustomGCode::Item& gcode : custom_gcode.gcodes) {
                    gcodes_json.emplace_back();
                    gcodes_json.back()["gcode"]["type"] = static_cast<int>(gcode.type);
                    gcodes_json.back()["gcode"]["print_z"] = gcode.print_z;
                    gcodes_json.back()["gcode"]["extruder"] = gcode.extruder;
                    gcodes_json.back()["gcode"]["color"] = gcode.color;
                    gcodes_json.back()["gcode"]["extra"] = gcode.extra;
                }
                beds_json.back()["custom_gcode"]["gcodes"] = gcodes_json;
            } else
                beds_json.back()["custom_gcode"] = nullptr;

        }
        cc_json["beds"] = beds_json;

        cc_json[PRESET_METADATA] = nlohmann::ordered_json(config_container->selected_preset().metadata());

        if (!config_container->virtual_extruders().empty()) {
            std::vector<std::string> extruder_colors = config_container->project_settings()
                                                           .items.opt("extruder_colour")
                                                           .get<std::vector<std::string>>();
            extruder_colors.resize(
                config_container->selected_preset().hw_config.material_slot_count()
            );
            for (std::string& color : extruder_colors) {
                if (color.empty()) {
                    color = "#808080";
                }
            }

            cc_json[VIRTUAL_EXTRUDERS] =
                Biz::Format::VirtualExtruder::serialize_virtual_extruders_to_project_json(
                    extruder_colors,
                    config_container->virtual_extruders()
                );
        }

        const auto& cfg_var = config_container->build_print_config();
        if (std::holds_alternative<Domain::ConfigPackFDM>(cfg_var))
            cc_json[CONFIGURATION] = nlohmann::ordered_json(Domain::as_boxes(std::get<Domain::ConfigPackFDM>(cfg_var)));
        else if (std::holds_alternative<Domain::ConfigPackSLA>(cfg_var))
            cc_json[CONFIGURATION] = nlohmann::ordered_json(Domain::as_boxes(std::get<Domain::ConfigPackSLA>(cfg_var)));
        else
            PANIC();
        all_containers_json.emplace_back(cc_json);
    }
    project_json[CONFIG_CONTAINERS] = all_containers_json;

    if (project_json.empty())
        return;

    write_file(archive, project_json, std::string{PRUSA_PROJECT_FILEPATH}.c_str());
}

void load(
    const json &project_json,
    const ModelMap &model_map,
    Domain::ProjectMetadata& project_metadata,
    std::vector<Loaded3MF::ConfigContainerData>& config_containers_data,
    Read3mfIssues& collected_issues
) {
    config_containers_data.clear();

    if (!is_valid(project_json, PROJECT_NAMES, collected_issues, RT::project_unknown_type))
        return;

    ObjectsSerialization::load_objects(project_json, OBJECTS, model_map, collected_issues);

    if (project_json.contains(PROJECT_METADATA)) {
        const auto& pm = project_json.at(PROJECT_METADATA);
        from_json(pm, "id", project_metadata.id, collected_issues);
        from_json(pm, "version", project_metadata.version, collected_issues);
    } else {
        collected_issues.add_issue(RT::project_config_issue);
    }

    // TODO: handle multiple config containers, add check that the keys exist in the JSON.
    for (const nlohmann::ordered_json& config_container : project_json[CONFIG_CONTAINERS]) {
        config_containers_data.emplace_back();

        tl::expected<Biz::Config::PresetAndConfig, std::string> preset_and_config =
            Biz::Config::load_preset_and_config(config_container);
        if (!preset_and_config) {
            collected_issues.add_issue(RT::project_config_issue);
        } else {
            config_containers_data.back().preset = std::move(preset_and_config->preset_metadata);
            config_containers_data.back().config_pack = std::move(preset_and_config->config_pack);
        }

        if (config_container.contains(VIRTUAL_EXTRUDERS)) {
            const nlohmann::json virtual_extruders_json = config_container[VIRTUAL_EXTRUDERS];
            config_containers_data.back().virtual_extruders_config.virtual_extruders =
                Biz::Format::VirtualExtruder::deserialize_virtual_extruders_from_project_json(
                    virtual_extruders_json
                );
        }

        if (config_container.contains("beds")) {
            int bed_idx{ 0 };
            for (const nlohmann::ordered_json& bed : config_container["beds"]) {
                double x = 0.;
                double y = 0.;
                from_json(bed, "position_x", x, collected_issues);
                from_json(bed, "position_y", y, collected_issues);
                config_containers_data.back().bed_offsets.emplace_back(x, y);

                if (bed.contains("wipe_tower")) {
                    const nlohmann::ordered_json& wipe_tower = bed.at("wipe_tower");
                    double wt_x = 0.;
                    double wt_y = 0.;
                    double wt_rotation = 0.;
                    from_json(wipe_tower, "x", wt_x, collected_issues);
                    from_json(wipe_tower, "y", wt_y, collected_issues);
                    from_json(wipe_tower, "rotation_angle", wt_rotation, collected_issues);
                    Domain::ModelWipeTower wt;
                    wt.position = Domain::Vec2d(wt_x, wt_y);
                    wt.rotation = wt_rotation;
                    config_containers_data.back().wipe_towers[bed_idx] = wt;
                }

                if (bed.contains("custom_gcode")) {
                    const nlohmann::ordered_json& custom_gcode = bed.at("custom_gcode");
                    Domain::CustomGCode::Info custom_gcode_info;
                    size_t tmp_val;
                    from_json(custom_gcode, "mode", tmp_val, collected_issues);
                    custom_gcode_info.mode = static_cast<Domain::CustomGCode::Mode>(tmp_val);
                    if (custom_gcode.contains("gcodes")) {
                        for (const nlohmann::ordered_json& g : custom_gcode.at("gcodes")) {
                            const nlohmann::ordered_json& gcode = g.at("gcode");
                            Domain::CustomGCode::Item gcode_item;
                            from_json(gcode, "type", tmp_val, collected_issues);
                            gcode_item.type = static_cast<Domain::CustomGCode::Type>(tmp_val);
                            from_json(gcode, "extruder", tmp_val, collected_issues);
                            gcode_item.extruder = static_cast<int>(tmp_val);
                            from_json(gcode, "print_z", gcode_item.print_z, collected_issues);
                            from_json(gcode, "color", gcode_item.color, collected_issues);
                            from_json(gcode, "extra", gcode_item.extra, collected_issues);

                            custom_gcode_info.gcodes.emplace_back(gcode_item);
                        }
                    }
                    config_containers_data.back().custom_gcodes[bed_idx] = custom_gcode_info;
                }
                bed_idx++;
            }
        }
    }    
    
}
} // namespace ProjectFileSerialization
} // namespace

void Slic3r::store_prusa_files(
    mz_zip_archive &archive,
    const Model &model,
    const Domain::ProjectMetadata& project_metadata,
    const Domain::Project::ConfigContainerList& config_containers,
    const StoredStructure &stored_structure
) {
    FacetsAnnotationSerialization::write(archive, model, stored_structure.volumes);
    EmbossShapeSerialization::write_svg_files(archive, model);
    ProjectFileSerialization::write(archive, model, project_metadata, config_containers, stored_structure);
}

PrusaFilesResult Slic3r::load_prusa_files(
    mz_zip_archive &archive,
    const ModelMap &model_map,
    Read3mfIssues& collected_issues
) {
    PrusaFilesResult result;
    result.used_file_indices = std::vector<bool>(mz_zip_reader_get_num_files(&archive), {false});

    auto get_json = [&archive, &collected_issues, &result](const char *filename, RT err_type)->std::optional<json> {
        tl::expected<ResultLoadJson, Read3mfIssue> result_json = load_json(archive, filename, err_type);
        if (! result_json)
            collected_issues.add_issue(result_json.error());

        if (result_json.value().file_index < 0)
            return {};
        //result.used_file_indices[result_json.file_index] = true;
        if (result_json.value().parsed_json.empty())
            return {};
        return result_json.value().parsed_json;
    };

    if (std::optional<json> project_json = get_json(
            std::string{ProjectFileSerialization::PRUSA_PROJECT_FILEPATH}.c_str(),
            RT::project_file_is_corrupted
        );
        project_json.has_value())
    {
        ProjectFileSerialization::load(
            *project_json,
            model_map,
            result.project_metadata,
            result.config_containers_data,
            collected_issues
        );
    }

    if (std::optional<json> facets_json = get_json(FacetsAnnotationSerialization::FACETS_ANNOTATION_FILE, RT::facets_annotation_file_is_corrupted);
        facets_json.has_value()) 
        FacetsAnnotationSerialization::load(*facets_json, model_map.volumes, collected_issues);

    return result;
}

bool Slic3r::process_embossed_svg(
    mz_zip_archive &archive, const mz_zip_archive_file_stat &stat, 
    Slic3r::Domain::Model &model, Read3mfIssues& collected_issues) 
{
    std::shared_ptr<std::string> data = nullptr;
    std::string filename(stat.m_filename);

    for (const ModelObject *object : model.objects)
    for (ModelVolume *volume : object->volumes) {
        std::optional<EmbossShape> &es = volume->emboss_shape;
        if (!es.has_value())
            continue;
        std::optional<EmbossShape::SvgFile> &svg = es->svg_file;
        if (!svg.has_value())
            continue;

        if (filename.compare(svg->path_in_3mf) != 0)
            continue;

        if (data == nullptr) { // first useage --> load data
            auto file = std::make_unique<std::string>(stat.m_uncomp_size, '\0');
            mz_bool res  = mz_zip_reader_extract_to_mem(&archive, stat.m_file_index, (void *) file->data(), stat.m_uncomp_size, 0);
            if (res == 0) {
                collected_issues.add_issue(Read3mfIssue(RT::cant_load_zip_file, filename));
                // it is used svg file but can't be readed from archive.
                return true; 
            }
            data = std::move(file);
        }
        svg->file_data = data; // copy shared pointer
    }

    if (data == nullptr) {
        // SVG is not used as embossed shape
        return false;
    }
    return true;
}
