///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef ARR2_SCENE_HPP
#define ARR2_SCENE_HPP

#include <stddef.h>
#include <boost/variant.hpp>
#include <boost/variant/variant.hpp>
#include <any>
#include <string_view>
#include <algorithm>
#include <functional>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>
#include <cstddef>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/AnyPtr.hpp"
#include "libslic3r/Arrange/ArrangeSettingsView.hpp"
#include "libslic3r/Arrange/SegmentedRectangleBed.hpp"
#include "libslic3r/Arrange/Core/Beds.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r { namespace arr2 {

// This module contains all the necessary high level interfaces for
// arrangement. No dependency on the rest of libslic3r is intoduced here. (No
// Model, ModelObject, etc...) except for ObjectID.


// An interface that allows to store arbitrary data (std::any) under a specific
// key in an object implementing the interface. This is later used to pass
// arbitrary parameters from any arrangeable object down to the arrangement core.
class AnyWritable
{
public:
    virtual ~AnyWritable() = default;

    virtual void write(std::string_view key, std::any d) = 0;
};

// The interface that captures the objects which are actually moved around.
// Implementations must provide means to extract the 2D outline that is used
// by the arrangement core.
class Arrangeable
{
public:
    virtual ~Arrangeable() = default;

    // ID is implementation specific, must uniquely identify an Arrangeable
    // object.
    virtual ObjectID id() const = 0;

    // This is different than id(), and identifies an underlying group into
    // which the Arrangeable belongs. Can be used to group arrangeables sharing
    // the same outline.
    virtual ObjectID   geometry_id() const    = 0;

    // Outline extraction can be a demanding operation, so there is a separate
    // method the extract the full outline of an object and the convex hull only
    // It will depend on the arrangement config to choose which one is called.
    // convex_outline might be considerably faster than calling full_outline()
    // and then calculating the convex hull from that.
    virtual ExPolygons full_outline() const   = 0;
    virtual Polygon    convex_outline() const = 0;

    // Envelope is the boundary that an arrangeble object might have which
    // is used when the object is being placed or moved around. Once it is
    // placed, the outline (convex or full) will be used to determine the
    // boundaries instead of the envelope. This concept can be used to
    // implement arranging objects with support structures that can overlap,
    // but never touch the actual object. In this case, full envelope would
    // return the silhouette of the object with supports (pad, brim, etc...) and
    // outline would be the actual object boundary.
    virtual ExPolygons full_envelope() const { return {}; }
    virtual Polygon    convex_envelope() const { return {}; }

    // Write the transformations determined by the arrangement into the object
    virtual void transform(const Vec2d &transl, double rot) = 0;

    // An arrangeable can be printable or unprintable, they should not be on
    // the same bed. (See arrange tasks)
    virtual bool is_printable() const { return true; }

    // An arrangeable can be selected or not, this will determine if treated
    // as static objects or movable ones.
    virtual bool is_selected() const { return true; }

    // Determines the order in which the objects are arranged. Higher priority
    // objects are arranged first.
    virtual int  priority() const { return 0; }

    // Any implementation specific properties can be passed to the arrangement
    // core by overriding this method. This implies that the specific Arranger
    // will be able to interpret these properties. An example usage is to mark
    // special objects (like a wipe tower)
    virtual void imbue_data(AnyWritable &datastore) const {}

    // for convinience to pass an AnyWritable created in the same expression
    // as the method call
    void imbue_data(AnyWritable &&datastore) const { imbue_data(datastore); }

    // An Arrangeable might reside on a logical bed instead of the real one
    // in case that the arrangement can not fit it onto the real bed. Handling
    // of logical beds is also implementation specific and are specified with
    // the next two methods:

    // Returns the bed index on which the given Arrangeable is sitting.
    virtual int get_bed_index() const = 0;

    // Assign the Arrangeable to the given bed index. Note that this
    // method can return false, indicating that the given bed is not available
    // to be occupied.
    virtual bool assign_bed(int bed_idx) = 0;
};

// Arrangeable objects are provided by an ArrangeableModel which is also able to
// create new arrangeables given a prototype id to copy.
class ArrangeableModel
{
public:
    virtual ~ArrangeableModel() = default;

    // Visit all arrangeable in this model and call the provided visitor
    virtual void for_each_arrangeable(std::function<void(Arrangeable &)>) = 0;
    virtual void for_each_arrangeable(std::function<void(const Arrangeable&)>) const = 0;

    // Visit a specific arrangeable identified by it's id
    virtual void visit_arrangeable(const ObjectID &id, std::function<void(const Arrangeable &)>) const = 0;
    virtual void visit_arrangeable(const ObjectID &id, std::function<void(Arrangeable &)>) = 0;

