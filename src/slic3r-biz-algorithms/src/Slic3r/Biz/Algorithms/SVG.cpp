#include "Slic3r/Biz/Algorithms/SVG.hpp"

#include <boost/nowide/cstdio.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

#include "Slic3r/Biz/Algorithms/Point.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Biz/Algorithms/ExPolygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r::Biz::Algorithms::SVG {

using Domain::BoundingBox2crd;
using Domain::coord_t;
using Domain::Line;
using Domain::Lines;
using Domain::Polygon;
using Domain::Polygons;
namespace bb = Biz::Algorithms::BoundingBox;
namespace expoly = Biz::Algorithms::ExPolygon;

SVG::SVG(const std::string &filename):
    arrows(false), fill("grey"), stroke("black"), flipY(false), filename(filename)
{
    this->f = boost::nowide::fopen(this->filename.c_str(), "w");
    if (f == NULL) {
        throw std::runtime_error{"Failed to open" + this->filename};
    }
    fprintf(this->f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n"
        "<svg height=\"2000\" width=\"2000\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
        "   <marker id=\"endArrow\" markerHeight=\"8\" markerUnits=\"strokeWidth\" markerWidth=\"10\" orient=\"auto\" refX=\"1\" refY=\"5\" viewBox=\"0 0 10 10\">\n"
        "      <polyline fill=\"darkblue\" points=\"0,0 10,5 0,10 1,5\" />\n"
        "   </marker>\n"
        );
    fprintf(this->f, "<rect fill='white' stroke='none' x='0' y='0' width='%f' height='%f'/>\n", 2000.f, 2000.f);
}

SVG::SVG(
    const std::string &filename,
    const BoundingBox2crd &bbox,
    const coord_t bbox_offset,
    bool flipY
):
    arrows(false),
    fill("grey"),
    stroke("black"),
    origin(bbox.min - Domain::Point(bbox_offset, bbox_offset)),
    flipY(flipY),
    filename(filename)
{
    this->f = boost::nowide::fopen(this->filename.c_str(), "w");
    if (f == NULL) {
        throw std::runtime_error{"Failed to open" + this->filename};
    }
    float w = to_svg_coord(bbox.max(0) - bbox.min(0) + 2 * bbox_offset);
    float h = to_svg_coord(bbox.max(1) - bbox.min(1) + 2 * bbox_offset);
    this->height   = h;
    fprintf(this->f,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.0//EN\" \"http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd\">\n"
        "<svg height=\"%f\" width=\"%f\" xmlns=\"http://www.w3.org/2000/svg\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
        "   <marker id=\"endArrow\" markerHeight=\"8\" markerUnits=\"strokeWidth\" markerWidth=\"10\" orient=\"auto\" refX=\"1\" refY=\"5\" viewBox=\"0 0 10 10\">\n"
        "      <polyline fill=\"darkblue\" points=\"0,0 10,5 0,10 1,5\" />\n"
        "   </marker>\n",
        h, w);
    fprintf(this->f, "<rect fill='white' stroke='none' x='0' y='0' width='%f' height='%f'/>\n", w, h);
}

SVG::~SVG() { if (f != NULL) Close(); }

void SVG::draw(const Line &line, std::string stroke, double stroke_width)
{
    fprintf(this->f,
        "   <line x1=\"%f\" y1=\"%f\" x2=\"%f\" y2=\"%f\" style=\"stroke: %s; stroke-width: %f\"",
        to_svg_x(line.a(0) - origin(0)), to_svg_y(line.a(1) - origin(1)), to_svg_x(line.b(0) - origin(0)), to_svg_y(line.b(1) - origin(1)), stroke.c_str(), (stroke_width == 0) ? 1.f : to_svg_coord(stroke_width));
    if (this->arrows)
        fprintf(this->f, " marker-end=\"url(#endArrow)\"");
    fprintf(this->f, "/>\n");
}

void SVG::draw(const Lines &lines, std::string stroke, double stroke_width)
{
    for (const Line &l : lines)
        this->draw(l, stroke, stroke_width);
}

void SVG::draw(const Domain::ExPolygon &expolygon, std::string fill, const float fill_opacity)
{
    this->fill = fill;
    
    std::string d;
    for (const Polygon &p : expoly::to_polygons(expolygon))
        d += this->get_path_d(p, true) + " ";
    this->path(d, true, 0, fill_opacity);
}

void SVG::draw_original(const Domain::ExPolygon &expolygon) {
    std::ostringstream d;
    auto write_d = [&d](const Domain::Points &pts) {
        d << "M ";
        for (const Domain::Point& p: pts)
            d << p.x() << " " << p.y() << " ";
        d << "z "; // closed path
    };
    for (const Polygon &p : expoly::to_polygons(expolygon))
        write_d(p.points);
    path(d.str(), false /*fill*/, 1 /*stroke_width*/, 0.f /*fill opacity*/);
}

