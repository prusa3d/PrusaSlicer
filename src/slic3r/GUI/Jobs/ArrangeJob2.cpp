///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ArrangeJob2.hpp"

#include <numeric>
#include <iterator>

#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/MultipleBeds.hpp>

#include <libslic3r/SLAPrint.hpp>
#include <libslic3r/Print.hpp>

#include <slic3r/GUI/Plater.hpp>
#include <slic3r/GUI/GLCanvas3D.hpp>
#include <slic3r/GUI/GUI_App.hpp>

#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

class GUISelectionMask: public arr2::SelectionMask {
    const Selection *m_sel;

public:
    explicit GUISelectionMask(const Selection *sel) : m_sel{sel} {}

    bool is_wipe_tower_selected(int wipe_tower_index) const override
    {
        const GLVolume *volume{GUI::get_selected_gl_volume(*m_sel)};
        if (volume != nullptr && volume->wipe_tower_bed_index == wipe_tower_index) {
            return true;
        }
        return false;
    }

    std::vector<bool> selected_objects() const override
    {
        auto selmap = m_sel->get_object_idxs();

        std::vector<bool> ret(m_sel->get_model()->objects.size(), false);

        for (auto sel : selmap) {
            ret[sel] = true;
        }

        return ret;
    }

    std::vector<bool> selected_instances(int obj_id) const override
    {
        auto objcnt = static_cast<int>(m_sel->get_model()->objects.size());
        auto icnt   = obj_id < objcnt ?
                          m_sel->get_model()->objects[obj_id]->instances.size() :
                          0;

        std::vector<bool> ret(icnt, false);

        auto selmap = m_sel->get_content();
        auto objit = selmap.find(obj_id);

        if (objit != selmap.end() && obj_id < objcnt) {
            ret = std::vector<bool>(icnt, false);
            for (auto sel : objit->second) {
                ret[sel] = true;
            }
        }

        return ret;
    }
};

static Polygon get_wtpoly(const GLCanvas3D::WipeTowerInfo &wti)
{

    auto bb = scaled(wti.bounding_box());
    Polygon poly = Polygon({
        {bb.min},
        {bb.max.x(), bb.min.y()},
        {bb.max},
        {bb.min.x(), bb.max.y()}
    });

    poly.rotate(wti.rotation());
    poly.translate(scaled(wti.pos()));

    return poly;
}

// Wipe tower logic based on GLCanvas3D::WipeTowerInfo implements the Arrangeable
// interface with this class:
class ArrangeableWT: public arr2::ArrangeableWipeTowerBase
{
    BoundingBox m_xl_bb;
    Vec2d m_orig_tr;
    double m_orig_rot;

public:
    explicit ArrangeableWT(const ObjectID                  &oid,
                           const GLCanvas3D::WipeTowerInfo &wti,
                           std::function<bool(int)>            sel_pred,
                           const BoundingBox                xl_bb = {})
        : arr2::ArrangeableWipeTowerBase{oid, get_wtpoly(wti), wti.bed_index(), std::move(sel_pred)}
        , m_orig_tr{wti.pos()}
        , m_orig_rot{wti.rotation()}
        , m_xl_bb{xl_bb}
    {}

    // Rotation is disabled for wipe tower in arrangement
    void transform(const Vec2d &transl, double /*rot*/) override
    {
        GLCanvas3D::WipeTowerInfo::apply_wipe_tower(m_orig_tr + transl, m_orig_rot, this->bed_index);
    }

    void imbue_data(arr2::AnyWritable &datastore) const override
    {
        // For XL printers, there is a requirement that the wipe tower
        // needs to be placed right beside the extruders which reside at the
        // top edge of the bed.
        if (m_xl_bb.defined) {
            Vec2crd xl_center = m_xl_bb.center();
            datastore.write("sink", Vec2crd{xl_center.x(), 2 * m_xl_bb.max.y()});
        }

        arr2::ArrangeableWipeTowerBase::imbue_data(datastore);
    }
};

// Now the wipe tower handler implementation for GLCanvas3D::WipeTowerInfo
// This is what creates the ArrangeableWT when the arrangement requests it.
// An object of this class is installed into the arrangement Scene.
struct WTH : public arr2::WipeTowerHandler
{
    GLCanvas3D::WipeTowerInfo wti;
    ObjectID oid;
    std::function<bool(int)> sel_pred;
    BoundingBox xl_bb;

    WTH(const ObjectID &objid,
        const GLCanvas3D::WipeTowerInfo &w,
        std::function<bool(int)> sel_predicate = [](int){ return false; })
        : wti(w), oid{objid}, sel_pred{std::move(sel_predicate)}
    {}

    template<class Self, class Fn>
    static void visit_(Self &&self, Fn &&fn)
    {
        ArrangeableWT wta{self.oid, self.wti, self.sel_pred, self.xl_bb};
        fn(wta);
    }

