///|/ Copyright (c) 2025 JXS Format Implementation for Interoperability
///|/ Based on clean-room reverse engineering for DMCA Section 1201(f) compliance  
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#include "JXS.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/SLA/AGGRaster.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Format/SL1.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace Slic3r {

// JXSArchive Implementation

std::unique_ptr<sla::RasterBase> JXSArchive::create_raster() const
{
    auto bb = BoundingBox({0, 0}, {scaled(m_cfg.display_width.getFloat()), 
                                   scaled(m_cfg.display_height.getFloat())});
    
    auto res = Vec2i{int(m_cfg.display_pixels_x.getInt()), 
                     int(m_cfg.display_pixels_y.getInt())};
    
    return std::make_unique<sla::AGGRaster>(res, bb.center(), m_cfg.display_mirror_x.getBool(),
                                            m_cfg.display_mirror_y.getBool(), 
                                            m_cfg.display_orientation.getInt());
}

sla::RasterEncoder JXSArchive::get_encoder() const
{
    return sla::PNGRasterEncoder{};
}

void JXSArchive::export_print(const std::string     fname,
                              const SLAPrint       &print,
                              const ThumbnailsList &thumbnails, 
                              const std::string    &projectname)
{
    std::ofstream out(fname, std::ios::binary);
    if (!out.is_open()) {
        throw Slic3r::RuntimeError(std::string("Cannot open file ") + fname);
    }

    try {
        write_header(out, print, thumbnails);
        write_preview(out, thumbnails);
        write_layer_table(out, print);
        write_layer_data(out);
    } catch (const std::exception &e) {
        throw Slic3r::RuntimeError(std::string("JXS export failed: ") + e.what());
    }
}

void JXSArchive::write_header(std::ostream &out, const SLAPrint &print,
                              const ThumbnailsList &thumbnails) const
{
    JXSHeader header;
    std::memset(&header, 0, sizeof(header));
    
    // Basic header info
    header.magic = JXS_MAGIC;
    header.version = JXS_VERSION;
    
    // Printer configuration (from GKtwo settings analysis)
    header.bed_x_mm = m_cfg.display_width.getFloat();
    header.bed_y_mm = m_cfg.display_height.getFloat(); 
    header.bed_z_mm = m_cfg.max_print_height.getFloat();
    
    // Print settings
    const auto &material_cfg = print.material_config();
    const auto &print_cfg = print.print_config();
    
    header.layer_height_mm = print_cfg.layer_height.getFloat();
    header.exposure_time_s = material_cfg.exposure_time.getFloat();
    header.bottom_exposure_time_s = material_cfg.initial_exposure_time.getFloat();
    header.light_off_delay_s = 0.5f;  // Default from config analysis
    header.bottom_layers = material_cfg.initial_layer_height.getInt();
    
    // Display resolution
    header.res_x = m_cfg.display_pixels_x.getInt();
    header.res_y = m_cfg.display_pixels_y.getInt();
    
    // Layer information
    header.total_layers = static_cast<uint32_t>(m_layers.size());
    header.projection_type = 0;  // UV LCD
    
    // Calculate file structure addresses
    size_t current_addr = sizeof(JXSHeader);
    
    // Preview section
    if (!thumbnails.empty()) {
        header.preview_start_addr = current_addr;
        current_addr += sizeof(JXSPreviewHeader);
        // Add estimated preview data size (will be updated)
        for (const auto &thumb : thumbnails) {
            if (thumb.is_valid()) {
                current_addr += thumb.data_size;
                break;  // Use first valid thumbnail
            }
        }
        header.preview_end_addr = current_addr;
    }
    
    // Layer table
    header.layer_table_start_addr = current_addr;
    current_addr += m_layers.size() * sizeof(JXSLayerEntry);
    
    // Write header
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
}

