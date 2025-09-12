///|/ Copyright (c) 2023 Nicholas Sica @NickSica
#ifndef _SLIC3R_FORMAT_CTB_HPP_
#define _SLIC3R_FORMAT_CTB_HPP_

#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "SLAArchiveFormatRegistry.hpp"
#include "SLAArchiveWriter.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cstdint>
#include <string>

#include <boost/log/trivial.hpp>
#include <boost/pfr/core.hpp>
#include <vector>

constexpr uint16_t CTB_SLA_FORMAT_VERSION_4 = 4;

#define RLE_ENCODING_LIMIT 0xFFF
#define RGB565_REPEAT_MASK 0x20

#define LAYER_SIZE_ESTIMATE (32 * 1024)

#define CTB_PAGE_SIZE 4294967296 // 4G

constexpr uint32_t MAGIC_V2 = 0x12FD0019; // v2 (cddlp magic number)
constexpr uint32_t MAGIC_V3 = 0x12FD0086;
constexpr uint32_t MAGIC_V4 = 0x12FD0106;
constexpr uint32_t MAGIC_ENCRYPTED = 0x12FD0107;

namespace Slic3r {

// clang-format off
typedef struct ctb_format_header
{
    std::uint32_t magic;   // 0x12fd_0019 for cbddlp; 0x12fd_0086 for ctb
    std::uint32_t version; // Version number
    float bed_size_x;      // Dimensions of the printer's output in mm
    float bed_size_y;      // |
    float bed_size_z;      // |
    std::uint64_t zero_pad;
    float overall_height; // height of the model in millimeters
    float
        layer_height; // layer height used at slicing in mm; actual height used by machine in layer table
    float exposure; // exposure time setting used at slicing in seconds for normal layers; actual
                    // time in layer table
    float bot_exposure;    // exposure time setting used at slicing in seconds for bottom layers;
                           // actual time in layer table
    float light_off_delay; //
    std::uint32_t bot_layer_count; //
    std::uint32_t res_x;           //
    std::uint32_t res_y;           //
    std::uint32_t large_preview_offset;
    std::uint32_t layer_table_offset;
    std::uint32_t layer_count; // Gets the number of records in the layer table for the first level
                               // set. In ctb files, that's equivalent to the total number of records
    std::uint32_t small_preview_offset; // Gets the file offsets of ImageHeader records describing
                                        // the smaller preview images.
    std::uint32_t print_time;
    std::uint32_t projector_type;
    std::uint32_t print_params_offset;
    std::uint32_t print_params_size;
    std::uint32_t antialias_level;
    std::uint16_t pwm_level;
    std::uint16_t bot_pwm_level;
    std::uint32_t encryption_key;
    std::uint32_t slicer_info_offset;
    std::uint32_t slicer_info_size;
} ctb_format_header;

typedef struct ctb_format_preview
{
    std::uint32_t size_x;
    std::uint32_t size_y;
    std::uint32_t image_offset;
    std::uint32_t image_len;
} ctb_format_preview;

typedef struct ctb_format_preview_pad
{
    std::uint32_t zero_pad1 = 0;
    std::uint32_t zero_pad2 = 0;
    std::uint32_t zero_pad3 = 0;
    std::uint32_t zero_pad4 = 0;
} ctb_format_preview_pad;

// raw image data in RGB565 format
typedef struct ctb_preview_data
{
    std::vector<std::uint8_t> large;
    std::vector<std::uint8_t> small;
} ctb_preview_data;

typedef struct ctb_format_print_params
{
    float bot_lift_height; // In mm
    float bot_lift_speed;  // In mmpm
    float lift_height;
    float lift_speed;
    float retract_speed;
    float resin_volume_ml;
    float resin_mass_g;
    float resin_cost;
    float bot_light_off_delay; // In seconds
    float light_off_delay;
    std::uint32_t bot_layer_count;
    std::uint32_t zero_pad1 = 0;
    std::uint32_t zero_pad2 = 0;
    std::uint32_t zero_pad3 = 0;
    std::uint32_t zero_pad4 = 0;
} ctb_format_print_params;

typedef struct ctb_format_slicer_info
{
    float         bot_lift_height2;
    float         bot_lift_speed2;
    float         lift_height2;
    float         lift_speed2;
    float         retract_height2;
    float         retract_speed2;
    float         rest_time_after_lift;
    std::uint32_t machine_name_offset;
    std::uint32_t machine_name_size;
    std::uint8_t  anti_alias_flag    = 0x0F; // 0 [No AA] / 8 [AA] for cbddlp files, 7(0x7) [No AA] / 15(0x0F) [AA] for ctb files
    std::uint16_t zero_pad1          = 0;
    std::uint8_t  per_layer_settings = 0; // 0 to not support, 0x20 (32) for v3 ctb and 0x40 for v4 ctb files to allow per layer parameters
    std::uint32_t timestamp_minutes;      // Time since epoch in minutes
    std::uint32_t antialias_level;
    std::uint32_t software_version; // ctb v3 = 17171200 | ctb v4 pro = 16777216
    float rest_time_after_retract;
    float rest_time_after_lift2;
    std::uint32_t transition_layer_count;
    std::uint32_t print_params_v4_offset;
    std::uint32_t zero_pad2 = 0;
    std::uint32_t zero_pad3 = 0;
} ctb_format_slicer_info;

typedef struct ctb_format_print_params_v4
{
    float bot_retract_speed;
    float bot_retract_speed2;
    std::uint32_t zero_pad1 = 0;
    float four1 = 4.0f; // I don't think anyone knows why but ok chitu
    std::uint32_t zero_pad2 = 0;
    float         four2     = 4.0f; // I don't think anyone knows why but ok chitu
    float         rest_time_after_retract;
    float         rest_time_after_lift;
    float         rest_time_before_lift;
    float         bot_retract_height2;
    float         unknown1 = 2955.996f; // 2955.996 or uint:1161347054 but changes
    std::uint32_t unknown2 = 73470;     // 73470 but changes
    std::uint32_t unknown3 = 5;         // 5 apparently??
    std::uint32_t last_layer_index;
    std::uint32_t zero_pad3 = 0;
    std::uint32_t zero_pad4 = 0;
    std::uint32_t zero_pad5 = 0;
    std::uint32_t zero_pad6 = 0;
    std::uint32_t disclaimer_offset;
    std::uint32_t disclaimer_len = 320; // pretty much always going to be 320
} ctb_print_params_v4;

typedef struct ctb_format_layer_data
{
    float pos_z;
    float exposure; // In seconds
    float light_off_delay;
    std::uint32_t data_offset;
    std::uint32_t data_size;
    std::uint32_t page_num;
    std::uint32_t table_size; // 36 add LayerHeaderEx table_size if v4
    std::uint32_t unknown1 = 0;
    std::uint32_t unknown2 = 0;
} ctb_format_layer_data;

typedef struct ctb_format_layer_data_ex
{
    // Technically in the data, leaving it here for reference
    // Chitu wants this layer_data twice in a row, unsure why
    // ctb_format_layer_data layer_data;
    std::uint32_t tot_size;
    float lift_height;
    float lift_speed;
    float lift_height2;
    float lift_speed2;
    float retract_speed;
    float retract_height2;
    float retract_speed2;
    float rest_time_before_lift;
    float rest_time_after_lift;
    float rest_time_after_retract;
    float light_pwm;
} ctb_format_layer_data_ex;

// Encrypted CTB- unencrypted doesn't get encrypted
typedef struct unencrypted_format_header
{
    uint32_t magic;
    uint32_t encrypted_header_size;
    uint32_t encrypted_header_offset;
    uint32_t unknown1 = 0;
    uint32_t unknown2 = 0;
    uint32_t signature_size;
    uint32_t signature_offset;
    uint32_t unknown3 = 0;
    uint16_t unknown4 = 0;
    uint16_t unknown5 = 0;
    uint32_t unknown6 = 0;
    uint32_t unknown7 = 42;
    uint32_t unknown8 = 0;
} unencrypted_format_header;

#pragma pack(push, 1)
typedef struct decrypted_format_header
{
    uint64_t checksum;
    uint32_t layer_table_offset;
    float    bed_size_x;
    float    bed_size_y;
    float    bed_size_z;
    uint32_t unknown1 = 0;
    uint32_t unknown2 = 0;
    float    overall_height;
    float    layer_height;
    float    exposure;
    float    bot_exposure;
    float    light_off_delay;
    uint32_t bot_layer_count;
    uint32_t res_x;
    uint32_t res_y;
    uint32_t layer_count;
    uint32_t large_preview_offset;
    uint32_t small_preview_offset; // Gets the file offsets of ImageHeader records describing the smaller preview images.
    uint32_t print_time;
    uint32_t projector_type;
    float    bot_lift_height;
    float    bot_lift_speed;
    float    lift_height;
    float    lift_speed;
    float    retract_speed; /* might be bottom retract speed, need to verify */
    float    resin_volume_ml;
    float    resin_mass_g;
    float    resin_cost;
    float    bot_light_off_delay;
    uint32_t unknown3 = 0;
    uint16_t pwm_level;
    uint16_t bot_pwm_level;
    uint32_t layer_xor_key;
    float    bot_lift_height2;
    float    bot_lift_speed2;
    float    lift_height2;
    float    lift_speed2;
    float    retract_height2;
    float    retract_speed2;
    float    rest_time_after_lift;
    uint32_t machine_name_offset;
    uint32_t machine_name_size;
    uint8_t  anti_alias_flag    = 0x0F; // 0 [No AA] / 8 [AA] for cbddlp files, 7(0x7) [No AA] / 15(0x0F) [AA] for ctb files
    uint16_t zero_pad1          = 0;
    uint8_t  per_layer_settings = 0; // 0 to not support, 0x20 (32) for v3 ctb and 0x40 for v4 ctb files to allow per layer parameters
    uint32_t unknown4           = 0;
    uint32_t unknown5           = 1;
    float    rest_time_after_retract;
    float    rest_time_after_lift2;
    uint32_t transition_layer_count;
    float    bot_retract_speed;
    float    bot_retract_speed2;
    uint32_t zero_pad2 = 0;
    float    four1     = 4.0f; // I don't think anyone knows why but ok chitu
    uint32_t zero_pad3 = 0;
    float    four2     = 4.0f; // I don't think anyone knows why but ok chitu
    float    rest_time_after_retract_repeat;
    float    rest_time_after_lift_repeat;
    float    rest_time_before_lift;
    float    bot_retract_height2;
    float    unknown6 = 2955.996f; // 2955.996 or uint:1161347054 but changes
    uint32_t unknown7 = 73470;     // 73470 but changes
    uint32_t unknown8 = 5;         // 5 apparently??
    uint32_t last_layer_index;
    uint32_t zero_pad4 = 0;
    uint32_t zero_pad5 = 0;
    uint32_t zero_pad6 = 0;
    uint32_t zero_pad7 = 0;
    uint32_t disclaimer_offset;
    uint32_t disclaimer_len = 320; // pretty much always going to be 320
    uint32_t zero_pad8      = 0;
    uint32_t zero_pad9      = 0;
    uint32_t zero_pad10     = 0;
    uint32_t zero_pad11     = 0;
} decrypted_format_header;
#pragma pack(pop)

typedef struct unencrypted_format_layer_pointers
{
    uint32_t offset;
    uint32_t page_num;
    uint32_t layer_table_size;
    uint32_t zero_pad = 0;
} unencrypted_format_layer_pointers;

typedef struct unencrypted_format_layer_header
{
    uint32_t table_size;
    float pos_z;
    float exposure;
    float light_off_delay;
    uint32_t layer_data_offset;
    uint32_t page_num;
    uint32_t layer_data_length;
    uint32_t unknown1 = 0;
    uint32_t encrypted_data_offset;
    uint32_t encrypted_data_length;
    float lift_height;
    float lift_speed;
    float lift_height2;
    float lift_speed2;
    float retract_speed;
    float retract_height2;
    float retract_speed2;
    float rest_time_before_lift;
    float rest_time_after_lift;
    float rest_time_after_retract;
    float light_pwm;
    uint32_t unknown2 = 0;
} unencrypted_format_layer_header;
// clang-format on

class CtbSLAArchive : public SLAArchiveWriter
{
    SLAPrinterConfig m_cfg;
    // TODO: Implement other CTB versions?
    uint16_t m_version;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

