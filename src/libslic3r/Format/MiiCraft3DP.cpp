#include "MiiCraft3DP.hpp"
#include <map>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>

#include "libslic3r/Zipper.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/MTUtils.hpp"

#include "libslic3r/miniz_extension.hpp" // IWYU pragma: keep
#include <LocalesUtils.hpp>
#include "libslic3r/GCode/ThumbnailData.hpp"

namespace Slic3r {

namespace {

using ModMap = std::map<std::string, std::string>;

class PrinterParams
{
public:
    PrinterParams(const char* model_id, int speed, int generation) : m_model_id_(model_id), m_speed_(speed)
        , m_generation_(generation) {}

    const char* m_model_id_;
    int m_speed_;
    ModMap m_mods_;
    int m_generation_;
};

std::string get_cfg_value(const DynamicPrintConfig& cfg, const std::string& key)
{
    std::string ret;

    if (cfg.has(key)) {
        auto opt = cfg.option(key);
        if (opt) ret = opt->serialize();
    }

    return ret;
}

float get_cfg_value_f(const DynamicConfig& cfg, const std::string& key)
{
    if (cfg.has(key)) {
        if (auto opt = cfg.option(key))
            return opt->getFloat();
    }

    return 0.0f;
}

int get_cfg_value_i(const DynamicConfig& cfg, const std::string& key)
{
    if (cfg.has(key)) {
        if (auto opt = cfg.option(key))
            return opt->getInt();
    }

    return 0;
}

std::string to_print_ini(const SLAPrint& print, size_t layer_num, std::shared_ptr<PrinterParams> param_ptr)
{
    std::string ret;
    CNumericLocalesSetter locales_setter; // for to_string

    ret += "[Common] \n";
    ret += "CustomerCode = A Series\n";
    ret += "ConfigVersion = " + std::string(SLIC3R_BUILD_ID) + " \n";
    ret += "LayerPartitionCount = 1 \n";
    ret += "LayerPartitionCount = " + std::to_string(layer_num) + " \n";
    ret += "SliceFileFormat = PNG \n";
    ret += "PrinterSerialNumber = "+ std::string(param_ptr->m_model_id_) +" \n";
    ret += "BaseStartNo = 0 \n";
    ret += "BaseStopNo = -1 \n";

    SLAPrintStatistics stats = print.print_statistics();
    // Set statistics values to the printer

    double used_material = (stats.objects_used_material +
        stats.support_used_material) / 1000;

    ret += "Volume = " + std::to_string(used_material) + " \n";
    ret += "EstimatedTime = " + std::to_string(int(stats.estimated_print_time)) + " \n";
    ret += "\n";
    ret += "[LayerPartition000000] \n";
    ret += "StartLayerNo = 0 \n";
    ret += "StopLayerNo = " + std::to_string(layer_num-1) + " \n";

    auto& cfg = print.full_print_config();
    float val = get_cfg_value_f(cfg, "layer_height");
    val *= 1000.0f;
    ret += "Thinkness = " + std::to_string(val) + "\n";
    val = get_cfg_value_f(cfg, "exposure_time");
    val *= 1000.0f;
    ret += "CuringTime = " + std::to_string(int(val)) + "\n";
    ret += "Speed = " + std::to_string(param_ptr->m_speed_) + " \n";
    ret += "\n";

    return ret;
}

std::string to_extra_para(const SLAPrint& print, std::shared_ptr<PrinterParams> param_ptr)
{
    std::string ret;
    CNumericLocalesSetter locales_setter; // for to_string

    auto& cfg = print.full_print_config();
    float val = get_cfg_value_f(cfg, "exposure_time");
    val *= 1000.0f;
    int curing_time = int(val);
    float initial_layer_height = get_cfg_value_f(cfg, "initial_layer_height");
    float layer_height = get_cfg_value_f(cfg, "layer_height");
    int initial_layer = int(initial_layer_height / layer_height);
    if (initial_layer == 0)
        initial_layer = 1;
    val = get_cfg_value_f(cfg, "initial_exposure_time");
    val *= 1000.0f;
    int initial_curing_time = int(val);
    int middle_layer = get_cfg_value_i(cfg, "faded_layers");
    SLAPrintStatistics stats = print.print_statistics();
    int estimated_time = int(stats.estimated_print_time);
    double used_material = (stats.objects_used_material +
        stats.support_used_material) / 1000;
    int volume = int(used_material);
    int resolution_x = get_cfg_value_i(cfg, "display_pixels_x");
    int resolution_y = get_cfg_value_i(cfg, "display_pixels_y");
    layer_height *= 1000.0f;

    ret += "{";
    ret += "\"speed\":" + std::to_string(param_ptr->m_speed_) + ", ";
    ret += "\"curing_time\":" + std::to_string(curing_time) + ", ";
    ret += "\"initial_layer\":" + std::to_string(initial_layer) + ", ";
    ret += "\"initial_curing_time\":" + std::to_string(initial_curing_time) + ", ";
    ret += "\"middle_layer\":" + std::to_string(middle_layer) + ", ";
    ret += "\"gap\":0, ";
    ret += "\"isDel\":0, ";
    ret += "\"filename\":\"\", ";
    ret += "\"savePath\":\"\", ";
    ret += "\"printing_setting\":\"" + get_cfg_value(cfg, "sla_print_settings_id") + "\", ";
    ret += "\"distortion\":3, ";
    if (param_ptr->m_generation_ == 5) {
        ret += "\"estimated_time\":" + std::to_string(estimated_time) + ", ";
        ret += "\"model_volume\":" + std::to_string(volume) + ", ";
    }
    ret += "\"print_delay\":10, ";
    ret += "\"FW_revision\":0, ";
    ret += "\"platform_size\":\"" + get_cfg_value(cfg, "display_width") + "\", ";
    ret += "\"thickness\":\"" + std::to_string(layer_height) + "\", ";
    ret += "\"resolution_x\":" + std::to_string(resolution_x) + ", ";
    ret += "\"resolution_y\":" + std::to_string(resolution_y) + ", ";
    ret += "\"resin_name\":\"" + get_cfg_value(cfg, "sla_material_settings_id") + "\", ";
    ret += "\"resin_index\":\"" + get_cfg_value(cfg, "sla_material_settings_id") + "\", ";
    ret += "\"power\":100";
    ret += "}";
    return ret;
}

void write_preview_image(Zipper& zipper, const ThumbnailData& data)
{
    size_t png_size = 0;

    void* png_data = tdefl_write_image_to_png_file_in_memory_ex(
        (const void*)data.pixels.data(), data.width, data.height, 4,
        &png_size, MZ_DEFAULT_LEVEL, 1);

    if (png_data != nullptr) {
        zipper.add_entry("printer_model.png",
            static_cast<const std::uint8_t*>(png_data),
            png_size);

        mz_free(png_data);
    }
}



class AlphaParams : public PrinterParams
{
public:
    AlphaParams(const char* model_id, int speed, int generation) : PrinterParams(model_id, speed, generation) {
        std::string mod_str;
        if (speed == 0) {
            mod_str = "picker(2,600,8000)\npicker(0,600,4000)\nidle(5000)\n";
            m_mods_["000000.mod"] = mod_str;
        }
        else if (speed == 1) {
            mod_str = "picker(2,600,5000)\npicker(0,600,2500)\nidle(3000)\n";
            m_mods_["000000.mod"] = mod_str;
        }
        else {
            mod_str = "picker(2,600,2500)\npicker(0,600,1200)\nidle(1000)\n";
            m_mods_["000000.mod"] = mod_str;
        }
    }
};

class PrimeParams : public PrinterParams
{
public:
    PrimeParams(const char* model_id, int speed, int generation) : PrinterParams(model_id, speed, generation) {
        std::string mod_str;
        if (speed == 0) {
            mod_str = "cart(0,500,3000)\npicker(2,600,2000)\ncart(1,500,2450)\npicker(0,500,2500)\npicker(0,100,4000)\nidle(5000)\n";
            m_mods_["000000.mod"] = mod_str;
            mod_str = "cart(0,350,3000)\npicker(2,450,1950)\ncart(1,350,1600)\npicker(0,350,1600)\npicker(0,100,5000)\nidle(2000)\n";
            m_mods_["000010.mod"] = mod_str;
        }
        else if (speed == 1) {
            mod_str = "cart(0,500,2500)\npicker(2,550,1950)\ncart(1,500,2450)\npicker(0,450,2500)\npicker(0,100,4000)\nidle(5000)\n";
            m_mods_["000000.mod"] = mod_str;
            mod_str = "cart(0,300,2500)\npicker(2,400,1950)\ncart(1,300,1600)\npicker(0,300,1600)\npicker(0,100,4000)\nidle(1000)\n";
            m_mods_["000008.mod"] = mod_str;
        }
        else {
            mod_str = "cart(0,500,2450)\npicker(2,500,1950)\ncart(1,500,2450)\npicker(0,400,1950)\npicker(0,100,3000)\nidle(5000)\n";
            m_mods_["000000.mod"] = mod_str;
            mod_str = "cart(0,200,2450)\npicker(2,400,1600)\ncart(1,200,1600)\npicker(0,300,1600)\npicker(0,100,4000)\n";
            m_mods_["000005.mod"] = mod_str;
        }
    }
};


class ParamsFactory
{
public:
    ParamsFactory() = default;