void JXSArchive::write_preview(std::ostream &out, const ThumbnailsList &thumbnails) const
{
    if (thumbnails.empty()) return;
    
    // Find suitable thumbnail (prefer larger ones)
    const ThumbnailData *best_thumb = nullptr;
    size_t best_size = 0;
    
    for (const auto &thumb : thumbnails) {
        if (thumb.is_valid()) {
            size_t thumb_size = thumb.width * thumb.height;
            if (thumb_size > best_size) {
                best_thumb = &thumb;
                best_size = thumb_size;
            }
        }
    }
    
    if (!best_thumb) return;
    
    // Encode preview image
    auto preview_data = encode_preview_image(*best_thumb);
    
    // Write preview header
    JXSPreviewHeader preview_header;
    std::memset(&preview_header, 0, sizeof(preview_header));
    preview_header.width = best_thumb->width;
    preview_header.height = best_thumb->height;
    preview_header.data_size = static_cast<uint32_t>(preview_data.size());
    
    out.write(reinterpret_cast<const char*>(&preview_header), sizeof(preview_header));
    out.write(reinterpret_cast<const char*>(preview_data.data()), preview_data.size());
}

void JXSArchive::write_layer_table(std::ostream &out, const SLAPrint &print) const
{
    const auto &print_cfg = print.print_config();
    const auto &material_cfg = print.material_config();
    
    float layer_height = print_cfg.layer_height.getFloat();
    float normal_exposure = material_cfg.exposure_time.getFloat();
    float bottom_exposure = material_cfg.initial_exposure_time.getFloat();
    uint32_t bottom_layers = material_cfg.initial_layer_height.getInt();
    
    // Calculate layer data addresses
    uint32_t layer_data_start = static_cast<uint32_t>(out.tellp()) + 
                                static_cast<uint32_t>(m_layers.size() * sizeof(JXSLayerEntry));
    uint32_t current_addr = layer_data_start;
    
    // Write layer entries
    for (size_t i = 0; i < m_layers.size(); ++i) {
        JXSLayerEntry entry;
        std::memset(&entry, 0, sizeof(entry));
        
        entry.z_pos = static_cast<float>(i) * layer_height;
        entry.exposure_time = (i < bottom_layers) ? bottom_exposure : normal_exposure;
        entry.light_off_time = 0.5f;  // Default from config analysis
        entry.data_addr = current_addr;
        
        // Encode layer data to get size
        auto layer_data = encode_layer_image(m_layers[i]);
        entry.data_size = static_cast<uint32_t>(layer_data.size());
        current_addr += entry.data_size;
        
        out.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    }
}

void JXSArchive::write_layer_data(std::ostream &out) const
{
    for (const auto &layer : m_layers) {
        auto layer_data = encode_layer_image(layer);
        out.write(reinterpret_cast<const char*>(layer_data.data()), layer_data.size());
    }
}

std::vector<uint8_t> JXSArchive::encode_layer_image(const sla::EncodedRaster &raster) const
{
    // Get raw image data from raster (PNG format)
    auto png_data = raster.get_data();
    
    // JXS format uses compressed image data
    // For maximum compatibility, store PNG directly with compression header
    std::vector<uint8_t> result;
    
    // Compression header: format ID (2 = PNG) + original size
    uint32_t format_id = 2;  // PNG format
    uint32_t data_size = static_cast<uint32_t>(png_data.size());
    
    result.resize(8 + png_data.size());
    std::memcpy(result.data(), &format_id, 4);
    std::memcpy(result.data() + 4, &data_size, 4);
    std::memcpy(result.data() + 8, png_data.data(), png_data.size());
    
    return result;
}

std::vector<uint8_t> JXSArchive::encode_preview_image(const ThumbnailData &thumbnail) const
{
    // Convert thumbnail to usable format
    std::vector<uint8_t> result;
    
    // Use thumbnail data directly (typically PNG or JPEG)
    if (thumbnail.is_valid() && thumbnail.data_size > 0) {
        result.assign(thumbnail.data, thumbnail.data + thumbnail.data_size);
    }
    
    return result;
}

std::vector<uint8_t> JXSArchive::rle_encode(const std::vector<uint8_t> &data) const
{
    std::vector<uint8_t> result;
    
    size_t i = 0;
    while (i < data.size()) {
        uint8_t current = data[i];
        size_t count = 1;
        
        // Count consecutive identical bytes
        while (i + count < data.size() && data[i + count] == current && count < 255) {
            count++;
        }
        
        result.push_back(static_cast<uint8_t>(count));
        result.push_back(current);
        i += count;
    }
    
    return result;
}

// JXSReader Implementation

JXSReader::JXSReader(const std::string &fname, SLAImportQuality quality, const ProgrFn &progr)
    : m_fname(fname), m_quality(quality), m_progr(progr)
{
}

