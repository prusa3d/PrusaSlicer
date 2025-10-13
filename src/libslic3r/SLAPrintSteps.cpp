///|/ Copyright (c) Prusa Research 2019 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include <libslic3r/Exception.hpp>
#include <libslic3r/SLAPrintSteps.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <libslic3r/Execution/ExecutionTBB.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/SLA/ZCorrection.hpp>
#include <libslic3r/ElephantFootCompensation.hpp>
#include <libslic3r/CSGMesh/ModelToCSGMesh.hpp>
#include <libslic3r/CSGMesh/SliceCSGMesh.hpp>
#include <libslic3r/CSGMesh/VoxelizeCSGMesh.hpp>
#include <libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp>
#include <libslic3r/OpenVDBUtils.hpp>
#include <libslic3r/QuadricEdgeCollapse.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/KDTreeIndirect.hpp>
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <tuple>
#include <vector>
#include <cassert>
//#include <libslic3r/ShortEdgeCollapse.hpp>

#include <boost/log/trivial.hpp>

#include "I18N.hpp"
#include "format.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/CSGMesh/CSGMesh.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Execution/Execution.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/PrintBase.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLA/Hollowing.hpp"
#include "libslic3r/SLA/JobController.hpp"
#include "libslic3r/SLA/RasterBase.hpp"
#include "libslic3r/SLA/SupportTree.hpp"
#include "libslic3r/SLA/SupportTreeStrategies.hpp"
#include "libslic3r/SLA/SupportIslands/SampleConfigFactory.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {

namespace {

const std::array<unsigned, slaposCount> OBJ_STEP_LEVELS = {
    13, // slaposAssembly
    13, // slaposHollowing,
    13, // slaposDrillHoles
    13, // slaposObjectSlice,
    13, // slaposSupportPoints,
    13, // slaposSupportTree,
    11, // slaposPad,
    11, // slaposSliceSupports,
};

std::string OBJ_STEP_LABELS(size_t idx)
{
    switch (idx) {
                                            // TRN Status of the SLA print calculation
    case slaposAssembly:             return _u8L("Assembling model from parts");
    case slaposHollowing:            return _u8L("Hollowing model");
    case slaposDrillHoles:           return _u8L("Drilling holes into model.");
    case slaposObjectSlice:          return _u8L("Slicing model");
    case slaposSupportPoints:        return _u8L("Generating support points");
    case slaposSupportTree:          return _u8L("Generating support tree");
    case slaposPad:                  return _u8L("Generating pad");
    case slaposSliceSupports:        return _u8L("Slicing supports");
    default:;
    }
    assert(false);
    return "Out of bounds!";
}

const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS = {
    10, // slapsMergeSlicesAndEval
    90, // slapsRasterize
};

std::string PRINT_STEP_LABELS(size_t idx)
{
    switch (idx) {
    case slapsMergeSlicesAndEval:   return _u8L("Merging slices and calculating statistics");
    case slapsRasterize:            return _u8L("Rasterizing layers");
    default:;
    }
    assert(false); return "Out of bounds!";
}

using namespace sla;

/// <summary>
/// Copy permanent support points from model to permanent_supports
/// </summary>
/// <param name="permanent_supports">OUTPUT</param>
/// <param name="object_supports"></param>
/// <param name="emesh"></param>
void prepare_permanent_support_points(
    SupportPoints &permanent_supports, 
    const SupportPoints &object_supports, 
    const Transform3d &object_trafo,
    const AABBMesh &emesh) {
    // update permanent support points
    permanent_supports.clear(); // previous supports are irelevant
    for (const SupportPoint &p : object_supports) {
        if (p.type != SupportPointType::manual_add)
            continue;

        // TODO: remove transformation
        // ?? Why need transform the position?
        Vec3f pos = (object_trafo * p.pos.cast<double>()).cast<float>();
        double dist_sq = emesh.squared_distance(pos.cast<double>());
        if (dist_sq >= sqr(p.head_front_radius)) {
            // TODO: inform user about skipping points, which are far from surface
            assert(false);
            continue; // skip points outside the mesh
        }
        permanent_supports.push_back(p); // copy
        permanent_supports.back().pos = pos; // ?? Why need transform the position?
    }

    // Prevent overlapped permanent supports
    auto point_accessor = [&permanent_supports](size_t idx, size_t dim) -> float & {
        return permanent_supports[idx].pos[dim]; };
    std::vector<size_t> indices(permanent_supports.size());
    std::iota(indices.begin(), indices.end(), 0);
    KDTreeIndirect<3, float, decltype(point_accessor)> tree(point_accessor, indices);
    for (SupportPoint &p : permanent_supports) {
        if (p.head_front_radius < 0.f)
            continue; // already marked for erase

        std::vector<size_t> near_indices = find_nearby_points(tree, p.pos, p.head_front_radius);
        assert(!near_indices.empty());
        if (near_indices.size() == 1)
            continue; // only support itself

        size_t index = &p - &permanent_supports.front();
        for (size_t near_index : near_indices) {
            if (near_index == index)
                continue; // support itself
            SupportPoint p_near = permanent_supports[near_index];
            if ((p.pos - p_near.pos).squaredNorm() > sqr(p.head_front_radius))
                continue; // not near point
            // IMPROVE: investigate neighbors of the near point
            
            // TODO: inform user about skip near point
            assert(false);
            permanent_supports[near_index].head_front_radius = -1.0f; // mark for erase
        }
    }

    permanent_supports.erase(std::remove_if(permanent_supports.begin(), permanent_supports.end(), 
        [](const SupportPoint &p) { return p.head_front_radius < 0.f; }),permanent_supports.end());

    std::sort(permanent_supports.begin(), permanent_supports.end(), 
        [](const SupportPoint& p1,const SupportPoint& p2){ return p1.pos.z() < p2.pos.z(); });
}

} // namespace

SLAPrint::Steps::Steps(SLAPrint *print)
    : m_print{print}
    , objcount{m_print->m_objects.size()}
    , ilhd{m_print->m_material_config.initial_layer_height.getFloat()}
    , ilh{float(ilhd)}
    , ilhs{scaled(ilhd)}
    , objectstep_scale{(max_objstatus - min_objstatus) / (objcount * 100.0)}
{}

void SLAPrint::Steps::apply_printer_corrections(SLAPrintObject &po, SliceOrigin o)
{
    if (o == soSupport && !po.m_supportdata) return;

    auto faded_lyrs = size_t(po.m_config.faded_layers.getInt());
    double min_w = m_print->m_printer_config.elefant_foot_min_width.getFloat() / 2.;
    double start_efc = m_print->m_printer_config.elefant_foot_compensation.getFloat();

    double doffs = m_print->m_printer_config.absolute_correction.getFloat();
    coord_t clpr_offs = scaled(doffs);

    faded_lyrs = std::min(po.m_slice_index.size(), faded_lyrs);
    size_t faded_lyrs_efc = std::max(size_t(1), faded_lyrs - 1);

    auto efc = [start_efc, faded_lyrs_efc](size_t pos) {
        return (faded_lyrs_efc - pos) * start_efc / faded_lyrs_efc;
    };

    std::vector<ExPolygons> &slices = o == soModel ?
                                          po.m_model_slices :
                                          po.m_supportdata->support_slices;

    if (clpr_offs != 0) for (size_t i = 0; i < po.m_slice_index.size(); ++i) {
        size_t idx = po.m_slice_index[i].get_slice_idx(o);
        if (idx < slices.size())
            slices[idx] = offset_ex(slices[idx], float(clpr_offs));
    }

    if (start_efc > 0.) for (size_t i = 0; i < faded_lyrs; ++i) {
        size_t idx = po.m_slice_index[i].get_slice_idx(o);
        if (idx < slices.size())
            slices[idx] = elephant_foot_compensation(slices[idx], min_w, efc(i));
    }

    if (o == soModel) { // Z correction applies only to the model slices
        slices = sla::apply_zcorrection(slices,
                                        m_print->m_material_config.zcorrection_layers.getInt());
    }
}