    void visit(std::function<void(arr2::Arrangeable &)> fn) override
    {
        visit_(*this, fn);
    }

    void visit(std::function<void(const arr2::Arrangeable &)> fn) const override
    {
        visit_(*this, fn);
    }

    void set_selection_predicate(std::function<bool(int)> pred) override
    {
        sel_pred = std::move(pred);
    }

    ObjectID get_id() const override {
        return this->oid;
    }
};

arr2::SceneBuilder build_scene(Plater &plater, ArrangeSelectionMode mode)
{
    arr2::SceneBuilder builder;

    const int current_bed{s_multiple_beds.get_active_bed()};
    const std::map<ObjectID, int> &beds_map{s_multiple_beds.get_inst_map()};
    if (mode == ArrangeSelectionMode::SelectionOnly) {
        auto gui_selection = std::make_unique<GUISelectionMask>(&plater.get_selection());

        std::set<ObjectID> considered_instances;
        for (std::size_t object_index{0}; object_index < plater.model().objects.size(); ++object_index) {
            const ModelObject *object{plater.model().objects[object_index]};
            for (std::size_t instance_index{0}; instance_index < object->instances.size(); ++instance_index) {
                const ModelInstance *instance{object->instances[instance_index]};

                const bool is_selected{gui_selection->selected_instances(object_index)[instance_index]};
                const auto instance_bed_index{beds_map.find(instance->id())};

                if (
                    is_selected
                    || instance_bed_index != beds_map.end()
                ) {
                    considered_instances.insert(instance->id());
                }
            }
        }
        builder.set_selection(std::move(gui_selection));
        builder.set_considered_instances(std::move(considered_instances));
    } else if (mode == ArrangeSelectionMode::CurrentBedSelectionOnly) {
        auto gui_selection{std::make_unique<GUISelectionMask>(&plater.get_selection())};

        std::set<ObjectID> considered_instances;
        arr2::BedConstraints constraints;
        for (std::size_t object_index{0}; object_index < plater.model().objects.size(); ++object_index) {
            const ModelObject *object{plater.model().objects[object_index]};
            for (std::size_t instance_index{0}; instance_index < object->instances.size(); ++instance_index) {
                const ModelInstance *instance{object->instances[instance_index]};

                const bool is_selected{gui_selection->selected_instances(object_index)[instance_index]};

                const auto instance_bed_index{beds_map.find(instance->id())};
                if (
                    is_selected
                    || (
                        instance_bed_index != beds_map.end()
                        && instance_bed_index->second == current_bed
                    )
                ) {
                    constraints.insert({instance->id(), current_bed});
                    considered_instances.insert(instance->id());
                }
            }
        }

        builder.set_selection(std::move(gui_selection));
        builder.set_bed_constraints(std::move(constraints));
        builder.set_considered_instances(std::move(considered_instances));
    } else if (mode == ArrangeSelectionMode::CurrentBedFull) {
        std::set<ObjectID> instances_on_bed;
        arr2::BedConstraints constraints;
        for (const auto &instance_bed : beds_map) {
            if (instance_bed.second == current_bed) {
                instances_on_bed.insert(instance_bed.first);
                constraints.insert(instance_bed);
            }
        }
        builder.set_bed_constraints(std::move(constraints));
        builder.set_considered_instances(std::move(instances_on_bed));
    }

    builder.set_arrange_settings(plater.canvas3D()->get_arrange_settings_view());

    const auto wipe_tower_infos = plater.canvas3D()->get_wipe_tower_infos();

    std::vector<AnyPtr<arr2::WipeTowerHandler>> handlers;

    for (const auto &info : wipe_tower_infos) {
        if (info) {
            if (mode == ArrangeSelectionMode::CurrentBedFull && info.bed_index() != current_bed) {
                continue;
            }
            auto handler{std::make_unique<WTH>(wipe_tower_instance_id(info.bed_index()), info)};
            if (plater.config() && is_XL_printer(*plater.config())) {
                handler->xl_bb = bounding_box(get_bed_shape(*plater.config()));
            }
            handlers.push_back(std::move(handler));
        }
    }

    if (plater.config()) {
        const Vec2crd gap{s_multiple_beds.get_bed_gap()};
        builder.set_bed(*plater.config(), gap);
    }

    builder.set_wipe_tower_handlers(std::move(handlers));

    builder.set_model(plater.model());

    if (plater.printer_technology() == ptSLA)
        builder.set_sla_print(&plater.active_sla_print());
    else
        builder.set_fff_print(&plater.active_fff_print());

    return builder;
}

FillBedJob2::FillBedJob2(arr2::Scene &&scene, const Callbacks &cbs) : Base(std::move(scene), _u8L("Filling bed"), cbs) {}

ArrangeJob2::ArrangeJob2(arr2::Scene &&scene, const Callbacks &cbs) : Base(std::move(scene), _u8L("Arranging"), cbs) {}

}} // namespace Slic3r
