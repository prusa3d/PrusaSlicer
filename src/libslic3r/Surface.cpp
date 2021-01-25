#include "BoundingBox.hpp"
#include "Surface.hpp"
#include "SVG.hpp"

namespace Slic3r {

bool
Surface::has_fill_void() const {
    return (this->surface_type & stDensVoid) != 0;
}

bool
Surface::has_fill_sparse() const {
    return (this->surface_type & stDensSparse) != 0;
}

bool
Surface::has_fill_solid() const {
    return (this->surface_type & stDensSolid) != 0;
}

bool
Surface::has_pos_external() const
{
    return has_pos_top() || has_pos_bottom();
}

bool
Surface::has_pos_top() const
{
    return (this->surface_type & stPosTop) != 0;
}

bool
Surface::has_pos_internal() const
{
    return (this->surface_type & stPosInternal) != 0;
}

bool
Surface::has_pos_bottom() const
{
    return (this->surface_type & stPosBottom) != 0;
}

bool
Surface::has_mod_bridge() const
{
    return (this->surface_type & stModBridge) != 0;
}
bool
Surface::has_mod_overBridge() const
{
    return (this->surface_type & stModOverBridge) != 0;
}

BoundingBox get_extents(const Surface &surface)
{
    return get_extents(surface.expolygon.contour);
}

BoundingBox get_extents(const Surfaces &surfaces)
{
    BoundingBox bbox;
    if (! surfaces.empty()) {
        bbox = get_extents(surfaces.front());
        for (size_t i = 1; i < surfaces.size(); ++ i)
            bbox.merge(get_extents(surfaces[i]));
    }
    return bbox;
}

BoundingBox get_extents(const SurfacesPtr &surfaces)
{
    BoundingBox bbox;
    if (! surfaces.empty()) {
        bbox = get_extents(*surfaces.front());
        for (size_t i = 1; i < surfaces.size(); ++ i)
            bbox.merge(get_extents(*surfaces[i]));
    }
    return bbox;
}

const char* surface_type_to_color_name(const SurfaceType surface_type)
{
    if ((surface_type & stPosTop) != 0) return "rgb(255,0,0)"; // "red";
    if (surface_type == (stPosBottom | stDensSolid | stModBridge)) return "rgb(0,0,255)"; // "blue";
    if ((surface_type & stPosBottom) != 0) return "rgb(0,255,0)"; // "green";
    if (surface_type == (stPosInternal | stDensSolid | stModBridge)) return "rgb(0,255,255)"; // cyan
    if (surface_type == (stPosInternal | stDensSolid | stModOverBridge)) return "rgb(0,255,128)"; // green-cyan
    if (surface_type == (stPosInternal | stDensSolid)) return "rgb(255,0,255)"; // magenta
    if (surface_type == (stPosInternal | stDensVoid)) return "rgb(128,128,128)"; // gray
    if (surface_type == (stPosInternal | stDensSparse)) return "rgb(255,255,128)"; // yellow 
    if ((surface_type & stPosPerimeter) != 0) return "rgb(128,0,0)"; // maroon
    return "rgb(64,64,64)"; //dark gray
}

Point export_surface_type_legend_to_svg_box_size()
{
    return Point(scale_(1.+10.*8.), scale_(3.)); 
}

void export_surface_type_legend_to_svg(SVG &svg, const Point &pos)
{
    // 1st row
    coord_t pos_x0 = pos(0) + scale_(1.);
    coord_t pos_x = pos_x0;
    coord_t pos_y = pos(1) + scale_(1.5);
    coord_t step_x = scale_(10.);
    svg.draw_legend(Point(pos_x, pos_y), "perimeter"      , surface_type_to_color_name(stPosPerimeter));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "top"            , surface_type_to_color_name(stPosTop));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom"         , surface_type_to_color_name(stPosBottom));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom bridge"  , surface_type_to_color_name(stPosBottom | stModBridge));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "invalid"        , surface_type_to_color_name(SurfaceType(-1)));
    // 2nd row
    pos_x = pos_x0;
    pos_y = pos(1)+scale_(2.8);
    svg.draw_legend(Point(pos_x, pos_y), "internal"       , surface_type_to_color_name(stPosInternal | stDensSparse));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "internal solid" , surface_type_to_color_name(stPosInternal | stDensSolid));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "internal bridge", surface_type_to_color_name(stPosInternal | stDensSolid | stModBridge));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "internal over bridge", surface_type_to_color_name(stPosInternal| stDensSolid | stModOverBridge));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "internal void"  , surface_type_to_color_name(stPosInternal | stDensVoid));
}

bool export_to_svg(const char *path, const Surfaces &surfaces, const float transparency)
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = surfaces.begin(); surface != surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));

    SVG svg(path, bbox);
    for (Surfaces::const_iterator surface = surfaces.begin(); surface != surfaces.end(); ++surface)
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    svg.Close();
    return true;
}

}
