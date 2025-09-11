///|/ Copyright (c) 2023 Nicholas Sica @NickSica
#include "CTB.hpp"
#include "libslic3r/Config.hpp"
#include <iostream>
#include <fstream>
#include <cstdint>
#include <chrono>
#include "openssl/sha.h"
#include "openssl/evp.h"

#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/SLAPrint.hpp"

#include <boost/log/trivial.hpp>
#include <boost/pfr/core.hpp>

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <boost/pfr/core.hpp>
#include <cstring>

#include <iostream>
#include <fstream>
#include <string>

// Special thanks to UVTools for the CTBv4 update and
// Catibo for the excellent writeup(https://github.com/cbiffle/catibo/blob/master/doc/cbddlp-ctb.adoc)

namespace Slic3r {
static void ctb_get_pixel_val(std::uint8_t pixel, uint32_t run_len, std::vector<uint8_t> &dst) {
    if (run_len == 0) {
        return;
    }

    if (run_len > 1) {
        pixel |= 0x80;
    }
    dst.push_back(pixel);

    if (run_len <= 1) {
        return;
    }

    if (run_len <= 0x7f) {
        dst.push_back((uint8_t) run_len);
        return;
    }

    if (run_len <= 0x3fff) {
        dst.push_back((uint8_t) ((run_len >> 8) | 0x80));
        dst.push_back((uint8_t) run_len);
        return;
    }

    if (run_len <= 0x1fffff) {
        dst.push_back((uint8_t) ((run_len >> 16) | 0xc0));
        dst.push_back((uint8_t) (run_len >> 8));
        dst.push_back((uint8_t) run_len);
        return;
    }

    if (run_len <= 0xfffffff) {
        dst.push_back((uint8_t) ((run_len >> 24) | 0xe0));
        dst.push_back((uint8_t) (run_len >> 16));
        dst.push_back((uint8_t) (run_len >> 8));
        dst.push_back((uint8_t) run_len);
    }
}

struct CTBRasterEncoder
{
    sla::EncodedRaster operator()(const void *ptr, size_t w, size_t h, size_t num_components) {
        std::vector<uint8_t> dst;
        uint32_t run_len = 0;
        std::uint8_t pixel = 0xFF;
        auto size = w * h * num_components;
        dst.reserve(size);

        const std::uint8_t *src = reinterpret_cast<const std::uint8_t *>(ptr);
        const std::uint8_t *src_end = src + size;
        while (src < src_end) {
            uint8_t tmp_pixel = (*src) >> 1;
            if (tmp_pixel == pixel) {
                run_len++;
            } else {
                ctb_get_pixel_val(pixel, run_len, dst);
                pixel = tmp_pixel;
                run_len = 1;
            }
            src++;
        }
        ctb_get_pixel_val(pixel, run_len, dst);
        dst.shrink_to_fit();

        return sla::EncodedRaster(std::move(dst), "ctb");
    }
};

namespace {

template<typename T> T get_cfg_value(const DynamicConfig &cfg, const std::string &key) {
    T ret{};

    if (cfg.has(key)) {
        if (auto opt = cfg.option(key)) {
            if (opt->type() == Slic3r::ConfigOptionType::coInt)
                ret = (T) opt->getInt();
            else if (opt->type() == Slic3r::ConfigOptionType::coFloat ||
                     opt->type() == Slic3r::ConfigOptionType::coPercent)
                ret = (T) opt->getFloat();
        }
    }

    return (T) ret;
}

// Need this because of struct padding
// Could use pragma packed or an attribute
// Could also turn this into a compile-time cost with a macro
template<typename T> static size_t get_struct_size(T &h) {
    size_t tot_size = 0;
    boost::pfr::for_each_field(h, [&tot_size](auto &x) { tot_size += sizeof(x); });
    return tot_size;
}

void fill_preview(
    ctb_format_preview &large_preview,
    ctb_format_preview &small_preview,
    ctb_preview_data &preview_images,
    const ThumbnailsList &thumbnails
) {
    auto vec_data = &preview_images.large;
    std::uint32_t first_width = thumbnails[0].width;
    uint16_t rep = 0;
    std::uint32_t prev_pixel{};
    auto rle = [&]() {
        if (rep == 0) {
            return;
        } else if (rep == 1) {
            vec_data->push_back((prev_pixel & ~RGB565_REPEAT_MASK) & 0xFF);
            vec_data->push_back((prev_pixel >> 8) & 0xFF);
        } else if (rep == 2) {
            for (int i = 0; i < 2; i++) {
                vec_data->push_back((prev_pixel & ~RGB565_REPEAT_MASK) & 0xFF);
                vec_data->push_back((prev_pixel >> 8) & 0xFF);
            }
        } else {
            vec_data->push_back((prev_pixel | RGB565_REPEAT_MASK) & 0xFF);
            vec_data->push_back((prev_pixel >> 8) & 0xFF);
            vec_data->push_back((rep - 1) & 0xFF);
            vec_data->push_back((((rep - 1) | 0x3000) >> 8) & 0xFF);
        }
    };

    for (auto thumb : thumbnails) {
        auto preview = &large_preview;
        if (thumb.width < first_width) {
            vec_data = &preview_images.small;
            preview = &small_preview;
        } else {
            vec_data = &preview_images.large;
        }

        preview->size_x = thumb.width;
        preview->size_y = thumb.height;

        vec_data->reserve(preview->size_x * preview->size_y * 4);

        // Convert to RGB565 and do RLE Encoding
        std::uint32_t i = thumb.pixels.size() - 1;
        rep = 0;
        // The color doesn't match the sl1s color- it comes out grey
        // while sl1s comes out orange
        while (i > 3) {
            std::uint16_t pixel;
            i--; // Alpha
            std::uint8_t b = thumb.pixels[i--];
            std::uint8_t g = thumb.pixels[i--];
            std::uint8_t r = thumb.pixels[i--];
            // convert to BGRA565
            pixel = ((r >> 3) << 11) | ((g >> 3) << 6) | (b >> 3);

            if (pixel == prev_pixel) {
                rep++;
                if (rep == RLE_ENCODING_LIMIT) {
                    rle();
                    rep = 0;
                }
            } else {
                rle();
                prev_pixel = pixel;
                rep = 1;
            }
        }
    }
    large_preview.image_len = preview_images.large.size();
    small_preview.image_len = preview_images.small.size();
}

void fill_header(
    ctb_format_header &h,
    ctb_format_print_params &print_params,
    ctb_format_slicer_info &slicer_info,
    ctb_format_print_params_v4 &print_params_v4,
    const SLAPrint &print,
    std::uint32_t layer_count
) {
    CNumericLocalesSetter locales_setter;

    auto &cfg = print.full_print_config();

    SLAPrintStatistics stats = print.print_statistics();

    h.magic = MAGIC_V4;
    // Version matches the CTB version
    // clang-format off
    h.version              = 4;
    h.bed_size_x           = get_cfg_value<float>(cfg, "display_width");
    h.bed_size_y           = get_cfg_value<float>(cfg, "display_height");
    h.bed_size_z           = get_cfg_value<float>(cfg, "max_print_height");
    h.zero_pad             = 0;
    h.layer_height         = get_cfg_value<float>(cfg, "layer_height");
    h.overall_height       = layer_count * h.layer_height; // model height- might be a way to get this from prusa slicer
    h.exposure             = get_cfg_value<float>(cfg, "exposure_time");
    h.bot_exposure         = get_cfg_value<float>(cfg, "initial_exposure_time");
    h.light_off_delay      = get_cfg_value<float>(cfg, "light_off_time");
    h.bot_layer_count      = get_cfg_value<uint32_t>(cfg, "faded_layers");
    h.res_x                = get_cfg_value<uint32_t>(cfg, "display_pixels_x");
    h.res_y                = get_cfg_value<uint32_t>(cfg, "display_pixels_y");
    h.large_preview_offset = get_struct_size(h);
    // Layer table offset is set below after the v4 params offset is created
    h.layer_count = layer_count;
    h.print_time = stats.estimated_print_time;
    h.projector_type = 1; // check for normal or mirrored- 0/1 respectively- LCD printers are
                          // "mirrored" for this purpose
    h.print_params_size = get_struct_size(print_params);
    h.antialias_level = 1;
    h.pwm_level = (uint16_t) (get_cfg_value<float>(cfg, "light_intensity") / 100 * 255);
    h.bot_pwm_level = (uint16_t) (get_cfg_value<float>(cfg, "bot_light_intensity") / 100 * 255);
    h.encryption_key = 0;
    h.slicer_info_size = get_struct_size(slicer_info);
    // h.level_set_count            = 0;  // Useless unless antialiasing for cbddlp

    print_params.bot_lift_height = get_cfg_value<float>(cfg, "bot_lift_distance");
    print_params.bot_lift_speed = get_cfg_value<float>(cfg, "bot_lift_speed");
    print_params.lift_height = get_cfg_value<float>(cfg, "lift_distance");
    print_params.lift_speed = get_cfg_value<float>(cfg, "lift_speed");
    print_params.retract_speed = get_cfg_value<float>(cfg, "sla_retract_speed");
    print_params.resin_volume_ml = get_cfg_value<float>(cfg, "bottle_volume");
    print_params.resin_mass_g = get_cfg_value<float>(cfg, "bottle_weight") * 1000.0f;
    print_params.resin_cost = get_cfg_value<float>(cfg, "bottle_cost");
    print_params.bot_light_off_delay = get_cfg_value<float>(cfg, "bot_light_off_time");
    print_params.light_off_delay = get_cfg_value<float>(cfg, "light_off_time");
    print_params.bot_layer_count = get_cfg_value<uint32_t>(cfg, "faded_layers");
    print_params.zero_pad1 = 0;
    print_params.zero_pad2 = 0;
    print_params.zero_pad3 = 0;
    print_params.zero_pad4 = 0;

    if (get_cfg_value<bool>(cfg, "enable_tsmc") == true) {
        slicer_info.bot_lift_height2        = get_cfg_value<float>(cfg, "tsmc_bot_lift_distance");
        slicer_info.bot_lift_speed2         = get_cfg_value<float>(cfg, "tsmc_bot_lift_speed");
        slicer_info.lift_height2            = get_cfg_value<float>(cfg, "tsmc_lift_distance");
        slicer_info.lift_speed2             = get_cfg_value<float>(cfg, "tsmc_lift_speed");
        slicer_info.retract_height2         = get_cfg_value<float>(cfg, "tsmc_retract_height");
        slicer_info.retract_speed2          = get_cfg_value<float>(cfg, "tsmc_sla_retract_speed");
        slicer_info.rest_time_after_lift2   = get_cfg_value<float>(cfg, "tsmc_rest_time_after_lift");
        print_params_v4.bot_retract_speed2  = get_cfg_value<float>(cfg, "tsmc_sla_bot_retract_speed");
        print_params_v4.bot_retract_height2 = get_cfg_value<float>(cfg, "tsmc_bot_retract_height");
    } else {
        slicer_info.bot_lift_height2        = 0;
        slicer_info.bot_lift_speed2         = 0;
        slicer_info.lift_height2            = 0;
        slicer_info.lift_speed2             = 0;
        slicer_info.retract_height2         = 0;
        slicer_info.retract_speed2          = 0;
        slicer_info.rest_time_after_lift2   = 0;
        print_params_v4.bot_retract_speed2  = 0;
        print_params_v4.bot_retract_height2 = 0;
    }
    slicer_info.rest_time_after_lift = get_cfg_value<float>(cfg, "rest_time_after_lift");
    slicer_info.anti_alias_flag      = 0x0F; // 0 [No AA] / 8 [AA] for cbddlp files, 7(0x7) [No AA] / 15(0x0F) [AA] for ctb files
    slicer_info.zero_pad1            = 0;
    // TODO: Maybe implement this setting in PrusaSlicer?
    slicer_info.per_layer_settings = 0; // 0 to not support, 0x20 (32) for v3 ctb and 0x40 for
                                        // v4 ctb files to allow per layer parameters
    slicer_info.timestamp_minutes = std::chrono::duration_cast<std::chrono::minutes>(
                                        std::chrono::system_clock::now().time_since_epoch()
    )
                                        .count(); // Time since epoch in minutes
    // TODO: Fix antialising- make sure it matches other slicers
    slicer_info.antialias_level = 8;
    // TODO: Does this need to be changed?
    slicer_info.software_version = 0x01090000; // ctb v3 = 17171200 | ctb v4 pro = 16777216
    slicer_info.rest_time_after_retract = get_cfg_value<float>(cfg, "rest_time_after_retract");
    slicer_info.transition_layer_count  = get_cfg_value<uint32_t>(cfg, "faded_layers");
    slicer_info.zero_pad2               = 0;
    slicer_info.zero_pad3               = 0;

    print_params_v4.bot_retract_speed       = get_cfg_value<float>(cfg, "sla_bot_retract_speed");
    print_params_v4.zero_pad1               = 0;
    print_params_v4.four1                   = 4.0f;
    print_params_v4.zero_pad2               = 0;
    print_params_v4.four2                   = 4.0f;
    print_params_v4.rest_time_after_retract = slicer_info.rest_time_after_retract;
    print_params_v4.rest_time_after_lift    = slicer_info.rest_time_after_lift;
    print_params_v4.rest_time_before_lift   = get_cfg_value<float>(cfg, "rest_time_before_lift");
    print_params_v4.unknown1                = 2955.996f;
    print_params_v4.unknown2                = 73470;
    print_params_v4.unknown3                = 4;
    print_params_v4.last_layer_index        = layer_count - 1;
    print_params_v4.zero_pad3               = 0;
    print_params_v4.zero_pad4               = 0;
    print_params_v4.zero_pad5               = 0;
    print_params_v4.zero_pad6               = 0;
    print_params_v4.disclaimer_len          = 320;

    if (layer_count < h.bot_layer_count) {
        h.bot_layer_count = layer_count;
    }
    //clang-format on
}

void fill_header_encrypted(
    unencrypted_format_header &u,
    decrypted_format_header &h,
    const SLAPrint &print,
    std::uint32_t layer_count
) {
    CNumericLocalesSetter locales_setter;

    auto &cfg = print.full_print_config();

    SLAPrintStatistics stats = print.print_statistics();

    // clang-format off
    u.magic = MAGIC_ENCRYPTED;
    u.unknown1 = 0;
    u.unknown2 = 0;
    u.unknown3 = 0;
    u.unknown4 = 1;
    u.unknown5 = 1;
    u.unknown6 = 0;
    u.unknown7 = 42;
    u.unknown8 = 0;

    // Version matches the CTB version
    h.checksum        = 0xCAFEBABE;
    h.bed_size_x      = get_cfg_value<float>(cfg, "display_width");
    h.bed_size_y      = get_cfg_value<float>(cfg, "display_height");
    h.bed_size_z      = get_cfg_value<float>(cfg, "max_print_height");
    h.unknown1        = 0;
    h.unknown2        = 0;
    h.layer_height    = get_cfg_value<float>(cfg, "layer_height");
    h.overall_height  = layer_count * h.layer_height; // model height- might be a way to get this from prusa slicer
    h.exposure        = get_cfg_value<float>(cfg, "exposure_time");
    h.bot_exposure    = get_cfg_value<float>(cfg, "initial_exposure_time");
    h.light_off_delay = get_cfg_value<float>(cfg, "light_off_time");
    h.bot_layer_count = get_cfg_value<uint32_t>(cfg, "faded_layers");
    h.res_x = get_cfg_value<uint32_t>(cfg, "display_pixels_x");
    h.res_y = get_cfg_value<uint32_t>(cfg, "display_pixels_y");
    h.layer_count = layer_count;
    // Layer table offset is set below after everything is created
    h.print_time = stats.estimated_print_time;
    h.projector_type = 1; // check for normal or mirrored- 0/1 respectively- LCD printers are
                          // "mirrored" for this purpose
    h.bot_lift_height = get_cfg_value<float>(cfg, "bot_lift_distance");
    h.bot_lift_speed = get_cfg_value<float>(cfg, "bot_lift_speed");
    h.lift_height = get_cfg_value<float>(cfg, "lift_distance");
    h.lift_speed = get_cfg_value<float>(cfg, "lift_speed");
    h.retract_speed = get_cfg_value<float>(cfg, "sla_retract_speed");
    h.resin_volume_ml = get_cfg_value<float>(cfg, "bottle_volume");
    h.resin_mass_g = get_cfg_value<float>(cfg, "bottle_weight") * 1000.0f;
    h.resin_cost = get_cfg_value<float>(cfg, "bottle_cost");
    h.bot_light_off_delay = get_cfg_value<float>(cfg, "bot_light_off_time");
    h.unknown3            = 1;
    h.pwm_level           = (uint16_t) (get_cfg_value<float>(cfg, "light_intensity") / 100 * 255);
    h.bot_pwm_level       = (uint16_t) (get_cfg_value<float>(cfg, "bot_light_intensity") / 100 * 255);
    h.layer_xor_key       = 0;
    // h.layer_xor_key       = 0xEFBEADDE;
    //  h.level_set_count            = 0;  // Useless unless antialiasing for cbddlp
    if (get_cfg_value<bool>(cfg, "enable_tsmc") == true) {
        h.bot_lift_height2      = get_cfg_value<float>(cfg, "tsmc_bot_lift_distance");
        h.bot_lift_speed2       = get_cfg_value<float>(cfg, "tsmc_bot_lift_speed");
        h.lift_height2          = get_cfg_value<float>(cfg, "tsmc_lift_distance");
        h.lift_speed2           = get_cfg_value<float>(cfg, "tsmc_lift_speed");
        h.retract_height2       = get_cfg_value<float>(cfg, "tsmc_retract_height");
        h.retract_speed2        = get_cfg_value<float>(cfg, "tsmc_sla_retract_speed");
        h.bot_retract_speed2    = get_cfg_value<float>(cfg, "tsmc_sla_bot_retract_speed");
        h.bot_retract_height2   = get_cfg_value<float>(cfg, "tsmc_bot_retract_height");
        h.rest_time_after_lift2 = get_cfg_value<float>(cfg, "tsmc_rest_time_after_lift");
    } else {
        h.bot_lift_height2      = 0;
        h.bot_lift_speed2       = 0;
        h.lift_height2          = 0;
        h.lift_speed2           = 0;
        h.retract_height2       = 0;
        h.retract_speed2        = 0;
        h.bot_retract_speed2    = 0;
        h.bot_retract_height2   = 0;
        h.rest_time_after_lift2 = 0;
    }
    h.rest_time_after_lift = get_cfg_value<float>(cfg, "rest_time_after_lift");
    h.anti_alias_flag      = 0x0F; // 0 [No AA] / 8 [AA] for cbddlp files, 7(0x7) [No AA] / 15(0x0F) [AA] for ctb files
    h.zero_pad1            = 0;
    // TODO: Maybe implement this setting in PrusaSlicer?
    h.per_layer_settings             = 0; // 0 to not support, 0x20 (32) for v3 ctb and 0x40 for v4 ctb files to allow per layer parameters
    h.unknown4                       = 0;
    h.unknown5                       = 1;
    h.rest_time_after_retract        = get_cfg_value<float>(cfg, "rest_time_after_retract");
    h.transition_layer_count         = get_cfg_value<uint32_t>(cfg, "faded_layers");
    h.bot_retract_speed              = get_cfg_value<float>(cfg, "sla_bot_retract_speed");
    h.zero_pad2                      = 0;
    h.four1                          = 4.0f;
    h.zero_pad3                      = 0;
    h.four2                          = 4.0f;
    h.rest_time_after_retract_repeat = h.rest_time_after_retract;
    h.rest_time_after_lift_repeat    = h.rest_time_after_lift;
    h.rest_time_before_lift          = get_cfg_value<float>(cfg, "rest_time_before_lift");
    h.unknown6                       = 2955.996f;
    h.unknown7                       = 73470;
    h.unknown8                       = 4;
    h.last_layer_index               = layer_count - 1;
    h.zero_pad4                      = 0;
    h.zero_pad5                      = 0;
    h.zero_pad6                      = 0;
    h.zero_pad7                      = 0;
    h.disclaimer_len                 = 320;
    h.zero_pad8                      = 0;
    h.zero_pad9                      = 0;
    h.zero_pad10                     = 0;
    h.zero_pad11                     = 0;

    if (layer_count < h.bot_layer_count) {
        h.bot_layer_count = layer_count;
    }
    // clang-format on
}

} // namespace

std::unique_ptr<sla::RasterBase> CtbSLAArchive::create_raster() const {
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

sla::RasterEncoder CtbSLAArchive::get_encoder() const { return CTBRasterEncoder{}; }

// Endian safe write of little endian 32bit ints
template<typename T> static void ctb_write_out(std::ofstream &out, T val) {
    for (size_t i = 0; i < sizeof(T); i++) {
        char i1 = (val & 0xFF);
        out.write((const char *) &i1, 1);
        val = val >> 8;
    }
}

static void ctb_write_out(std::ofstream &out, float val) {
    std::uint32_t *f = (std::uint32_t *) &val;
    ctb_write_out(out, *f);
}

static void ctb_write_out(std::ofstream &out, std::string val) {
    out.write(val.c_str(), val.length());
}

template<typename T> static void ctb_write_section(std::ofstream &out, T &h) {
    boost::pfr::for_each_field(h, [&out](auto &x) { ctb_write_out(out, *&x); });
}

static void ctb_write_preview(
    std::ofstream &out,
    ctb_format_preview &large,
    ctb_format_preview &small,
    ctb_format_preview_pad &preview_pad,
    int is_encrypted,
    ctb_preview_data &data
) {
    boost::pfr::for_each_field(large, [&out](auto &x) { ctb_write_out(out, x); });
    if (!is_encrypted) {
        boost::pfr::for_each_field(preview_pad, [&out](auto &x) { ctb_write_out(out, x); });
    }

    for (auto i : data.large) {
        ctb_write_out(out, (uint8_t) i);
    }

    boost::pfr::for_each_field(small, [&out](auto &x) { ctb_write_out(out, x); });
    if (!is_encrypted) {
        boost::pfr::for_each_field(preview_pad, [&out](auto &x) { ctb_write_out(out, x); });
    }

    for (auto i : data.small) {
        ctb_write_out(out, (uint8_t) i);
    }
}

static void ctb_write_print_disclaimer(
    std::ofstream &out, int reserved_size, int is_encrypted, std::string disclaimer
) {
    // Garbage data is too big for boost's for_each_field
    if (!is_encrypted) {
        for (int i = 0; i < reserved_size; i++) {
            ctb_write_out(out, (uint8_t) 0);
        }
    }

    ctb_write_out(out, disclaimer);
}

std::string xor_cipher(std::string input, std::string key) {
    std::string output;
    output.resize(input.length());
    for (long unsigned int i = 0; i < input.length(); i++) {
        output[i] = input[i] ^ key[i % key.length()];
    }
    return output;
}

int encrypt(std::string input, std::string key, std::string iv, unsigned char *encrypted_string) {
    EVP_CIPHER_CTX *ctx;
    if (!(ctx = EVP_CIPHER_CTX_new())) {
        throw;
    }

    // There are functions for reporting errors from EVP we should use them below
    // Might need to use a packed struct
    int len;
    if (!EVP_EncryptInit_ex(
            ctx, EVP_aes_256_cbc(), NULL, reinterpret_cast<const unsigned char *>(key.c_str()),
            reinterpret_cast<const unsigned char *>(iv.c_str())
        )) {
        BOOST_LOG_TRIVIAL(error) << "Error in initialization";
        throw;
    }

    if (!EVP_CIPHER_CTX_set_padding(ctx, 0)) {
        BOOST_LOG_TRIVIAL(error) << "Error in setting padding";
        throw;
    }

    if (!EVP_EncryptUpdate(
            ctx, encrypted_string, &len, reinterpret_cast<const unsigned char *>(input.c_str()),
            input.length()
        )) {
        BOOST_LOG_TRIVIAL(error) << "Error updating encryption";
        throw;
    }
    int encrypted_len = len;
    if (!EVP_EncryptFinal_ex(ctx, encrypted_string + len, &len)) {
        BOOST_LOG_TRIVIAL(error) << "Error finalizing encryption";
        throw;
    }
    encrypted_len += len;
    EVP_CIPHER_CTX_free(ctx);
    return encrypted_len;
}

void CtbSLAArchive::export_print(
    const std::string fname,
    const SLAPrint &print,
    const ThumbnailsList &thumbnails,
    const std::string & /*projectname*/
) {
    std::uint32_t layer_count = m_layers.size();

    ctb_format_header header{};
    ctb_format_preview large_preview{};
    ctb_format_preview_pad preview_pad{};
    ctb_format_preview small_preview{};
    ctb_preview_data preview_images{};
    ctb_format_print_params print_params{};
    ctb_format_slicer_info slicer_info{};
    ctb_format_print_params_v4 print_params_v4{};
    ctb_format_layer_data layer_data{};
    ctb_format_layer_data_ex layer_data_ex{};
    std::vector<uint8_t> layer_images;

    unencrypted_format_header unencrypted_header{};
    union decrypt_header {
        decrypted_format_header header_struct{};
        char buffer[sizeof(decrypted_format_header)];
    };
    union decrypt_header decrypted_header{};
    unencrypted_format_layer_header layer_header{};
    unencrypted_format_layer_pointers layer_pointers{};

    // This is all zeros- just use the size
    int reserved_size = 384;
    const std::string disclaimer_text =
        "Layout and record format for the ctb and cbddlp file types are the copyrighted programs "
        "or codes of CBD Technology (China) "
        "Inc..The Customer or User shall not in any manner reproduce, distribute, modify, "
        "decompile, disassemble, decrypt, extract, "
        "reverse engineer, lease, assign, or sublicense the said programs or codes.";

    auto &cfg = print.full_print_config();
    std::string machine_name = cfg.option("printer_model")->serialize();
    uint8_t is_encrypted = 0;
    if (machine_name == "MARS3PRO4K" || machine_name == "MARS3ULTRA4K") {
        is_encrypted = 1;
        fill_header_encrypted(unencrypted_header, decrypted_header.header_struct, print, layer_count);
        decrypted_header.header_struct.machine_name_size = machine_name.length();
    } else {
        fill_header(header, print_params, slicer_info, print_params_v4, print, layer_count);
        slicer_info.machine_name_size = machine_name.length();
    }

    preview_pad.zero_pad1 = 0;
    preview_pad.zero_pad2 = 0;
    preview_pad.zero_pad3 = 0;
    preview_pad.zero_pad4 = 0;
    fill_preview(large_preview, small_preview, preview_images, thumbnails);

    if (is_encrypted) {
        // Encryption has to happen earlier due to offset calculations
        std::string key = xor_cipher(
            "\x80\x29\xFB\x40\x10\x8D\x51\x73\x86\x2A\x50\x8D\xAD\x2E\x8E\xF5\xF8\x31\x0D\x59\xF8"
            "\x0C\xEF\xDD\x37"
            "\x70\x92\x43\xB4\x3B\x49\x8F",
            "PrusaSlicer"
        );
        std::string iv = xor_cipher(
            "\x5F\x73\x7F\x76\x64\x58\x6A\x6E\x6B\x63\x78\x5C\x7E\x78\x7A\x6E", "PrusaSlicer"
        );
        unsigned char hash[SHA256_DIGEST_LENGTH];
        unsigned char encrypted_hash[512];
        unsigned char encrypted_header[512];
        // std::string   checksum = "\xCA\xFE\xBA\xBE";
        unsigned char checksum[8] = {0xBE, 0xBA, 0xFE, 0xCA, 0x00, 0x00, 0x00, 0x00};

        unsigned int hash_len;
        EVP_MD_CTX *ctx_sha256 = EVP_MD_CTX_create();
        EVP_DigestInit_ex(ctx_sha256, EVP_sha256(), NULL);
        EVP_DigestUpdate(ctx_sha256, checksum, 8);
        EVP_DigestFinal_ex(ctx_sha256, hash, &hash_len);
        EVP_MD_CTX_destroy(ctx_sha256);

        // SHA256(reinterpret_cast<const unsigned char *>(checksum.c_str()), checksum.length(), hash);
        std::string hash_string{reinterpret_cast<char *>(hash), hash_len};

        int encrypted_len = encrypt(hash_string, key, iv, encrypted_hash);
        int header_encrypted_len = get_struct_size(decrypted_header.header_struct);

        // Fill out all the offsets now that we have the info we need
        unencrypted_header.signature_size = encrypted_len;
        unencrypted_header.encrypted_header_size = header_encrypted_len;

        unencrypted_header.encrypted_header_offset = get_struct_size(unencrypted_header);
        decrypted_header.header_struct.large_preview_offset =
            unencrypted_header.encrypted_header_offset + header_encrypted_len;
        decrypted_header.header_struct.small_preview_offset =
            decrypted_header.header_struct.large_preview_offset + get_struct_size(large_preview) +
            preview_images.large.size();
        decrypted_header.header_struct.machine_name_offset =
            decrypted_header.header_struct.small_preview_offset + get_struct_size(small_preview) +
            preview_images.small.size();
        decrypted_header.header_struct.disclaimer_offset =
            decrypted_header.header_struct.machine_name_offset + machine_name.length();
        decrypted_header.header_struct.layer_table_offset =
            decrypted_header.header_struct.disclaimer_offset + disclaimer_text.length();

        large_preview.image_offset = decrypted_header.header_struct.small_preview_offset -
            preview_images.large.size();
        small_preview.image_offset = decrypted_header.header_struct.machine_name_offset -
            preview_images.small.size();
        layer_pointers.offset = decrypted_header.header_struct.layer_table_offset;

        layer_pointers.layer_table_size = 0x58; // Always 0x58
        layer_pointers.zero_pad = 0;
        layer_header.table_size = 0x58;
        layer_header.pos_z = 0.0f;
        layer_header.encrypted_data_offset = 0;
        layer_header.encrypted_data_length = 0;
        layer_header.unknown1 = 0;
        layer_header.unknown2 = 0;
        layer_header.rest_time_before_lift = decrypted_header.header_struct.rest_time_before_lift;
        layer_header.rest_time_after_lift = decrypted_header.header_struct.rest_time_after_lift;
        layer_header.rest_time_after_retract = decrypted_header.header_struct.rest_time_after_retract;

        std::string decrypted_header_string{
            decrypted_header.buffer, get_struct_size(decrypted_header.header_struct)
        };
        header_encrypted_len = encrypt(decrypted_header_string, key, iv, encrypted_header);

        try {
            // open the file and write the contents
            std::ofstream out;
            out.open(fname, std::ios::binary | std::ios::out | std::ios::trunc);
            // Can't do this until we know where the encryption settings will live
            out.seekp(unencrypted_header.encrypted_header_offset);
            out.write(reinterpret_cast<const char *>(encrypted_header), header_encrypted_len);
            ctb_write_preview(
                out, large_preview, small_preview, preview_pad, is_encrypted, preview_images
            );

            ctb_write_out(out, machine_name);

            ctb_write_print_disclaimer(out, reserved_size, is_encrypted, disclaimer_text);

            // layers
            layer_images.reserve(layer_count * LAYER_SIZE_ESTIMATE);
            size_t i = 0;
            unsigned long layer_data_offset;
            layer_data_offset = decrypted_header.header_struct.layer_table_offset +
                get_struct_size(layer_pointers) * layer_count;

            // clang-format off
            for (const sla::EncodedRaster &rst : m_layers) {
                if (i < header.bot_layer_count) {
                    layer_header.exposure = decrypted_header.header_struct.exposure;
                    layer_header.light_off_delay = decrypted_header.header_struct.light_off_delay;
                    layer_header.lift_height = decrypted_header.header_struct.lift_height;
                    layer_header.lift_speed = decrypted_header.header_struct.lift_speed;
                    layer_header.lift_height2 = decrypted_header.header_struct.lift_height2;
                    layer_header.lift_speed2 = decrypted_header.header_struct.lift_speed2;
                    layer_header.retract_speed = decrypted_header.header_struct.retract_speed;
                    layer_header.retract_height2 = decrypted_header.header_struct.retract_height2;
                    layer_header.retract_speed2 = decrypted_header.header_struct.retract_speed2;
                    layer_header.light_pwm = decrypted_header.header_struct.pwm_level;
                } else {
                    layer_header.exposure = decrypted_header.header_struct.bot_exposure;
                    layer_header.light_off_delay = decrypted_header.header_struct.bot_light_off_delay;
                    layer_header.lift_height     = decrypted_header.header_struct.bot_lift_height;
                    layer_header.lift_speed      = decrypted_header.header_struct.bot_lift_speed;
                    layer_header.lift_height2    = decrypted_header.header_struct.bot_lift_height2;
                    layer_header.lift_speed2     = decrypted_header.header_struct.bot_lift_speed2;
                    layer_header.retract_speed   = decrypted_header.header_struct.bot_retract_speed;
                    layer_header.retract_height2 = decrypted_header.header_struct.bot_retract_height2;
                    layer_header.retract_speed2 = decrypted_header.header_struct.bot_retract_speed2;
                    layer_header.light_pwm = decrypted_header.header_struct.bot_pwm_level;
                }
                // clang-format on

                layer_pointers.page_num = layer_data_offset /
                    CTB_PAGE_SIZE; // I'm not 100% sure if I did this correctly
                layer_pointers.offset = layer_data_offset;
                ctb_write_section(out, layer_pointers);

                long curr_pos = out.tellp();
                out.seekp(layer_data_offset);
                // layer_data_offset += rst.size() + get_struct_size(layer_header);
                layer_header.layer_data_offset = layer_data_offset + get_struct_size(layer_header);
                layer_header.page_num = layer_header.layer_data_offset / CTB_PAGE_SIZE;
                layer_header.layer_data_length = rst.size();
                ctb_write_section(out, layer_header);
                // add the rle encoded layer image into the buffer
                out.write(reinterpret_cast<const char *>(rst.data()), rst.size());

                out.seekp(curr_pos);

                layer_data_offset += layer_header.layer_data_length + get_struct_size(layer_header);
                layer_header.pos_z += decrypted_header.header_struct.layer_height;
                i++;
            }

            out.seekp(layer_data_offset);
            unencrypted_header.signature_offset = layer_data_offset;
            out.write(reinterpret_cast<const char *>(encrypted_hash), encrypted_len);
            out.seekp(0);
            ctb_write_section(out, unencrypted_header);
            out.close();
        } catch (std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << e.what();
            // Rethrow the exception
            throw;
        }
    } else {
        // Fill out all the offsets now that we have the info we need

        // clang-format off
        header.small_preview_offset = header.large_preview_offset + get_struct_size(large_preview) + preview_images.large.size() +
                                      get_struct_size(preview_pad);
        header.print_params_offset = header.small_preview_offset + get_struct_size(small_preview) + preview_images.small.size() +
                                     get_struct_size(preview_pad);
        header.slicer_info_offset          = header.print_params_offset + header.print_params_size;
        slicer_info.machine_name_offset    = header.slicer_info_offset + header.slicer_info_size;
        slicer_info.print_params_v4_offset = slicer_info.machine_name_offset + machine_name.length();
        print_params_v4.disclaimer_offset  = slicer_info.print_params_v4_offset + reserved_size + get_struct_size(print_params_v4);
        header.layer_table_offset          = print_params_v4.disclaimer_offset + disclaimer_text.length();

        large_preview.image_offset = header.small_preview_offset - preview_images.large.size();
        small_preview.image_offset = header.print_params_offset - preview_images.small.size();

        layer_data.pos_z = 0.0f;
        layer_data.data_offset = header.layer_table_offset;
        layer_data.table_size = 36 +
            get_struct_size(layer_data_ex); // 36 add LayerHeaderEx table_size if v4
        layer_data.unknown1 = 0;
        layer_data.unknown2 = 0;
        layer_data_ex.rest_time_before_lift = print_params_v4.rest_time_before_lift;
        layer_data_ex.rest_time_after_lift = print_params_v4.rest_time_after_lift;
        layer_data_ex.rest_time_after_retract = print_params_v4.rest_time_after_retract;
        // clang-format on

        try {
            // open the file and write the contents
            std::ofstream out;
            out.open(fname, std::ios::binary | std::ios::out | std::ios::trunc);
            // Can't do this until we know where the encryption settings will live
            ctb_write_section(out, header);
            ctb_write_preview(
                out, large_preview, small_preview, preview_pad, is_encrypted, preview_images
            );

            ctb_write_section(out, print_params);
            ctb_write_section(out, slicer_info);
            ctb_write_out(out, machine_name);

            ctb_write_section(out, print_params_v4);
            ctb_write_print_disclaimer(out, reserved_size, is_encrypted, disclaimer_text);

            // layers
            layer_images.reserve(layer_count * LAYER_SIZE_ESTIMATE);
            size_t i = 0;
            unsigned long layer_data_offset;
            layer_data_offset = header.layer_table_offset +
                get_struct_size(layer_data) * layer_count;

            // clang-format off
            for (const sla::EncodedRaster &rst : m_layers) {
                if (i < header.bot_layer_count) {
                    layer_data.exposure           = header.bot_exposure;
                    layer_data.light_off_delay    = print_params.bot_light_off_delay;
                    layer_data_ex.lift_height     = print_params.bot_lift_height;
                    layer_data_ex.lift_speed      = print_params.bot_lift_speed;
                    layer_data_ex.lift_height2    = slicer_info.bot_lift_height2;
                    layer_data_ex.lift_speed2     = slicer_info.bot_lift_speed2;
                    layer_data_ex.retract_speed   = print_params_v4.bot_retract_speed;
                    layer_data_ex.retract_height2 = print_params_v4.bot_retract_height2;
                    layer_data_ex.retract_speed2 = print_params_v4.bot_retract_speed2;
                    layer_data_ex.light_pwm = header.bot_pwm_level;
                } else {
                    layer_data.exposure = header.exposure;
                    layer_data.light_off_delay = print_params.light_off_delay;
                    layer_data_ex.lift_height = print_params.lift_height;
                    layer_data_ex.lift_speed = print_params.lift_speed;
                    layer_data_ex.lift_height2 = slicer_info.lift_height2;
                    layer_data_ex.lift_speed2 = slicer_info.lift_speed2;
                    layer_data_ex.retract_speed = print_params.retract_speed;
                    layer_data_ex.retract_height2 = slicer_info.retract_height2;
                    layer_data_ex.retract_speed2 = slicer_info.retract_speed2;
                    layer_data_ex.light_pwm = header.pwm_level;
                }
                // clang-format on

                long curr_pos = out.tellp();
                out.seekp(layer_data_offset);

                layer_data_offset += get_struct_size(layer_data) + get_struct_size(layer_data_ex);
                // TODO: This was multiplied by anti_alias_level- find out if i need it
                layer_data.page_num = layer_data_offset /
                    CTB_PAGE_SIZE; // I'm not 100% sure if I did this correctly
                layer_data.data_offset = layer_data_offset - layer_data.page_num * CTB_PAGE_SIZE;
                layer_data.data_size = rst.size();
                layer_data_ex.tot_size = get_struct_size(layer_data) +
                    get_struct_size(layer_data_ex) + layer_data.data_size;

                ctb_write_section(out, layer_data);
                ctb_write_section(out, layer_data_ex);
                out.write(reinterpret_cast<const char *>(rst.data()), rst.size());

                out.seekp(curr_pos);
                ctb_write_section(out, layer_data);

                // add the rle encoded layer image into the buffer
                layer_data_offset += layer_data.data_size;
                layer_data.pos_z += header.layer_height;
                i++;
            }
            out.close();
        } catch (std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << e.what();
            // Rethrow the exception
            throw;
        }
    }
}
} // namespace Slic3r
