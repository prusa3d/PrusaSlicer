///|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "SL1_SVG.hpp"

#include <LocalesUtils.hpp>

#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Format/ZipperArchiveImport.hpp"
#include "libslic3r/Format/SL1.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Zipper.hpp"
#include "libslic3r/libslic3r.h"

#define NANOSVG_IMPLEMENTATION
#include <cstdint>
#include <algorithm>
#include <string_view>
#include <array>
#include <cmath>
#include <iterator>
#include <type_traits>
#include <utility>
#include <cstddef>

#include "nanosvg/nanosvg.h"

namespace Slic3r {
class SLAPrint;
}  // namespace Slic3r

using namespace std::literals;

namespace Slic3r {

namespace {

size_t constexpr coord_t_bufsize = 40;

// A fast and locale independent implementation of int=>str
char const* decimal_from(coord_t snumber, char* buffer)
{
    std::make_unsigned_t<coord_t> number = 0;

    char* ret = buffer;

    if( snumber < 0 ) {
        *buffer++ = '-';
        number = -snumber;
    } else
        number = snumber;

    if( number == 0 ) {
        *buffer++ = '0';
    } else {
        char* p_first = buffer;
        while( number != 0 ) {
            *buffer++ = '0' + number % 10;
            number /= 10;
        }
        std::reverse( p_first, buffer );
    }

    *buffer = '\0';

    return ret;
}

inline std::string coord2str(coord_t crd)
{
    char buf[coord_t_bufsize];
    return decimal_from(crd, buf);
}

// Apply the sla::RasterBase::Trafo onto an ExPolygon
void transform(ExPolygon &ep, const sla::RasterBase::Trafo &tr, const BoundingBox &bb)
{
    if (tr.flipXY) {
        for (auto &p : ep.contour.points) std::swap(p.x(), p.y());
        for (auto &h : ep.holes)
            for (auto &p : h.points) std::swap(p.x(), p.y());
    }

    if (tr.mirror_x){
        for (auto &p : ep.contour.points) p.x() = bb.max.x() - p.x() + bb.min.x();
        for (auto &h : ep.holes)
            for (auto &p : h.points) p.x() = bb.max.x() - p.x() + bb.min.x();
    }

    if (tr.mirror_y){
        for (auto &p : ep.contour.points) p.y() = bb.max.y() - p.y() + bb.min.y();
        for (auto &h : ep.holes)
            for (auto &p : h.points) p.y() = bb.max.y() - p.y() + bb.min.y();
    }
}

// Append the svg string representation of a Polygon to the input 'buf'
void append_svg(std::string &buf, const Polygon &poly)
{
    if (poly.points.empty())
        return;

    Point c = poly.points.front();

    char intbuf[coord_t_bufsize];

    buf += "<path d=\"M "sv;
    buf += decimal_from(c.x(), intbuf);
    buf += " "sv;
    buf += decimal_from(c.y(), intbuf);
    buf += " l "sv;

    for (const Point &p : poly) {
        Point d = p - c;
        if (d.x() == 0 && d.y() == 0)
            continue;
        buf += " "sv;
        buf += decimal_from(d.x(), intbuf);
        buf += " "sv;
        buf += decimal_from(d.y(), intbuf);
        c = p;
    }
    buf += " z\""sv; // mark path as closed
    buf += " />\n"sv;
}

} // namespace

// A fake raster from SVG
class SVGRaster : public sla::RasterBase {
    // Resolution here will be used for svg boundaries
    BoundingBox     m_bb;
    sla::Resolution m_res;
    Trafo           m_trafo;
    Vec2d           m_sc;

    std::string m_svg;

public:
    SVGRaster(const BoundingBox &svgarea, sla::Resolution res, Trafo tr = {})
        : m_bb{svgarea}
        , m_res{res}
        , m_trafo{tr}
        , m_sc{double(m_res.width_px) / m_bb.size().x(), double(m_res.height_px) / m_bb.size().y()}
    {
        // Inside the svg header, the boundaries will be defined in mm to
        // the actual bed size. The viewport is then defined to work with our
        // scaled coordinates. All the exported polygons will be in these scaled
        // coordinates but svg rendering software will interpret them correctly
        // in mm due to the header's definition.
        std::string wf = float_to_string_decimal_point(unscaled<float>(m_bb.size().x()));
        std::string hf = float_to_string_decimal_point(unscaled<float>(m_bb.size().y()));
        std::string w  = coord2str(coord_t(m_res.width_px));
        std::string h  = coord2str(coord_t(m_res.height_px));

        // Notice the header also defines the fill-rule as nonzero which should
        // generate correct results for our ExPolygons.

        // Add svg header.
        m_svg =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n"
            "<svg height=\"" + hf + "mm" + "\" width=\"" + wf + "mm" + "\" viewBox=\"0 0 " + w + " " + h +
            "\" style=\"fill: white; stroke: none; fill-rule: nonzero\" "
            "xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n";
    }

    void draw(const ExPolygon& poly) override
    {
        auto cpoly = poly;

        double tol = std::min(m_bb.size().x() / double(m_res.width_px),
                              m_bb.size().y() / double(m_res.height_px));

        ExPolygons cpolys = poly.simplify(tol);

        for (auto &cpoly : cpolys) {
            transform(cpoly, m_trafo, m_bb);

            for (auto &p : cpoly.contour.points)
                p = {std::round(p.x() * m_sc.x()), std::round(p.y() * m_sc.y())};

            for (auto &h : cpoly.holes)
                for (auto &p : h)
                    p = {std::round(p.x() * m_sc.x()), std::round(p.y() * m_sc.y())};

            append_svg(m_svg, cpoly.contour);
            for (auto &h : cpoly.holes)
                append_svg(m_svg, h);
        }
    }

