///|/ Copyright (c) 2025 JXS Format Implementation for Interoperability
///|/ Based on clean-room reverse engineering for DMCA Section 1201(f) compliance
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef _SLIC3R_FORMAT_JXS_HPP_
#define _SLIC3R_FORMAT_JXS_HPP_

#include <cstdint>
#include <string>
#include <memory>
#include <vector>

#include "SLAArchiveWriter.hpp"
#include "SLAArchiveReader.hpp" 
#include "SLAArchiveFormatRegistry.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

namespace Slic3r {

class SLAPrint;

// JXS format constants based on reverse engineering analysis
constexpr uint32_t JXS_MAGIC = 0x015853584; // 'JXS\x01' 
constexpr uint32_t JXS_VERSION = 1;

// JXS file header structure (based on CTB/CBDDLP analysis and GKtwo config)
struct JXSHeader {
    uint32_t magic;                     // Magic signature 'JXS\x01'
    uint32_t version;                   // Format version
    
    // Printer bed dimensions (from GKtwo config analysis)
    float bed_x_mm;                     // 228.1 for GKtwo
    float bed_y_mm;                     // 128.3 for GKtwo
    float bed_z_mm;                     // 245.0 max height
    
    uint8_t unknown1[12];               // Reserved/padding
    
    // Print settings
    float layer_height_mm;              // Layer height
    float exposure_time_s;              // Normal layer exposure time
    float bottom_exposure_time_s;       // Bottom layer exposure time  
    float light_off_delay_s;            // Light off delay
    uint32_t bottom_layers;             // Number of bottom layers
    
    // Display resolution (from GKtwo config analysis)
    uint32_t res_x;                     // 7680 for GKtwo
    uint32_t res_y;                     // 4320 for GKtwo
    
    // File structure pointers
    uint32_t preview_start_addr;        // Preview image address
    uint32_t layer_table_start_addr;    // Layer table address
    uint32_t preview_end_addr;          // Preview end address
    uint32_t encryption_key;            // Encryption key (0 for none)
    uint32_t slicer_start_addr;         // Slicer info address
    uint32_t slicer_end_addr;           // Slicer info end
    
    // JXS-specific fields
    uint32_t total_layers;              // Total layer count
    uint32_t projection_type;           // 0 = UV LCD
    
    uint8_t reserved[32];               // Future expansion
} __attribute__((packed));

// Layer table entry structure
struct JXSLayerEntry {
    float z_pos;                        // Layer Z position in mm
    float exposure_time;                // Layer exposure time
    float light_off_time;               // Light off time 
    uint32_t data_addr;                 // Layer data address
    uint32_t data_size;                 // Layer data size
    uint8_t unknown[16];                // Reserved
} __attribute__((packed));

// Preview image header
struct JXSPreviewHeader {
    uint32_t width;                     // Preview width
    uint32_t height;                    // Preview height
    uint32_t data_size;                 // Preview data size
    uint8_t unknown[16];                // Reserved
} __attribute__((packed));

class JXSArchive : public SLAArchiveWriter {
    SLAPrinterConfig m_cfg;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

    SLAPrinterConfig & cfg() { return m_cfg; }
    const SLAPrinterConfig & cfg() const { return m_cfg; }

public:
    JXSArchive() = default;
    explicit JXSArchive(const SLAPrinterConfig &cfg) : m_cfg(cfg) {}
    explicit JXSArchive(SLAPrinterConfig &&cfg) : m_cfg(std::move(cfg)) {}

    void export_print(const std::string     fname,
                      const SLAPrint       &print, 
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname = "") override;

private:
    // Internal helper methods
    void write_header(std::ostream &out, const SLAPrint &print, 
                      const ThumbnailsList &thumbnails) const;
    void write_preview(std::ostream &out, const ThumbnailsList &thumbnails) const;
    void write_layer_table(std::ostream &out, const SLAPrint &print) const;
    void write_layer_data(std::ostream &out) const;
    
    // Image encoding helpers
    std::vector<uint8_t> encode_layer_image(const sla::EncodedRaster &raster) const;
    std::vector<uint8_t> encode_preview_image(const ThumbnailData &thumbnail) const;
    std::vector<uint8_t> rle_encode(const std::vector<uint8_t> &data) const;
};

class JXSReader : public SLAArchiveReader {
public:
    JXSReader(const std::string &fname, SLAImportQuality quality, const ProgrFn &progr);
    
    ConfigSubstitutions read(std::vector<ExPolygons> &slices,
                            DynamicPrintConfig &profile) override;
    
private:
    std::string m_fname;
    SLAImportQuality m_quality;
    ProgrFn m_progr;
    
    // Helper methods for reading JXS files
    bool read_header(std::istream &in, JXSHeader &header);
    bool read_preview(std::istream &in, const JXSHeader &header);  
    bool read_layers(std::istream &in, const JXSHeader &header,
                     std::vector<ExPolygons> &slices);
    std::vector<uint8_t> rle_decode(const std::vector<uint8_t> &data, 
                                    size_t expected_size) const;
};

// Factory function for registry
inline ArchiveEntry create_jxs_format()
{
    return ArchiveEntry{
        "JXS",                          // id
        L("JXS archive (JuXin Slicer)"), // description
        "jxs",                          // main extension
        {},                             // no aliases
        
        // Writer factory
        [](const auto &cfg) { 
            return std::make_unique<JXSArchive>(cfg); 
        },
        
        // Reader factory  
        [](const std::string &fname, SLAImportQuality quality, const ProgrFn &progr) {
            return std::make_unique<JXSReader>(fname, quality, progr);
        }
    };
}

} // namespace Slic3r

#endif // _SLIC3R_FORMAT_JXS_HPP_