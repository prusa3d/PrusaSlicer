///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/ Copyright (c) 2023 Mimoja @Mimoja
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef _SLIC3R_FORMAT_PWMX_HPP_
#define _SLIC3R_FORMAT_PWMX_HPP_

#include <stdint.h>
#include <string>
#include <memory>
#include <utility>
#include <cinttypes>

#include "SLAArchiveWriter.hpp"
#include "SLAArchiveFormatRegistry.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

namespace Slic3r {
class SLAPrint;
}  // namespace Slic3r

constexpr uint16_t ANYCUBIC_SLA_FORMAT_VERSION_1 = 1;
constexpr uint16_t ANYCUBIC_SLA_FORMAT_VERSION_515 = 515;
constexpr uint16_t ANYCUBIC_SLA_FORMAT_VERSION_516 = 516;
constexpr uint16_t ANYCUBIC_SLA_FORMAT_VERSION_517 = 517;

namespace Slic3r {

class AnycubicSLAArchive: public SLAArchiveWriter {
    SLAPrinterConfig m_cfg;
    uint16_t m_version;

protected:
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

    SLAPrinterConfig & cfg() { return m_cfg; }
    const SLAPrinterConfig & cfg() const { return m_cfg; }

public:
    
    AnycubicSLAArchive() = default;
    explicit AnycubicSLAArchive(const SLAPrinterConfig &cfg):
        m_cfg(cfg), m_version(ANYCUBIC_SLA_FORMAT_VERSION_1) {}
    explicit AnycubicSLAArchive(SLAPrinterConfig &&cfg):
        m_cfg(std::move(cfg)), m_version(ANYCUBIC_SLA_FORMAT_VERSION_1) {}

    explicit AnycubicSLAArchive(const SLAPrinterConfig &cfg, uint16_t version):
        m_cfg(cfg), m_version(version) {}
    explicit AnycubicSLAArchive(SLAPrinterConfig &&cfg, uint16_t version):
        m_cfg(std::move(cfg)), m_version(version) {}

    void export_print(const std::string     fname,
                      const SLAPrint       &print,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname = "") override;
};

inline Slic3r::ArchiveEntry anycubic_sla_format_versioned(const char *fileformat, const char *desc, uint16_t version)
{
    Slic3r::ArchiveEntry entry(fileformat);

    entry.desc = desc;
    entry.ext  = fileformat;
    entry.wrfactoryfn = [version] (const auto &cfg) { return std::make_unique<AnycubicSLAArchive>(cfg, version); };

    return entry;
}

inline Slic3r::ArchiveEntry anycubic_sla_format(const char *fileformat, const char *desc)
{
    return anycubic_sla_format_versioned(fileformat, desc, ANYCUBIC_SLA_FORMAT_VERSION_1);
}

} // namespace Slic3r::sla

#endif // _SLIC3R_FORMAT_PWMX_HPP_
