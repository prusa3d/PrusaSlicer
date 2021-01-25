#ifndef slic3r_ExtrusionEntityCollection_hpp_
#define slic3r_ExtrusionEntityCollection_hpp_

#include "libslic3r.h"
#include "Exception.hpp"
#include "ExtrusionEntity.hpp"

namespace Slic3r {

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

class ExtrusionEntityCollection : public ExtrusionEntity
{
public:
    virtual ExtrusionEntityCollection* clone() const override { return new ExtrusionEntityCollection(*this); }
    // Create a new object, initialize it with this object using the move semantics.
	virtual ExtrusionEntityCollection* clone_move() override { return new ExtrusionEntityCollection(std::move(*this)); }


    /// Owned ExtrusionEntities and descendent ExtrusionEntityCollections.
    /// Iterating over this needs to check each child to see if it, too is a collection.
    ExtrusionEntitiesPtr entities;     // we own these entities
    bool no_sort;
    ExtrusionEntityCollection(): no_sort(false) {}
    ExtrusionEntityCollection(const ExtrusionEntityCollection &other) : no_sort(other.no_sort) { this->append(other.entities); }
    ExtrusionEntityCollection(ExtrusionEntityCollection &&other) : entities(std::move(other.entities)), no_sort(other.no_sort) {}
    explicit ExtrusionEntityCollection(const ExtrusionPaths &paths);
    ExtrusionEntityCollection& operator=(const ExtrusionEntityCollection &other);
    ExtrusionEntityCollection& operator=(ExtrusionEntityCollection &&other)
        { this->entities = std::move(other.entities); this->no_sort = other.no_sort; return *this; }
    ~ExtrusionEntityCollection() { clear(); }

    /// Operator to convert and flatten this collection to a single vector of ExtrusionPaths.
    explicit operator ExtrusionPaths() const;
    
    bool is_collection() const override { return true; }
    ExtrusionRole role() const override {
        ExtrusionRole out = erNone;
        for (const ExtrusionEntity *ee : entities) {
            ExtrusionRole er = ee->role();
            out = (out == erNone || out == er) ? er : erMixed;
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
            std::move(std::begin(src), std::end(src), std::back_inserter(entities));
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
    static ExtrusionEntityCollection chained_path_from(const ExtrusionEntitiesPtr &extrusion_entities, const Point &start_near, ExtrusionRole role = erMixed);
    ExtrusionEntityCollection chained_path_from(const Point &start_near, ExtrusionRole role = erNone) const {
        if (role == erNone) role = this->role();
        if( this->no_sort || (role == erMixed) )
            return *this;
        else
            return chained_path_from(this->entities, start_near, role); 
    }
    void reverse() override;
    const Point& first_point() const override { return this->entities.front()->first_point(); }
    const Point& last_point() const override { return this->entities.back()->last_point(); }
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

    /// Recursively count paths and loops contained in this collection 
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
        for (ExtrusionEntity* extrusion_entity : this->entities)
            extrusion_entity->collect_polylines(dst);
    }

    double length() const override {
        throw Slic3r::RuntimeError("Calling length() on a ExtrusionEntityCollection");
        return 0.;        
    }
    virtual void visit(ExtrusionVisitor &visitor) { visitor.use(*this); };
    virtual void visit(ExtrusionVisitorConst &visitor) const { visitor.use(*this); };
};

//// visitors /////

class CountEntities : public ExtrusionVisitorConst {
public:
    size_t count(const ExtrusionEntity &coll) { coll.visit(*this); return leaf_number; }
    size_t leaf_number = 0;
    virtual void default_use(const ExtrusionEntity &entity) override { ++leaf_number; }
    virtual void use(const ExtrusionEntityCollection &coll) override;
};

class FlatenEntities : public ExtrusionVisitorConst {
    ExtrusionEntityCollection to_fill;
    bool preserve_ordering;
public:
    FlatenEntities(bool preserve_ordering) : preserve_ordering(preserve_ordering) {}
    FlatenEntities(ExtrusionEntityCollection pattern, bool preserve_ordering) : preserve_ordering(preserve_ordering) {
        to_fill.no_sort = pattern.no_sort;
    }
    ExtrusionEntityCollection get() {
        return to_fill;
    };
    ExtrusionEntityCollection&& flatten(const ExtrusionEntityCollection &to_flatten) &&;
    virtual void default_use(const ExtrusionEntity &entity) override { to_fill.append(entity); }
    virtual void use(const ExtrusionEntityCollection &coll) override;
};

} // namespace Slic3r

#endif
