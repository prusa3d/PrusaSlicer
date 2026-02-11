#include "AnycubicSLAZip.hpp"

#include <algorithm>
#include <array>
#include <boost/log/trivial.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

#include <nlohmann/json.hpp>

#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Zipper.hpp"
#include "LocalesUtils.hpp"

// Anycubic ZIP-based SLA archive format (.pm7m, .pm7, .pm4u, .pwsz)
//
// The archive is a standard ZIP containing the following entries:
//
//   anycubic_photon_resins.pwsp   [required]  JSON - machine & resin settings
//   layers_controller.conf        [required]  JSON - per-layer parameters
//   print_info.json               [optional]  JSON - cost, time, volume
//   software_info.conf            [optional]  JSON - slicer metadata
//   scene.slice                   [optional]  Binary - scene manifest
//   preview_images/preview_N.png  [optional]  PNG thumbnails (224x168, 336x252)
//   layer_images/layer_N.pw0Img   [required]  PW0 RLE-encoded layer images
//
// scene.slice binary layout:
//   Offset  Size   Field
//   0       16     Magic "ANYCUBIC-PWSZ\0\0\0"
//   16      64     Software version string (null-padded)
//   80      4      BinaryType  (uint32: 3 = FPGA Release)
//   84      4      Version     (uint32: 1)
//   88      4      SliceType   (uint32)
//   92      4      ModelUnit   (uint32: 0=mm, 1=cm, 2=m)
//   96      4      PointRatio  (float: 1.0)
//   100     4      LayerCount  (uint32)
//   104     24     Bounding box: XMin, YMin, ZMin, XMax, YMax, ZMax (float)
//   128     4      ModelStats  (uint32)
//   132     256    Padding (64 x uint32)
//   388     4      Separator "<---"
//   392     4      LayerDefCount (uint32)
//   396     N*64   Per-layer defs: Height, Area, BBox(4 floats),
//                  ObjectCount, MaxContourArea, Padding(8 x uint32)
//   end     4      End marker "--->"