indexed_triangle_set SLAPrint::Steps::generate_preview_vdb(
    SLAPrintObject &po, SLAPrintObjectStep step)
{
    // Empirical upper limit to not get excessive performance hit
    constexpr double MaxPreviewVoxelScale = 12.;

    // update preview mesh
    double vscale = std::min(MaxPreviewVoxelScale,
                             1. / po.m_config.layer_height.getFloat());

    auto   voxparams = csg::VoxelizeParams{}
                         .voxel_scale(vscale)
                         .exterior_bandwidth(1.f)
                         .interior_bandwidth(1.f);

    voxparams.statusfn([&po](int){
        return po.m_print->cancel_status() != CancelStatus::NOT_CANCELED;
    });

    auto r = range(po.m_mesh_to_slice);
    auto grid = csg::voxelize_csgmesh(r, voxparams);
    auto m = grid ? grid_to_mesh(*grid, 0., 0.01) : indexed_triangle_set{};
    float loss_less_max_error = float(1e-6);
    its_quadric_edge_collapse(m, 0U, &loss_less_max_error);

    return m;
}

void SLAPrint::Steps::generate_preview(SLAPrintObject &po, SLAPrintObjectStep step)
{
    using std::chrono::high_resolution_clock;

    auto start{high_resolution_clock::now()};

    auto r = range(po.m_mesh_to_slice);
    auto m = indexed_triangle_set{};

    bool handled   = false;

    if (is_all_positive(r)) {
        m = csgmesh_merge_positive_parts(r);
        handled = true;
    } else if (csg::check_csgmesh_booleans(r) == r.end()) {
        MeshBoolean::cgal::CGALMeshPtr cgalmeshptr;
        try {
            cgalmeshptr = csg::perform_csgmesh_booleans(r);
        } catch (...) {
            // leaves cgalmeshptr as nullptr
        }

        if (cgalmeshptr) {
            m = MeshBoolean::cgal::cgal_to_indexed_triangle_set(*cgalmeshptr);
            handled = true;
        } else {
            BOOST_LOG_TRIVIAL(warning) << "CSG mesh is not egligible for proper CGAL booleans!";
        }
    } else {
        // Normal cgal processing failed. If there are no negative volumes,
        // the hollowing can be tried with the old algorithm which didn't handled volumes.
        // If that fails for any of the drillholes, the voxelization fallback is
        // used.

        bool is_pure_model = is_all_positive(po.mesh_to_slice(slaposAssembly));
        bool can_hollow    = po.m_hollowing_data && po.m_hollowing_data->interior &&
                          !sla::get_mesh(*po.m_hollowing_data->interior).empty();


        bool hole_fail = false;
        if (step == slaposHollowing && is_pure_model) {
            if (can_hollow) {
                m = csgmesh_merge_positive_parts(r);
                sla::hollow_mesh(m, *po.m_hollowing_data->interior,
                                 sla::hfRemoveInsideTriangles);
            }

            handled = true;
        } else if (step == slaposDrillHoles && is_pure_model) {
            if (po.m_model_object->sla_drain_holes.empty()) {
                // Get the last printable preview
                auto &meshp = po.get_mesh_to_print();
                if (meshp)
                    m = *(meshp);

                handled = true;
            } else if (can_hollow) {
                m = csgmesh_merge_positive_parts(r);
                sla::hollow_mesh(m, *po.m_hollowing_data->interior);
                sla::DrainHoles drainholes = po.transformed_drainhole_points();

                auto ret = sla::hollow_mesh_and_drill(
                    m, *po.m_hollowing_data->interior, drainholes,
                    [/*&po, &drainholes, */&hole_fail](size_t i)
                    {
                        hole_fail = /*drainholes[i].failed =
                                po.model_object()->sla_drain_holes[i].failed =*/ true;
                    });

                if (ret & static_cast<int>(sla::HollowMeshResult::FaultyMesh)) {
                    po.active_step_add_warning(
                        PrintStateBase::WarningLevel::NON_CRITICAL,
                        _u8L("Mesh to be hollowed is not suitable for hollowing (does not "
                          "bound a volume)."));
                }

                if (ret & static_cast<int>(sla::HollowMeshResult::FaultyHoles)) {
                    po.active_step_add_warning(
                        PrintStateBase::WarningLevel::NON_CRITICAL,
                        _u8L("Unable to drill the current configuration of holes into the "
                          "model."));
                }

                handled = true;

                if (ret & static_cast<int>(sla::HollowMeshResult::DrillingFailed)) {
                    po.active_step_add_warning(
                        PrintStateBase::WarningLevel::NON_CRITICAL, _u8L(
                        "Drilling holes into the mesh failed. "
                        "This is usually caused by broken model. Try to fix it first."));

                    handled = false;
                }

                if (hole_fail) {
                    po.active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
                                               _u8L("Failed to drill some holes into the model"));

                    handled = false;
                }
            }
        }
    }

    if (!handled) { // Last resort to voxelization.
        po.active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
                                   _u8L("Some parts of the print will be previewed with approximated meshes. "
                                     "This does not affect the quality of slices or the physical print in any way."));
        m = generate_preview_vdb(po, step);
    }

    po.m_preview_meshes[step] =
            std::make_shared<const indexed_triangle_set>(std::move(m));

    for (size_t i = size_t(step) + 1; i < slaposCount; ++i)
    {
        po.m_preview_meshes[i] = {};
    }

    auto stop{high_resolution_clock::now()};

    if (!po.m_preview_meshes[step]->empty()) {
        using std::chrono::duration;
        using std::chrono::seconds;

        BOOST_LOG_TRIVIAL(trace) << "Preview gen took: " << duration<double>{stop - start}.count();
    } else
        BOOST_LOG_TRIVIAL(error) << "Preview failed!";

    using namespace std::string_literals;

    report_status(-2, "Reload preview from step "s + std::to_string(int(step)), SlicingStatus::RELOAD_SLA_PREVIEW);
}

static inline
void clear_csg(std::multiset<CSGPartForStep> &s, SLAPrintObjectStep step)
{
    auto r = s.equal_range(step);
    s.erase(r.first, r.second);
}

struct csg_inserter {
    std::multiset<CSGPartForStep> &m;
    SLAPrintObjectStep key;

    csg_inserter &operator*() { return *this; }
    void operator=(csg::CSGPart &&part)
    {
        part.its_ptr.convert_unique_to_shared();
        m.emplace(key, std::move(part));
    }
    csg_inserter& operator++() { return *this; }
};

void SLAPrint::Steps::mesh_assembly(SLAPrintObject &po)
{
    po.m_mesh_to_slice.clear();
    po.m_supportdata.reset();
    po.m_hollowing_data.reset();

    csg::model_to_csgmesh(*po.model_object(), po.trafo(),
                          csg_inserter{po.m_mesh_to_slice, slaposAssembly},
                          csg::mpartsPositive | csg::mpartsNegative | csg::mpartsDoSplits);

    generate_preview(po, slaposAssembly);
}

