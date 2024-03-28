///|/ Copyright (c) 2024 Felix Reißmann @felix-rm
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/

#pragma once

#include "SLAArchiveWriter.hpp"
#include "SLAArchiveReader.hpp"

#include "libslic3r/PrintConfig.hpp"

namespace Slic3r {

class GOOWriter : public SLAArchiveWriter
{
    SLAPrinterConfig m_cfg{};

protected:
    SLAPrinterConfig &cfg() { return m_cfg; }
    const SLAPrinterConfig &cfg() const { return m_cfg; }

    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

public:
    GOOWriter() = default;
    explicit GOOWriter(const SLAPrinterConfig &cfg) : m_cfg(cfg) {}
    explicit GOOWriter(SLAPrinterConfig &&cfg) : m_cfg(std::move(cfg)) {}

    void export_print(
        const std::string filename,
        const SLAPrint &print,
        const ThumbnailsList &thumbnails,
        const std::string &project_name = ""
    ) override;
};

class GOOReader : public SLAArchiveReader
{
    using progress_callback_t = std::function<bool(int)>;

    SLAImportQuality m_quality = SLAImportQuality::Balanced;
    progress_callback_t m_progress_callback;
    std::string m_filename;

protected:
    std::string &filename() { return m_filename; }
    const std::string &filename() const { return m_filename; }

    progress_callback_t &progress_callback() { return m_progress_callback; }
    const progress_callback_t &progress_callback() const { return m_progress_callback; }

public:
    GOOReader() = default;
    GOOReader(
        const std::string &filename, //
        SLAImportQuality quality,
        progress_callback_t progress_callback
    )
        : m_quality(quality), m_progress_callback(progress_callback), m_filename(filename) {}

    // If the profile is missing from the archive (older PS versions did not have
    // it), profile_out's initial value will be used as fallback. profile_out will be empty on
    // function return if the archive did not contain any profile.
    ConfigSubstitutions read(std::vector<ExPolygons> &slices, DynamicPrintConfig &profile_out)
        override;

    ConfigSubstitutions read(DynamicPrintConfig &profile) override { return {}; }
};

} // namespace Slic3r