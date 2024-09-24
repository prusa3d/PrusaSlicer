///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SCENEBUILDER_HPP
#define SCENEBUILDER_HPP

#include <assert.h>
#include <stddef.h>
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>
#include <cstddef>

#include "Scene.hpp"
#include "Core/ArrangeItemTraits.hpp"
#include "libslic3r/AnyPtr.hpp"
#include "libslic3r/Arrange/Core/Beds.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r {

class Model;
class ModelInstance;
class ModelWipeTower;
class Print;
class SLAPrint;
class SLAPrintObject;
class PrintObject;
class DynamicPrintConfig;

namespace arr2 {

using SelectionPredicate = std::function<bool()>;

// Objects implementing this interface should know how to present the wipe tower
// as an Arrangeable. If the wipe tower is not present, the overloads of visit() shouldn't do
// anything. (See MissingWipeTowerHandler)
class WipeTowerHandler
{
public:
    virtual ~WipeTowerHandler() = default;

    virtual void visit(std::function<void(Arrangeable &)>) = 0;
    virtual void visit(std::function<void(const Arrangeable &)>) const = 0;
    virtual void set_selection_predicate(SelectionPredicate pred) = 0;
};

// Something that has a bounding box and can be displaced by arbitrary 2D offset and rotated
// by arbitrary rotation. Used as targets to place on virtual beds. Normally this would correspond
// to ModelInstances but the same functionality was needed in more contexts.
class VBedPlaceable {
public:
    virtual ~VBedPlaceable() = default;

    virtual BoundingBoxf bounding_box() const = 0;
    virtual void displace(const Vec2d &transl, double rot) = 0;
};

// An interface to handle virtual beds for VBedPlaceable objects. A VBedPlaceable
// may be assigned to a logical bed identified by an integer index value (zero
// is the actual physical bed). The VBedPlaceable may still be outside of it's
// bed, regardless of being assigned to it. The handler object should provide
// means to read the assigned bed index of a VBedPlaceable, to assign a
// different bed index and to provide a trafo that maps it to the physical bed
// given a logical bed index. The reason is that the arrangement expects items
// to be in the coordinate system of the physical bed.
class VirtualBedHandler
{
public:
    virtual ~VirtualBedHandler() = default;

    // Returns the bed index on which the given VBedPlaceable is sitting.
    virtual int get_bed_index(const VBedPlaceable &obj) const = 0;

    // The returned trafo can be used to displace the VBedPlaceable
    // to the coordinate system of the physical bed, should that differ from
    // the coordinate space of a logical bed.
    virtual Transform3d get_physical_bed_trafo(int bed_index) const = 0;

    // Assign the VBedPlaceable to the given bed index. Note that this
    // method can return false, indicating that the given bed is not available
    // to be occupied (e.g. the handler has a limited amount of logical bed)
    virtual bool assign_bed(VBedPlaceable &obj, int bed_idx) = 0;

    bool assign_bed(VBedPlaceable &&obj, int bed_idx)
    {
        return assign_bed(obj, bed_idx);
    }

    static std::unique_ptr<VirtualBedHandler> create(const ExtendedBed &bed);
};

// Holds the info about which object (ID) is selected/unselected
class SelectionMask
{
public:
    virtual ~SelectionMask() = default;

    virtual std::vector<bool> selected_objects() const = 0;
    virtual std::vector<bool> selected_instances(int obj_id) const = 0;
    virtual bool is_wipe_tower() const = 0;
};

class FixedSelection : public Slic3r::arr2::SelectionMask
{
    std::vector<std::vector<bool>> m_seldata;
    bool                           m_wp = false;

public:
    FixedSelection() = default;

    explicit FixedSelection(std::initializer_list<std::vector<bool>> seld,
                            bool wp = false)
        : m_seldata{std::move(seld)}, m_wp{wp}
    {}

    explicit FixedSelection(const Model &m);

    explicit FixedSelection(const SelectionMask &other);

