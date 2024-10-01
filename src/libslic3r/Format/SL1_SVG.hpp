///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SL1_SVG_HPP
#define SL1_SVG_HPP

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "SL1.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Format/SLAArchiveReader.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/SLA/RasterBase.hpp"

namespace Slic3r {
class DynamicPrintConfig;
class SLAPrint;

class SL1_SVGArchive: public SL1Archive {
protected:

    // Override the factory methods to produce svg instead of a real raster.
    std::unique_ptr<sla::RasterBase> create_raster() const override;
    sla::RasterEncoder get_encoder() const override;

public:

    void export_print(const std::string     fname,
                      const SLAPrint       &print,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname = "") override;

    using SL1Archive::SL1Archive;
};

class SL1_SVGReader: public SLAArchiveReader {
    std::function<bool(int)> m_progr;
    std::string m_fname;

public:
    // If the profile is missing from the archive (older PS versions did not have
    // it), profile_out's initial value will be used as fallback. profile_out will be empty on
    // function return if the archive did not contain any profile.
    ConfigSubstitutions read(std::vector<ExPolygons> &slices,
                             DynamicPrintConfig      &profile_out) override;

    ConfigSubstitutions read(DynamicPrintConfig &profile) override;

    SL1_SVGReader() = default;
    SL1_SVGReader(const std::string       &fname,
                  SLAImportQuality         /*quality*/,
                  const ProgrFn & progr)
        : m_progr(progr), m_fname(fname)
    {}
};

} // namespace Slic3r

#endif // SL1_SVG_HPP
