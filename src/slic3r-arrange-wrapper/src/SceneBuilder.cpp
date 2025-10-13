///|/ Copyright (c) Prusa Research 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef SCENEBUILDER_CPP
#define SCENEBUILDER_CPP

#include <cmath>
#include <limits>
#include <numeric>
#include <cstdlib>
#include <iterator>

#include <libslic3r/Model.hpp>
#include <libslic3r/MultipleBeds.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/SLAPrint.hpp>
#include <libslic3r/Geometry/ConvexHull.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Geometry.hpp>
#include <libslic3r/PrintConfig.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>

#include <arrange/Beds.hpp>
#include <arrange/ArrangeItemTraits.hpp>

#include <arrange-wrapper/SceneBuilder.hpp>
#include <arrange-wrapper/Scene.hpp>

namespace Slic3r { namespace arr2 {

coord_t get_skirt_inset(const Print &fffprint)
{
    float skirt_inset = 0.f;

    if (fffprint.has_skirt()) {
        float skirtflow = fffprint.objects().empty()
                              ? 0
                              : fffprint.skirt_flow().width();
        skirt_inset = fffprint.config().skirts.value * skirtflow
                      + fffprint.config().skirt_distance.value;
    }

    return scaled(skirt_inset);
}

coord_t brim_offset(const PrintObject &po)
{
    const BrimType brim_type       = po.config().brim_type.value;
    const float    brim_separation = po.config().brim_separation.getFloat();
    const float    brim_width      = po.config().brim_width.getFloat();
    const bool     has_outer_brim  = brim_type == BrimType::btOuterOnly ||
                                brim_type == BrimType::btOuterAndInner;

    // How wide is the brim? (in scaled units)
    return has_outer_brim ? scaled(brim_width + brim_separation) : 0;
}

size_t model_instance_count (const Model &m)
{
    return std::accumulate(m.objects.begin(),
                           m.objects.end(),
                           size_t(0),
                           [](size_t s, const Slic3r::ModelObject *mo) {
                               return s + mo->instances.size();
                           });
}

void transform_instance(ModelInstance     &mi,
                        const Vec2d       &transl_unscaled,
                        double             rot,
                        const Transform3d &physical_tr)
{
    auto trafo = mi.get_transformation().get_matrix();
    auto tr = Transform3d::Identity();
    tr.translate(to_3d(transl_unscaled, 0.));
    trafo = physical_tr.inverse() * tr * Eigen::AngleAxisd(rot, Vec3d::UnitZ()) * physical_tr * trafo;

    mi.set_transformation(Geometry::Transformation{trafo});

    mi.invalidate_object_bounding_box();
}

BoundingBoxf3 instance_bounding_box(const ModelInstance &mi,
                                    const Transform3d &tr,
                                    bool dont_translate)
{
    BoundingBoxf3 bb;
    const Transform3d inst_matrix
        = dont_translate ? mi.get_transformation().get_matrix_no_offset()
                         : mi.get_transformation().get_matrix();

    for (ModelVolume *v : mi.get_object()->volumes) {
        if (v->is_model_part()) {
            bb.merge(v->mesh().transformed_bounding_box(tr * inst_matrix
                                                        * v->get_matrix()));
        }
    }

    return bb;
}

BoundingBoxf3 instance_bounding_box(const ModelInstance &mi, bool dont_translate)
{
    return instance_bounding_box(mi, Transform3d::Identity(), dont_translate);
}

bool check_coord_bounds(const BoundingBoxf &bb)
{
    return std::abs(bb.min.x()) < UnscaledCoordLimit &&
           std::abs(bb.min.y()) < UnscaledCoordLimit &&
           std::abs(bb.max.x()) < UnscaledCoordLimit &&
           std::abs(bb.max.y()) < UnscaledCoordLimit;
}

ExPolygons extract_full_outline(const ModelInstance &inst, const Transform3d &tr)
{
    ExPolygons outline;

    if (check_coord_bounds(to_2d(instance_bounding_box(inst, tr)))) {
        for (const ModelVolume *v : inst.get_object()->volumes) {
            Polygons vol_outline;

            vol_outline = project_mesh(v->mesh().its,
                                       tr * inst.get_matrix() * v->get_matrix(),
                                       [] {});
            switch (v->type()) {
            case ModelVolumeType::MODEL_PART:
                outline = union_ex(outline, vol_outline);
                break;
            case ModelVolumeType::NEGATIVE_VOLUME:
                outline = diff_ex(outline, vol_outline);
                break;
            default:;
            }
        }
    }

    return outline;
}

Polygon extract_convex_outline(const ModelInstance &inst, const Transform3d &tr)
{
    auto bb = to_2d(instance_bounding_box(inst, tr));
    Polygon ret;

    if (check_coord_bounds(bb)) {
        ret = inst.get_object()->convex_hull_2d(tr * inst.get_matrix());
    }

    return ret;
}

inline static bool is_infinite_bed(const ExtendedBed &ebed) noexcept
{
    bool ret = false;
    visit_bed(
        [&ret](auto &rawbed) {
            ret = std::is_convertible_v<decltype(rawbed), InfiniteBed>;
        },
        ebed);

    return ret;
}

void SceneBuilder::set_brim_and_skirt()
{
    if (!m_fff_print)
        return;

    m_brims_offs = 0;

    for (const PrintObject *po : m_fff_print->objects()) {
        if (po) {
            m_brims_offs = std::max(m_brims_offs, brim_offset(*po));
        }
    }

    m_skirt_offs = get_skirt_inset(*m_fff_print);
}

void SceneBuilder::build_scene(Scene &sc) &&
{
    if (m_sla_print && !m_fff_print) {
        m_arrangeable_model = std::make_unique<ArrangeableSLAPrint>(m_sla_print.get(), *this);
    } else {
        m_arrangeable_model = std::make_unique<ArrangeableSlicerModel>(*this);
    }

    if (m_fff_print && !m_sla_print) {
        if (is_infinite_bed(m_bed)) {
            set_bed(*m_fff_print, Vec2crd::Zero());
        } else {
            set_brim_and_skirt();
        }
    }

    // Call the parent class implementation of build_scene to finish constructing of the scene
    std::move(*this).SceneBuilderBase<SceneBuilder>::build_scene(sc);
}

void SceneBuilder::build_arrangeable_slicer_model(ArrangeableSlicerModel &amodel)
{
    if (!m_model)
        m_model = std::make_unique<Model>();

    if (!m_selection)
        m_selection = std::make_unique<FixedSelection>(*m_model);

    if (!m_vbed_handler) {
        m_vbed_handler = VirtualBedHandler::create(m_bed);
    }

    if (m_fff_print && !m_xl_printer)
        m_xl_printer = is_XL_printer(m_fff_print->config());

    const bool has_wipe_tower{std::any_of(
        m_wipetower_handlers.begin(),
        m_wipetower_handlers.end(),
        [](const AnyPtr<WipeTowerHandler> &handler){
            bool is_on_current_bed{false};
            handler->visit([&](const Arrangeable &arrangeable){
                is_on_current_bed = arrangeable.get_bed_index() == s_multiple_beds.get_active_bed();
            });
            return is_on_current_bed;
        }
    )};

    if (m_xl_printer && !has_wipe_tower) {
        m_bed = XLBed{bounding_box(m_bed), bed_gap(m_bed)};
    }

    amodel.m_vbed_handler = std::move(m_vbed_handler);
    amodel.m_model = std::move(m_model);
    amodel.m_selmask = std::move(m_selection);
    amodel.m_wths = std::move(m_wipetower_handlers);
    amodel.m_bed_constraints = std::move(m_bed_constraints);
    amodel.m_considered_instances = std::move(m_considered_instances);

    for (auto &wth : amodel.m_wths) {
        wth->set_selection_predicate(
            [&amodel](int wipe_tower_index){
                return amodel.m_selmask->is_wipe_tower_selected(wipe_tower_index);
            }
        );
    }
}

int XStriderVBedHandler::get_bed_index(const VBedPlaceable &obj) const
{
    int bedidx = 0;
    auto stride_s = stride_scaled();
    if (stride_s > 0) {
        double bedx = unscaled(m_start);
        auto instance_bb = obj.bounding_box();
        auto reference_pos_x = (instance_bb.min.x() - bedx);
        auto stride = unscaled(stride_s);

        auto bedidx_d = std::floor(reference_pos_x / stride);

        if (bedidx_d < std::numeric_limits<int>::min())
            bedidx = std::numeric_limits<int>::min();
        else if (bedidx_d > std::numeric_limits<int>::max())
            bedidx = std::numeric_limits<int>::max();
        else
            bedidx = static_cast<int>(bedidx_d);
    }

    return bedidx;
}

bool XStriderVBedHandler::assign_bed(VBedPlaceable &obj, int bed_index)
{
    bool ret = false;
    auto stride_s = stride_scaled();
    if (bed_index == 0 || (bed_index > 0 && stride_s > 0)) {
        auto current_bed_index = get_bed_index(obj);
        auto stride = unscaled(stride_s);
        auto transl = Vec2d{(bed_index - current_bed_index) * stride, 0.};
        obj.displace(transl, 0.);

        ret = true;
    }

    return ret;
}

Transform3d XStriderVBedHandler::get_physical_bed_trafo(int bed_index) const
{
    auto stride_s = stride_scaled();
    auto tr = Transform3d::Identity();
    tr.translate(Vec3d{-bed_index * unscaled(stride_s), 0., 0.});

    return tr;
}

int YStriderVBedHandler::get_bed_index(const VBedPlaceable &obj) const
{
    int bedidx = 0;
    auto stride_s = stride_scaled();
    if (stride_s > 0) {
        double ystart = unscaled(m_start);
        auto instance_bb = obj.bounding_box();
        auto reference_pos_y = (instance_bb.min.y() - ystart);
        auto stride = unscaled(stride_s);

        auto bedidx_d = std::floor(reference_pos_y / stride);

        if (bedidx_d < std::numeric_limits<int>::min())
            bedidx = std::numeric_limits<int>::min();
        else if (bedidx_d > std::numeric_limits<int>::max())
            bedidx = std::numeric_limits<int>::max();
        else
            bedidx = static_cast<int>(bedidx_d);
    }

    return bedidx;
}

bool YStriderVBedHandler::assign_bed(VBedPlaceable &obj, int bed_index)
{
    bool ret = false;
    auto stride_s = stride_scaled();
    if (bed_index == 0 || (bed_index > 0 && stride_s > 0)) {
        auto current_bed_index = get_bed_index(obj);
        auto stride = unscaled(stride_s);
        auto transl = Vec2d{0., (bed_index - current_bed_index) * stride};
        obj.displace(transl, 0.);

        ret = true;
    }

    return ret;
}

Transform3d YStriderVBedHandler::get_physical_bed_trafo(int bed_index) const
{
    auto stride_s = stride_scaled();
    auto tr = Transform3d::Identity();
    tr.translate(Vec3d{0., -bed_index * unscaled(stride_s), 0.});

    return tr;
}

int GridStriderVBedHandler::get_bed_index(const VBedPlaceable &obj) const
{
    Vec2i crd = {m_xstrider.get_bed_index(obj), m_ystrider.get_bed_index(obj)};

    return BedsGrid::grid_coords2index(crd);
}

bool GridStriderVBedHandler::assign_bed(VBedPlaceable &inst, int bed_idx)
{
    if (bed_idx < 0) {
        return false;
    }
    Vec2i crd = BedsGrid::index2grid_coords(bed_idx);

    bool retx = m_xstrider.assign_bed(inst, crd.x());
    bool rety = m_ystrider.assign_bed(inst, crd.y());

    return retx && rety;
}

Transform3d GridStriderVBedHandler::get_physical_bed_trafo(int bed_idx) const
{
    Vec2i crd = BedsGrid::index2grid_coords(bed_idx);

    Transform3d ret = m_xstrider.get_physical_bed_trafo(crd.x()) *
                      m_ystrider.get_physical_bed_trafo(crd.y());

    return ret;
}

FixedSelection::FixedSelection(const Model &m) : m_wp{true}
{
    m_seldata.resize(m.objects.size());
    for (size_t i = 0; i < m.objects.size(); ++i) {
        m_seldata[i].resize(m.objects[i]->instances.size(), true);
    }
}

FixedSelection::FixedSelection(const SelectionMask &other)
{
    auto obj_sel = other.selected_objects();
    m_seldata.reserve(obj_sel.size());
    for (int oidx = 0; oidx < static_cast<int>(obj_sel.size()); ++oidx)
        m_seldata.emplace_back(other.selected_instances(oidx));
}

std::vector<bool> FixedSelection::selected_objects() const
{
    auto ret = Slic3r::reserve_vector<bool>(m_seldata.size());
    std::transform(m_seldata.begin(),
                   m_seldata.end(),
                   std::back_inserter(ret),
                   [](auto &a) {
                       return std::any_of(a.begin(), a.end(), [](bool b) {
                           return b;
                       });
                   });
    return ret;
}

static std::vector<size_t> find_true_indices(const std::vector<bool> &v)
{
    auto ret = reserve_vector<size_t>(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        if (v[i])
            ret.emplace_back(i);

    return ret;
}

std::vector<size_t> selected_object_indices(const SelectionMask &sm)
{
    auto sel = sm.selected_objects();
    return find_true_indices(sel);
}

std::vector<size_t> selected_instance_indices(int obj_idx, const SelectionMask &sm)
{
    auto sel = sm.selected_instances(obj_idx);
    return find_true_indices(sel);
}

SceneBuilder::SceneBuilder() = default;
SceneBuilder::~SceneBuilder() = default;
SceneBuilder::SceneBuilder(SceneBuilder &&) = default;
SceneBuilder& SceneBuilder::operator=(SceneBuilder&&) = default;

SceneBuilder &&SceneBuilder::set_model(AnyPtr<Model> mdl)
{
    m_model = std::move(mdl);
    return std::move(*this);
}

SceneBuilder &&SceneBuilder::set_model(Model &mdl)
{
    m_model = &mdl;
    return std::move(*this);
}

SceneBuilder &&SceneBuilder::set_fff_print(AnyPtr<const Print> mdl_print)
{
    m_fff_print = std::move(mdl_print);
    return std::move(*this);
}

SceneBuilder &&SceneBuilder::set_sla_print(AnyPtr<const SLAPrint> mdl_print)
{
    m_sla_print = std::move(mdl_print);

    return std::move(*this);
}

SceneBuilder &&SceneBuilder::set_bed(const DynamicPrintConfig &cfg, const Vec2crd &gap)
{
    Points bedpts = get_bed_shape(cfg);

    if (is_XL_printer(cfg)) {
        m_xl_printer = true;
    }

    m_bed = arr2::to_arrange_bed(bedpts, gap);

    return std::move(*this);
}

SceneBuilder &&SceneBuilder::set_bed(const Print &print, const Vec2crd &gap)
{
    Points bedpts = get_bed_shape(print.config());

    if (is_XL_printer(print.config())) {
        m_bed = XLBed{get_extents(bedpts), gap};
    } else {
        m_bed = arr2::to_arrange_bed(bedpts, gap);
    }

    set_brim_and_skirt();

    return std::move(*this);
}

SceneBuilder &&SceneBuilder::set_sla_print(const SLAPrint *slaprint)
{
    m_sla_print = slaprint;
    return std::move(*this);
}

int ArrangeableWipeTowerBase::get_bed_index() const {
    return this->bed_index;
}

bool ArrangeableWipeTowerBase::assign_bed(int bed_idx)
{
    return bed_idx == this->bed_index;
}

bool PhysicalOnlyVBedHandler::assign_bed(VBedPlaceable &inst, int bed_idx)
{
    return bed_idx == PhysicalBedId;
}

ArrangeableSlicerModel::ArrangeableSlicerModel(SceneBuilder &builder)
{
    builder.build_arrangeable_slicer_model(*this);
}

ArrangeableSlicerModel::~ArrangeableSlicerModel() = default;

void ArrangeableSlicerModel::for_each_arrangeable(
    std::function<void(Arrangeable &)> fn)
{
    for_each_arrangeable_(*this, fn);

    for (auto &wth : m_wths) {
        wth->visit(fn);
    }
}

void ArrangeableSlicerModel::for_each_arrangeable(
    std::function<void(const Arrangeable &)> fn) const
{
    for_each_arrangeable_(*this, fn);

    for (auto &wth : m_wths) {
        wth->visit(fn);
    }
}

ObjectID ArrangeableSlicerModel::add_arrangeable(const ObjectID &prototype_id)
{
    ObjectID ret;

    auto [inst, pos] = find_instance_by_id(*m_model, prototype_id);
    if (inst) {
        auto new_inst = inst->get_object()->add_instance(*inst);
        if (new_inst) {
            ret = new_inst->id();
        }
    }

    return ret;
}

std::optional<int> get_bed_constraint(
        const ObjectID &id,
        const BedConstraints &bed_constraints
) {
    const auto found_constraint{bed_constraints.find(id)};
    if (found_constraint == bed_constraints.end()) {
        return std::nullopt;
    }
    return found_constraint->second;
}

bool should_include_instance(
    const ObjectID &instance_id,
    const std::set<ObjectID> &considered_instances
) {
    if (considered_instances.find(instance_id) == considered_instances.end()) {
        return false;
    }
    return true;
}

template<class Self, class Fn>
void ArrangeableSlicerModel::for_each_arrangeable_(Self &&self, Fn &&fn)
{
    InstPos pos;
    for (auto *obj : self.m_model->objects) {
        for (auto *inst : obj->instances) {
            if (!self.m_considered_instances || should_include_instance(inst->id(), *self.m_considered_instances)) {
                ArrangeableModelInstance ainst{
                    inst,
                    self.m_vbed_handler.get(),
                    self.m_selmask.get(),
                    pos,
                    get_bed_constraint(inst->id(), self.m_bed_constraints)
                };
                fn(ainst);
            }
            ++pos.inst_idx;
        }
        pos.inst_idx = 0;
        ++pos.obj_idx;
    }
}

template<class Self, class Fn>
void ArrangeableSlicerModel::visit_arrangeable_(Self &&self, const ObjectID &id, Fn &&fn)
{
    for (auto &wth : self.m_wths) {
        if (id == wth->get_id()) {
            wth->visit(fn);
            return;
        }
    }

    auto [inst, pos] = find_instance_by_id(*self.m_model, id);

    if (inst) {
        ArrangeableModelInstance ainst{
            inst,
            self.m_vbed_handler.get(),
            self.m_selmask.get(),
            pos,
            get_bed_constraint(id, self.m_bed_constraints)
        };
        fn(ainst);
    }
}

void ArrangeableSlicerModel::visit_arrangeable(
    const ObjectID &id, std::function<void(const Arrangeable &)> fn) const
{
    visit_arrangeable_(*this, id, fn);
}

void ArrangeableSlicerModel::visit_arrangeable(
    const ObjectID &id, std::function<void(Arrangeable &)> fn)
{
    visit_arrangeable_(*this, id, fn);
}

template<class Self, class Fn>
void ArrangeableSLAPrint::for_each_arrangeable_(Self &&self, Fn &&fn)
{
    InstPos pos;
    for (auto *obj : self.m_model->objects) {
        for (auto *inst : obj->instances) {
            if (!self.m_considered_instances || should_include_instance(inst->id(), *self.m_considered_instances)) {
                ArrangeableModelInstance ainst{inst, self.m_vbed_handler.get(),
                                               self.m_selmask.get(), pos, get_bed_constraint(inst->id(), self.m_bed_constraints)};

                auto obj_id = inst->get_object()->id();
                const SLAPrintObject *po =
                    self.m_slaprint->get_print_object_by_model_object_id(obj_id);

                if (po) {
                    auto &vbh = self.m_vbed_handler;
                    auto phtr = vbh->get_physical_bed_trafo(vbh->get_bed_index(VBedPlaceableMI{*inst}));
                    ArrangeableSLAPrintObject ainst_po{
                        po,
                        &ainst,
                        get_bed_constraint(inst->id(), self.m_bed_constraints),
                        phtr * inst->get_matrix()
                    };
                    fn(ainst_po);
                } else {
                    fn(ainst);
                }
            }
            ++pos.inst_idx;
        }
        pos.inst_idx = 0;
        ++pos.obj_idx;
    }
}

void ArrangeableSLAPrint::for_each_arrangeable(
    std::function<void(Arrangeable &)> fn)
{
    for_each_arrangeable_(*this, fn);

    for (auto &wth : m_wths) {
        wth->visit(fn);
    }
}

void ArrangeableSLAPrint::for_each_arrangeable(
    std::function<void(const Arrangeable &)> fn) const
{
    for_each_arrangeable_(*this, fn);

    for (auto &wth : m_wths) {
        wth->visit(fn);
    }
}

template<class Self, class Fn>
void ArrangeableSLAPrint::visit_arrangeable_(Self &&self, const ObjectID &id, Fn &&fn)
{
    auto [inst, pos] = find_instance_by_id(*self.m_model, id);

    if (inst) {
        ArrangeableModelInstance ainst{inst, self.m_vbed_handler.get(),
                                       self.m_selmask.get(), pos, std::nullopt};

        auto obj_id = inst->get_object()->id();
        const SLAPrintObject *po =
            self.m_slaprint->get_print_object_by_model_object_id(obj_id);

        if (po) {
            auto &vbh = self.m_vbed_handler;
            auto phtr = vbh->get_physical_bed_trafo(vbh->get_bed_index(VBedPlaceableMI{*inst}));
            ArrangeableSLAPrintObject ainst_po{
                po,
                &ainst,
                get_bed_constraint(inst->id(), self.m_bed_constraints),
                phtr * inst->get_matrix()
            };
            fn(ainst_po);
        } else {
            fn(ainst);
        }
    }
}

void ArrangeableSLAPrint::visit_arrangeable(
    const ObjectID &id, std::function<void(const Arrangeable &)> fn) const
{
    visit_arrangeable_(*this, id, fn);
}

void ArrangeableSLAPrint::visit_arrangeable(
    const ObjectID &id, std::function<void(Arrangeable &)> fn)
{
    visit_arrangeable_(*this, id, fn);
}

template<class InstPtr, class VBedHPtr>
ExPolygons ArrangeableModelInstance<InstPtr, VBedHPtr>::full_outline() const
{
    int bedidx = m_vbedh->get_bed_index(*this);
    auto tr = m_vbedh->get_physical_bed_trafo(bedidx);

    return extract_full_outline(*m_mi, tr);
}

template<class InstPtr, class VBedHPtr>
Polygon ArrangeableModelInstance<InstPtr, VBedHPtr>::convex_outline() const
{
    int bedidx = m_vbedh->get_bed_index(*this);
    auto tr = m_vbedh->get_physical_bed_trafo(bedidx);

    return extract_convex_outline(*m_mi, tr);
}

template<class InstPtr, class VBedHPtr>
bool ArrangeableModelInstance<InstPtr, VBedHPtr>::is_selected() const
{
    bool ret = false;

    if (m_selmask) {
        auto sel = m_selmask->selected_instances(m_pos_within_model.obj_idx);
        if (m_pos_within_model.inst_idx < sel.size() &&
            sel[m_pos_within_model.inst_idx])
            ret = true;
    }

    return ret;
}

template<class InstPtr, class VBedHPtr>
void ArrangeableModelInstance<InstPtr, VBedHPtr>::transform(const Vec2d &transl, double rot)
{
    if constexpr (!std::is_const_v<InstPtr> && !std::is_const_v<VBedHPtr>) {
        int bedidx = m_vbedh->get_bed_index(*this);
        auto physical_trafo = m_vbedh->get_physical_bed_trafo(bedidx);

        transform_instance(*m_mi, transl, rot, physical_trafo);
    }
}

template<class InstPtr, class VBedHPtr>
bool ArrangeableModelInstance<InstPtr, VBedHPtr>::assign_bed(int bed_idx)
{
    bool ret = false;

    if constexpr (!std::is_const_v<InstPtr> && !std::is_const_v<VBedHPtr>)
        ret = m_vbedh->assign_bed(*this, bed_idx);

    return ret;
}

template class ArrangeableModelInstance<ModelInstance, VirtualBedHandler>;
template class ArrangeableModelInstance<const ModelInstance, const VirtualBedHandler>;

ExPolygons ArrangeableSLAPrintObject::full_outline() const
{
    ExPolygons ret;

    auto laststep = m_po->last_completed_step();
    if (laststep < slaposCount && laststep > slaposSupportTree) {
        Polygons polys;
        auto omesh = m_po->get_mesh_to_print();
        auto &smesh = m_po->support_mesh();

        Transform3d trafo_instance = m_inst_trafo * m_po->trafo().inverse();

        if (omesh) {
            Polygons ptmp = project_mesh(*omesh, trafo_instance, [] {});
            std::move(ptmp.begin(), ptmp.end(), std::back_inserter(polys));
        }

        Polygons ptmp = project_mesh(smesh.its, trafo_instance, [] {});
        std::move(ptmp.begin(), ptmp.end(), std::back_inserter(polys));
        ret = union_ex(polys);
    } else {
        ret = m_arrbl->full_outline();
    }

    return ret;
}

ExPolygons ArrangeableSLAPrintObject::full_envelope() const
{
    ExPolygons ret = full_outline();

    auto laststep = m_po->last_completed_step();
    if (laststep < slaposCount && laststep > slaposSupportTree) {
        auto &pmesh = m_po->pad_mesh();
        if (!pmesh.empty()) {

            Transform3d trafo_instance = m_inst_trafo * m_po->trafo().inverse();

            Polygons ptmp = project_mesh(pmesh.its, trafo_instance, [] {});
            ret = union_ex(ret, ptmp);
        }
    }

    return ret;
}

Polygon ArrangeableSLAPrintObject::convex_outline() const
{
    Polygons polys;

    polys.emplace_back(m_arrbl->convex_outline());

    auto laststep = m_po->last_completed_step();
    if (laststep < slaposCount && laststep > slaposSupportTree) {
        auto omesh = m_po->get_mesh_to_print();
        auto &smesh = m_po->support_mesh();

        Transform3f trafo_instance = m_inst_trafo.cast<float>();
        trafo_instance = trafo_instance * m_po->trafo().cast<float>().inverse();

        Polygons polys;
        polys.reserve(3);
        auto zlvl = -m_po->get_elevation();

        if (omesh) {
            polys.emplace_back(
                its_convex_hull_2d_above(*omesh, trafo_instance, zlvl));
        }

        polys.emplace_back(
            its_convex_hull_2d_above(smesh.its, trafo_instance, zlvl));
    }

    return Geometry::convex_hull(polys);
}

Polygon ArrangeableSLAPrintObject::convex_envelope() const
{
    Polygons polys;

    polys.emplace_back(convex_outline());

    auto laststep = m_po->last_completed_step();
    if (laststep < slaposCount && laststep > slaposSupportTree) {
        auto &pmesh = m_po->pad_mesh();
        if (!pmesh.empty()) {

            Transform3f trafo_instance = m_inst_trafo.cast<float>();
            trafo_instance = trafo_instance * m_po->trafo().cast<float>().inverse();
            auto zlvl = -m_po->get_elevation();

            polys.emplace_back(
                its_convex_hull_2d_above(pmesh.its, trafo_instance, zlvl));
        }
    }

    return Geometry::convex_hull(polys);
}

DuplicableModel::DuplicableModel(AnyPtr<Model> mdl, AnyPtr<VirtualBedHandler> vbh, const BoundingBox &bedbb)
    : m_model{std::move(mdl)}, m_vbh{std::move(vbh)}, m_duplicates(1), m_bedbb{bedbb}
{
}

DuplicableModel::~DuplicableModel() = default;

ObjectID DuplicableModel::add_arrangeable(const ObjectID &prototype_id)
{
    ObjectID ret;
    if (prototype_id.valid()) {
        size_t idx = prototype_id.id - 1;
        if (idx < m_duplicates.size()) {
            ModelDuplicate md = m_duplicates[idx];
            md.id = m_duplicates.size();
            ret = md.id.id + 1;
            m_duplicates.emplace_back(std::move(md));
        }
    }

    return ret;
}

void DuplicableModel::apply_duplicates()
{
    for (ModelObject *o : m_model->objects) {
        // make a copy of the pointers in order to avoid recursion
        // when appending their copies
        ModelInstancePtrs instances = o->instances;
        o->instances.clear();
        for (const ModelInstance *i : instances) {
            for (const ModelDuplicate &md : m_duplicates) {
                ModelInstance *instance = o->add_instance(*i);
                arr2::transform_instance(*instance, md.tr, md.rot);
            }
        }
        for (auto *i : instances)
            delete i;

        instances.clear();

        o->invalidate_bounding_box();
    }
}

template<class Mdl, class Dup, class VBH>
ObjectID ArrangeableFullModel<Mdl, Dup, VBH>::geometry_id() const { return m_mdl->id(); }

template<class Mdl, class Dup, class VBH>
ExPolygons ArrangeableFullModel<Mdl, Dup, VBH>::full_outline() const
{
    auto ret = reserve_vector<ExPolygon>(arr2::model_instance_count(*m_mdl));

    auto transl = Transform3d::Identity();
    transl.translate(to_3d(m_dup->tr, 0.));
    Transform3d trafo = transl* Eigen::AngleAxisd(m_dup->rot, Vec3d::UnitZ());

    for (auto *mo : m_mdl->objects) {
        for (auto *mi : mo->instances) {
            auto expolys = arr2::extract_full_outline(*mi, trafo);
            std::move(expolys.begin(), expolys.end(), std::back_inserter(ret));
        }
    }

    return ret;
}

template<class Mdl, class Dup, class VBH>
Polygon ArrangeableFullModel<Mdl, Dup, VBH>::convex_outline() const
{
    auto ret = reserve_polygons(arr2::model_instance_count(*m_mdl));

    auto transl = Transform3d::Identity();
    transl.translate(to_3d(m_dup->tr, 0.));
    Transform3d trafo = transl* Eigen::AngleAxisd(m_dup->rot, Vec3d::UnitZ());

    for (auto *mo : m_mdl->objects) {
        for (auto *mi : mo->instances) {
            ret.emplace_back(arr2::extract_convex_outline(*mi, trafo));
        }
    }

    return Geometry::convex_hull(ret);
}

template class ArrangeableFullModel<Model, ModelDuplicate, VirtualBedHandler>;
template class ArrangeableFullModel<const Model, const ModelDuplicate, const VirtualBedHandler>;

std::unique_ptr<VirtualBedHandler> VirtualBedHandler::create(const ExtendedBed &bed)
{
    std::unique_ptr<VirtualBedHandler> ret;
    if (is_infinite_bed(bed)) {
        ret = std::make_unique<PhysicalOnlyVBedHandler>();
    } else {
        Vec2crd gap;
        visit_bed([&gap](auto &rawbed) { gap = bed_gap(rawbed); }, bed);
        BoundingBox bedbb;
        visit_bed([&bedbb](auto &rawbed) { bedbb = bounding_box(rawbed); }, bed);

        ret = std::make_unique<GridStriderVBedHandler>(bedbb, gap);
    }

    return ret;
}

}} // namespace Slic3r::arr2

#endif // SCENEBUILDER_CPP