    std::vector<bool> selected_objects() const override;

    std::vector<bool> selected_instances(int obj_id) const override
    {
        return obj_id < int(m_seldata.size()) ? m_seldata[obj_id] :
                                                std::vector<bool>{};
    }

    bool is_wipe_tower() const override { return m_wp; }
};

// Common part of any Arrangeable which is a wipe tower
struct ArrangeableWipeTowerBase: public Arrangeable
{
    ObjectID oid;

    Polygon poly;
    SelectionPredicate selection_pred;

    ArrangeableWipeTowerBase(
        const ObjectID &objid,
        Polygon shape,
        SelectionPredicate selection_predicate = [] { return false; })
        : oid{objid},
          poly{std::move(shape)},
          selection_pred{std::move(selection_predicate)}
    {}

    ObjectID id() const override { return oid; }
    ObjectID geometry_id() const override { return {}; }

    ExPolygons full_outline() const override
    {
        auto cpy = poly;
        return {ExPolygon{std::move(cpy)}};
    }

    Polygon convex_outline() const override
    {
        return poly;
    }

    bool is_selected() const override
    {
        return selection_pred();
    }

    int get_bed_index() const override;
    bool assign_bed(int /*bed_idx*/) override;

    int priority() const override { return 1; }

    void transform(const Vec2d &transl, double rot) override {}

    void imbue_data(AnyWritable &datastore) const override
    {
        datastore.write("is_wipe_tower", {});
    }
};

class SceneBuilder;

struct InstPos { size_t obj_idx = 0, inst_idx = 0; };

// Implementing ArrangeableModel interface for PrusaSlicer's Model, ModelObject, ModelInstance data
// hierarchy
class ArrangeableSlicerModel: public ArrangeableModel
{
protected:
    AnyPtr<Model> m_model;
    AnyPtr<WipeTowerHandler> m_wth; // Determines how wipe tower is handled
    AnyPtr<VirtualBedHandler> m_vbed_handler; // Determines how virtual beds are handled
    AnyPtr<const SelectionMask> m_selmask;  // Determines which objects are selected/unselected

private:
    friend class SceneBuilder;

    template<class Self, class Fn>
    static void for_each_arrangeable_(Self &&self, Fn &&fn);

    template<class Self, class Fn>
    static void visit_arrangeable_(Self &&self, const ObjectID &id, Fn &&fn);

public:
    explicit ArrangeableSlicerModel(SceneBuilder &builder);
    ~ArrangeableSlicerModel();

    void for_each_arrangeable(std::function<void(Arrangeable &)>) override;
    void for_each_arrangeable(std::function<void(const Arrangeable&)>) const override;

    void visit_arrangeable(const ObjectID &id, std::function<void(const Arrangeable &)>) const override;
    void visit_arrangeable(const ObjectID &id, std::function<void(Arrangeable &)>) override;

    ObjectID add_arrangeable(const ObjectID &prototype_id) override;

    Model & get_model() { return *m_model; }
    const Model &get_model() const { return *m_model; }
};

// SceneBuilder implementation for PrusaSlicer API.
class SceneBuilder: public SceneBuilderBase<SceneBuilder>
{
protected:
    AnyPtr<Model> m_model;
    AnyPtr<WipeTowerHandler> m_wipetower_handler;
    AnyPtr<VirtualBedHandler> m_vbed_handler;
    AnyPtr<const SelectionMask> m_selection;

    AnyPtr<const SLAPrint> m_sla_print;
    AnyPtr<const Print>    m_fff_print;
    bool m_xl_printer = false;

    void set_brim_and_skirt();

public:
    SceneBuilder();
    ~SceneBuilder();
    SceneBuilder(SceneBuilder&&);
    SceneBuilder& operator=(SceneBuilder&&);

    SceneBuilder && set_model(AnyPtr<Model> mdl);

    SceneBuilder && set_model(Model &mdl);

