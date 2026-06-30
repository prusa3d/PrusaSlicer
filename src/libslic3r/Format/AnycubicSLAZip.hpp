#ifndef _SLIC3R_FORMAT_ANYCUBIC_SLA_ZIP_HPP_
#define _SLIC3R_FORMAT_ANYCUBIC_SLA_ZIP_HPP_

#include <string>
#include <memory>

#include "SLAArchiveWriter.hpp"
#include "SLAArchiveFormatRegistry.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

namespace Slic3r {

class SLAPrint;

class AnycubicSLAZipArchive : public SLAArchiveWriter {
    SLAPrinterConfig m_cfg;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

public:
    AnycubicSLAZipArchive() = default;
    explicit AnycubicSLAZipArchive(const SLAPrinterConfig &cfg) : m_cfg(cfg) {}
    explicit AnycubicSLAZipArchive(SLAPrinterConfig &&cfg) : m_cfg(std::move(cfg)) {}

    void export_print(
        const std::string fname,
        const SLAPrint &print,
        const ThumbnailsList &thumbnails,
        const std::string &projectname = ""
    ) override;
};

inline Slic3r::ArchiveEntry anycubic_sla_zip_format(const char *fileformat, const char *desc) {
    Slic3r::ArchiveEntry entry(fileformat);

    entry.desc = desc;
    entry.ext = fileformat;
    entry.wrfactoryfn = [](const auto &cfg) {
        return std::make_unique<AnycubicSLAZipArchive>(cfg);
    };

    return entry;
}

} // namespace Slic3r

#endif // _SLIC3R_FORMAT_ANYCUBIC_SLA_ZIP_HPP_