    SLAPrinterConfig &cfg() { return m_cfg; }
    const SLAPrinterConfig &cfg() const { return m_cfg; }

public:
    CtbSLAArchive() = default;
    explicit CtbSLAArchive(const SLAPrinterConfig &cfg) : m_cfg(cfg), m_version(4) {}
    explicit CtbSLAArchive(SLAPrinterConfig &&cfg) : m_cfg(std::move(cfg)), m_version(4) {}
    explicit CtbSLAArchive(const SLAPrinterConfig &cfg, uint16_t version)
        : m_cfg(cfg), m_version(version) {}
    explicit CtbSLAArchive(SLAPrinterConfig &&cfg, uint16_t version)
        : m_cfg(std::move(cfg)), m_version(version) {}

    void export_print(
        const std::string fname,
        const SLAPrint &print,
        const ThumbnailsList &thumbnails,
        const std::string &projectname = ""
    ) override;
};

inline Slic3r::ArchiveEntry ctb_sla_format_versioned(
    const char *fileformat, const char *desc, uint16_t version
) {
    Slic3r::ArchiveEntry entry(fileformat);

    entry.desc = desc;
    entry.ext = fileformat;
    entry.wrfactoryfn = [version](const auto &cfg) {
        return std::make_unique<CtbSLAArchive>(cfg, version);
    };

    return entry;
}

inline Slic3r::ArchiveEntry ctb_sla_format(const char *fileformat, const char *desc) {
    return ctb_sla_format_versioned(fileformat, desc, CTB_SLA_FORMAT_VERSION_4);
}
} // namespace Slic3r

#endif // _SLIC3R_FORMAT_CTB_HPP_