    // Add a new arrangeable which is a copy of the one matching prototype_id
    // Return the new object id or an invalid id if the new object was not
    // created.
    virtual ObjectID add_arrangeable(const ObjectID &prototype_id) = 0;

    size_t arrangeable_count() const
    {
        size_t cnt = 0;
        for_each_arrangeable([&cnt](auto &) { ++cnt; });

        return cnt;
    }
};

// The special bed type used by XL printers
using XLBed = SegmentedRectangleBed<std::integral_constant<size_t, 4>,
                                    std::integral_constant<size_t, 4>>;

// ExtendedBed is a variant type holding all bed types supported by the
// arrange core and the additional XLBed

template<class... Args> struct ExtendedBed_
{
    using Type =
        boost::variant<XLBed, /* insert other types if needed*/ Args...>;
};

template<class... Args> struct ExtendedBed_<boost::variant<Args...>>
{
    using Type = boost::variant<XLBed, Args...>;
};

using ExtendedBed = typename ExtendedBed_<ArrangeBed>::Type;

template<class BedFn> void visit_bed(BedFn &&fn, const ExtendedBed &bed)
{
    boost::apply_visitor(fn, bed);
}

template<class BedFn> void visit_bed(BedFn &&fn, ExtendedBed &bed)
{
    boost::apply_visitor(fn, bed);
}

inline BoundingBox bounding_box(const ExtendedBed &bed)
{
    BoundingBox bedbb;
    visit_bed([&bedbb](auto &rawbed) { bedbb = bounding_box(rawbed); }, bed);

    return bedbb;
}

class Scene;

// SceneBuilderBase is intended for Scene construction. A simple constructor
// is not enough here to capture all the possible ways of constructing a Scene.
// Subclasses of SceneBuilderBase can add more domain specific methods and
// overloads. An rvalue object of this class is handed over to the Scene
// constructor which can then establish itself using the provided builder.

// A little CRTP is used to implement fluent interface returning Subclass
// references.
template<class Subclass>
class SceneBuilderBase
{
protected:
    AnyPtr<ArrangeableModel> m_arrangeable_model;

    AnyPtr<const ArrangeSettingsView> m_settings;

    ExtendedBed m_bed = arr2::InfiniteBed{};

    coord_t m_brims_offs = 0;
    coord_t m_skirt_offs = 0;

public:

    virtual ~SceneBuilderBase() = default;

    SceneBuilderBase() = default;
    SceneBuilderBase(const SceneBuilderBase &) = delete;
    SceneBuilderBase& operator=(const SceneBuilderBase &) = delete;
    SceneBuilderBase(SceneBuilderBase &&) = default;
    SceneBuilderBase& operator=(SceneBuilderBase &&) = default;

    // All setters return an rvalue reference so that at the end, the
    // build_scene method can be called fluently

    Subclass &&set_arrange_settings(AnyPtr<const ArrangeSettingsView> settings)
    {
        m_settings = std::move(settings);
        return std::move(static_cast<Subclass&>(*this));
    }

    Subclass &&set_arrange_settings(const ArrangeSettingsView &settings)
    {
        m_settings = std::make_unique<ArrangeSettings>(settings);
        return std::move(static_cast<Subclass&>(*this));
    }

    Subclass &&set_bed(const Points &pts)
    {
        m_bed = arr2::to_arrange_bed(pts);
        return std::move(static_cast<Subclass&>(*this));
    }

    Subclass && set_bed(const arr2::ArrangeBed &bed)
    {
        m_bed = bed;
        return std::move(static_cast<Subclass&>(*this));
    }

    Subclass &&set_bed(const XLBed &bed)
    {
        m_bed = bed;
        return std::move(static_cast<Subclass&>(*this));
    }

    Subclass &&set_arrangeable_model(AnyPtr<ArrangeableModel> model)
    {
        m_arrangeable_model = std::move(model);
        return std::move(static_cast<Subclass&>(*this));
    }

    // Can only be called on an rvalue instance (hence the && at the end),
    // the method will potentially move its content into sc
    virtual void build_scene(Scene &sc) &&;
};

class BasicSceneBuilder: public SceneBuilderBase<BasicSceneBuilder> {};

// The Scene class captures all data needed to do an arrangement.
class Scene
{
    template <class Sub> friend class SceneBuilderBase;

    // These fields always need to be initialized to valid objects after
    // construction of Scene which is ensured by the SceneBuilder
    AnyPtr<ArrangeableModel>                m_amodel;
    AnyPtr<const ArrangeSettingsView>       m_settings;
    ExtendedBed m_bed;

public:
    // Scene can only be built from an rvalue SceneBuilder whose content will
    // potentially be moved to the constructed Scene object.
    template<class Sub>
    explicit Scene(SceneBuilderBase<Sub> &&bld)
    {
        std::move(bld).build_scene(*this);
    }