    SceneBuilder && set_fff_print(AnyPtr<const Print> fffprint);
    SceneBuilder && set_sla_print(AnyPtr<const SLAPrint> mdl_print);

    using SceneBuilderBase<SceneBuilder>::set_bed;

    SceneBuilder &&set_bed(const DynamicPrintConfig &cfg);
    SceneBuilder &&set_bed(const Print &print);

    SceneBuilder && set_wipe_tower_handler(WipeTowerHandler &wth)
    {
        m_wipetower_handler = &wth;
        return std::move(*this);
    }

    SceneBuilder && set_wipe_tower_handler(AnyPtr<WipeTowerHandler> wth)
    {
        m_wipetower_handler = std::move(wth);
        return std::move(*this);
    }

    SceneBuilder && set_virtual_bed_handler(AnyPtr<VirtualBedHandler> vbedh)
    {
        m_vbed_handler = std::move(vbedh);
        return std::move(*this);
    }

    SceneBuilder && set_sla_print(const SLAPrint *slaprint);

    SceneBuilder && set_selection(AnyPtr<const SelectionMask> sel)
    {
        m_selection = std::move(sel);
        return std::move(*this);
    }

    // Can only be called on an rvalue instance (hence the && at the end),
    // the method will potentially move its content into sc
    void build_scene(Scene &sc) && override;

    void build_arrangeable_slicer_model(ArrangeableSlicerModel &amodel);
};

struct MissingWipeTowerHandler : public WipeTowerHandler
{
    void visit(std::function<void(Arrangeable &)>) override {}
    void visit(std::function<void(const Arrangeable &)>) const override {}
    void set_selection_predicate(std::function<bool()>) override {}
};

// Only a physical bed, non-zero bed index values are discarded.
class PhysicalOnlyVBedHandler final : public VirtualBedHandler
{
public:
    using VirtualBedHandler::assign_bed;

    int get_bed_index(const VBedPlaceable &obj) const override { return 0; }

    Transform3d get_physical_bed_trafo(int bed_index) const override
    {
        return Transform3d::Identity();
    }

    bool assign_bed(VBedPlaceable &inst, int bed_idx) override;
};

// A virtual bed handler implementation, that defines logical beds to be created
// on the right side of the physical bed along the X axis in a row
class XStriderVBedHandler final : public VirtualBedHandler
{
    coord_t m_stride_scaled;
    coord_t m_start;

public:
    explicit XStriderVBedHandler(const BoundingBox &bedbb, coord_t xgap)
        : m_stride_scaled{bedbb.size().x() + 2 * std::max(0, xgap)},
          m_start{bedbb.min.x() - std::max(0, xgap)}
    {
    }

    coord_t stride_scaled() const { return m_stride_scaled; }

    // Can return negative indices when the instance is to the left of the
    // physical bed
    int get_bed_index(const VBedPlaceable &obj) const override;

    // Only positive beds are accepted
    bool assign_bed(VBedPlaceable &inst, int bed_idx) override;

    using VirtualBedHandler::assign_bed;

    Transform3d get_physical_bed_trafo(int bed_index) const override;
};

// Same as XStriderVBedHandler only that it lays out vbeds on the Y axis
class YStriderVBedHandler final : public VirtualBedHandler
{
    coord_t m_stride_scaled;
    coord_t m_start;

public:
    coord_t stride_scaled() const { return m_stride_scaled; }

    explicit YStriderVBedHandler(const BoundingBox &bedbb, coord_t ygap)
        : m_stride_scaled{bedbb.size().y() + 2 * std::max(0, ygap)}
        , m_start{bedbb.min.y() - std::max(0, ygap)}
    {}

    int get_bed_index(const VBedPlaceable &obj) const override;
    bool assign_bed(VBedPlaceable &inst, int bed_idx) override;