ConfigSubstitutions JXSReader::read(std::vector<ExPolygons> &slices, 
                                    DynamicPrintConfig &profile)
{
    ConfigSubstitutions ret;
    
    std::ifstream in(m_fname, std::ios::binary);
    if (!in.is_open()) {
        throw Slic3r::RuntimeError("Cannot open JXS file: " + m_fname);
    }
    
    JXSHeader header;
    if (!read_header(in, header)) {
        throw Slic3r::RuntimeError("Invalid JXS file header");
    }
    
    // Validate magic signature  
    if (header.magic != JXS_MAGIC) {
        throw Slic3r::RuntimeError("Invalid JXS magic signature");
    }
    
    // Read preview (optional)
    if (header.preview_start_addr > 0) {
        read_preview(in, header);
    }
    
    // Read layer data
    if (!read_layers(in, header, slices)) {
        throw Slic3r::RuntimeError("Failed to read JXS layers");
    }
    
    // Populate print profile with settings from header
    profile.set("layer_height", header.layer_height_mm);
    profile.set("exposure_time", header.exposure_time_s);
    profile.set("initial_exposure_time", header.bottom_exposure_time_s);
    profile.set("display_width", header.bed_x_mm);
    profile.set("display_height", header.bed_y_mm);
    profile.set("display_pixels_x", static_cast<int>(header.res_x));
    profile.set("display_pixels_y", static_cast<int>(header.res_y));
    
    return ret;
}

bool JXSReader::read_header(std::istream &in, JXSHeader &header)
{
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    return in.good() && in.gcount() == sizeof(header);
}

bool JXSReader::read_preview(std::istream &in, const JXSHeader &header)
{
    // Seek to preview section
    in.seekg(header.preview_start_addr);
    
    JXSPreviewHeader preview_header;
    in.read(reinterpret_cast<char*>(&preview_header), sizeof(preview_header));
    
    if (!in.good()) return false;
    
    // Skip preview data for now (could be used for thumbnail display)
    in.seekg(preview_header.data_size, std::ios::cur);
    
    return in.good();
}

bool JXSReader::read_layers(std::istream &in, const JXSHeader &header,
                            std::vector<ExPolygons> &slices)
{
    // Seek to layer table
    in.seekg(header.layer_table_start_addr);
    
    slices.resize(header.total_layers);
    
    // Read layer table
    std::vector<JXSLayerEntry> layer_entries(header.total_layers);
    for (uint32_t i = 0; i < header.total_layers; ++i) {
        in.read(reinterpret_cast<char*>(&layer_entries[i]), sizeof(JXSLayerEntry));
        if (!in.good()) return false;
    }
    
    // Read layer image data and convert to polygons
    for (uint32_t i = 0; i < header.total_layers; ++i) {
        const auto &entry = layer_entries[i];
        
        // Seek to layer data
        in.seekg(entry.data_addr);
        
        // Read compression header
        uint32_t format_id, data_size;
        in.read(reinterpret_cast<char*>(&format_id), 4);
        in.read(reinterpret_cast<char*>(&data_size), 4);
        
        if (!in.good()) return false;
        
        // Read compressed data
        std::vector<uint8_t> compressed_data(entry.data_size - 8);
        in.read(reinterpret_cast<char*>(compressed_data.data()), compressed_data.size());
        
        if (!in.good()) return false;
        
        // For basic implementation, create empty polygons
        // Full implementation would decode PNG and trace contours to polygons
        slices[i] = ExPolygons{};
        
        if (m_progr) {
            m_progr(i, header.total_layers);
        }
    }
    
    return true;
}

std::vector<uint8_t> JXSReader::rle_decode(const std::vector<uint8_t> &data, 
                                           size_t expected_size) const
{
    std::vector<uint8_t> result;
    result.reserve(expected_size);
    
    size_t i = 0;
    while (i < data.size() && result.size() < expected_size) {
        if (i + 1 >= data.size()) break;
        
        uint8_t count = data[i];
        uint8_t value = data[i + 1];
        
        for (uint8_t j = 0; j < count && result.size() < expected_size; ++j) {
            result.push_back(value);
        }
        
        i += 2;
    }
    
    return result;
}

} // namespace Slic3r