    static std::shared_ptr<PrinterParams> create(const std::string& model_id, int speed)
    {
        std::shared_ptr<PrinterParams> ret;
        if (model_id == "ALPHA")
        {
            ret = std::make_shared<AlphaParams>(model_id.c_str(), speed, 5);
        }
        else if (model_id == "PRIME")
        {
            ret = std::make_shared<PrimeParams>(model_id.c_str(), speed, 4);
        }
        else {
            ret = std::make_shared<PrimeParams>(model_id.c_str(), speed, 3);
        }

        return ret;
    }
};

}

std::unique_ptr<sla::RasterBase> MiiCraft3DPArchive::create_raster() const
{
    sla::Resolution res;
    sla::PixelDim   pxdim;
    std::array<bool, 2>         mirror;

    double w = m_cfg.display_width.getFloat();
    double h = m_cfg.display_height.getFloat();
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

    res = sla::Resolution{ pw, ph };
    pxdim = sla::PixelDim{ w / pw, h / ph };
    sla::RasterBase::Trafo tr{ orientation, mirror };

    double gamma = m_cfg.gamma_correction.getFloat();

    return sla::create_raster_grayscale_aa(res, pxdim, gamma, tr);
}

sla::RasterEncoder MiiCraft3DPArchive::get_encoder() const
{
    return sla::PNGRasterEncoder{};
}

void MiiCraft3DPArchive::export_print(const std::string     fname,
                                      const SLAPrint& print,
                                      const ThumbnailsList& thumbnails,
                                      const std::string& prjname)
{
    Zipper zipper{ fname, Zipper::FAST_COMPRESSION };

    std::string project =
        prjname.empty() ?
        boost::filesystem::path(zipper.get_filename()).stem().string() :
        prjname;

    auto& cfg = print.full_print_config();
    std::string model_id = get_cfg_value(cfg, "printer_model");
    const std::string mps = get_cfg_value(cfg, "material_print_speed");
    int speed = mps == "slow" ? 0 : mps == "fast" ? 2 : 1;
    std::shared_ptr<PrinterParams> p_params;

    try {
        p_params = ParamsFactory::create(model_id, speed);


        zipper.add_entry("Printer.ini");
        zipper << to_print_ini(print, m_layers.size(), p_params);
        zipper.add_entry("extra.para");
        zipper << to_extra_para(print, p_params);

        for (auto& mod : p_params->m_mods_) {
            zipper.add_entry(mod.first);
            zipper << mod.second;
        }

        size_t i = 0;
        for (const sla::EncodedRaster& rst : m_layers) {

            std::string imgname = string_printf("layer%.6d", i++) + "." +
                rst.extension();

            zipper.add_entry(imgname.c_str(), rst.data(), rst.size());
        }

        if (thumbnails.size() > 0 && thumbnails[0].is_valid()) {
            write_preview_image(zipper, thumbnails[0]);
        }

        zipper.finalize();
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
        // Rethrow the exception
        throw;
    }
}

}