    Transform3d get_physical_bed_trafo(int bed_index) const override;
};

class GridStriderVBedHandler: public VirtualBedHandler
{
    // This vbed handler defines a grid of virtual beds with a large number
    // of columns so that it behaves as XStrider for regular cases.
    // The goal is to handle objects residing at world coordinates
    // not representable with scaled coordinates. Combining XStrider with
    // YStrider takes care of the X and Y axis to be mapped into the physical
    // bed's coordinate region (which is representable in scaled coords)
    static const int Cols;
    static const int HalfCols;
    static const int Offset;

    XStriderVBedHandler m_xstrider;
    YStriderVBedHandler m_ystrider;

public:
    GridStriderVBedHandler(const BoundingBox &bedbb,
                           coord_t            gap)
        : m_xstrider{bedbb, gap}
        , m_ystrider{bedbb, gap}
    {}

    Vec2i raw2grid(int bedidx) const;
    int grid2raw(const Vec2i &crd) const;

    int get_bed_index(const VBedPlaceable &obj) const override;
    bool assign_bed(VBedPlaceable &inst, int bed_idx) override;

    Transform3d get_physical_bed_trafo(int bed_index) const override;
};

std::vector<size_t> selected_object_indices(const SelectionMask &sm);
std::vector<size_t> selected_instance_indices(int obj_idx, const SelectionMask &sm);

coord_t get_skirt_inset(const Print &fffprint);

coord_t brim_offset(const PrintObject &po);

// unscaled coords are necessary to be able to handle bigger coordinate range
// than what is available with scaled coords. This is useful when working with
// virtual beds.
void transform_instance(ModelInstance     &mi,
                        const Vec2d       &transl_unscaled,
                        double             rot,
                        const Transform3d &physical_tr = Transform3d::Identity());

BoundingBoxf3 instance_bounding_box(const ModelInstance &mi,
                                    bool dont_translate = false);

BoundingBoxf3 instance_bounding_box(const ModelInstance &mi,
                                    const Transform3d &tr,
                                    bool dont_translate = false);

constexpr double UnscaledCoordLimit = 1000.;

ExPolygons extract_full_outline(const ModelInstance &inst,
                                const Transform3d &tr = Transform3d::Identity());

Polygon extract_convex_outline(const ModelInstance &inst,
                               const Transform3d &tr = Transform3d::Identity());

size_t model_instance_count (const Model &m);

class VBedPlaceableMI : public VBedPlaceable
{
    ModelInstance *m_mi;

public:
    explicit VBedPlaceableMI(ModelInstance &mi) : m_mi{&mi} {}

    BoundingBoxf bounding_box() const override { return to_2d(instance_bounding_box(*m_mi)); }
    void         displace(const Vec2d &transl, double rot) override
    {
        transform_instance(*m_mi, transl, rot);
    }
};

// Arrangeable interface implementation for ModelInstances
template<class InstPtr, class VBedHPtr>
class ArrangeableModelInstance : public Arrangeable, VBedPlaceable
{
    InstPtr *m_mi;
    VBedHPtr *m_vbedh;
    const SelectionMask *m_selmask;
    InstPos m_pos_within_model;

public:
    explicit ArrangeableModelInstance(InstPtr *mi,
                                      VBedHPtr *vbedh,
                                      const SelectionMask *selmask,
                                      const InstPos &pos)
        : m_mi{mi}, m_vbedh{vbedh}, m_selmask{selmask}, m_pos_within_model{pos}
    {
        assert(m_mi != nullptr && m_vbedh != nullptr);
    }

    // Arrangeable:
    ObjectID   id() const override { return m_mi->id(); }
    ObjectID   geometry_id() const override { return m_mi->get_object()->id(); }
    ExPolygons full_outline() const override;
    Polygon    convex_outline() const override;
    bool       is_printable() const override { return m_mi->printable; }
    bool       is_selected() const override;
    void       transform(const Vec2d &tr, double rot) override;

    int        get_bed_index() const override { return m_vbedh->get_bed_index(*this); }
    bool       assign_bed(int bed_idx) override;

