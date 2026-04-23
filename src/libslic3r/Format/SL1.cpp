///|/ Copyright (c) Prusa Research 2020 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SL1.hpp"

#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#include <sstream>

#include "libslic3r/Time.hpp"
#include "libslic3r/Zipper.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "libslic3r/miniz_extension.hpp" // IWYU pragma: keep
#include <LocalesUtils.hpp>
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Utils/JsonUtils.hpp"

#include "SLAArchiveReader.hpp"
#include "SLAArchiveFormatRegistry.hpp"
#include "ZipperArchiveImport.hpp"

#include "libslic3r/MarchingSquares.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Execution/ExecutionTBB.hpp"

#include "libslic3r/SLA/RasterBase.hpp"

#include <nlohmann/json.hpp>

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

namespace Slic3r {

using ConfMap = std::map<std::string, std::string>;

namespace {

std::string to_ini(const ConfMap &m)
{
    std::string ret;
    for (auto &param : m)
        ret += param.first + " = " + param.second + "\n";

    return ret;
}

// Not all keys have the same name in PS config and in SL1 / SLX config.json
// Also, some values need to be multiplied to convert to the right unit
// And they can be placed in different JSON hierarchy levels
struct ConfigKeyMapping {
    std::string target_key;   // Renamed key (e.g., "layerHeight")
    double multiplier;         // Value multiplier (e.g., 1000.0 for s->ms)
    std::string json_path;     // JSON location: "root", "exposure_profile", "tilt", or "" (no special placement)
};

static ConfigKeyMapping get_key_mapping(const std::string& opt_key)
{
    // SINGLE SOURCE OF TRUTH for all config key transformations
    static const std::map<std::string, ConfigKeyMapping> mappings = {
        // Config.ini entries (appear at root level in config.json)
        {"layer_height",             {"layerHeight",     1.0, "root"}},
        {"exposure_time",            {"expTime",         1.0, "root"}},
        {"min_exposure_time",        {"min_expTime",     1.0, "root"}},
        {"max_exposure_time",        {"max_expTime",     1.0, "root"}},
        {"initial_exposure_time",    {"expTimeFirst",    1.0, "root"}},
        {"min_initial_exposure_time",{"min_expTimeFirst",1.0, "root"}},
        {"max_initial_exposure_time",{"max_expTimeFirst",1.0, "root"}},
        {"material_print_speed",     {"expUserProfile",  1.0, "root"}},
        {"sla_material_settings_id", {"materialName",    1.0, "root"}},
        {"printer_model",            {"printerModel",    1.0, "root"}},
        {"printer_variant",          {"printerVariant",  1.0, "root"}},
        {"printer_settings_id",      {"printerProfile",  1.0, "root"}},
        {"sla_print_settings_id",    {"printProfile",    1.0, "root"}},
        {"faded_layers",             {"numFade",         1.0, "root"}},

        // Exposure profile entries
        {"area_fill",               {"area_fill",               1.0, "exposure_profile"}},
        {"printing_temperature",    {"printing_temperature",    1.0, "exposure_profile"}},

        // Tilt options - Time values (s -> ms: 1000x multiplier + _ms suffix)
        {"delay_before_exposure",       {"delay_before_exposure_ms",    1000.0, "tilt"}},
        {"delay_to_reflood",            {"delay_to_reflood_ms",         1000.0, "tilt"}},
        {"delay_after_exposure",        {"delay_after_exposure_ms",     1000.0, "tilt"}},
        {"tilt_down_offset_delay",      {"tilt_down_offset_delay_ms",   1000.0, "tilt"}},
        {"tilt_up_offset_delay",        {"tilt_up_offset_delay_ms",     1000.0, "tilt"}},
        {"tilt_down_delay",             {"tilt_down_delay_ms",          1000.0, "tilt"}},
        {"tilt_up_delay",               {"tilt_up_delay_ms",            1000.0, "tilt"}},
        {"dynamic_delay_before_timeout",{"dynamic_delay_before_timeout_ms", 1000.0, "tilt"}},

        // Tilt options - Distance values (mm -> nm: 1000000x multiplier + _nm suffix)
        {"tower_hop_height",        {"tower_hop_height_nm", 1000000.0, "tilt"}},

        // Tilt options - Speed values (_speed -> _profile replacement)
        {"tower_speed",                 {"tower_profile",               1.0, "tilt"}},
        {"tilt_down_initial_speed",     {"tilt_down_initial_profile",   1.0, "tilt"}},
        {"tilt_down_finish_speed",      {"tilt_down_finish_profile",    1.0, "tilt"}},
        {"tilt_up_initial_speed",       {"tilt_up_initial_profile",     1.0, "tilt"}},
        {"tilt_up_finish_speed",        {"tilt_up_finish_profile",      1.0, "tilt"}},

        {"tilt_down_initial_speed_slx", {"tilt_down_initial_profile",   1.0, "tilt"}},
        {"tilt_down_finish_speed_slx",  {"tilt_down_finish_profile",    1.0, "tilt"}},
        {"tilt_up_initial_speed_slx",   {"tilt_up_initial_profile",     1.0, "tilt"}},
        {"tilt_up_finish_speed_slx",    {"tilt_up_finish_profile",      1.0, "tilt"}},

        // Tilt options - Enum profiles
        {"dynamic_delay_before_profile",{"dynamic_delay_before_profile",1.0, "tilt"}},
        {"dynamic_tilt_up_profile",     {"dynamic_tilt_up_profile",     1.0, "tilt"}},
        {"dynamic_tilt_down_profile",   {"dynamic_tilt_down_profile",   1.0, "tilt"}},
    };

    auto it = mappings.find(opt_key);
    if (it != mappings.end()) {
        return it->second;
    }

    // Default: no transformation, no special path
    return {opt_key, 1.0, ""};
}

using json = nlohmann::ordered_json;

static float get_print_area(const SLAPrint &print) {
    float area = 0.;
    for (const SLAPrintObject* obj : print.objects())
        area += obj->surface_area_estimate() * double(obj->instances().size());
    return area;
}

// Serialize a scalar config option to a JSON node
// Handles key mapping, multiplier, and type conversions automatically
static void serialize_config_option(const std::string& opt_key, const ConfigOption* opt, json& node)
{
    if (!opt)
        return;

    auto mapping = get_key_mapping(opt_key);

    if (opt->is_nil()) {
        node[mapping.target_key] = nullptr;
        return;
    }

    switch (opt->type()) {
    case coFloat: {
        auto value = static_cast<const ConfigOptionFloat*>(opt);
        if (opt_key == "area_fill") {
            // area_fill should be output as an int
            node[mapping.target_key] = int(mapping.multiplier * value->value);
        } else {
            node[mapping.target_key] = mapping.multiplier * value->value;
        }
        break;
    }
    case coInt: {
        auto value = static_cast<const ConfigOptionInt*>(opt);
        if (mapping.multiplier != 1.0) {
            BOOST_LOG_TRIVIAL(fatal) << "Multiplier for int config option " << opt_key << " is not supported.";
            std::terminate();
        }
        node[mapping.target_key] = static_cast<int>(value->value);
        break;
    }
    case coBool: {
        auto value = static_cast<const ConfigOptionBool*>(opt);
        node[mapping.target_key] = value->value;
        break;
    }
    case coString: {
        auto value = static_cast<const ConfigOptionString*>(opt);
        node[mapping.target_key] = value->value;
        break;
    }
    case coPercent: {
        auto value = static_cast<const ConfigOptionPercent*>(opt);
        node[mapping.target_key] = mapping.multiplier * value->value;
        break;
    }
    case coFloatOrPercent: {
        auto value = static_cast<const ConfigOptionFloatOrPercent*>(opt);
        node[mapping.target_key] = mapping.multiplier * value->value;
        break;
    }
    default:
        // For other types (enums, complex types, etc.), use serialize()
        node[mapping.target_key] = opt->serialize();
        break;
    }
}

// Serialize a vector config option to dual JSON nodes (below/above area fill)
// Handles key mapping, multiplier, and type conversions for vector types
static void serialize_tilt_option_to_json(std::string opt_key, const ConfigOption* opt, bool is_slx, json& below_node, json& above_node)
{
    assert(opt != nullptr);

    const t_config_enum_names& tilt_enum_names  = ConfigOptionEnum< TiltSpeeds>::get_enum_names();
    const t_config_enum_names& tilt_enum_names_slx  = ConfigOptionEnum< TiltSpeedsSLX>::get_enum_names();
    const t_config_enum_names& tower_enum_names = ConfigOptionEnum<TowerSpeeds>::get_enum_names();

    const std::string& key = get_key_mapping(opt_key).target_key;
    const double mult = get_key_mapping(opt_key).multiplier;

    switch (opt->type()) {
    case coFloats: {
        auto values = static_cast<const ConfigOptionFloats*>(opt);
        below_node[key] = static_cast<int>(mult * values->get_at(0));
        above_node[key] = static_cast<int>(mult * values->get_at(1));
    }
    break;
    case coInts: {
        auto values = static_cast<const ConfigOptionInts*>(opt);
        below_node[key] = static_cast<int>(mult * values->get_at(0));
        above_node[key] = static_cast<int>(mult * values->get_at(1));
    }
    break;
    case coBools: {
        auto values = static_cast<const ConfigOptionBools*>(opt);
        below_node[key] = values->get_at(0);
        above_node[key] = values->get_at(1);
    }
    break;
    case coEnums: {
        t_config_enum_names enum_names;
        if (opt_key == "tower_speed")
            enum_names = tower_enum_names;
        else if (boost::starts_with(opt_key, "tilt_")) {
            if (is_slx && boost::ends_with(opt_key, "_slx")) {
                enum_names = tilt_enum_names_slx;
                opt_key.resize(opt_key.size() - 4); // trim the suffix
            }
            else if (! is_slx && ! boost::ends_with(opt_key, "_slx"))
                enum_names = tilt_enum_names;
            else
                return;
        }
        else if (opt_key == "dynamic_delay_before_profile")
            enum_names = ConfigOptionEnum<TiltDynamicDelayBefore>::get_enum_names();
        else if (opt_key == "dynamic_tilt_up_profile")
            enum_names = ConfigOptionEnum<TiltDynamicUp>::get_enum_names();
        else if (opt_key == "dynamic_tilt_down_profile")
            enum_names = ConfigOptionEnum<TiltDynamicDown>::get_enum_names();
        else
            std::terminate();

        auto values = static_cast<const ConfigOptionEnums<TiltSpeeds>*>(opt);
        below_node[key] = enum_names[values->get_at(0)];
        above_node[key] = enum_names[values->get_at(1)];
    }
    break;
    case coNone:
    default:
        break;
    }
}

// Centralized function to serialize DynamicPrintConfig to JSON structure
// Uses get_key_mapping() to determine key renaming, multipliers, and JSON hierarchy placement
struct SerializedConfigNodes {
    json root_level = json::object();           // Options that go at root/default level
    json exposure_profile = json::object();     // Options under exposure_profile node
    json below_area_fill = json::object();      // Tilt options for below area fill threshold
    json above_area_fill = json::object();      // Tilt options for above area fill threshold
};

static SerializedConfigNodes serialize_dynamic_config(const DynamicPrintConfig &cfg, bool is_slx)
{
    SerializedConfigNodes result;

    for (const std::string &opt_key : cfg.keys()) {
        const ConfigOption* opt = cfg.option(opt_key);
        if (!opt)
            continue;

        auto mapping = get_key_mapping(opt_key);

        // Check if this is a tilt option
        bool is_tilt = std::find(tilt_options().begin(), tilt_options().end(), opt_key) != tilt_options().end();

        if (is_tilt) {
            // Tilt options go into below_area_fill and above_area_fill
            serialize_tilt_option_to_json(opt_key, opt, is_slx,
                                         result.below_area_fill,
                                         result.above_area_fill);
        }
        else if (mapping.json_path == "exposure_profile") {
            // Use centralized serialization with key mapping and multipliers
            serialize_config_option(opt_key, opt, result.exposure_profile);
        }
        else if (mapping.json_path == "root") {
            // Root level options (these typically come from ConfMap, not serialized here)
            // But included for completeness when serializing full config
            serialize_config_option(opt_key, opt, result.root_level);
        }
        else {
            // Default: place at root level with transformed key
            serialize_config_option(opt_key, opt, result.root_level);
        }
    }

    return result;
}

static json get_original_values(const DynamicPrintConfig &cfg, bool is_slx)
{
    auto serialized = serialize_dynamic_config(cfg, is_slx);

    json& node = serialized.root_level;
    json& exp_node = serialized.exposure_profile;

    if (!serialized.below_area_fill.empty())
        exp_node["below_area_fill"] = serialized.below_area_fill;
    if (!serialized.above_area_fill.empty())
        exp_node["above_area_fill"] = serialized.above_area_fill;
    if (!serialized.exposure_profile.empty())
        node["exposure_profile"] = exp_node;
    
    return node;
}

// Helper to create exposure_profile node with tilt options
static json create_exposure_profile_node(const DynamicPrintConfig &cfg, bool is_slx)
{
    json below_node = json::object();
    json above_node = json::object();

    // Serialize tilt options
    for (const std::string& opt_key : tilt_options()) {
        serialize_tilt_option_to_json(opt_key, cfg.option(opt_key), is_slx, below_node, above_node);
    }

    // Build exposure profile node
    json profile_node = json::object();

    // Add area_fill using centralized serialization
    serialize_config_option("area_fill", cfg.option("area_fill"), profile_node);

    // Add printing_temperature (nullable) - special handling required
    serialize_config_option("printing_temperature", cfg.option("printing_temperature"), profile_node);

    profile_node["below_area_fill"] = below_node;
    profile_node["above_area_fill"] = above_node;

    return profile_node;
}

std::string to_json(const SLAPrint& print, const ConfMap &m)
{
    const auto& cfg = print.full_print_config();
    const bool is_slx = cfg.opt_string("printer_model") == "SLX";

    // Build JSON object using nlohmann::ordered_json
    json root = json::object();

    // Convert ConfMap to boost::property_tree, serialize with type conversion,
    // then parse back as nlohmann::json for proper types
    namespace pt = boost::property_tree;
    pt::ptree iniconf_tree;
    for (auto& param : m)
        iniconf_tree.put(param.first, param.second);

    // Use existing helper that converts string values to proper JSON types
    std::string iniconf_json_str = write_json_with_post_process(iniconf_tree);

    // Parse as nlohmann::json to get properly typed values
    json iniconf_json;
    try {
        iniconf_json = json::parse(iniconf_json_str);
    } catch (const json::parse_error& e) {
        // If this happens, it indicates a bug in write_json_with_post_process or the way we populate iniconf_tree
        BOOST_LOG_TRIVIAL(error) << "Failed to parse intermediate JSON: " << e.what();
        throw;
    }

    // Merge into root
    for (auto& [key, value] : iniconf_json.items())
        root[key] = value;

    // Add SLX-specific fields
    if (is_slx) {
        if (auto material_uuid = cfg.opt_string("material_uuid"); ! material_uuid.empty())
            root["material_uuid"] = material_uuid;
        if (! print.model().sla_workflow_uuid.empty())
            root["workflow_uuid"] = print.model().sla_workflow_uuid;
    }

    // Add standard fields
    root["surface_area"] = get_print_area(print);
    root["version"] = is_slx ? 2 : 1;

    // Add exposure_profile using centralized logic
    root["exposure_profile"] = create_exposure_profile_node(cfg, is_slx);

    // Add original_values
    root["original_values"] = get_original_values(print.original_config(), is_slx);

    // Serialize to JSON string with 4-space indentation
    return root.dump(4);
}

std::string get_cfg_value(const DynamicPrintConfig &cfg, const std::string &key)
{
    std::string ret;
    
    if (cfg.has(key)) {
        auto opt = cfg.option(key);
        if (opt) ret = opt->serialize();
    }
    
    return ret;    
}

void fill_iniconf(ConfMap &m, const SLAPrint &print)
{
    CNumericLocalesSetter locales_setter; // for to_string
    auto &cfg = print.full_print_config();
    m[get_key_mapping("layer_height").target_key]    = get_cfg_value(cfg, "layer_height");
    m[get_key_mapping("exposure_time").target_key]   = get_cfg_value(cfg, "exposure_time");
    m[get_key_mapping("initial_exposure_time").target_key] = get_cfg_value(cfg, "initial_exposure_time");
    const std::string mps = get_cfg_value(cfg, "material_print_speed");
    m[get_key_mapping("material_print_speed").target_key] = mps == "slow" ? "1" : mps == "fast" ? "0" : "2";
    m[get_key_mapping("sla_material_settings_id").target_key]= get_cfg_value(cfg, "sla_material_settings_id");
    m[get_key_mapping("printer_model").target_key]   = get_cfg_value(cfg, "printer_model");
    m[get_key_mapping("printer_variant").target_key] = get_cfg_value(cfg, "printer_variant");
    m[get_key_mapping("printer_settings_id").target_key]     = get_cfg_value(cfg, "printer_settings_id");
    m[get_key_mapping("sla_print_settings_id").target_key]   = get_cfg_value(cfg, "sla_print_settings_id");
    m["fileCreationTimestamp"] = Utils::utc_timestamp();
    m["prusaSlicerVersion"]    = SLIC3R_BUILD_ID;
    
    SLAPrintStatistics stats = print.print_statistics();
    // Set statistics values to the printer
    
    double used_material = (stats.objects_used_material +
                            stats.support_used_material) / 1000;
    
    int num_fade = print.default_object_config().faded_layers.getInt();
    num_fade = num_fade >= 0 ? num_fade : 0;
    
    m["usedMaterial"] = std::to_string(used_material);
    m[get_key_mapping("faded_layers").target_key] = get_cfg_value(cfg, "faded_layers");
    m["numSlow"]      = std::to_string(stats.slow_layers_count);
    m["numFast"]      = std::to_string(stats.fast_layers_count);
    m["printTime"]    = std::to_string(stats.estimated_print_time);

    bool hollow_en = false;
    auto it = print.objects().begin();
    while (!hollow_en && it != print.objects().end())
        hollow_en = (*it++)->config().hollowing_enable;

    m["hollow"] = hollow_en ? "1" : "0";
    
    m["action"] = "print";
}

void fill_slicerconf(ConfMap &m, const SLAPrint &print)
{
    using namespace std::literals::string_view_literals;
    
    // Sorted list of config keys, which shall not be stored into the ini.
    static constexpr auto banned_keys = { 
		"compatible_printers"sv,
        "compatible_prints"sv,
        //FIXME The print host keys should not be exported to full_print_config anymore. The following keys may likely be removed.
        "print_host"sv,
        "printhost_apikey"sv,
        "printhost_cafile"sv
    };
    
    assert(std::is_sorted(banned_keys.begin(), banned_keys.end()));
    auto is_banned = [](const std::string &key) {
        return std::binary_search(banned_keys.begin(), banned_keys.end(), key);
    };

    auto is_tilt_param = [](const std::string& key) -> bool {
        const auto& keys = tilt_options();
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    };
    
    auto &cfg = print.full_print_config();
    for (const std::string &key : cfg.keys())
        if (! is_banned(key) && !is_tilt_param(key) && ! cfg.option(key)->is_nil())
            m[key] = cfg.opt_serialize(key);
    
}

} // namespace

std::unique_ptr<sla::RasterBase> SL1Archive::create_raster() const
{
    sla::Resolution res;
    sla::PixelDim   pxdim;
    std::array<bool, 2>         mirror;

    double w  = m_cfg.display_width.getFloat();
    double h  = m_cfg.display_height.getFloat();
    auto   pw = size_t(m_cfg.display_pixels_x.getInt());
    auto   ph = size_t(m_cfg.display_pixels_y.getInt());

    mirror[X] = m_cfg.display_mirror_x.getBool();
    mirror[Y] = m_cfg.display_mirror_y.getBool();
    
    auto ro = m_cfg.display_orientation.getInt();
    sla::RasterBase::Orientation orientation =
        ro == sla::RasterBase::roPortrait ? sla::RasterBase::roPortrait :
                                            sla::RasterBase::roLandscape;
    
    if (orientation == sla::RasterBase::roPortrait) {
        std::swap(w, h);
        std::swap(pw, ph);
    }

    res   = sla::Resolution{pw, ph};
    pxdim = sla::PixelDim{w / pw, h / ph};
    sla::RasterBase::Trafo tr{orientation, mirror};

    double gamma = m_cfg.gamma_correction.getFloat();

    return sla::create_raster_grayscale_aa(res, pxdim, gamma, tr);
}

sla::RasterEncoder SL1Archive::get_encoder() const
{
    return sla::PNGRasterEncoder{};
}

static void write_thumbnail(Zipper &zipper, const ThumbnailData &data)
{
    size_t png_size = 0;

    void  *png_data = tdefl_write_image_to_png_file_in_memory_ex(
         (const void *) data.pixels.data(), data.width, data.height, 4,
         &png_size, MZ_DEFAULT_LEVEL, 1);

    if (png_data != nullptr) {
        zipper.add_entry("thumbnail/thumbnail" + std::to_string(data.width) +
                             "x" + std::to_string(data.height) + ".png",
                         static_cast<const std::uint8_t *>(png_data),
                         png_size);

        mz_free(png_data);
    }
}

void SL1Archive::export_print(Zipper               &zipper,
                              const SLAPrint       &print,
                              const ThumbnailsList &thumbnails,
                              const std::string    &prjname)
{
    std::string project =
        prjname.empty() ?
            boost::filesystem::path(zipper.get_filename()).stem().string() :
            prjname;

    ConfMap iniconf, slicerconf;
    fill_iniconf(iniconf, print);

    iniconf["jobDir"] = project;

    fill_slicerconf(slicerconf, print);

    try {
        zipper.add_entry("config.ini");
        zipper << to_ini(iniconf);
        zipper.add_entry("prusaslicer.ini");
        zipper << to_ini(slicerconf);

        zipper.add_entry("config.json");
        zipper << to_json(print, iniconf);

        size_t i = 0;
        for (const sla::EncodedRaster &rst : m_layers) {

            std::string imgname = project + string_printf("%.5d", i++) + "." +
                                  rst.extension();

            zipper.add_entry(imgname.c_str(), rst.data(), rst.size());
        }

        for (const ThumbnailData& data : thumbnails)
            if (data.is_valid())
                write_thumbnail(zipper, data);

        zipper.finalize();
    } catch(std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
        // Rethrow the exception
        throw;
    }
}

void SL1Archive::export_print(const std::string     fname,
                              const SLAPrint       &print,
                              const ThumbnailsList &thumbnails,
                              const std::string    &prjname)
{
    Zipper zipper{fname, Zipper::FAST_COMPRESSION};

    export_print(zipper, print, thumbnails, prjname);
}

} // namespace Slic3r