namespace Slic3r {

using json = nlohmann::ordered_json;

// PW0 RLE encoder - same encoding as used by the older binary Anycubic formats.
// Encodes grayscale raster data using run-length encoding where:
// - Fully transparent (0x00) or fully opaque (0xF0) pixels use 2-byte encoding
//   with up to 0xFFF run length
// - Anti-aliased pixels (0x10-0xE0) use 1-byte encoding with up to 0xF run length
static void anycubic_pw0_get_pixel_span(const std::uint8_t *ptr,
                                        const std::uint8_t *end,
                                        std::uint8_t &pixel,
                                        size_t &span_len) {
    size_t max_len;

    span_len = 0;
    pixel = (*ptr) & 0xF0;
    max_len = (pixel == 0 || pixel == 0xF0) ? 0xFFF : 0xF;
    while (ptr < end && span_len < max_len && ((*ptr) & 0xF0) == pixel) {
        span_len++;
        ptr++;
    }
}

struct AnycubicPW0RasterEncoder {
    sla::EncodedRaster operator()(const void *ptr, size_t w, size_t h, size_t num_components) {
        std::vector<uint8_t> dst;
        size_t span_len;
        std::uint8_t pixel;
        auto size = w * h * num_components;
        dst.reserve(size);

        const std::uint8_t *src = reinterpret_cast<const std::uint8_t *>(ptr);
        const std::uint8_t *src_end = src + size;
        while (src < src_end) {
            anycubic_pw0_get_pixel_span(src, src_end, pixel, span_len);
            src += span_len;
            if (pixel == 0 || pixel == 0xF0) {
                pixel = pixel | (span_len >> 8);
                dst.push_back(pixel);
                pixel = span_len & 0xFF;
                dst.push_back(pixel);
            } else {
                pixel = pixel | span_len;
                dst.push_back(pixel);
            }
        }

        return sla::EncodedRaster(std::move(dst), "pw0Img");
    }
};

namespace {

float get_cfg_value_f(const DynamicConfig &cfg, const std::string &key, const float def = 0.f) {
    if (cfg.has(key)) {
        if (auto opt = cfg.option(key)) {
            return opt->getFloat();
        }
    }
    return def;
}

int get_cfg_value_i(const DynamicConfig &cfg, const std::string &key, const int def = 0) {
    if (cfg.has(key)) {
        if (auto opt = cfg.option(key)) {
            return opt->getInt();
        }
    }
    return def;
}

// Round a float to the given number of decimal places so that the
// float -> double promotion noise (e.g. 0.05f -> 0.05000000074505806)
// is removed before JSON serialization.
static double round_f(float val, int decimals = 4) {
    double factor = std::pow(10.0, decimals);
    return std::round(static_cast<double>(val) * factor) / factor;
}

// Serialize a JSON object with 2-space indent, ASCII escaping, and a
// trailing newline - matching the format the printer firmware expects.
static std::string dump_json(const json &j) {
    return j.dump(2, ' ', true) + "\n";
}

// Generate the anycubic_photon_resins.pwsp JSON
std::string generate_settings_json(const SLAPrint &print,
                                   std::uint32_t layer_count,
                                   float pixel_size_um) {
    auto &cfg = print.full_print_config();

    int res_x = get_cfg_value_i(cfg, "display_pixels_x");
    int res_y = get_cfg_value_i(cfg, "display_pixels_y");
    double disp_w = round_f(get_cfg_value_f(cfg, "display_width"), 2);
    double disp_h = round_f(get_cfg_value_f(cfg, "display_height"), 2);
    double max_z = round_f(get_cfg_value_f(cfg, "max_print_height", 300.f), 1);
    double layer_h = round_f(get_cfg_value_f(cfg, "layer_height"), 4);
    double exp_time = round_f(get_cfg_value_f(cfg, "exposure_time"), 2);
    double bot_exp = round_f(get_cfg_value_f(cfg, "initial_exposure_time"), 2);
    int bot_layers = get_cfg_value_i(cfg, "faded_layers");
    double lift_h = 8.0;
    double lift_speed = 2.0;
    double ret_speed = 3.0;
    double wait_time = 0.5;
    int pixel_um = static_cast<int>(std::round(pixel_size_um));

    if (bot_layers > (int) layer_count) {
        bot_layers = layer_count;
    }

    json root;
    root["version"] = "3";

    root["machine_type"] = {
        {"version", "3"},
        {"name", "Anycubic Photon Mono M7 Max"},
        {"key_suffix", "pm7m"},
        {"key_image_format", "pw0Img"},
        {"res_x", res_x},
        {"res_y", res_y},
        {"xy_pixel", pixel_um},
        {"xy_pixel_y", pixel_um},
        {"max_samples", 16},
        {"property", 119},
        {"print_xsize", disp_w},
        {"print_ysize", disp_h},
        {"print_zsize", max_z},
        {"max_file_version", 518},
        {"prev_back_color", {0.0, 0.28, 0.39}},
        {"prev_model_color", {0.8, 0.8, 0.8}},
        {"prev_supports_color", {0.07, 0.93, 0.93}},
        {"prev_image_size", {224, 168}},
        {"child_screen", {{{"x", 0}, {"y", 0}, {"width", res_x}, {"height", res_y}}}},
        {"prev2_back_color", {0.08, 0.11, 0.16}},
        {"prev2_image_size", {336, 252}},
        {"raster_segments_capacity", 0},
        {"raster_antialiasing", 8},
        {"cloudprev_back_color", {0.0, 0.28, 0.39}},
        {"cloudprev_imag_size", {800, 600}},
    };

    json temp_coeffs = json::array();
    struct TempCoeff {
        double temp, x_coeff, y_comp;
    };
    for (auto [temp, x_coeff, y_comp] : std::initializer_list<TempCoeff>{
            {10.0, 184.27, 1675.8},
            {25.0, 197.78, 1803.2},
            {35.0, 161.19, 1417.3},
            {45.0, 167.31, 1480.9},
            {55.0, 166.76, 1474.1},
        }) {
        temp_coeffs.push_back({
            {"temperature", temp},
            {"x_coefficient", x_coeff},
            {"y_compensation", y_comp},
        });
    }

    json resin = {
        {"version", "2"},
        {"property", {
            {"version", "3"},
            {"code", "10"},
            {"currency", "\u20ac"},
            {"name", "default_resin"},
            {"price", 25.0},
            {"type", "Standard resin"},
            {"volume", 1000.0},
            {"subfunc_code", 0},
            {"density", 1.2},
            {"target_temperature", 25.0},
        }},
        {"depth_penetration_curve", {
            {"zthick_min", 0.01},
            {"zthick_max", 0.2},
            {"light_intensity", 9000.0},
            {"safety_coefficient", 1.6},
            {"current_tempcurve_selector", 0},
            {"temperature_coefficients", temp_coeffs},
        }},
        {"slice_extpara", {
            {"version", "3"},
            {"multi_state_used", 0},
            {"transition_layercount", 0},
            {"transition_type", 0},
            {"multi_state_paras", {
                {"bott_0", {{"height", lift_h}, {"up_speed", lift_speed}, {"down_speed", ret_speed}}},
                {"bott_1", {{"height", 4.0}, {"up_speed", 3.0}, {"down_speed", 3.0}}},
                {"normal_0", {{"height", lift_h}, {"up_speed", lift_speed}, {"down_speed", ret_speed}}},
                {"normal_1", {{"height", 5.0}, {"up_speed", 6.0}, {"down_speed", 6.0}}},
            }},
            {"exposure_compensate", 0.0},
            {"intelli_mode", 0},
            {"max_acceleration", 2.0},
            {"separate_support_exposure_delayed", 0.0},
        }},
        {"slicepara", {
            {"anti_count", 1},
            {"blur_level", 0},
            {"bott_layers", bot_layers},
            {"bott_time", bot_exp},
            {"exposure_time", exp_time},
            {"gray_level", 0},
            {"off_time", wait_time},
            {"use_indivi_layerpara", 0.0},
            {"use_random_erode", 0},
            {"zthick", layer_h},
            {"zup_height", lift_h},
            {"zup_speed", lift_speed},
            {"zdown_speed", ret_speed},
        }},
    };

    json zeros4 = {0.0, 0.0, 0.0, 0.0};

    root["machine_extern"] = {
        {"version", "3"},
        {"alias", "Anycubic Photon Mono M7 Max"},
        {"picture", "AnycubicPhotonMonoM7Max.png"},
        {"cloud_property", 0},
        {"device_cn_code", ""},
        {"factory_resins", {resin}},
        {"user_resins", json::array()},
        {"active_resins", {"default_resin"}},
        {"firmware_calc_print_time", 1},
        {"firmware_calc_print_time_paras", {
            {"version", "2"},
            {"MACHINE_AXIS_STEPS_PER_UNIT", {100.0, 100.0, 3200.0, 94.0}},
            {"MACHINE_BLOCK_BUFFER_SIZE", 32},
            {"MACHINE_DEFAULT_ACCELERATION", 1000.0},
            {"MACHINE_DEFAULT_MINSEGMENTTIME", 20000.0},
            {"MACHINE_DEFAULT_XYJERK", 20.0},
            {"MACHINE_DEFAULT_ZJERK", 0.2},
            {"MACHINE_GENERATE_FRAME_TIME", 450.0},
            {"MACHINE_MAX_ACCELERATION", {1000.0, 1000.0, 160.0, 1000.0}},
            {"MACHINE_MAX_FEEDRATE", {200.0, 200.0, 20.0, 45.0}},
            {"MACHINE_MAX_STEP_FREQUENCY", 256000.0},
            {"MACHINE_MINIMUM_PLANNER_SPEED", 0.5},
            {"MACHINE_NOR_LAYER_DOWN_HEIGHT_DIV", 0.25},
            {"MACHINE_NOR_LAYER_DOWN_SPEED_DIV", 0.5},
            {"MACHINE_NOR_LAYER_UP_HEIGHT_DIV", 0.25},
            {"MACHINE_NOR_LAYER_UP_SPEED_DIV", 0.5},
            {"MACHINE_STEP_MUL", 1.0},
            {"MACHINE_TIME_COMPENSATE", 0.0},
            {"MACHINE_TIM_PRES", 30.0},
            {"MACHINE_TIM_RCC_CLK", 60.0},
            {"FUNCTION", 1.0},
            {"MACHINE_MODE_ACCELERATION", zeros4},
            {"LAYER_COMPENSATE", zeros4},
            {"HEIGHT_COMPENSATE", zeros4},
            {"TIMES_COMPENSATE", zeros4},
        }},
        {"firmware_calc_exp_time_paras", {
            {"precision_range_branch", {0.0, 5.0, 25.0}},
            {"precision_per_volume", 5.0},
            {"precision_coeff_value", {0.024, 0.01, -0.2}},
            {"energy_coeff", 0.0},
            {"machine_exposure_ton", 0.4},
        }},
    };

    return dump_json(root);
}

// Generate layers_controller.conf JSON
std::string generate_layers_json(const SLAPrint &print,
                                 const std::vector<sla::EncodedRaster> &layers,
                                 float bot_exp_time,
                                 float exp_time,
                                 float layer_h,
                                 float bot_layer_h,
                                 int bot_layers,
                                 float lift_h,
                                 float lift_speed) {
    std::uint32_t layer_count = layers.size();

    double d_lift_h = round_f(lift_h, 2);
    double d_lift_speed = round_f(lift_speed, 2);
    double d_bot_exp = round_f(bot_exp_time, 2);
    double d_exp = round_f(exp_time, 2);
    double d_layer_h = round_f(layer_h, 4);
    double d_bot_layer_h = round_f(bot_layer_h, 4);

    json paras = json::array();
    double min_height = 0.0;
    for (std::uint32_t i = 0; i < layer_count; i++) {
        double cur_exp = (i < (std::uint32_t) bot_layers) ? d_bot_exp : d_exp;
        double cur_layer = (i < (std::uint32_t) bot_layers) ? d_bot_layer_h : d_layer_h;

        paras.push_back({
            {"exposure_time", cur_exp},
            {"layer_index", i},
            {"layer_minheight", min_height},
            {"layer_thickness", cur_layer},
            {"zup_height", d_lift_h},
            {"zup_speed", d_lift_speed},
        });
        min_height = round_f(static_cast<float>(min_height + cur_layer), 4);
    }

    json root;
    root["count"] = layer_count;
    root["paras"] = paras;
    return dump_json(root);
}

// Generate print_info.json
std::string generate_print_info_json(float cost, float print_time, float volume_ml) {
    json root = {
        {"cost", round_f(cost, 2)},
        {"currency", "\u20ac"},
        {"print_time", round_f(print_time, 1)},
        {"volume", round_f(volume_ml, 2)},
    };
    return dump_json(root);
}

// Generate software_info.conf JSON
std::string generate_software_info_json() {
    json root = {
        {"mark", "PrusaSlicer"},
        {"opengl", "3.3-CoreProfile"},
        {"os", "linux"},
        {"Version", SLIC3R_BUILD_ID},
    };
    return dump_json(root);
}

// Generate the scene.slice binary data
// This is a simplified version - contains the ANYCUBIC-PWSZ magic,
// layer count, and zeroed bounding data.
std::vector<uint8_t> generate_scene_slice(std::uint32_t layer_count,
                                          float layer_h,
                                          float total_height) {
    std::vector<uint8_t> data;

    // Magic: "ANYCUBIC-PWSZ" padded to 16 bytes
    const char magic[16] = "ANYCUBIC-PWSZ";
    data.insert(data.end(), magic, magic + 16);

    // Software string padded to 64 bytes
    std::string sw = "PrusaSlicer " SLIC3R_BUILD_ID;
    sw.resize(64, '\0');
    data.insert(data.end(), sw.begin(), sw.end());

    // Binary type (uint32): 3 = FPGA Release
    auto write_u32 = [&](uint32_t val) {
        data.push_back(val & 0xFF);
        data.push_back((val >> 8) & 0xFF);
        data.push_back((val >> 16) & 0xFF);
        data.push_back((val >> 24) & 0xFF);
    };
    auto write_f32 = [&](float val) {
        uint32_t *f = reinterpret_cast<uint32_t *>(&val);
        write_u32(*f);
    };

    write_u32(3);            // BinaryType
    write_u32(1);            // Version
    write_u32(0);            // SliceType
    write_u32(0);            // ModelUnit (mm)
    write_f32(1.0f);         // PointRatio
    write_u32(layer_count);  // LayerCount
    write_f32(0.0f);         // XStartBoundingRect
    write_f32(0.0f);         // YStartBoundingRect
    write_f32(0.0f);         // ZMin
    write_f32(0.0f);         // XEndBoundingRect
    write_f32(0.0f);         // YEndBoundingRect
    write_f32(total_height); // ZMax
    write_u32(0);            // ModelStats

    // Padding: 64 uint32s
    for (int i = 0; i < 64; i++) {
        write_u32(0);
    }

    // Separator: "<---"
    data.push_back('<');
    data.push_back('-');
    data.push_back('-');
    data.push_back('-');

    // LayerDefCount
    write_u32(layer_count);

    // Per-layer definitions (each is 64 bytes: 8 floats + 8 uint32 padding)
    float height = 0.f;
    for (uint32_t i = 0; i < layer_count; i++) {
        height += layer_h;
        write_f32(height); // Height
        write_f32(0.0f);   // Area
        write_f32(0.0f);   // XStart
        write_f32(0.0f);   // YStart
        write_f32(0.0f);   // XEnd
        write_f32(0.0f);   // YEnd
        write_u32(0);      // ObjectCount
        write_f32(0.0f);   // MaxContourArea
        // Padding: 8 uint32s
        for (int j = 0; j < 8; j++) {
            write_u32(0);
        }
    }

    // End marker: "--->"
    data.push_back('-');
    data.push_back('-');
    data.push_back('-');
    data.push_back('>');

    return data;
}

void write_png_thumbnail(Zipper &zipper, const ThumbnailData &data, const std::string &entry_name) {
    size_t png_size = 0;
    void *png_data = tdefl_write_image_to_png_file_in_memory_ex((const void *) data.pixels.data(),
                                                                data.width,
                                                                data.height,
                                                                4,
                                                                &png_size,
                                                                MZ_DEFAULT_LEVEL,
                                                                1);
    if (png_data != nullptr) {
        zipper.add_entry(entry_name, static_cast<const std::uint8_t *>(png_data), png_size);
        mz_free(png_data);
    }
}

} // anonymous namespace

std::unique_ptr<sla::RasterBase> AnycubicSLAZipArchive::create_raster() const {
    sla::Resolution res;
    sla::PixelDim pxdim;
    std::array<bool, 2> mirror;

    double w = m_cfg.display_width.getFloat();
    double h = m_cfg.display_height.getFloat();
    auto pw = size_t(m_cfg.display_pixels_x.getInt());
    auto ph = size_t(m_cfg.display_pixels_y.getInt());

    mirror[X] = m_cfg.display_mirror_x.getBool();
    mirror[Y] = m_cfg.display_mirror_y.getBool();

    auto ro = m_cfg.display_orientation.getInt();
    sla::RasterBase::Orientation orientation = ro == sla::RasterBase::roPortrait ?
        sla::RasterBase::roPortrait :
        sla::RasterBase::roLandscape;

    if (orientation == sla::RasterBase::roPortrait) {
        std::swap(w, h);
        std::swap(pw, ph);
    }

    res = sla::Resolution{pw, ph};
    pxdim = sla::PixelDim{w / pw, h / ph};
    sla::RasterBase::Trafo tr{orientation, mirror};

    double gamma = m_cfg.gamma_correction.getFloat();

    return sla::create_raster_grayscale_aa(res, pxdim, gamma, tr);
}

sla::RasterEncoder AnycubicSLAZipArchive::get_encoder() const {
    return AnycubicPW0RasterEncoder{};
}

void AnycubicSLAZipArchive::export_print(const std::string fname,
                                         const SLAPrint &print,
                                         const ThumbnailsList &thumbnails,
                                         const std::string & /*projectname*/) {
    CNumericLocalesSetter locales_setter;

    std::uint32_t layer_count = m_layers.size();
    auto &cfg = print.full_print_config();

    float layer_h = get_cfg_value_f(cfg, "layer_height");
    float bot_layer_h = get_cfg_value_f(cfg, "initial_layer_height");
    float exp_time = get_cfg_value_f(cfg, "exposure_time");
    float bot_exp = get_cfg_value_f(cfg, "initial_exposure_time");
    int bot_layers = get_cfg_value_i(cfg, "faded_layers");
    float lift_h = 8.0f;
    float lift_speed = 2.0f;

    if (bot_layers > (int) layer_count) {
        bot_layers = layer_count;
    }

    // Compute pixel size in microns
    float disp_w = get_cfg_value_f(cfg, "display_width");
    int res_x = get_cfg_value_i(cfg, "display_pixels_x");
    float pixel_um = (res_x > 0) ? (disp_w / res_x * 1000.f) : 50.f;

    // Compute statistics
    SLAPrintStatistics stats = print.print_statistics();
    float volume_ml = (stats.objects_used_material + stats.support_used_material) / 1000.f;
    float bottle_cost = get_cfg_value_f(cfg, "bottle_cost");
    float bottle_vol = get_cfg_value_f(cfg, "bottle_volume");
    float cost = (bottle_vol > 0.f) ? (volume_ml * bottle_cost / bottle_vol) : 0.f;

    // Estimate print time
    float print_time = (bot_layers * bot_exp) + ((layer_count - bot_layers) * exp_time) +
        (layer_count * lift_h / 3.0f) +                             // retract
        (layer_count * lift_h / lift_speed) + (layer_count * 0.5f); // wait time

    float total_height = (bot_layers * bot_layer_h) + ((layer_count - bot_layers) * layer_h);

    try {
        Zipper zipper{fname, Zipper::FAST_COMPRESSION};

        // Write thumbnails as PNG
        int preview_idx = 0;
        for (const ThumbnailData &data : thumbnails) {
            if (data.is_valid()) {
                write_png_thumbnail(zipper, data, "preview_images/preview_" + std::to_string(preview_idx) + ".png");
                preview_idx++;
            }
        }

        // Write JSON metadata files
        std::string settings_json = generate_settings_json(print, layer_count, pixel_um);
        zipper.add_entry("anycubic_photon_resins.pwsp");
        zipper << settings_json;

        std::string print_info = generate_print_info_json(cost, print_time, volume_ml);
        zipper.add_entry("print_info.json");
        zipper << print_info;

        std::string layers_json = generate_layers_json(
            print, m_layers, bot_exp, exp_time, layer_h, bot_layer_h, bot_layers, lift_h, lift_speed);
        zipper.add_entry("layers_controller.conf");
        zipper << layers_json;

        std::string sw_info = generate_software_info_json();
        zipper.add_entry("software_info.conf");
        zipper << sw_info;

        // Write scene.slice binary
        std::vector<uint8_t> scene = generate_scene_slice(layer_count, layer_h, total_height);
        zipper.add_entry("scene.slice", scene.data(), scene.size());

        // Write layer images
        size_t i = 0;
        for (const sla::EncodedRaster &rst : m_layers) {
            std::string imgname = "layer_images/layer_" + std::to_string(i) + "." + rst.extension();
            zipper.add_entry(imgname.c_str(), rst.data(), rst.size());
            i++;
        }

        zipper.finalize();
    } catch (std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << e.what();
        throw;
    }
}

} // namespace Slic3r
