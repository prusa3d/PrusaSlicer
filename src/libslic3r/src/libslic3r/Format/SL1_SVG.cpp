/////|/ Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv
/////|/
/////|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
/////|/
#include "SL1_SVG.hpp"

#include <LocalesUtils.hpp>

#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

//#define NANOSVG_IMPLEMENTATION // ysCommented - this is no need anymore, we are linking nanosvg library in libslic3r 
#include <cstdint>
#include <algorithm>
#include <string_view>
#include <array>
#include <cmath>
#include <iterator>
#include <type_traits>
#include <utility>
#include <cstddef>
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

#include "nanosvg/nanosvg.h"

using namespace Slic3r;
using namespace Slic3r::sla;
using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Slicing;
using namespace std::literals;

namespace {

namespace BB = Biz::Algorithms::BoundingBox;

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
        , m_sc{double(m_res.width_px) / BB::sizes(m_bb).x(), double(m_res.height_px) / BB::sizes(m_bb).y()}
    {
        // Inside the svg header, the boundaries will be defined in mm to
        // the actual bed size. The viewport is then defined to work with our
        // scaled coordinates. All the exported polygons will be in these scaled
        // coordinates but svg rendering software will interpret them correctly
        // in mm due to the header's definition.
        std::string wf = float_to_string_decimal_point(unscaled<float>(BB::sizes(m_bb).x()));
        std::string hf = float_to_string_decimal_point(unscaled<float>(BB::sizes(m_bb).y()));
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

        double tol = std::min(BB::sizes(m_bb).x() / double(m_res.width_px),
                              BB::sizes(m_bb).y() / double(m_res.height_px));

        ExPolygons cpolys = Algorithms::ExPolygon::simplify(poly, tol);

        for (auto &cpoly : cpolys) {
            transform(cpoly, m_trafo, m_bb);

            for (auto &p : cpoly.contour.points)
                p = Point{static_cast<coord_t>(std::round(p.x() * m_sc.x())), static_cast<coord_t>(std::round(p.y() * m_sc.y()))};

            for (auto &h : cpoly.holes)
                for (auto &p : h)
                    p = Point{static_cast<coord_t>(std::round(p.x() * m_sc.x())), static_cast<coord_t>(std::round(p.y() * m_sc.y()))};

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

class Sl1SVGRasterizer : public ISlaRasterizer
{
    BoundingBox m_svgarea;
    Resolution m_resolution;
    RasterBase::Trafo m_tr;

public:
    explicit Sl1SVGRasterizer(const SLAPrintConfigView& cfg)
    {
        auto w = cfg.get<double>("display_width");
        auto h = cfg.get<double>("display_height");

        float precision_nm = scaled<float>(cfg.get<double>("sla_output_precision"));
        auto res_x = size_t(std::round(scaled(w) / precision_nm));
        auto res_y = size_t(std::round(scaled(h) / precision_nm));

        std::array<bool, 2> mirror;

        mirror[X] = cfg.get<bool>("display_mirror_x");
        mirror[Y] = cfg.get<bool>("display_mirror_y");

        auto ro = cfg.get<int>("display_orientation");
        sla::RasterBase::Orientation orientation = ro == sla::RasterBase::roPortrait
            ? sla::RasterBase::roPortrait
            : sla::RasterBase::roLandscape;

        if (orientation == sla::RasterBase::roPortrait) {
            std::swap(w, h);
            std::swap(res_x, res_y);
        }

        m_svgarea = BoundingBox{{0, 0}, {scaled(w), scaled(h)}};
        m_resolution = Resolution{res_x, res_y};
        m_tr = RasterBase::Trafo{orientation, mirror};
    }

    Sla::FileData create_file(const ExPolygons& slice) override
    {
        std::unique_ptr<sla::RasterBase> raster =
            std::make_unique<SVGRaster>(m_svgarea, m_resolution, m_tr);
        for (const ExPolygon& part : slice)
            raster->draw(part);

        EncodedRaster encoded_raster = raster->encode(nullptr);
        return std::move(encoded_raster.m_buffer);
    }
};
} // namespace

std::unique_ptr<ISlaRasterizer> Slic3r::create_sl1_svg_rasterizer(const SLAPrintConfigView& cfg) {
    return std::make_unique<Sl1SVGRasterizer>(cfg);
}