    const ArrangeableModel &model() const noexcept { return *m_amodel; }
    ArrangeableModel       &model() noexcept { return *m_amodel; }

    const ArrangeSettingsView &settings() const noexcept { return *m_settings; }

    template<class BedFn> void visit_bed(BedFn &&fn) const
    {
        arr2::visit_bed(fn, m_bed);
    }

    const ExtendedBed & bed() const { return m_bed; }

    std::vector<ObjectID> selected_ids() const;
};

// Get all the ObjectIDs of Arrangeables which are in selected state
std::set<ObjectID> selected_geometry_ids(const Scene &sc);

// A dummy, empty ArrangeableModel for testing and as placeholder to avoiod using nullptr
class EmptyArrangeableModel: public ArrangeableModel
{
public:
    void for_each_arrangeable(std::function<void(Arrangeable &)>) override {}
    void for_each_arrangeable(std::function<void(const Arrangeable&)>) const override {}
    void visit_arrangeable(const ObjectID &id, std::function<void(const Arrangeable &)>) const override {}
    void visit_arrangeable(const ObjectID &id, std::function<void(Arrangeable &)>) override {}
    ObjectID add_arrangeable(const ObjectID &prototype_id) override { return {}; }
};

template<class Subclass>
void SceneBuilderBase<Subclass>::build_scene(Scene &sc) &&
{
    if (!m_arrangeable_model)
        m_arrangeable_model = std::make_unique<EmptyArrangeableModel>();

    if (!m_settings)
        m_settings = std::make_unique<arr2::ArrangeSettings>();

    // Apply the bed minimum distance by making the original bed smaller
    // and arranging on this smaller bed.
    coord_t inset = std::max(scaled(m_settings->get_distance_from_bed()),
                             m_skirt_offs + m_brims_offs);

    // Objects have also a minimum distance from each other implemented
    // as inflation applied to object outlines. This object distance
    // does not apply to the bed, so the bed is inflated by this amount
    // to compensate.
    coord_t md = scaled(m_settings->get_distance_from_objects());
    md = md / 2 - inset;

    // Applying the final bed with the corrected dimensions to account
    // for safety distances
    visit_bed([md](auto &rawbed) { rawbed = offset(rawbed, md); }, m_bed);

    sc.m_settings = std::move(m_settings);
    sc.m_amodel = std::move(m_arrangeable_model);
    sc.m_bed = std::move(m_bed);
}

// Arrange tasks produce an object implementing this interface. The arrange
// result can be applied to an ArrangeableModel which may or may not succeed.
// The ArrangeableModel could be in a different state (it's objects may have
// changed or removed) than it was at the time of arranging.
class ArrangeResult
{
public:
    virtual ~ArrangeResult() = default;

    virtual bool apply_on(ArrangeableModel &mdlwt) = 0;
};

enum class Tasks { Arrange, FillBed };

class ArrangeTaskCtl
{
public:
    virtual ~ArrangeTaskCtl() = default;

    virtual void update_status(int st) = 0;

    virtual bool was_canceled() const = 0;
};

class DummyCtl : public ArrangeTaskCtl
{
public:
    void update_status(int) override {}
    bool was_canceled() const override { return false; }
};

class ArrangeTaskBase
{
public:
    using Ctl = ArrangeTaskCtl;

    virtual ~ArrangeTaskBase() = default;

    [[nodiscard]] virtual std::unique_ptr<ArrangeResult> process(Ctl &ctl) = 0;

    [[nodiscard]] virtual int item_count_to_process() const = 0;

    [[nodiscard]] static std::unique_ptr<ArrangeTaskBase> create(
        Tasks task_type, const Scene &sc);

    [[nodiscard]] std::unique_ptr<ArrangeResult> process(Ctl &&ctl)
    {
        return process(ctl);
    }

    [[nodiscard]] std::unique_ptr<ArrangeResult> process()
    {
        return process(DummyCtl{});
    }
};

bool arrange(Scene &scene, ArrangeTaskCtl &ctl);
inline bool arrange(Scene &scene, ArrangeTaskCtl &&ctl = DummyCtl{})
{
    return arrange(scene, ctl);
}

inline bool arrange(Scene &&scene, ArrangeTaskCtl &ctl)
{
    return arrange(scene, ctl);
}

inline bool arrange(Scene &&scene, ArrangeTaskCtl &&ctl = DummyCtl{})
{
    return arrange(scene, ctl);
}

template<class Builder, class Ctl = DummyCtl>
bool arrange(SceneBuilderBase<Builder> &&builder, Ctl &&ctl = {})
{
    return arrange(Scene{std::move(builder)}, ctl);
}

} // namespace arr2
} // namespace Slic3r

#endif // ARR2_SCENE_HPP