void SLAPrint::Steps::hollow_model(SLAPrintObject &po)
{
    po.m_hollowing_data.reset();
    po.m_supportdata.reset();
    clear_csg(po.m_mesh_to_slice, slaposDrillHoles);
    clear_csg(po.m_mesh_to_slice, slaposHollowing);

    if (! po.m_config.hollowing_enable.getBool()) {
        BOOST_LOG_TRIVIAL(info) << "Skipping hollowing step!";
        return;
    }

    BOOST_LOG_TRIVIAL(info) << "Performing hollowing step!";

    double thickness = po.m_config.hollowing_min_thickness.getFloat();
    double quality  = po.m_config.hollowing_quality.getFloat();
    double closing_d = po.m_config.hollowing_closing_distance.getFloat();
    sla::HollowingConfig hlwcfg{thickness, quality, closing_d};
    sla::JobController ctl;
    ctl.stopcondition = [this]() { return canceled(); };
    ctl.cancelfn = [this]() { throw_if_canceled(); };

    sla::InteriorPtr interior =
        generate_interior(po.mesh_to_slice(), hlwcfg, ctl);

    if (!interior || sla::get_mesh(*interior).empty())
        BOOST_LOG_TRIVIAL(warning) << "Hollowed interior is empty!";
    else {
        po.m_hollowing_data.reset(new SLAPrintObject::HollowingData());
        po.m_hollowing_data->interior = std::move(interior);

        indexed_triangle_set &m = sla::get_mesh(*po.m_hollowing_data->interior);

        if (!m.empty()) {
            // simplify mesh lossless
            float loss_less_max_error = 2*std::numeric_limits<float>::epsilon();
            its_quadric_edge_collapse(m, 0U, &loss_less_max_error);

            its_compactify_vertices(m);
            its_merge_vertices(m);
        }

        // Put the interior into the target mesh as a negative
        po.m_mesh_to_slice
            .emplace(slaposHollowing,
                     csg::CSGPart{std::make_shared<indexed_triangle_set>(m),
                                  csg::CSGType::Difference});

        generate_preview(po, slaposHollowing);
    }
}

// Drill holes into the hollowed/original mesh.
void SLAPrint::Steps::drill_holes(SLAPrintObject &po)
{
    po.m_supportdata.reset();
    clear_csg(po.m_mesh_to_slice, slaposDrillHoles);

    csg::model_to_csgmesh(*po.model_object(), po.trafo(),
                          csg_inserter{po.m_mesh_to_slice, slaposDrillHoles},
                          csg::mpartsDrillHoles);

    generate_preview(po, slaposDrillHoles);

    // Release the data, won't be needed anymore, takes huge amount of ram
    if (po.m_hollowing_data && po.m_hollowing_data->interior)
        po.m_hollowing_data->interior.reset();
}

template<class Pred>
static std::vector<ExPolygons> slice_volumes(
    const ModelVolumePtrs     &volumes,
    const std::vector<float>  &slice_grid,
    const Transform3d         &trafo,
    const MeshSlicingParamsEx &slice_params,
    Pred &&predicate)
{
    indexed_triangle_set mesh;
    for (const ModelVolume *vol : volumes) {
        if (predicate(vol)) {
            indexed_triangle_set vol_mesh = vol->mesh().its;
            its_transform(vol_mesh, trafo * vol->get_matrix());
            its_merge(mesh, vol_mesh);
        }
    }

    std::vector<ExPolygons> out;

    if (!mesh.empty()) {
        out = slice_mesh_ex(mesh, slice_grid, slice_params);
    }

    return out;
}

template<class Cont> BoundingBoxf3 csgmesh_positive_bb(const Cont &csg)
{
    // Calculate the biggest possible bounding box of the mesh to be sliced
    // from all the positive parts that it contains.
    BoundingBoxf3 bb3d;

    bool skip = false;
    for (const auto &m : csg) {
        auto op = csg::get_operation(m);
        auto stackop = csg::get_stack_operation(m);
        if (stackop == csg::CSGStackOp::Push && op != csg::CSGType::Union)
            skip = true;

        if (!skip && csg::get_mesh(m) && op == csg::CSGType::Union)
            bb3d.merge(bounding_box(*csg::get_mesh(m), csg::get_transform(m)));

        if (stackop == csg::CSGStackOp::Pop)
            skip = false;
    }

    return bb3d;
}

void SLAPrint::Steps::prepare_for_generate_supports(SLAPrintObject &po) {
    using namespace sla;
    std::vector<ExPolygons> slices = po.get_model_slices(); // copy
    const std::vector<float> &heights = po.m_model_height_levels;
#ifdef USE_ISLAND_GUI_FOR_SETTINGS
    const PrepareSupportConfig &prepare_cfg = SampleConfigFactory::get_sample_config(po.config().support_head_front_diameter).prepare_config; // use configuration edited by GUI
#else // USE_ISLAND_GUI_FOR_SETTINGS
    const PrepareSupportConfig prepare_cfg; // use Default values of the configuration
#endif // USE_ISLAND_GUI_FOR_SETTINGS
    ThrowOnCancel cancel = [this]() { throw_if_canceled(); };

    // scaling for the sub operations
    double d = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportPoints] / 200.0;
    double init = current_status();
    StatusFunction status = [this, d, init](unsigned st) {
        double current = init + st * d;
        if (std::round(current_status()) < std::round(current))
            report_status(current, OBJ_STEP_LABELS(slaposSupportPoints));
    };
    po.m_support_point_generator_data =
        prepare_generator_data(std::move(slices), heights, prepare_cfg, cancel, status);
}