    // VBedPlaceable:
    BoundingBoxf bounding_box() const override { return to_2d(instance_bounding_box(*m_mi)); }
    void         displace(const Vec2d &transl, double rot) override
    {
        if constexpr (!std::is_const_v<InstPtr>)
            transform_instance(*m_mi, transl, rot);
    }
};

extern template class ArrangeableModelInstance<ModelInstance, VirtualBedHandler>;
extern template class ArrangeableModelInstance<const ModelInstance, const VirtualBedHandler>;

// Arrangeable implementation for an SLAPrintObject to be able to arrange with the supports and pad
class ArrangeableSLAPrintObject : public Arrangeable
{
    const SLAPrintObject *m_po;
    Arrangeable          *m_arrbl;
    Transform3d           m_inst_trafo;

public:
    ArrangeableSLAPrintObject(const SLAPrintObject *po,
                              Arrangeable *arrbl,
                              const Transform3d &inst_tr = Transform3d::Identity())
        : m_po{po}, m_arrbl{arrbl}, m_inst_trafo{inst_tr}
    {}

    ObjectID id() const override { return m_arrbl->id(); }
    ObjectID geometry_id() const override { return m_arrbl->geometry_id(); }

    ExPolygons full_outline() const override;
    ExPolygons full_envelope() const override;

    Polygon convex_outline() const override;
    Polygon convex_envelope() const override;

    void transform(const Vec2d &transl, double rot) override
    {
        m_arrbl->transform(transl, rot);
    }
    int  get_bed_index() const override { return m_arrbl->get_bed_index(); }
    bool assign_bed(int bedidx) override
    {
        return m_arrbl->assign_bed(bedidx);
    }

    bool is_printable() const override { return m_arrbl->is_printable(); }
    bool is_selected() const override { return m_arrbl->is_selected(); }
    int  priority() const override { return m_arrbl->priority(); }
};

// Extension of ArrangeableSlicerModel for SLA
class ArrangeableSLAPrint : public ArrangeableSlicerModel {
    const SLAPrint *m_slaprint;

    friend class SceneBuilder;

    template<class Self, class Fn>
    static void for_each_arrangeable_(Self &&self, Fn &&fn);

    template<class Self, class Fn>
    static void visit_arrangeable_(Self &&self, const ObjectID &id, Fn &&fn);

public:
    explicit ArrangeableSLAPrint(const SLAPrint *slaprint, SceneBuilder &builder)
        : m_slaprint{slaprint}
        , ArrangeableSlicerModel{builder}
    {
        assert(slaprint != nullptr);
    }

    void for_each_arrangeable(std::function<void(Arrangeable &)>) override;

    void for_each_arrangeable(
        std::function<void(const Arrangeable &)>) const override;

    void visit_arrangeable(
        const ObjectID &id,
        std::function<void(const Arrangeable &)>) const override;

    void visit_arrangeable(const ObjectID &id,
                           std::function<void(Arrangeable &)>) override;
};

template<class Mdl>
auto find_instance_by_id(Mdl &&model, const ObjectID &id)
{
    std::remove_reference_t<
        decltype(std::declval<Mdl>().objects[0]->instances[0])>
        ret = nullptr;

    InstPos pos;

    for (auto * obj : model.objects) {
        for (auto *inst : obj->instances) {
            if (inst->id() == id) {
                ret = inst;
                break;
            }
            ++pos.inst_idx;
        }

        if (ret)
            break;

        ++pos.obj_idx;
        pos.inst_idx = 0;
    }

    return std::make_pair(ret, pos);
}

struct ModelDuplicate
{
    ObjectID id;
    Vec2d    tr  = Vec2d::Zero();
    double   rot = 0.;
    int      bed_idx = Unarranged;
};

// Implementing the Arrangeable interface with the whole Model being one outline
// with all its objects and instances.
template<class Mdl, class Dup, class VBH>
class ArrangeableFullModel: public Arrangeable, VBedPlaceable
{
    Mdl *m_mdl;
    Dup *m_dup;
    VBH *m_vbh;

public:
    explicit ArrangeableFullModel(Mdl *mdl,
                                  Dup *md,
                                  VBH *vbh)
        : m_mdl{mdl}, m_dup{md}, m_vbh{vbh}
    {
        assert(m_mdl != nullptr);
    }