void SVG::draw_outline(const Domain::ExPolygon &expolygon, std::string stroke_outer, std::string stroke_holes, double stroke_width)
{
    draw_outline(expolygon.contour, stroke_outer, stroke_width);
    for (Polygons::const_iterator it = expolygon.holes.begin(); it != expolygon.holes.end(); ++ it) {
        draw_outline(*it, stroke_holes, stroke_width);
    }
}

void SVG::draw(const Domain::ExPolygons &expolygons, std::string fill, const float fill_opacity)
{
    for (Domain::ExPolygons::const_iterator it = expolygons.begin(); it != expolygons.end(); ++it)
        this->draw(*it, fill, fill_opacity);
}

void SVG::draw_outline(const Domain::ExPolygons &expolygons, std::string stroke_outer, std::string stroke_holes, double stroke_width)
{
    for (Domain::ExPolygons::const_iterator it = expolygons.begin(); it != expolygons.end(); ++ it)
        draw_outline(*it, stroke_outer, stroke_holes, stroke_width);
}

void SVG::draw(const Polygon &polygon, std::string fill)
{
    this->fill = fill;
    this->path(this->get_path_d(polygon, true), !fill.empty(), 0, 1.f);
}

void SVG::draw(const Polygons &polygons, std::string fill)
{
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it)
        this->draw(*it, fill);
}

void SVG::draw(const Domain::Polyline &polyline, std::string stroke, double stroke_width)
{
    this->stroke = stroke;
    this->path(this->get_path_d(polyline, false), false, stroke_width, 1.f);
}

void SVG::draw(const Domain::Polylines &polylines, std::string stroke, double stroke_width)
{
    for (Domain::Polylines::const_iterator it = polylines.begin(); it != polylines.end(); ++it)
        this->draw(*it, stroke, stroke_width);
}

void SVG::draw(const Domain::Point &point, std::string fill, coord_t iradius)
{
    float radius = (iradius == 0) ? 3.f : to_svg_coord(iradius);
    std::ostringstream svg;
    svg << "   <circle cx=\"" << to_svg_x(point(0) - origin(0)) << "\" cy=\"" << to_svg_y(point(1) - origin(1))
        << "\" r=\"" << radius << "\" "
        << "style=\"stroke: none; fill: " << fill << "\" />";
    
    fprintf(this->f, "%s\n", svg.str().c_str());
}

void SVG::draw(const Domain::Points &points, std::string fill, coord_t radius)
{
    for (Domain::Points::const_iterator it = points.begin(); it != points.end(); ++it)
        this->draw(*it, fill, radius);
}

void SVG::draw_outline(const Polygon &polygon, std::string stroke, double stroke_width)
{
    this->stroke = stroke;
    this->path(this->get_path_d(polygon, true), false, stroke_width, 1.f);
}

void SVG::draw_outline(const Polygons &polygons, std::string stroke, double stroke_width)
{
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        draw_outline(*it, stroke, stroke_width);
}

void SVG::path(const std::string &d, bool fill, double stroke_width, const float fill_opacity)
{
    float lineWidth = 0.f;
    if (! fill)
        lineWidth = (stroke_width == 0) ? 2.f : to_svg_coord(stroke_width);

    fprintf(
        this->f,
        "   <path d=\"%s\" style=\"fill: %s; stroke: %s; stroke-width: %f; fill-type: evenodd\" %s fill-opacity=\"%f\" />\n",
        d.c_str(),
        fill ? this->fill.c_str() : "none",
        this->stroke.c_str(),
        lineWidth, 
        (this->arrows && !fill) ? " marker-end=\"url(#endArrow)\"" : "",
        fill_opacity
    );
}

std::string SVG::get_path_d(const Domain::MultiPoint &mp, bool closed) const
{
    std::ostringstream d;
    d << "M ";
    for (Domain::Points::const_iterator p = mp.points.begin(); p != mp.points.end(); ++p) {
        d << to_svg_x((*p)(0) - origin(0)) << " ";
        d << to_svg_y((*p)(1) - origin(1)) << " ";
    }
    if (closed) d << "z";
    return d.str();
}

void SVG::draw_text(const Domain::Point &pt, const char *text, const char *color, const double font_size)
{
    fprintf(this->f,
        R"(<text x="%f" y="%f" font-family="sans-serif" font-size="%fpx" fill="%s">%s</text>)",
        to_svg_x(float(pt.x() - origin.x())),
        to_svg_y(float(pt.y() - origin.y())),
        font_size,
        color, text);
}

void SVG::draw_legend(const Domain::Point &pt, const char *text, const char *color, const double font_size)
{
    fprintf(this->f,
        R"(<circle cx="%f" cy="%f" r="%f" fill="%s"/>)",
        to_svg_x(float(pt.x() - origin.x())),
        to_svg_y(float(pt.y() - origin.y())),
        font_size,
        color);
    fprintf(this->f,
        R"(<text x="%f" y="%f" font-family="sans-serif" font-size="%fpx" fill="%s">%s</text>)",
        to_svg_x(float(pt.x() - origin.x())) + 20.f,
        to_svg_y(float(pt.y() - origin.y())),
        font_size,
        "black", text);
}

