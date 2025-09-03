#ifndef _SLIC3R_FORMAT_3DP_HPP_
#define _SLIC3R_FORMAT_3DP_HPP_

#include "SLAArchiveFormatRegistry.hpp"

namespace Slic3r {

class MiiCraft3DPArchive : public SLAArchiveWriter {
    SLAPrinterConfig m_cfg;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

    SLAPrinterConfig& cfg() { return m_cfg; }
    const SLAPrinterConfig& cfg() const { return m_cfg; }

public:

    MiiCraft3DPArchive() = default;
    explicit MiiCraft3DPArchive(const SLAPrinterConfig& cfg) : m_cfg(cfg) {}
    explicit MiiCraft3DPArchive(SLAPrinterConfig&& cfg) : m_cfg(std::move(cfg)) {}

    void export_print(const std::string     fname,
        const SLAPrint& print,
        const ThumbnailsList& thumbnails,
        const std::string& projectname = "") override;
};

inline Slic3r::ArchiveEntry miicraft_3dp_format(const char* fileformat, const char* desc)
{
    Slic3r::ArchiveEntry entry(fileformat);

    entry.desc = desc;
    entry.ext = fileformat;
    entry.wrfactoryfn = [](const auto& cfg) { return std::make_unique<MiiCraft3DPArchive>(cfg); };

    return entry;
}

}

#endif