#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include "Slic3r/Biz/ResultExport/SLA/Zipper.hpp"

#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Image.hpp"
#include "Slic3r/Biz/Algorithms/ImageUtils.hpp"
#include "Slic3r/Time.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Version.hpp"
#include "Slic3r/Biz/Algorithms/MiniZWrapper.hpp" // IWYU pragma: keep
#include "libslic3r/SLAResult.hpp"

#include <LocalesUtils.hpp>
#include <sstream>
#include <Slic3r/Log.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>
#include <regex>
#include <iomanip>
#include <nlohmann/json.hpp>

using namespace Slic3r::Biz::Slicing;
using json = nlohmann::json;
using ConfMap = std::map<std::string, std::string>;
using Slic3r::Domain::EnumVectorWrapper;

namespace Slic3r::Biz::PrintHost::Sla {

namespace {

std::string to_ini(const ConfMap &m)
{
    std::string ret;
    for (auto &param : m)
        ret += param.first + " = " + param.second + "\n";

    return ret;
}

const std::vector<std::string> ms_opts{
    "delay_before_exposure",
    "delay_after_exposure",
    "tilt_down_offset_delay",
    "tilt_up_offset_delay",
    "tilt_down_delay",
    "tilt_up_delay",
};

const std::string tower_hop_height_opt{
   "tower_hop_height"
};

const std::vector<std::string> speed_opts{
    "tower_speed",
    "tilt_down_initial_speed",
    "tilt_down_finish_speed",
    "tilt_up_initial_speed",
    "tilt_up_finish_speed",
};

const std::string use_tilt_opt{"use_tilt"};

const std::vector<std::string> count_opts{
    "tilt_down_offset_steps",
    "tilt_down_cycles",
    "tilt_up_offset_steps",
    "tilt_up_cycles"
};

std::string tilt_options_to_json(const Domain::ConfigView& cfg, const ConfMap& iniconf)
{
    json below_node;
    json above_node;

    const double ms_coeff{1e3};
    for (const std::string& key : ms_opts) {
        const auto values = cfg.get<std::vector<double>>(key);
        const std::string insert_key{key + "_ms"};     
        below_node[insert_key] = static_cast<int>(ms_coeff * values.at(0));
        above_node[insert_key] = static_cast<int>(ms_coeff * values.at(1));
    }

    {
        const auto values = cfg.get<std::vector<double>>(tower_hop_height_opt);
        const double nm_coeff{1e6};
        const std::string insert_key{tower_hop_height_opt + "_nm"};
        below_node[insert_key] = static_cast<int>(nm_coeff * values.at(0));
        above_node[insert_key] = static_cast<int>(nm_coeff * values.at(1));
    }

    for (const std::string& key : speed_opts) {
        const auto values = cfg.get<Domain::EnumVectorWrapper>(key).get_strings();
        const std::string insert_key{boost::replace_all_copy(key, "_speed", "_profile")};
        below_node[insert_key] = values.at(0);
        above_node[insert_key] = values.at(1);
    }

    {
        const auto values = cfg.get<std::vector<bool>>(use_tilt_opt);
        below_node[use_tilt_opt] = values.at(0);
        above_node[use_tilt_opt] = values.at(1);
    }

    for (const std::string& key : count_opts) {
        const auto values = cfg.get<std::vector<int>>(key);
        below_node[key] = values.at(0);
        above_node[key] = values.at(1);
    }

    json profile_node;
    profile_node["area_fill"] = cfg.get<double>("area_fill");
    profile_node["below_area_fill"] = below_node;
    profile_node["above_area_fill"] = above_node;

    json root;

    for (const auto& param : iniconf) {
        root[param.first] = param.second;
    }

    root["version"] = "1";
    root["exposure_profile"] = profile_node;

    return root.dump(-1);
}


static std::string serialize(const double value)
{
    std::ostringstream ss;
    if (std::isfinite(value))
        ss << value;
    else if (std::isnan(value)) {
        throw std::runtime_error("Serializing NaN");
    } else
        throw std::runtime_error("Serializing invalid number");
    return ss.str();
}

void fill_iniconf(ConfMap &m, const Domain::ConfigView &cfg, const Domain::SLA::PrintStatistics &stats) {
    using Domain::SLAMaterialSpeed;
    using Domain::SLAMaterialSpeed::slamsSlow;
    using Domain::SLAMaterialSpeed::slamsFast;

    CNumericLocalesSetter locales_setter; // for to_string
    m["layerHeight"]    = serialize(cfg.get<double>("layer_height"));
    m["expTime"]        = serialize(cfg.get<double>("exposure_time"));
    m["expTimeFirst"]   = serialize(cfg.get<double>("initial_exposure_time"));
    const Domain::SLAMaterialSpeed mps = cfg.get<Domain::SLAMaterialSpeed>("material_print_speed");
    m["expUserProfile"] = mps == slamsSlow ? "1" : mps == slamsFast ? "0" : "2";

    // TODO commented out, until we know how to reference the settings
    //m["materialName"]   = cfg.get<std::string>("sla_material_settings_id");
    m["printerModel"]   = cfg.get<std::string>("printer_model");
    m["printerVariant"] = cfg.get<std::string>("printer_variant");
    //m["printerProfile"] = cfg.get<std::string>("printer_settings_id");
    //m["printProfile"]   = cfg.get<std::string>("sla_print_settings_id");
    m["fileCreationTimestamp"] = Utils::utc_timestamp();
    m["prusaSlicerVersion"]    = SLIC3R_BUILD_ID;

    // Set statistics values to the printer
    double used_material = (stats.objects_used_material +
                            stats.support_used_material) / 1000;
    m["usedMaterial"] = std::to_string(used_material);
    m["numFade"]      = std::to_string(stats.count_faded_layers);
    m["numSlow"]      = std::to_string(stats.slow_layers_count);
    m["numFast"]      = std::to_string(stats.fast_layers_count);
    m["printTime"]    = std::to_string(stats.estimated_print_time);
    m["hollow"] = stats.hollowing_enable ? "1" : "0";
    m["action"] = "print";
}

static void write_thumbnail(Zipper &zipper, const Domain::Image &data)
{
    size_t png_size = 0;

    void  *png_data = tdefl_write_image_to_png_file_in_memory_ex(
         (const void *) data.pixels.data(), data.width(), data.height(), 4,
         &png_size, MZ_DEFAULT_LEVEL, 1);

    if (png_data != nullptr) {
        zipper.add_entry("thumbnail/thumbnail" + std::to_string(data.width()) +
                             "x" + std::to_string(data.height()) + ".png",
                         static_cast<const std::uint8_t *>(png_data),
                         png_size);

        mz_free(png_data);
    }
}
}

void store_sl1(const std::string& file_path, const Slicing::SLAResultData& data)
{
    std::string layer_extension = ".png";
    Zipper::e_compression compression = Zipper::FAST_COMPRESSION;
    if (data.files.type == Slicing::Sla::FileDataType::sl1_svg){
        layer_extension = ".svg";
        compression = Zipper::TIGHT_COMPRESSION;
    }

    Zipper zipper{file_path, compression};
    std::string project = data.project_name.empty() ?
        boost::filesystem::path(zipper.get_filename()).stem().string() :
        data.project_name;

    const auto& stats = *data.print_statistics;

    const Biz::Slicing::SerializedConfig& serialized_config{data.serialized_config};
    const Domain::ConfigView& print_config{data.config};

    ConfMap iniconf;
    fill_iniconf(iniconf, print_config, stats);

    iniconf["jobDir"] = project;

    try {
        zipper.add_entry("config.ini");
        zipper << to_ini(iniconf);
        zipper.add_entry("config.json");
        zipper << tilt_options_to_json(print_config, iniconf);

        zipper.add_entry("prusaslicer.ini");
        zipper << serialized_config.ini;
        zipper.add_entry("prusaslicer.json");
        zipper << serialized_config.json;


        size_t i = 0;
        for (const Slicing::Sla::FileData& rst : data.files.data) {
            std::string imgname = project + string_printf("%.5d", i++) + layer_extension;
            zipper.add_entry(imgname.c_str(), rst.data(), rst.size());
        }

        for (const Domain::Image& data : data.thumbnails)
            if (Biz::Algorithms::ImageUtils::is_valid(data))
                write_thumbnail(zipper, data);

        zipper.finalize();
    } catch(std::exception& e) {
        SPDLOG_ERROR("{}", e.what());
        // Rethrow the exception
        throw;
    }
}

} // namespace Slic3r::Biz::PrintHost::Sla