void SVG::Close()
{
    fprintf(this->f, "</svg>\n");
    fclose(this->f);
    this->f = NULL;
//    printf("SVG written to %s\n", this->filename.c_str());
}

void SVG::export_expolygons(const std::string& path, const Domain::BoundingBox2crd &bbox, const Domain::ExPolygons &expolygons, std::string stroke_outer, std::string stroke_holes, double stroke_width)
{
    Biz::Algorithms::SVG::SVG svg(path, bbox);
    svg.draw(expolygons);
    svg.draw_outline(expolygons, stroke_outer, stroke_holes, stroke_width);
    svg.Close();
}

// Paint the expolygons in the order they are presented, thus the latter overwrites the former expolygon.
// 1) Paint all areas with the provided ExPolygonAttributes::color_fill and ExPolygonAttributes::fill_opacity.
// 2) Optionally paint outlines of the areas if ExPolygonAttributes::outline_width > 0.
//    Paint with ExPolygonAttributes::color_contour and ExPolygonAttributes::color_holes.
//    If color_contour is empty, color_fill is used. If color_hole is empty, color_contour is used.
// 3) Optionally paint points of all expolygon contours with ExPolygonAttributes::radius_points if radius_points > 0.
// 4) Paint ExPolygonAttributes::legend into legend using the ExPolygonAttributes::color_fill if legend is not empty.
void SVG::export_expolygons(const std::string& path, const std::vector<std::pair<Domain::ExPolygons, ExPolygonAttributes>> &expolygons_with_attributes)
{
    using Slic3r::Biz::Algorithms::Point::round;

    if (expolygons_with_attributes.empty())
        return;

    size_t num_legend = std::count_if(expolygons_with_attributes.begin(), expolygons_with_attributes.end(), [](const auto &v){ return ! v.second.legend.empty(); });
    // Format in num_columns.
    size_t num_columns = 3;
    // Width of the column.
    coord_t step_x = scale_(20.);
    Domain::Point legend_size(round(Domain::Vec2d{
        scale_(1.) + num_columns * step_x,
        scale_(0.4 + 1.3 * (num_legend + num_columns - 1) / num_columns)
    }).cast<coord_t>());

    Domain::BoundingBox2crd bbox = expoly::get_extents(expolygons_with_attributes.front().first);
    for (size_t i = 0; i < expolygons_with_attributes.size(); ++ i)
        bbox = bb::merge(bbox, expoly::get_extents(expolygons_with_attributes[i].first));
    // Legend y.
    coord_t pos_y  = bbox.max.y() + scale_(1.5);
    bbox = bb::merge(bbox, Domain::Point(std::max(bbox.min.x() + legend_size.x(), bbox.max.x()), bbox.max.y() + legend_size.y()));

    Biz::Algorithms::SVG::SVG svg(path, bbox);
    for (const auto &exp_with_attr : expolygons_with_attributes)
        svg.draw(exp_with_attr.first, exp_with_attr.second.color_fill, exp_with_attr.second.fill_opacity);
    for (const auto &exp_with_attr : expolygons_with_attributes) {
        if (exp_with_attr.second.outline_width > 0) {
            std::string color_contour = exp_with_attr.second.color_contour;
            if (color_contour.empty())
                color_contour = exp_with_attr.second.color_fill;
            std::string color_holes = exp_with_attr.second.color_holes;
            if (color_holes.empty())
                color_holes = color_contour;
            svg.draw_outline(exp_with_attr.first, color_contour, color_holes, exp_with_attr.second.outline_width);
        }
    }
    for (const auto &exp_with_attr : expolygons_with_attributes)
    	if (exp_with_attr.second.radius_points > 0)
			for (const Domain::ExPolygon &expoly : exp_with_attr.first)
    			svg.draw(expoly::to_points(expoly), exp_with_attr.second.color_points, exp_with_attr.second.radius_points);

    // Export legend.
    // 1st row
    coord_t pos_x0 = bbox.min.x() + scale_(1.);
    coord_t pos_x  = pos_x0;
    size_t  i_legend = 0;
    for (const auto &exp_with_attr : expolygons_with_attributes) {
        if (! exp_with_attr.second.legend.empty()) {
            svg.draw_legend(Domain::Point(pos_x, pos_y), exp_with_attr.second.legend.c_str(), exp_with_attr.second.color_fill.c_str());
            if ((++ i_legend) % num_columns == 0) {
                pos_x  = pos_x0;
                pos_y += scale_(1.3);
            } else {
                pos_x += step_x;
            }
        }
    }
    svg.Close();
}

float SVG::to_svg_coord(int x)
{
    // return x;
    return Scaling::unscaled<float>(x) * 10.f;
}

float SVG::to_svg_x(int x)
{
    return to_svg_coord(x);
}

float SVG::to_svg_y(int x) const
{
    return flipY ? this->height - to_svg_coord(x) : to_svg_coord(x);
}

} // namespace Slic3r