    ObjectID id() const override { return m_dup->id.id + 1; }
    ObjectID geometry_id() const override;

    ExPolygons full_outline() const override;

    Polygon convex_outline() const override;

    bool is_printable() const override { return true; }
    bool is_selected() const override { return m_dup->id == 0; }

    int get_bed_index() const override
    {
        return m_vbh->get_bed_index(*this);
    }

    void transform(const Vec2d &tr, double rot) override
    {
        if constexpr (!std::is_const_v<Mdl> && !std::is_const_v<Dup>) {
            m_dup->tr += tr;
            m_dup->rot += rot;
        }
    }

    bool assign_bed(int bed_idx) override
    {
        bool ret = false;

        if constexpr (!std::is_const_v<VBH> && !std::is_const_v<Dup>) {
            if ((ret = m_vbh->assign_bed(*this, bed_idx)))
                m_dup->bed_idx = bed_idx;
        }

        return ret;
    }

    BoundingBoxf bounding_box() const override { return unscaled(get_extents(convex_outline())); }
    void displace(const Vec2d &transl, double rot) override
    {
        transform(transl, rot);
    }
};

extern template class ArrangeableFullModel<Model, ModelDuplicate, VirtualBedHandler>;
extern template class ArrangeableFullModel<const Model, const ModelDuplicate, const VirtualBedHandler>;

// An implementation of the ArrangeableModel to be used for the full model 'duplicate' feature
// accessible from CLI
class DuplicableModel: public ArrangeableModel {
    AnyPtr<Model> m_model;
    AnyPtr<VirtualBedHandler> m_vbh;
    std::vector<ModelDuplicate> m_duplicates;
    BoundingBox m_bedbb;

    template<class Self, class Fn>
    static void visit_arrangeable_(Self &&self, const ObjectID &id, Fn &&fn)
    {
        if (id.valid()) {
            size_t idx = id.id - 1;
            if (idx < self.m_duplicates.size()) {
                auto &md = self.m_duplicates[idx];
                ArrangeableFullModel arrbl{self.m_model.get(), &md, self.m_vbh.get()};
                fn(arrbl);
            }
        }
    }

public:
    explicit DuplicableModel(AnyPtr<Model> mdl,
                             AnyPtr<VirtualBedHandler> vbh,
                             const BoundingBox &bedbb);
    ~DuplicableModel();

    void for_each_arrangeable(std::function<void(Arrangeable &)> fn) override
    {
        for (ModelDuplicate &md : m_duplicates) {
            ArrangeableFullModel arrbl{m_model.get(), &md, m_vbh.get()};
            fn(arrbl);
        }
    }
    void for_each_arrangeable(std::function<void(const Arrangeable&)> fn) const override
    {
        for (const ModelDuplicate &md : m_duplicates) {
            ArrangeableFullModel arrbl{m_model.get(), &md, m_vbh.get()};
            fn(arrbl);
        }
    }
    void visit_arrangeable(const ObjectID &id, std::function<void(const Arrangeable &)> fn) const override
    {
        visit_arrangeable_(*this, id, fn);
    }
    void visit_arrangeable(const ObjectID &id, std::function<void(Arrangeable &)> fn) override
    {
        visit_arrangeable_(*this, id, fn);
    }

    ObjectID add_arrangeable(const ObjectID &prototype_id) override;

    void apply_duplicates();
};

} // namespace arr2
} // namespace Slic3r

#endif // SCENEBUILDER_HPP