// The slicing will be performed on an imaginary 1D grid which starts from
// the bottom of the bounding box created around the supported model. So
// the first layer which is usually thicker will be part of the supports
// not the model geometry. Exception is when the model is not in the air
// (elevation is zero) and no pad creation was requested. In this case the
// model geometry starts on the ground level and the initial layer is part
// of it. In any case, the model and the supports have to be sliced in the
// same imaginary grid (the height vector argument to TriangleMeshSlicer).
void SLAPrint::Steps::slice_model(SLAPrintObject &po)
{
    // The first mesh in the csg sequence is assumed to be a positive part
    assert(po.m_mesh_to_slice.empty() ||
           csg::get_operation(*po.m_mesh_to_slice.begin()) == csg::CSGType::Union);

    auto bb3d = csgmesh_positive_bb(po.m_mesh_to_slice);

    // We need to prepare the slice index...

    double  lhd  = m_print->m_objects.front()->m_config.layer_height.getFloat();
    float   lh   = float(lhd);
    coord_t lhs  = scaled(lhd);
    double  minZ = bb3d.min(Z) - po.get_elevation();
    double  maxZ = bb3d.max(Z);
    auto    minZf = float(minZ);
    coord_t minZs = scaled(minZ);
    coord_t maxZs = scaled(maxZ);

    po.m_slice_index.clear();

    size_t cap = size_t(1 + (maxZs - minZs - ilhs) / lhs);
    po.m_slice_index.reserve(cap);

    po.m_slice_index.emplace_back(minZs + ilhs, minZf + ilh / 2.f, ilh);

    for(coord_t h = minZs + ilhs + lhs; h <= maxZs; h += lhs)
        po.m_slice_index.emplace_back(h, unscaled<float>(h) - lh / 2.f, lh);

    // Just get the first record that is from the model:
    auto slindex_it =
        po.closest_slice_record(po.m_slice_index, float(bb3d.min(Z)));

    if(slindex_it == po.m_slice_index.end())
        //TRN To be shown at the status bar on SLA slicing error.
        throw Slic3r::RuntimeError(format("Model named: %s can not be sliced. This can be caused by the model mesh being broken. "
                                          "Repairing it might fix the problem.", po.model_object()->name));

    po.m_model_height_levels.clear();
    po.m_model_height_levels.reserve(po.m_slice_index.size());
    for(auto it = slindex_it; it != po.m_slice_index.end(); ++it)
        po.m_model_height_levels.emplace_back(it->slice_level());

    po.m_model_slices.clear();
    MeshSlicingParamsEx params;
    params.closing_radius = float(po.config().slice_closing_radius.value);
    switch (po.config().slicing_mode.value) {
    case SlicingMode::Regular:    params.mode = MeshSlicingParams::SlicingMode::Regular; break;
    case SlicingMode::EvenOdd:    params.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
    case SlicingMode::CloseHoles: params.mode = MeshSlicingParams::SlicingMode::Positive; break;
    }
    auto  thr        = [this]() { m_print->throw_if_canceled(); };
    auto &slice_grid = po.m_model_height_levels;

    po.m_model_slices = slice_csgmesh_ex(po.mesh_to_slice(), slice_grid, params, thr);

    auto mit = slindex_it;
    for (size_t id = 0;
         id < po.m_model_slices.size() && mit != po.m_slice_index.end();
         id++) {
        mit->set_model_slice_idx(po, id); ++mit;
    }

    // We apply the printer correction offset here.
    apply_printer_corrections(po, soModel);

    // Prepare data for the support point generator only when supports are enabled
    if (po.m_config.supports_enable.getBool())
        // We need to prepare data in previous step to create interactive support point generation
        prepare_for_generate_supports(po);
//    po.m_preview_meshes[slaposObjectSlice] = po.get_mesh_to_print();
//    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}


struct SuppPtMask {
    const std::vector<ExPolygons> &blockers;
    const std::vector<ExPolygons> &enforcers;
    bool enforcers_only = false;
};

static void filter_support_points_by_modifiers(sla::SupportPoints &pts,
                                               const SuppPtMask &mask,
                                               const std::vector<float> &slice_grid)
{
    assert((mask.blockers.empty() || mask.blockers.size() == slice_grid.size()) &&
           (mask.enforcers.empty() || mask.enforcers.size() == slice_grid.size()));

    auto new_pts = reserve_vector<sla::SupportPoint>(pts.size());

    for (size_t i = 0; i < pts.size(); ++i) {
        const sla::SupportPoint &sp = pts[i];
        Point sp2d = scaled(to_2d(sp.pos));

        auto it = std::lower_bound(slice_grid.begin(), slice_grid.end(), sp.pos.z());
        if (it != slice_grid.end()) {
            size_t idx = std::distance(slice_grid.begin(), it);
            bool is_enforced = false;
            if (idx < mask.enforcers.size()) {
                for (size_t enf_idx = 0;
                     !is_enforced && enf_idx < mask.enforcers[idx].size();
                     ++enf_idx)
                {
                    if (mask.enforcers[idx][enf_idx].contains(sp2d))
                        is_enforced = true;
                }
            }

            bool is_blocked = false;
            if (!is_enforced) {
                if (!mask.enforcers_only) {
                    if (idx < mask.blockers.size()) {
                        for (size_t blk_idx = 0;
                             !is_blocked && blk_idx < mask.blockers[idx].size();
                             ++blk_idx)
                        {
                            if (mask.blockers[idx][blk_idx].contains(sp2d))
                                is_blocked = true;
                        }
                    }
                } else {
                    is_blocked = true;
                }
            }

            if (!is_blocked)
                new_pts.emplace_back(sp);
        }
    }

    pts.swap(new_pts);
}

// In this step we check the slices, identify island and cover them with
// support points. Then we sprinkle the rest of the mesh.
void SLAPrint::Steps::support_points(SLAPrintObject &po)
{
    using namespace sla;
    // If supports are disabled, we can skip the model scan.
    if(!po.m_config.supports_enable.getBool()) return;

    if (!po.m_supportdata) {
        auto &meshp = po.get_mesh_to_print();
        assert(meshp);
        po.m_supportdata =
            std::make_unique<SLAPrintObject::SupportData>(*meshp);
    }

    po.m_supportdata->input.zoffset = csgmesh_positive_bb(po.m_mesh_to_slice)
                                          .min.z();

    const ModelObject& mo = *po.m_model_object;

    BOOST_LOG_TRIVIAL(debug) << "Support point count "
                             << mo.sla_support_points.size();

    if (mo.sla_points_status == PointsStatus::UserModified) {
        // There are either some points on the front-end, or the user
        // removed them on purpose. No calculation will be done.
        po.m_supportdata->input.pts = po.transformed_support_points();
        return;
    }
    // Unless the user modified the points or we already did the calculation,
    // we will do the autoplacement. Otherwise we will just blindly copy the
    // frontend data into the backend cache.
    // if (mo.sla_points_status != PointsStatus::UserModified) 

    throw_if_canceled();
    const SLAPrintObjectConfig& cfg = po.config();

    // the density config value is in percents:
    SupportPointGeneratorConfig config;
    config.density_relative = float(cfg.support_points_density_relative / 100.f);
        
    switch (cfg.support_tree_type) {
    case SupportTreeType::Default:
    case SupportTreeType::Organic:
        config.head_diameter = float(cfg.support_head_front_diameter);
        break;
    case SupportTreeType::Branching:
        config.head_diameter = float(cfg.branchingsupport_head_front_diameter);
        break;
    }
    
    // copy current configuration for sampling islands
#ifdef USE_ISLAND_GUI_FOR_SETTINGS
    // use static variable to propagate data from GUI
    config.island_configuration = SampleConfigFactory::get_sample_config(config.density_relative);
#else // USE_ISLAND_GUI_FOR_SETTINGS
    config.island_configuration = SampleConfigFactory::apply_density(
            SampleConfigFactory::create(config.head_diameter), config.density_relative);
#endif // USE_ISLAND_GUI_FOR_SETTINGS

    // scaling for the sub operations
    double d = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportPoints] / 100.0;
    double init = current_status();

    auto statuscb = [this, d, init](unsigned st)
    {
        double current = init + st * d;
        if(std::round(current_status()) < std::round(current))
            report_status(current, OBJ_STEP_LABELS(slaposSupportPoints));
    };

    // Construction of this object does the calculation.
    throw_if_canceled();

    // TODO: filter small unprintable islands in slices
    // (Island with area smaller than 1 pixel was skipped in support generator)

    SupportPointGeneratorData &data = po.m_support_point_generator_data; 
    SupportPoints &permanent_supports = data.permanent_supports;
    const SupportPoints &object_supports = po.model_object()->sla_support_points;
    const Transform3d& object_trafo = po.trafo();
    const AABBMesh& emesh = po.m_supportdata->input.emesh;
    prepare_permanent_support_points(permanent_supports, object_supports, object_trafo, emesh);

    ThrowOnCancel cancel = [this]() { throw_if_canceled(); };
    StatusFunction status = statuscb;
    LayerSupportPoints layer_support_points = generate_support_points(data, config, cancel, status);

    // Maximal move of support point to mesh surface,
    // no more than height of layer
    assert(po.m_model_height_levels.size() > 1);
    double allowed_move = (po.m_model_height_levels[1] - po.m_model_height_levels[0]) +
        std::numeric_limits<float>::epsilon();
    SupportPoints support_points = 
        move_on_mesh_surface(layer_support_points, emesh, allowed_move, cancel);

    // The Generator count with permanent support positions but do not convert to LayerSupportPoints.
    // To preserve permanent 3d position it is necessary to append points after move_on_mesh_surface
    support_points.insert(support_points.end(), 
        permanent_supports.begin(), permanent_supports.end());

    throw_if_canceled();

    MeshSlicingParamsEx params;
    params.closing_radius = float(po.config().slice_closing_radius.value);
    std::vector<ExPolygons> blockers =
        slice_volumes(po.model_object()->volumes,
                        po.m_model_height_levels, po.trafo(), params,
                        [](const ModelVolume *vol) {
                            return vol->is_support_blocker();
                        });

    std::vector<ExPolygons> enforcers =
        slice_volumes(po.model_object()->volumes,
                        po.m_model_height_levels, po.trafo(), params,
                        [](const ModelVolume *vol) {
                            return vol->is_support_enforcer();
                        });

    SuppPtMask mask{blockers, enforcers, po.config().support_enforcers_only.getBool()};
    filter_support_points_by_modifiers(support_points, mask, po.m_model_height_levels);

    po.m_supportdata->input.pts = support_points;

    BOOST_LOG_TRIVIAL(debug)
        << "Automatic support points: "
        << po.m_supportdata->input.pts.size();

    // Using RELOAD_SLA_SUPPORT_POINTS to tell the Plater to pass
    // the update status to GLGizmoSlaSupports
    report_status(-1, _u8L("Generating support points"),
                    SlicingStatus::RELOAD_SLA_SUPPORT_POINTS);
}