    Trafo trafo() const override { return m_trafo; }

    // The encoder is ignored here, the svg text does not need any further
    // encoding.
    sla::EncodedRaster encode(sla::RasterEncoder /*encoder*/) const override
    {
        std::vector<uint8_t> data;
        constexpr auto finish = "</svg>\n"sv;

        data.reserve(m_svg.size() + std::size(finish));

        std::copy(m_svg.begin(), m_svg.end(), std::back_inserter(data));
        std::copy(finish.begin(), finish.end() - 1, std::back_inserter(data));

        return sla::EncodedRaster{std::move(data), "svg"};
    }
};

std::unique_ptr<sla::RasterBase> SL1_SVGArchive::create_raster() const
{
    auto w = cfg().display_width.getFloat();
    auto h = cfg().display_height.getFloat();

    float precision_nm = scaled<float>(cfg().sla_output_precision.getFloat());
    auto res_x = size_t(std::round(scaled(w) / precision_nm));
    auto res_y = size_t(std::round(scaled(h) / precision_nm));

    std::array<bool, 2> mirror;

    mirror[X] = cfg().display_mirror_x.getBool();
    mirror[Y] = cfg().display_mirror_y.getBool();

    auto ro = cfg().display_orientation.getInt();
    sla::RasterBase::Orientation orientation =
        ro == sla::RasterBase::roPortrait ? sla::RasterBase::roPortrait :
                                            sla::RasterBase::roLandscape;

    if (orientation == sla::RasterBase::roPortrait) {
        std::swap(w, h);
        std::swap(res_x, res_y);
    }

    BoundingBox svgarea{{0, 0}, {scaled(w), scaled(h)}};

    sla::RasterBase::Trafo tr{orientation, mirror};

    // Gamma does not really make sense in an svg, right?
    // double gamma = cfg().gamma_correction.getFloat();
    return std::make_unique<SVGRaster>(svgarea, sla::Resolution{res_x, res_y}, tr);
}

// SVG does not need additional binary encoding.
sla::RasterEncoder SL1_SVGArchive::get_encoder() const
{
    return nullptr;
}

void SL1_SVGArchive::export_print(const std::string     fname,
                                  const SLAPrint       &print,
                                  const ThumbnailsList &thumbnails,
                                  const std::string    &projectname)
{
    // Export code is completely identical to SL1, only the compression level
    // is elevated, as the SL1 has already compressed PNGs with deflate,
    // but the svg is just text.
    Zipper zipper{fname, Zipper::TIGHT_COMPRESSION};

    SL1Archive::export_print(zipper, print, thumbnails, projectname);
}

struct NanoSVGParser {
    NSVGimage *image;
    static constexpr const char *Units = "mm"; // Denotes user coordinate system
    static constexpr float Dpi = 1.f;        // Not needed
    explicit NanoSVGParser(char* input): image{nsvgParse(input, Units, Dpi)} {}
    ~NanoSVGParser() {  nsvgDelete(image); }
};

ConfigSubstitutions SL1_SVGReader::read(std::vector<ExPolygons> &slices,
                                        DynamicPrintConfig      &profile_out)
{
    std::vector<std::string> includes = { CONFIG_FNAME, PROFILE_FNAME, "svg"};
    ZipperArchive arch = read_zipper_archive(m_fname, includes, {});
    auto [profile_use, config_substitutions] = extract_profile(arch, profile_out);

    RasterParams rstp = get_raster_params(profile_use);

    struct Status
    {
        double                                 incr, val, prev;
        bool                                   stop  = false;
    } st{100. / arch.entries.size(), 0., 0.};

    for (const EntryBuffer &entry : arch.entries) {
        if (st.stop) break;

        st.val += st.incr;
        double curr = std::round(st.val);
        if (curr > st.prev) {
            st.prev = curr;
            st.stop = !m_progr(int(curr));
        }

        // Don't want to use dirty casts for the buffer to be usable in
        // the NanoSVGParser until performance is not a bottleneck here.
        auto svgtxt = reserve_vector<char>(entry.buf.size() + 1);
        std::copy(entry.buf.begin(), entry.buf.end(), std::back_inserter(svgtxt));
        svgtxt.emplace_back('\0');
        NanoSVGParser svgp(svgtxt.data());

        Polygons polys;
        for (NSVGshape *shape = svgp.image->shapes; shape != nullptr; shape = shape->next) {
            for (NSVGpath *path = shape->paths; path != nullptr; path = path->next) {
                Polygon p;
                for (int i = 0; i < path->npts; ++i) {
                    size_t c = 2 * i;
                    p.points.emplace_back(scaled(Vec2f(path->pts[c], path->pts[c + 1])));
                }
                polys.emplace_back(p);
            }
        }

        // Create the slice from the read polygons. Here, the fill rule has to
        // be the same as stated in the svg file which is `nonzero` when exported
        // using SL1_SVGArchive. Would be better to parse it from the svg file,
        // but if it's different, the file is probably corrupted anyways.
        ExPolygons expolys = union_ex(polys, ClipperLib::pftNonZero);
        invert_raster_trafo(expolys, rstp.trafo, rstp.width, rstp.height);
        slices.emplace_back(expolys);
    }

    // Compile error without the move
    return std::move(config_substitutions);
}

ConfigSubstitutions SL1_SVGReader::read(DynamicPrintConfig &out)
{
    ZipperArchive arch = read_zipper_archive(m_fname, {"prusaslicer.ini"}, {});
    return out.load(arch.profile, ForwardCompatibilitySubstitutionRule::Enable);
}

} // namespace Slic3r