// /////////////////////////////////////////////////////////////////////////////
// Reader implementation
// /////////////////////////////////////////////////////////////////////////////

namespace marchsq {

template<> struct _RasterTraits<Slic3r::png::ImageGreyscale> {
    using Rst = Slic3r::png::ImageGreyscale;

       // The type of pixel cell in the raster
    using ValueType = uint8_t;

       // Value at a given position
    static uint8_t get(const Rst &rst, size_t row, size_t col)
    {
        return rst.get(row, col);
    }

       // Number of rows and cols of the raster
    static size_t rows(const Rst &rst) { return rst.rows; }
    static size_t cols(const Rst &rst) { return rst.cols; }
};

} // namespace marchsq

namespace Slic3r {

template<class Fn> static void foreach_vertex(ExPolygon &poly, Fn &&fn)
{
    for (auto &p : poly.contour.points) fn(p);
    for (auto &h : poly.holes)
        for (auto &p : h.points) fn(p);
}

void invert_raster_trafo(ExPolygons &                  expolys,
                         const sla::RasterBase::Trafo &trafo,
                         coord_t                       width,
                         coord_t                       height)
{
    if (trafo.flipXY) std::swap(height, width);

    for (auto &expoly : expolys) {
        if (trafo.mirror_y)
            foreach_vertex(expoly, [height](Point &p) {p.y() = height - p.y(); });

        if (trafo.mirror_x)
            foreach_vertex(expoly, [width](Point &p) {p.x() = width - p.x(); });

        expoly.translate(-trafo.center_x, -trafo.center_y);

        if (trafo.flipXY)
            foreach_vertex(expoly, [](Point &p) { std::swap(p.x(), p.y()); });

        if ((trafo.mirror_x + trafo.mirror_y + trafo.flipXY) % 2) {
            expoly.contour.reverse();
            for (auto &h : expoly.holes) h.reverse();
        }
    }
}

RasterParams get_raster_params(const DynamicPrintConfig &cfg)
{
    auto *opt_disp_cols = cfg.option<ConfigOptionInt>("display_pixels_x");
    auto *opt_disp_rows = cfg.option<ConfigOptionInt>("display_pixels_y");
    auto *opt_disp_w    = cfg.option<ConfigOptionFloat>("display_width");
    auto *opt_disp_h    = cfg.option<ConfigOptionFloat>("display_height");
    auto *opt_mirror_x  = cfg.option<ConfigOptionBool>("display_mirror_x");
    auto *opt_mirror_y  = cfg.option<ConfigOptionBool>("display_mirror_y");
    auto *opt_orient    = cfg.option<ConfigOptionEnum<SLADisplayOrientation>>("display_orientation");

    if (!opt_disp_cols || !opt_disp_rows || !opt_disp_w || !opt_disp_h ||
        !opt_mirror_x || !opt_mirror_y || !opt_orient)
        throw MissingProfileError("Invalid SL1 / SL1S file");

    RasterParams rstp;

    rstp.px_w = opt_disp_w->value / (opt_disp_cols->value - 1);
    rstp.px_h = opt_disp_h->value / (opt_disp_rows->value - 1);

    rstp.trafo = sla::RasterBase::Trafo{opt_orient->value == sladoLandscape ?
                                            sla::RasterBase::roLandscape :
                                            sla::RasterBase::roPortrait,
                                        {opt_mirror_x->value, opt_mirror_y->value}};

    rstp.height = scaled(opt_disp_h->value);
    rstp.width  = scaled(opt_disp_w->value);

    return rstp;
}

namespace {

ExPolygons rings_to_expolygons(const std::vector<marchsq::Ring> &rings,
                               double px_w, double px_h)
{
    auto polys = reserve_vector<ExPolygon>(rings.size());

    for (const marchsq::Ring &ring : rings) {
        Polygon poly; Points &pts = poly.points;
        pts.reserve(ring.size());

        for (const marchsq::Coord &crd : ring)
            pts.emplace_back(scaled(crd.c * px_w), scaled(crd.r * px_h));

        polys.emplace_back(poly);
    }

    // TODO: Is a union necessary?
    return union_ex(polys);
}

std::vector<ExPolygons> extract_slices_from_sla_archive(
    ZipperArchive           &arch,
    const RasterParams      &rstp,
    const marchsq::Coord    &win,
    std::function<bool(int)> progr)
{
    std::vector<ExPolygons> slices(arch.entries.size());

    struct Status
    {
        double                                 incr, val, prev;
        bool                                   stop  = false;
        execution::SpinningMutex<ExecutionTBB> mutex = {};
    } st{100. / slices.size(), 0., 0.};

    execution::for_each(
        ex_tbb, size_t(0), arch.entries.size(),
        [&arch, &slices, &st, &rstp, &win, progr](size_t i) {
            // Status indication guarded with the spinlock
            {
                std::lock_guard lck(st.mutex);
                if (st.stop) return;

                st.val += st.incr;
                double curr = std::round(st.val);
                if (curr > st.prev) {
                    st.prev = curr;
                    st.stop = !progr(int(curr));
                }
            }

            png::ImageGreyscale img;
            png::ReadBuf        rb{arch.entries[i].buf.data(),
                            arch.entries[i].buf.size()};
            if (!png::decode_png(rb, img)) return;

            constexpr uint8_t isoval = 128;
            auto              rings = marchsq::execute(img, isoval, win);
            ExPolygons        expolys = rings_to_expolygons(rings, rstp.px_w,
                                                            rstp.px_h);

            // Invert the raster transformations indicated in the profile metadata
            invert_raster_trafo(expolys, rstp.trafo, rstp.width, rstp.height);

            slices[i] = std::move(expolys);
        },
        execution::max_concurrency(ex_tbb));

    if (st.stop) slices = {};

    return slices;
}

} // namespace

ConfigSubstitutions SL1Reader::read(std::vector<ExPolygons> &slices,
                                    DynamicPrintConfig      &profile_out)
{
    Vec2i windowsize;

    switch(m_quality)
    {
    case SLAImportQuality::Fast: windowsize = {8, 8}; break;
    case SLAImportQuality::Balanced: windowsize = {4, 4}; break;
    default:
    case SLAImportQuality::Accurate:
        windowsize = {2, 2}; break;
    };

    // Ensure minimum window size for marching squares
    windowsize.x() = std::max(2, windowsize.x());
    windowsize.y() = std::max(2, windowsize.y());

    std::vector<std::string> includes = { "ini", "png"};
    std::vector<std::string> excludes = { "thumbnail" };
    ZipperArchive arch = read_zipper_archive(m_fname, includes, excludes);
    auto [profile_use, config_substitutions] = extract_profile(arch, profile_out);

    RasterParams   rstp = get_raster_params(profile_use);
    marchsq::Coord win  = {windowsize.y(), windowsize.x()};
    slices = extract_slices_from_sla_archive(arch, rstp, win, m_progr);

    return std::move(config_substitutions);
}

ConfigSubstitutions SL1Reader::read(DynamicPrintConfig &out)
{
    ZipperArchive arch = read_zipper_archive(m_fname, {"ini"}, {"png", "thumbnail"});
    return out.load(arch.profile, ForwardCompatibilitySubstitutionRule::Enable);
}

} // namespace Slic3r