void SLAPrint::Steps::support_tree(SLAPrintObject &po)
{
    if(!po.m_supportdata) return;

    // If the zero elevation mode is engaged, we have to filter out all the
    // points that are on the bottom of the object
    if (is_zero_elevation(po.config())) {
        // remove_bottom_points
        std::vector<sla::SupportPoint> &pts = po.m_supportdata->input.pts;
        float lvl(po.m_supportdata->input.zoffset + EPSILON); 

        // get iterator to the reorganized vector end
        auto endit = std::remove_if(pts.begin(), pts.end(), 
            [lvl](const sla::SupportPoint &sp) {
                return sp.pos.z() <= lvl; });

        // erase all elements after the new end
        pts.erase(endit, pts.end());
    }

    po.m_supportdata->input.cfg = make_support_cfg(po.m_config);
    po.m_supportdata->input.pad_cfg = make_pad_cfg(po.m_config);

    // scaling for the sub operations
    double d = objectstep_scale * OBJ_STEP_LEVELS[slaposSupportTree] / 100.0;
    double init = current_status();
    sla::JobController ctl;

    ctl.statuscb = [this, d, init](unsigned st, const std::string &logmsg) {
        double current = init + st * d;
        if (std::round(current_status()) < std::round(current))
            report_status(current, OBJ_STEP_LABELS(slaposSupportTree),
                          SlicingStatus::DEFAULT, logmsg);
    };
    ctl.stopcondition = [this]() { return canceled(); };
    ctl.cancelfn = [this]() { throw_if_canceled(); };

    po.m_supportdata->create_support_tree(ctl);

    if (!po.m_config.supports_enable.getBool()) return;

    throw_if_canceled();

    // Create the unified mesh
    auto rc = SlicingStatus::RELOAD_SCENE;

    // This is to prevent "Done." being displayed during merged_mesh()
    report_status(-1, _u8L("Visualizing supports"));

    BOOST_LOG_TRIVIAL(debug) << "Processed support point count "
                             << po.m_supportdata->input.pts.size();

    // Check the mesh for later troubleshooting.
    if(po.support_mesh().empty())
        BOOST_LOG_TRIVIAL(warning) << "Support mesh is empty";

    report_status(-1, _u8L("Visualizing supports"), rc);
}

void SLAPrint::Steps::generate_pad(SLAPrintObject &po) {
    // this step can only go after the support tree has been created
    // and before the supports had been sliced. (or the slicing has to be
    // repeated)

    if(po.m_config.pad_enable.getBool()) {
        if (!po.m_supportdata) {
            auto &meshp = po.get_mesh_to_print();
            assert(meshp);
            po.m_supportdata =
                std::make_unique<SLAPrintObject::SupportData>(*meshp);
        }

        // Get the distilled pad configuration from the config
        // (Again, despite it was retrieved in the previous step. Note that
        // on a param change event, the previous step might not be executed
        // depending on the specific parameter that has changed).
        sla::PadConfig pcfg = make_pad_cfg(po.m_config);
        po.m_supportdata->input.pad_cfg = pcfg;

        sla::JobController ctl;
        ctl.stopcondition = [this]() { return canceled(); };
        ctl.cancelfn = [this]() { throw_if_canceled(); };
        po.m_supportdata->create_pad(ctl);

        if (!validate_pad(po.m_supportdata->pad_mesh.its, pcfg))
            throw Slic3r::SlicingError(
                    _u8L("No pad can be generated for this model with the "
                      "current configuration"));

    } else if(po.m_supportdata) {
        po.m_supportdata->pad_mesh = {};
    }

    throw_if_canceled();
    report_status(-1, _u8L("Visualizing supports"), SlicingStatus::RELOAD_SCENE);
}

