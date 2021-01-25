#include "SurfaceCollection.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"

#include <map>

namespace Slic3r {

SurfaceCollection::operator Polygons() const
{
	return to_polygons(surfaces);
}

SurfaceCollection::operator ExPolygons() const
{
	return to_expolygons(surfaces);
}

void
SurfaceCollection::simplify(double tolerance)
{
    Surfaces ss;
    for (Surfaces::const_iterator it_s = this->surfaces.begin(); it_s != this->surfaces.end(); ++it_s) {
        ExPolygons expp;
        it_s->expolygon.simplify(tolerance, &expp);
        for (ExPolygons::const_iterator it_e = expp.begin(); it_e != expp.end(); ++it_e) {
            Surface s = *it_s;
            s.expolygon = *it_e;
            ss.push_back(s);
        }
    }
    this->surfaces = ss;
}

/* group surfaces by common properties */
void
SurfaceCollection::group(std::vector<SurfacesPtr> *retval)
{
    for (Surfaces::iterator it = this->surfaces.begin(); it != this->surfaces.end(); ++it) {
        // find a group with the same properties
        SurfacesPtr* group = NULL;
        for (std::vector<SurfacesPtr>::iterator git = retval->begin(); git != retval->end(); ++git)
            if (! git->empty() && surfaces_could_merge(*git->front(), *it)) {
                group = &*git;
                break;
            }
        // if no group with these properties exists, add one
        if (group == NULL) {
            retval->resize(retval->size() + 1);
            group = &retval->back();
        }
        // append surface to group
        group->push_back(&*it);
    }
}

SurfacesConstPtr
SurfaceCollection::filter_by_type(const SurfaceType type) const
{
    SurfacesConstPtr ss;
    for (const Surface & surface : this->surfaces) {
        if (surface.surface_type == type) ss.push_back(&surface);
    }
    return ss;
}

SurfacesConstPtr
SurfaceCollection::filter_by_type_flag(const SurfaceType allowed, const SurfaceType not_allowed) const
{
    SurfacesConstPtr ss;
    for (const Surface & surface : this->surfaces) {
        if ((surface.surface_type & allowed) == allowed && (surface.surface_type & not_allowed) == 0) ss.push_back(&surface);
    }
    return ss;
}

SurfacesConstPtr
SurfaceCollection::filter_by_types(const SurfaceType *types, int ntypes) const
{
    SurfacesConstPtr ss;
    for (const Surface & surface : this->surfaces) {
        for (int i = 0; i < ntypes; ++ i) {
            if (surface.surface_type == types[i]) {
                ss.push_back(&surface);
                break;
            }
        }
    }
    return ss;
}

void
SurfaceCollection::filter_by_type(const SurfaceType type, Polygons* polygons) const
{
    for (const Surface & surface : this->surfaces) {
        if (surface.surface_type == type) {
            Polygons pp = surface.expolygon;
            polygons->insert(polygons->end(), pp.begin(), pp.end());
        }
    }
}
void
SurfaceCollection::filter_by_type_flag(Polygons* polygons, const SurfaceType flags_needed, const SurfaceType flags_not_allowed) const
{
    for (const Surface & surface : this->surfaces) {
        if ((surface.surface_type & flags_needed) == flags_needed && (surface.surface_type & flags_not_allowed)==0) {
            Polygons pp = surface.expolygon;
            polygons->insert(polygons->end(), pp.begin(), pp.end());
        }
    }
}

void
SurfaceCollection::keep_type(const SurfaceType type)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if (surfaces[i].surface_type == type) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void
SurfaceCollection::keep_type_flag(const SurfaceType type_to_keep, const SurfaceType type_to_remove)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if ((surfaces[i].surface_type & type_to_keep) == type_to_keep && (surfaces[i].surface_type & type_to_remove) == 0) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void
SurfaceCollection::keep_types(const SurfaceType *types, int ntypes)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        bool keep = false;
        for (int k = 0; k < ntypes; ++ k) {
            if (surfaces[i].surface_type == types[k]) {
                keep = true;
                break;
            }
        }
        if (keep) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void
SurfaceCollection::keep_types_flag(const SurfaceType types_to_keep, const SurfaceType type_to_remove)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        if ((surfaces[i].surface_type & types_to_keep) != 0 && (surfaces[i].surface_type & type_to_remove) == 0) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void
SurfaceCollection::remove_type(const SurfaceType type)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        if (surfaces[i].surface_type != type) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void
SurfaceCollection::remove_types(const SurfaceType *types, int ntypes)
{
    size_t j = 0;
    for (size_t i = 0; i < surfaces.size(); ++ i) {
        bool remove = false;
        for (int k = 0; k < ntypes; ++ k) {
            if (surfaces[i].surface_type == types[k]) {
                remove = true;
                break;
            }
        }
        if (! remove) {
            if (j < i)
                std::swap(surfaces[i], surfaces[j]);
            ++ j;
        }
    }
    if (j < surfaces.size())
        surfaces.erase(surfaces.begin() + j, surfaces.end());
}

void SurfaceCollection::export_to_svg(const char *path, bool show_labels) 
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->surfaces.begin(); surface != this->surfaces.end(); ++surface) {
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
        if (show_labels) {
            int idx = int(surface - this->surfaces.begin());
            char label[64];
            sprintf(label, "%d", idx);
            svg.draw_text(surface->expolygon.contour.points.front(), label, "black");
        }
    }
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

}
