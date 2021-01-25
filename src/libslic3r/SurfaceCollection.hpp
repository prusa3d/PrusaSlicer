#ifndef slic3r_SurfaceCollection_hpp_
#define slic3r_SurfaceCollection_hpp_

#include "libslic3r.h"
#include "Surface.hpp"
#include <vector>

namespace Slic3r {

class SurfaceCollection
{
public:
    Surfaces surfaces;
    
    SurfaceCollection() {};
    SurfaceCollection(const Surfaces &surfaces) : surfaces(surfaces) {};
    SurfaceCollection(Surfaces &&surfaces) : surfaces(std::move(surfaces)) {};
    operator Polygons() const;
    operator ExPolygons() const;
    void simplify(double tolerance);
    void group(std::vector<SurfacesPtr> *retval);
    template <class T> bool any_internal_contains(const T &item) const {
        for (const Surface &surface : this->surfaces) if (surface.has_pos_internal() && surface.expolygon.contains(item)) return true;
        return false;
    }
    template <class T> bool any_bottom_contains(const T &item) const {
        for (const Surface &surface : this->surfaces) if (surface.has_pos_bottom() && surface.expolygon.contains(item)) return true;
        return false;
    }
    SurfacesConstPtr filter_by_type(const SurfaceType type) const;
    SurfacesConstPtr filter_by_type_flag(const SurfaceType allowed, const SurfaceType not_allowed = stNone) const;
    SurfacesConstPtr filter_by_types(const SurfaceType *types, int ntypes) const;
    void keep_type(const SurfaceType type);
    void keep_type_flag(const SurfaceType flags_needed, const SurfaceType flags_to_remove = stNone);
    void keep_types(const SurfaceType *types, int ntypes);
    void keep_types_flag(const SurfaceType flags_to_keep, const SurfaceType flags_to_remove = stNone);
    void remove_type(const SurfaceType type);
    void remove_types(const SurfaceType *types, int ntypes);
    void filter_by_type(const SurfaceType type, Polygons* polygons) const;
    void filter_by_type_flag(Polygons* polygons, const SurfaceType flags_needed, const SurfaceType flags_not_allowed = stNone) const;
    void set_type(SurfaceType type) {
        for (Surface &surface : this->surfaces)
            surface.surface_type = type;
    }

    void clear() { surfaces.clear(); }
    bool empty() const { return surfaces.empty(); }
	size_t size() const { return surfaces.size(); }
    bool has(SurfaceType type) const { 
        for (const Surface &surface : this->surfaces) 
            if (surface.surface_type == type) return true;
        return false;
    }

    void set(const SurfaceCollection &coll) { surfaces = coll.surfaces; }
    void set(SurfaceCollection &&coll) { surfaces = std::move(coll.surfaces); }
    void set(const ExPolygons &src, SurfaceType surfaceType) { clear(); this->append(src, surfaceType); }
    void set(const ExPolygons &src, const Surface &surfaceTempl) { clear(); this->append(src, surfaceTempl); }
    void set(const Surfaces &src) { clear(); this->append(src); }
    void set(ExPolygons &&src, SurfaceType surfaceType) { clear(); this->append(std::move(src), surfaceType); }
    void set(ExPolygons &&src, const Surface &surfaceTempl) { clear(); this->append(std::move(src), surfaceTempl); }
    void set(Surfaces &&src) { clear(); this->append(std::move(src)); }

    void append(const SurfaceCollection &coll) { this->append(coll.surfaces); }
    void append(SurfaceCollection &&coll) { this->append(std::move(coll.surfaces)); }
    void append(const ExPolygons &src, SurfaceType surfaceType) { surfaces_append(this->surfaces, src, surfaceType); }
    void append(const ExPolygons &src, const Surface &surfaceTempl) { surfaces_append(this->surfaces, src, surfaceTempl); }
    void append(const Surfaces &src) { surfaces_append(this->surfaces, src); }
    void append(ExPolygons &&src, SurfaceType surfaceType) { surfaces_append(this->surfaces, std::move(src), surfaceType); }
    void append(ExPolygons &&src, const Surface &surfaceTempl) { surfaces_append(this->surfaces, std::move(src), surfaceTempl); }
    void append(Surfaces &&src) { surfaces_append(this->surfaces, std::move(src)); }

    // For debugging purposes:
    void export_to_svg(const char *path, bool show_labels);
};

}

#endif