// Slicing the support geometries similarly to the model slicing procedure.
// If the pad had been added previously (see step "base_pool" than it will
// be part of the slices)
void SLAPrint::Steps::slice_supports(SLAPrintObject &po) {
    auto& sd = po.m_supportdata;

    if(sd) sd->support_slices.clear();

    // Don't bother if no supports and no pad is present.
    if (!po.m_config.supports_enable.getBool() && !po.m_config.pad_enable.getBool())
        return;

    if(sd) {
        auto heights = reserve_vector<float>(po.m_slice_index.size());

        for(auto& rec : po.m_slice_index) heights.emplace_back(rec.slice_level());

        sla::JobController ctl;
        ctl.stopcondition = [this]() { return canceled(); };
        ctl.cancelfn = [this]() { throw_if_canceled(); };

        sd->support_slices =
            sla::slice(sd->tree_mesh.its, sd->pad_mesh.its, heights,
                       float(po.config().slice_closing_radius.value), ctl);
    }

    for (size_t i = 0; i < sd->support_slices.size() && i < po.m_slice_index.size(); ++i)
        po.m_slice_index[i].set_support_slice_idx(po, i);

    apply_printer_corrections(po, soSupport);

    // Using RELOAD_SLA_PREVIEW to tell the Plater to pass the update
    // status to the 3D preview to load the SLA slices.
    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

// get polygons for all instances in the object
static ExPolygons get_all_polygons(const SliceRecord& record, SliceOrigin o)
{
    if (!record.print_obj()) return {};

    ExPolygons polygons;
    auto &input_polygons = record.get_slice(o);
    auto &instances = record.print_obj()->instances();
    bool is_lefthanded = record.print_obj()->is_left_handed();
    polygons.reserve(input_polygons.size() * instances.size());

    for (const ExPolygon& polygon : input_polygons) {
        if(polygon.contour.empty()) continue;

        for (size_t i = 0; i < instances.size(); ++i)
        {
            ExPolygon poly;

            // We need to reverse if is_lefthanded is true but
            bool needreverse = is_lefthanded;

            // should be a move
            poly.contour.points.reserve(polygon.contour.size() + 1);

            auto& cntr = polygon.contour.points;
            if(needreverse)
                for(auto it = cntr.rbegin(); it != cntr.rend(); ++it)
                    poly.contour.points.emplace_back(it->x(), it->y());
            else
                for(auto& p : cntr)
                    poly.contour.points.emplace_back(p.x(), p.y());

            for(auto& h : polygon.holes) {
                poly.holes.emplace_back();
                auto& hole = poly.holes.back();
                hole.points.reserve(h.points.size() + 1);

                if(needreverse)
                    for(auto it = h.points.rbegin(); it != h.points.rend(); ++it)
                        hole.points.emplace_back(it->x(), it->y());
                else
                    for(auto& p : h.points)
                        hole.points.emplace_back(p.x(), p.y());
            }

            if(is_lefthanded) {
                for(auto& p : poly.contour) p.x() = -p.x();
                for(auto& h : poly.holes) for(auto& p : h) p.x() = -p.x();
            }

            poly.rotate(double(instances[i].rotation));
            poly.translate(Point{instances[i].shift.x(), instances[i].shift.y()});

            polygons.emplace_back(std::move(poly));
        }
    }

    return polygons;
}

void SLAPrint::Steps::initialize_printer_input()
{
    auto &printer_input = m_print->m_printer_input;

    // clear the rasterizer input
    printer_input.clear();

    size_t mx = 0;
    for(SLAPrintObject * o : m_print->m_objects) {
        if(auto m = o->get_slice_index().size() > mx) mx = m;
    }

    printer_input.reserve(mx);

    auto eps = coord_t(SCALED_EPSILON);

    for(SLAPrintObject * o : m_print->m_objects) {
        coord_t gndlvl = o->get_slice_index().front().print_level() - ilhs;

        for(const SliceRecord& slicerecord : o->get_slice_index()) {
            if (!slicerecord.is_valid())
                throw Slic3r::SlicingError(
                    _u8L("There are unprintable objects. Try to "
                      "adjust support settings to make the "
                      "objects printable."));

            coord_t lvlid = slicerecord.print_level() - gndlvl;

            // Neat trick to round the layer levels to the grid.
            lvlid = eps * (lvlid / eps);

            auto it = std::lower_bound(printer_input.begin(),
                                       printer_input.end(),
                                       PrintLayer(lvlid));

            if(it == printer_input.end() || it->level() != lvlid)
                it = printer_input.insert(it, PrintLayer(lvlid));


            it->add(slicerecord);
        }
    }
}

static int Ms(int s)
{
    return s;
}

// constant values from FW
int tiltHeight              = 4959; //nm
int tower_microstep_size_nm = 250000;
int first_extra_slow_layers = 3;
int refresh_delay_ms        = 0;

static int nm_to_tower_microsteps(int nm) {
    // add implementation
    return nm / tower_microstep_size_nm;
}

static int count_move_time(const std::string& axis_name, double length, int steprate)
{
    if (length < 0 || steprate < 0)
        return 0;

    // sla - fw checks every 0.1 s if axis is still moving.See: Axis._wait_to_stop_delay.Additional 0.021 s is
    // measured average delay of the system.Thus, the axis movement time is always quantized by this value.
    double delay = 0.121;

    // Both axes use linear ramp movements. This factor compensates the tilt acceleration and deceleration time.
    double tilt_comp_factor = 0.1;

    // Both axes use linear ramp movements.This factor compensates the tower acceleration and deceleration time.
    int tower_comp_factor = 20000;

    int l = int(length);
    return axis_name == "tower" ? Ms((int(l / (steprate * delay) + (steprate + l) / tower_comp_factor) + 1) * (delay * 1000)) :
                                  Ms((int(l / (steprate * delay) + tilt_comp_factor) + 1) * (delay * 1000));
}

struct ExposureProfile {

    // map of internal TowerSpeeds to maximum_steprates (usteps/s)
    // this values was provided in default_tower_moving_profiles.json by SLA-team
    std::map<TowerSpeeds, int> tower_speeds = {
        { tsLayer1 , 800   },
        { tsLayer2 , 1600  },
        { tsLayer3 , 2400  },
        { tsLayer4 , 3200  },
        { tsLayer5 , 4000  },
        { tsLayer8 , 6400  },
        { tsLayer11, 8800  },
        { tsLayer14, 11200 },
        { tsLayer18, 14400 },
        { tsLayer22, 17600 },
        { tsLayer24, 19200 },
    };

    // map of internal TiltSpeeds to maximum_steprates (usteps/s)
    // this values was provided in default_tilt_moving_profiles.json by SLA-team
    std::map<TiltSpeeds, int> tilt_speeds = {
        { tsMove120  , 120   },
        { tsLayer200 , 200   },
        { tsMove300  , 300   },
        { tsLayer400 , 400   },
        { tsLayer600 , 600   },
        { tsLayer800 , 800   },
        { tsLayer1000, 1000  },
        { tsLayer1250, 1250  },
        { tsLayer1500, 1500  },
        { tsLayer1750, 1750  },
        { tsLayer2000, 2000  },
        { tsLayer2250, 2250  },
        { tsMove5120 , 5120  },
        { tsMove8000 , 8000  },
    };

    int     delay_before_exposure_ms    { 0 };
    int     delay_after_exposure_ms     { 0 };
    int     tilt_down_offset_delay_ms   { 0 };
    int     tilt_down_delay_ms          { 0 };
    int     tilt_up_offset_delay_ms     { 0 };
    int     tilt_up_delay_ms            { 0 };
    int     tower_hop_height_nm         { 0 };
    int     tilt_down_offset_steps      { 0 };
    int     tilt_down_cycles            { 0 };
    int     tilt_up_offset_steps        { 0 };
    int     tilt_up_cycles              { 0 };
    bool    use_tilt                    { true };
    int     tower_speed                 { 0 };
    int     tilt_down_initial_speed     { 0 };
    int     tilt_down_finish_speed      { 0 };
    int     tilt_up_initial_speed       { 0 };
    int     tilt_up_finish_speed        { 0 };

    ExposureProfile() {}

    ExposureProfile(const SLAMaterialConfig& config, int opt_id)
    {
        delay_before_exposure_ms    = int(1000 * config.delay_before_exposure.get_at(opt_id));
        delay_after_exposure_ms     = int(1000 * config.delay_after_exposure.get_at(opt_id));
        tilt_down_offset_delay_ms   = int(1000 * config.tilt_down_offset_delay.get_at(opt_id));
        tilt_down_delay_ms          = int(1000 * config.tilt_down_delay.get_at(opt_id));
        tilt_up_offset_delay_ms     = int(1000 * config.tilt_up_offset_delay.get_at(opt_id));
        tilt_up_delay_ms            = int(1000 * config.tilt_up_delay.get_at(opt_id));
        tower_hop_height_nm         = int(config.tower_hop_height.get_at(opt_id) * 1000000);
        tilt_down_offset_steps      = config.tilt_down_offset_steps.get_at(opt_id);
        tilt_down_cycles            = config.tilt_down_cycles.get_at(opt_id);
        tilt_up_offset_steps        = config.tilt_up_offset_steps.get_at(opt_id);
        tilt_up_cycles              = config.tilt_up_cycles.get_at(opt_id);
        use_tilt                    = config.use_tilt.get_at(opt_id);
        tower_speed                 = tower_speeds.at(static_cast<TowerSpeeds>(config.tower_speed.getInts()[opt_id]));
        tilt_down_initial_speed     = tilt_speeds.at(static_cast<TiltSpeeds>(config.tilt_down_initial_speed.getInts()[opt_id]));
        tilt_down_finish_speed      = tilt_speeds.at(static_cast<TiltSpeeds>(config.tilt_down_finish_speed.getInts()[opt_id]));
        tilt_up_initial_speed       = tilt_speeds.at(static_cast<TiltSpeeds>(config.tilt_up_initial_speed.getInts()[opt_id]));
        tilt_up_finish_speed        = tilt_speeds.at(static_cast<TiltSpeeds>(config.tilt_up_finish_speed.getInts()[opt_id]));
    }
};

static int layer_peel_move_time(int layer_height_nm, ExposureProfile p)
{
    int profile_change_delay = Ms(20);  // propagation delay of sending profile change command to MC
    int sleep_delay = Ms(2);            // average delay of the Linux system sleep function

    int tilt = Ms(0);
    if (p.use_tilt) {
        tilt += profile_change_delay;
        // initial down movement
        tilt += count_move_time(
            "tilt",
            p.tilt_down_offset_steps,
            p.tilt_down_initial_speed);
        // initial down delay
        tilt += p.tilt_down_offset_delay_ms + sleep_delay;
        // profile change delay if down finish profile is different from down initial
        tilt += profile_change_delay;
        // cycle down movement
        tilt += p.tilt_down_cycles * count_move_time(
            "tilt",
            int((tiltHeight - p.tilt_down_offset_steps) / p.tilt_down_cycles),
            p.tilt_down_finish_speed);
        // cycle down delay
        tilt += p.tilt_down_cycles * (p.tilt_down_delay_ms + sleep_delay);

        // profile change delay if up initial profile is different from down finish
        tilt += profile_change_delay;
        // initial up movement
        tilt += count_move_time(
            "tilt",
            tiltHeight - p.tilt_up_offset_steps,
            p.tilt_up_initial_speed);
        // initial up delay
        tilt += p.tilt_up_offset_delay_ms + sleep_delay;
        // profile change delay if up initial profile is different from down finish
        tilt += profile_change_delay;
        // finish up movement
        tilt += p.tilt_up_cycles * count_move_time(
            "tilt",
            int(p.tilt_up_offset_steps / p.tilt_up_cycles),
            p.tilt_up_finish_speed);
        // cycle down delay
        tilt += p.tilt_up_cycles * (p.tilt_up_delay_ms + sleep_delay);
    }

    int tower = Ms(0);
    if (p.tower_hop_height_nm > 0) {
        tower += count_move_time(
            "tower",
            nm_to_tower_microsteps(int(p.tower_hop_height_nm) + layer_height_nm),
            p.tower_speed);
        tower += count_move_time(
            "tower",
            nm_to_tower_microsteps(int(p.tower_hop_height_nm)),
            p.tower_speed);
        tower += profile_change_delay;
    }
    else {
        tower += count_move_time(
            "tower",
            nm_to_tower_microsteps(layer_height_nm),
            p.tower_speed);
        tower += profile_change_delay;
    }
    return int(tilt + tower);
}

// Merging the slices from all the print objects into one slice grid and
// calculating print statistics from the merge result.
void SLAPrint::Steps::merge_slices_and_eval_stats() {

    initialize_printer_input();

    auto &print_statistics = m_print->m_print_statistics;
    auto &printer_config   = m_print->m_printer_config;
    auto &material_config  = m_print->m_material_config;
    auto &printer_input    = m_print->m_printer_input;

    print_statistics.clear();

    const double area_fill = material_config.area_fill.getFloat()*0.01;// 0.5 (50%);
    const double fast_tilt = printer_config.fast_tilt_time.getFloat();// 5.0;
    const double slow_tilt = printer_config.slow_tilt_time.getFloat();// 8.0;
    const double hv_tilt   = printer_config.high_viscosity_tilt_time.getFloat();// 10.0;

    const double init_exp_time = material_config.initial_exposure_time.getFloat();
    const double exp_time      = material_config.exposure_time.getFloat();

    const int fade_layers_cnt = m_print->m_default_object_config.faded_layers.getInt();// 10 // [3;20]

    ExposureProfile below(material_config, 0);
    ExposureProfile above(material_config, 1);

    const int           first_slow_layers   = fade_layers_cnt + first_extra_slow_layers;
    const bool          is_prusa_print = SLAPrint::is_prusa_print(printer_config.printer_model);

    const auto width          = scaled<double>(printer_config.display_width.getFloat());
    const auto height         = scaled<double>(printer_config.display_height.getFloat());
    const double display_area = width*height;

    std::vector<std::tuple<double, double, bool, double, double>> layers_info; // time, area, is_fast, models_volume, supports_volume
    layers_info.resize(printer_input.size());

    const double delta_fade_time = (init_exp_time - exp_time) / (fade_layers_cnt + 1);

    // Going to parallel:
    auto printlayerfn = [this,
            // functions and read only vars
            area_fill, display_area, exp_time, init_exp_time, fast_tilt, slow_tilt, hv_tilt, material_config, delta_fade_time, is_prusa_print, first_slow_layers, below, above,

            // write vars
            &layers_info](size_t sliced_layer_cnt)
    {
        PrintLayer &layer = m_print->m_printer_input[sliced_layer_cnt];

        // vector of slice record references
        auto& slicerecord_references = layer.slices();

        if(slicerecord_references.empty()) return;

        // Layer height should match for all object slices for a given level.
        const auto l_height = double(slicerecord_references.front().get().layer_height());

        // Calculation of the consumed material

        ExPolygons model_polygons;
        ExPolygons supports_polygons;

        size_t c = std::accumulate(layer.slices().begin(),
                                   layer.slices().end(),
                                   size_t(0),
                                   [](size_t a, const SliceRecord &sr) {
            return a + sr.get_slice(soModel).size();
        });

        model_polygons.reserve(c);

        c = std::accumulate(layer.slices().begin(),
                            layer.slices().end(),
                            size_t(0),
                            [](size_t a, const SliceRecord &sr) {
            return a + sr.get_slice(soSupport).size();
        });

        supports_polygons.reserve(c);

        for(const SliceRecord& record : layer.slices()) {

            ExPolygons modelslices = get_all_polygons(record, soModel);
            for(ExPolygon& p_tmp : modelslices) model_polygons.emplace_back(std::move(p_tmp));

            ExPolygons supportslices = get_all_polygons(record, soSupport);
            for(ExPolygon& p_tmp : supportslices) supports_polygons.emplace_back(std::move(p_tmp));

        }

        model_polygons = union_ex(model_polygons);
        double layer_model_area = 0;
        for (const ExPolygon& polygon : model_polygons)
            layer_model_area += area(polygon);

        const double models_volume = (layer_model_area < 0 || layer_model_area > 0) ? layer_model_area * l_height : 0.;

        if(!supports_polygons.empty()) {
            if(model_polygons.empty()) supports_polygons = union_ex(supports_polygons);
            else supports_polygons = diff_ex(supports_polygons, model_polygons);
            // allegedly, union of subject is done withing the diff according to the pftPositive polyFillType
        }

        double layer_support_area = 0;
        for (const ExPolygon& polygon : supports_polygons)
            layer_support_area += area(polygon);

        const double supports_volume = (layer_support_area < 0 || layer_support_area > 0) ? layer_support_area * l_height : 0.;
        const double layer_area = layer_model_area + layer_support_area;

        // Here we can save the expensively calculated polygons for printing
        ExPolygons trslices;
        trslices.reserve(model_polygons.size() + supports_polygons.size());
        for(ExPolygon& poly : model_polygons) trslices.emplace_back(std::move(poly));
        for(ExPolygon& poly : supports_polygons) trslices.emplace_back(std::move(poly));

        layer.transformed_slices(union_ex(trslices));

        // Calculation of the printing time
        // + Calculation of the slow and fast layers to the future controlling those values on FW
        double layer_times = 0.0;
        bool is_fast_layer = false;

        if (is_prusa_print) {
            is_fast_layer = int(sliced_layer_cnt) < first_slow_layers || layer_area <= display_area * area_fill;
            const int l_height_nm = 1000000 * l_height;

            layer_times = layer_peel_move_time(l_height_nm, is_fast_layer ? below : above) +
                            (is_fast_layer ? below : above).delay_before_exposure_ms +
                            (is_fast_layer ? below : above).delay_after_exposure_ms +
                            refresh_delay_ms * 5 +                  // ~ 5x frame display wait
                            124;                                    // Magical constant to compensate remaining computation delay in exposure thread

            layer_times *= 0.001; // All before calculations are made in ms, but we need it in s
        }
        else {
            is_fast_layer = layer_area <= display_area*area_fill;
            const double tilt_time = material_config.material_print_speed == slamsSlow              ? slow_tilt :
                                     material_config.material_print_speed == slamsHighViscosity     ? hv_tilt   :
                                     is_fast_layer ? fast_tilt : slow_tilt;

            layer_times += tilt_time;

            //// Per layer times (magical constants cuclulated from FW)
            static double exposure_safe_delay_before{ 3.0 };
            static double exposure_high_viscosity_delay_before{ 3.5 };
            static double exposure_slow_move_delay_before{ 1.0 };

            if (material_config.material_print_speed == slamsSlow)
                layer_times += exposure_safe_delay_before;
            else if (material_config.material_print_speed == slamsHighViscosity)
                layer_times += exposure_high_viscosity_delay_before;
            else if (!is_fast_layer)
                layer_times += exposure_slow_move_delay_before;

            // Increase layer time for "magic constants" from FW
            layer_times += (
                l_height * 5  // tower move
                + 120 / 1000  // Magical constant to compensate remaining computation delay in exposure thread
            );
        }

        // We are done with tilt time, but we haven't added the exposure time yet.
        layer_times += std::max(exp_time, init_exp_time - sliced_layer_cnt * delta_fade_time);

        // Collect values for this layer.
        layers_info[sliced_layer_cnt] = std::make_tuple(layer_times, layer_area * SCALING_FACTOR * SCALING_FACTOR, is_fast_layer, models_volume, supports_volume);
    };

    // sequential version for debugging:
    // for(size_t i = 0; i < printer_input.size(); ++i) printlayerfn(i);
    execution::for_each(ex_tbb, size_t(0), printer_input.size(), printlayerfn,
                        execution::max_concurrency(ex_tbb));

    print_statistics.clear();

    if (printer_input.size() == 0)
        print_statistics.estimated_print_time = NaNd;
    else {
        size_t i=0;
        for (const auto& [time, area, is_fast, models_volume, supports_volume] : layers_info) {
            print_statistics.fast_layers_count += int(is_fast);
            print_statistics.slow_layers_count += int(! is_fast);
            print_statistics.layers_areas.emplace_back(area);
            print_statistics.estimated_print_time += time;
            print_statistics.layers_times_running_total.emplace_back(time + (i==0 ? 0. : print_statistics.layers_times_running_total[i-1]));
            print_statistics.objects_used_material += models_volume  * SCALING_FACTOR * SCALING_FACTOR;
            print_statistics.support_used_material += supports_volume * SCALING_FACTOR * SCALING_FACTOR;
            ++i;
        }
        if (is_prusa_print)
            // For our SLA printers, we add an error of the estimate:
            print_statistics.estimated_print_time_tolerance = 0.03 * print_statistics.estimated_print_time;
    }

    report_status(-2, "", SlicingStatus::RELOAD_SLA_PREVIEW);
}

// Rasterizing the model objects, and their supports
void SLAPrint::Steps::rasterize()
{
    if(canceled() || !m_print->m_archiver) return;

    // coefficient to map the rasterization state (0-99) to the allocated
    // portion (slot) of the process state
    double sd = (100 - max_objstatus) / 100.0;

    // slot is the portion of 100% that is realted to rasterization
    unsigned slot = PRINT_STEP_LEVELS[slapsRasterize];

    // pst: previous state
    double pst = current_status();

    double increment = (slot * sd) / m_print->m_printer_input.size();
    double dstatus = current_status();

    execution::SpinningMutex<ExecutionTBB> slck;

    // procedure to process one height level. This will run in parallel
    auto lvlfn =
        [this, &slck, increment, &dstatus, &pst]
        (sla::RasterBase& raster, size_t idx)
    {
        PrintLayer& printlayer = m_print->m_printer_input[idx];
        if(canceled()) return;

        for (const ExPolygon& poly : printlayer.transformed_slices())
            raster.draw(poly);

        // Status indication guarded with the spinlock
        {
            std::lock_guard lck(slck);
            dstatus += increment;
            double st = std::round(dstatus);
            if(st > pst) {
                report_status(st, PRINT_STEP_LABELS(slapsRasterize));
                pst = st;
            }
        }
    };

    // last minute escape
    if(canceled()) return;

    // Print all the layers in parallel
    m_print->m_archiver->draw_layers(m_print->m_printer_input.size(), lvlfn,
                                    [this]() { return canceled(); }, ex_tbb);
}

std::string SLAPrint::Steps::label(SLAPrintObjectStep step)
{
    return OBJ_STEP_LABELS(step);
}

std::string SLAPrint::Steps::label(SLAPrintStep step)
{
    return PRINT_STEP_LABELS(step);
}

double SLAPrint::Steps::progressrange(SLAPrintObjectStep step) const
{
    return OBJ_STEP_LEVELS[step] * objectstep_scale;
}

double SLAPrint::Steps::progressrange(SLAPrintStep step) const
{
    return PRINT_STEP_LEVELS[step] * (100 - max_objstatus) / 100.0;
}

void SLAPrint::Steps::execute(SLAPrintObjectStep step, SLAPrintObject &obj)
{
    switch(step) {
    case slaposAssembly: mesh_assembly(obj); break;
    case slaposHollowing: hollow_model(obj); break;
    case slaposDrillHoles: drill_holes(obj); break;
    case slaposObjectSlice: slice_model(obj); break;
    case slaposSupportPoints:  support_points(obj); break;
    case slaposSupportTree: support_tree(obj); break;
    case slaposPad: generate_pad(obj); break;
    case slaposSliceSupports: slice_supports(obj); break;
    case slaposCount: assert(false);
    }
}

void SLAPrint::Steps::execute(SLAPrintStep step)
{
    switch (step) {
    case slapsMergeSlicesAndEval: merge_slices_and_eval_stats(); break;
    case slapsRasterize: rasterize(); break;
    case slapsCount: assert(false);
    }
}

}
