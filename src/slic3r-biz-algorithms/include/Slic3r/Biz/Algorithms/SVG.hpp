#ifndef slic3r_SVG_hpp_
#define slic3r_SVG_hpp_

#include <stdio.h>
#include <string>
#include <utility>
#include <vector>
#include <cstdio>

#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Line.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/MultiPoint.hpp"
#include "Slic3r/Domain/Point.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Polyline.hpp"

namespace Slic3r::Biz::Algorithms::SVG {

class SVG
{
public:
    bool arrows;
    std::string fill, stroke;
    Domain::Point origin;
    float height;
    bool  flipY;

    SVG(const std::string &filename);
    SVG(
        const std::string &filename,
        const Domain::BoundingBox2crd &bbox,
        const Domain::coord_t bbox_offset = scale_(1.),
        bool flipY = true
    );
    ~SVG();

    void draw(const Domain::Line &line, std::string stroke = "black", double stroke_width = 0);
    void draw(const Domain::Lines &lines, std::string stroke = "black", double stroke_width = 0);

    void draw(const Domain::ExPolygon &expolygon, std::string fill = "grey", const float fill_opacity=1.f);
    void draw_outline(const Domain::ExPolygon &polygon, std::string stroke_outer = "black", std::string stroke_holes = "blue", double stroke_width = 0);
    void draw(const Domain::ExPolygons &expolygons, std::string fill = "grey", const float fill_opacity=1.f);
    void draw_outline(const Domain::ExPolygons &polygons, std::string stroke_outer = "black", std::string stroke_holes = "blue", double stroke_width = 0);

    void draw(const Domain::Polygon &polygon, std::string fill = "grey");
    void draw_outline(const Domain::Polygon &polygon, std::string stroke = "black", double stroke_width = 0);
    void draw(const Domain::Polygons &polygons, std::string fill = "grey");
    void draw_outline(const Domain::Polygons &polygons, std::string stroke = "black", double stroke_width = 0);
    void draw(const Domain::Polyline &polyline, std::string stroke = "black", double stroke_width = 0);
    void draw(const Domain::Polylines &polylines, std::string stroke = "black", double stroke_width = 0);
    void draw(const Domain::Point &point, std::string fill = "black", Domain::coord_t radius = 0);
    void draw(const Domain::Points &points, std::string fill = "black", Domain::coord_t radius = 0);

    void draw_text(const Domain::Point &pt, const char *text, const char *color, double font_size = 20.f);
    void draw_legend(const Domain::Point &pt, const char *text, const char *color, double font_size = 10.f);

    // Draw no scaled expolygon coordinates
    void draw_original(const Domain::ExPolygon &exPoly);

    void Close();

    private:
    std::string filename;
    FILE* f;

    void path(const std::string &d, bool fill, double stroke_width, const float fill_opacity);
    std::string get_path_d(const Domain::MultiPoint &mp, bool closed = false) const;

public:
    static void export_expolygons(
        const std::string &path,
        const Domain::BoundingBox2crd &bbox,
        const Domain::ExPolygons &expolygons,
        std::string stroke_outer = "black",
        std::string stroke_holes = "blue",
        double stroke_width = 0
    );

    static void export_expolygons(
        const std::string &path,
        const Domain::ExPolygons &expolygons,
        std::string stroke_outer = "black",
        std::string stroke_holes = "blue",
        double stroke_width = 0
    );

    struct ExPolygonAttributes
    {
        ExPolygonAttributes() : ExPolygonAttributes("gray", "black", "blue") {}
        ExPolygonAttributes(const std::string &color) :
            ExPolygonAttributes(color, color, color) {}

        ExPolygonAttributes(
            const std::string &color_fill,
            const std::string &color_contour,
            const std::string &color_holes,
            const Domain::coord_t      outline_width = scale_(0.05),
            const float        fill_opacity  = 0.5f,
            const std::string &color_points = "black",
            const Domain::coord_t      radius_points = 0) :
            color_fill      (color_fill),
            color_contour   (color_contour),
            color_holes     (color_holes),
            color_points 	(color_points),
            outline_width   (outline_width),
            fill_opacity    (fill_opacity),
            radius_points	(radius_points)
            {}

        ExPolygonAttributes(
            const std::string &legend,
            const std::string &color_fill,
            const std::string &color_contour,
            const std::string &color_holes,
            const Domain::coord_t      outline_width = scale_(0.05),
            const float        fill_opacity  = 0.5f,
            const std::string &color_points = "black",
            const Domain::coord_t      radius_points = 0) :
            legend          (legend),
            color_fill      (color_fill),
            color_contour   (color_contour),
            color_holes     (color_holes),
            color_points    (color_points),
            outline_width   (outline_width),
            fill_opacity    (fill_opacity),
            radius_points   (radius_points)
            {}

        ExPolygonAttributes(
            const std::string &legend,
            const std::string &color_fill,
            const float        fill_opacity) :
            legend          (legend),
            color_fill      (color_fill),
            fill_opacity    (fill_opacity)
            {}

        std::string     legend;
        std::string     color_fill;
        std::string     color_contour;
        std::string     color_holes;
        std::string   	color_points;
        Domain::coord_t         outline_width { 0 };
        float           fill_opacity;
        Domain::coord_t			radius_points { 0 };
    };

    // Paint the expolygons in the order they are presented, thus the latter overwrites the former expolygon.
    // 1) Paint all areas with the provided ExPolygonAttributes::color_fill and ExPolygonAttributes::fill_opacity.
    // 2) Optionally paint outlines of the areas if ExPolygonAttributes::outline_width > 0.
    //    Paint with ExPolygonAttributes::color_contour and ExPolygonAttributes::color_holes.
    //    If color_contour is empty, color_fill is used. If color_hole is empty, color_contour is used.
    // 3) Optionally paint points of all expolygon contours with ExPolygonAttributes::radius_points if radius_points > 0.
    // 4) Paint ExPolygonAttributes::legend into legend using the ExPolygonAttributes::color_fill if legend is not empty. 
    static void export_expolygons(const char *path, const std::vector<std::pair<Domain::ExPolygons, ExPolygonAttributes>> &expolygons_with_attributes);
    static void export_expolygons(const std::string &path, const std::vector<std::pair<Domain::ExPolygons, ExPolygonAttributes>> &expolygons_with_attributes);

private:
    static float to_svg_coord(int x);
    static float to_svg_x(int x);
    float to_svg_y(int x) const;
};

}

#endif
