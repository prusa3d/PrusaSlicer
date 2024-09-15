///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ExtrusionEntityCollection_hpp_
#define slic3r_ExtrusionEntityCollection_hpp_

#include <stddef.h>
#include <iterator>
#include <utility>
#include <vector>
#include <cstddef>

#include "libslic3r.h"
#include "Exception.hpp"
#include "ExtrusionEntity.hpp"
#include "libslic3r/ExtrusionRole.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Polyline.hpp"

namespace Slic3r {

#if 0
// Remove those items from extrusion_entities, that do not match role.
// Do nothing if role is mixed.
// Removed elements are NOT being deleted.
void filter_by_extrusion_role_in_place(ExtrusionEntitiesPtr &extrusion_entities, ExtrusionRole role);

// Return new vector of ExtrusionEntities* with only those items from input extrusion_entities, that match role.
// Return all extrusion entities if role is mixed.
// Returned extrusion entities are shared with the source vector, they are NOT cloned, they are considered to be owned by extrusion_entities.
inline ExtrusionEntitiesPtr filter_by_extrusion_role(const ExtrusionEntitiesPtr &extrusion_entities, ExtrusionRole role)
{
	ExtrusionEntitiesPtr out { extrusion_entities }; 
	filter_by_extrusion_role_in_place(out, role);
	return out;
}
#endif

class ExtrusionEntityCollection : public ExtrusionEntity
{
public:
    ExtrusionEntity* clone() const override;
    // Create a new object, initialize it with this object using the move semantics.
	ExtrusionEntity* clone_move() override { return new ExtrusionEntityCollection(std::move(*this)); }

    ExtrusionEntitiesPtr entities;     // we own these entities
    bool no_sort;
    ExtrusionEntityCollection(): no_sort(false) {}
    ExtrusionEntityCollection(const ExtrusionEntityCollection &other) : no_sort(other.no_sort) { this->append(other.entities); }
    ExtrusionEntityCollection(ExtrusionEntityCollection &&other) : entities(std::move(other.entities)), no_sort(other.no_sort) {}
    explicit ExtrusionEntityCollection(const ExtrusionPaths &paths);
    ExtrusionEntityCollection& operator=(const ExtrusionEntityCollection &other);
    ExtrusionEntityCollection& operator=(ExtrusionEntityCollection &&other) {
        this->clear();
        this->entities = std::move(other.entities);
        this->no_sort  = other.no_sort;
        return *this;
    }
    ~ExtrusionEntityCollection() override { clear(); }
    explicit operator ExtrusionPaths() const;

    ExtrusionEntitiesPtr::const_iterator    cbegin() const { return this->entities.cbegin(); }
    ExtrusionEntitiesPtr::const_iterator    cend()   const { return this->entities.cend(); }
    ExtrusionEntitiesPtr::const_iterator    begin()  const { return this->entities.cbegin(); }
    ExtrusionEntitiesPtr::const_iterator    end()    const { return this->entities.cend(); }
    ExtrusionEntitiesPtr::iterator          begin()        { return this->entities.begin(); }
    ExtrusionEntitiesPtr::iterator          end()          { return this->entities.end(); }

    bool is_collection() const override { return true; }
    ExtrusionRole role() const override {
        ExtrusionRole out{ ExtrusionRole::None };
        for (const ExtrusionEntity *ee : entities) {
            ExtrusionRole er = ee->role();
            out = (out == ExtrusionRole::None || out == er) ? er : ExtrusionRole::Mixed;
        }
        return out;
    }
    bool can_reverse() const override { return !this->no_sort; }
    bool empty() const { return this->entities.empty(); }
    void clear();
    void swap (ExtrusionEntityCollection &c);
    void append(const ExtrusionEntity &entity) { this->entities.emplace_back(entity.clone()); }
    void append(ExtrusionEntity &&entity) { this->entities.emplace_back(entity.clone_move()); }
    void append(const ExtrusionEntitiesPtr &entities) {
        this->entities.reserve(this->entities.size() + entities.size());
        for (const ExtrusionEntity *ptr : entities)
            this->entities.emplace_back(ptr->clone());
    }
    void append(ExtrusionEntitiesPtr &&src) {
        if (entities.empty())
            entities = std::move(src);
        else {
            entities.insert(entities.end(),
                std::make_move_iterator(src.begin()),
                std::make_move_iterator(src.end()));
            // Removing pointers to polymorphic extrusions from the donor object
            // so that they will not be deleted twice.
            src.clear();
        }
    }
    void append(const ExtrusionPaths &paths) {
        this->entities.reserve(this->entities.size() + paths.size());
        for (const ExtrusionPath &path : paths)
            this->entities.emplace_back(path.clone());
    }
    void append(ExtrusionPaths &&paths) {
        this->entities.reserve(this->entities.size() + paths.size());
        for (ExtrusionPath &path : paths)
            this->entities.emplace_back(new ExtrusionPath(std::move(path)));
    }
    void replace(size_t i, const ExtrusionEntity &entity);
    void remove(size_t i);
    void reverse() override;
    const Point& first_point() const override { return this->entities.front()->first_point(); }
    const Point& last_point() const override { return this->entities.back()->last_point(); }
    const Point& middle_point() const override { return this->entities[this->entities.size() / 2]->middle_point(); }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    size_t size() const { return entities.size(); }
    // Recursively count paths and loops contained in this collection. 
    // this->items_count() >= this->size()
    size_t items_count() const;
    /// Returns a flattened copy of this ExtrusionEntityCollection. That is, all of the items in its entities vector are not collections.
    /// You should be iterating over flatten().entities if you are interested in the underlying ExtrusionEntities (and don't care about hierarchy).
    /// \param preserve_ordering Flag to method that will flatten if and only if the underlying collection is sortable when True (default: False).
    ExtrusionEntityCollection flatten(bool preserve_ordering = false) const;
    double min_mm3_per_mm() const override;
    double total_volume() const override { double volume=0.; for (const auto& ent : entities) volume+=ent->total_volume(); return volume; }

    // Following methods shall never be called on an ExtrusionEntityCollection.
    Polyline as_polyline() const override {
        throw Slic3r::RuntimeError("Calling as_polyline() on a ExtrusionEntityCollection");
        return Polyline();
    };

    void collect_polylines(Polylines &dst) const override {
        for (const ExtrusionEntity *extrusion_entity : this->entities)
            extrusion_entity->collect_polylines(dst);
    }

    void   collect_points(Points &dst) const override {
        for (const ExtrusionEntity *extrusion_entity : this->entities)
            extrusion_entity->collect_points(dst);
    }

    double length() const override {
        throw Slic3r::RuntimeError("Calling length() on a ExtrusionEntityCollection");
        return 0.;        
    }
};

} // namespace Slic3r

#endif
