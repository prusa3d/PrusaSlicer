///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, Roman Beránek @zavorka, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
/**
 * In this file we will implement the automatic SLA support tree generation.
 *
 */

#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/DefaultSupportTree.hpp>
#include <libslic3r/SLA/BranchingTreeSLA.hpp>
#include <libslic3r/MTUtils.hpp>
#include <libslic3r/TriangleMeshSlicer.hpp>
#include <boost/log/trivial.hpp>
#include <chrono>
#include <iterator>
#include <cstddef>

#include "libslic3r/Point.hpp"
#include "libslic3r/SLA/JobController.hpp"
#include "libslic3r/SLA/Pad.hpp"
#include "libslic3r/SLA/SupportTreeStrategies.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/libslic3r.h"


namespace Slic3r { namespace sla {

indexed_triangle_set create_support_tree(const SupportableMesh &sm,
                                         const JobController   &ctl)
{
    auto builder = make_unique<SupportTreeBuilder>(ctl);

    if (sm.cfg.enabled) {
        using std::chrono::high_resolution_clock;
        auto start{high_resolution_clock::now()};

        switch (sm.cfg.tree_type) {
        case SupportTreeType::Default: {
            create_default_tree(*builder, sm);
            break;
        }
        case SupportTreeType::Branching: {
            create_branching_tree(*builder, sm);
            break;
        }
        case SupportTreeType::Organic: {
            // TODO
        }
        default:;
        }

        auto stop{high_resolution_clock::now()};

        using std::chrono::duration;
        using std::chrono::seconds;
        BOOST_LOG_TRIVIAL(info) << "Support tree creation took: "
                                << duration<double>{stop - start}.count()
                                << " seconds";

        builder->merge_and_cleanup();   // clean metadata, leave only the meshes.
    }

    indexed_triangle_set out = builder->retrieve_mesh(MeshType::Support);

    return out;
}

indexed_triangle_set create_pad(const SupportableMesh      &sm,
                                const indexed_triangle_set &support_mesh,
                                const JobController        &ctl)
{
    constexpr float PadSamplingLH = 0.1f;

    ExPolygons model_contours; // This will store the base plate of the pad.
    double pad_h  = sm.pad_cfg.full_height();
    auto   gndlvl = float(ground_level(sm));
    float  zstart = gndlvl - bool(sm.pad_cfg.embed_object) * sm.pad_cfg.wall_thickness_mm;
    float  zend   = zstart + float(pad_h + PadSamplingLH + EPSILON);
    auto  heights = grid(zstart, zend, PadSamplingLH);

    if (!sm.cfg.enabled || sm.pad_cfg.embed_object) {
        // No support (thus no elevation) or zero elevation mode
        // we sometimes call it "builtin pad" is enabled so we will
        // get a sample from the bottom of the mesh and use it for pad
        // creation.
        sla::pad_blueprint(*sm.emesh.get_triangle_mesh(), model_contours,
                           heights, ctl.cancelfn);
    }

    ExPolygons sup_contours;
    pad_blueprint(support_mesh, sup_contours, heights, ctl.cancelfn);

    indexed_triangle_set out;
    create_pad(sup_contours, model_contours, out, sm.pad_cfg);

    Vec3f offs{.0f, .0f, gndlvl};
    for (auto &p : out.vertices) p += offs;

    its_merge_vertices(out);

    return out;
}

std::vector<ExPolygons> slice(const indexed_triangle_set &sup_mesh,
                              const indexed_triangle_set &pad_mesh,
                              const std::vector<float>   &grid,
                              float                       cr,
                              const JobController        &ctl)
{
    using Slices = std::vector<ExPolygons>;

    auto slices = reserve_vector<Slices>(2);

    if (!sup_mesh.empty()) {
        slices.emplace_back();
        slices.back() = slice_mesh_ex(sup_mesh, grid, cr, ctl.cancelfn);
    }

    if (!pad_mesh.empty()) {
        slices.emplace_back();

        auto bb     = bounding_box(pad_mesh);
        auto maxzit = std::upper_bound(grid.begin(), grid.end(), bb.max.z());

        auto cap     = grid.end() - maxzit;
        auto padgrid = reserve_vector<float>(size_t(cap > 0 ? cap : 0));
        std::copy(grid.begin(), maxzit, std::back_inserter(padgrid));

        slices.back() = slice_mesh_ex(pad_mesh, padgrid, cr, ctl.cancelfn);
    }

    size_t len = grid.size();
    for (const Slices &slv : slices)
        len = std::min(len, slv.size());

    // Either the support or the pad or both has to be non empty
    if (slices.empty()) return {};

    Slices &mrg = slices.front();

    for (auto it = std::next(slices.begin()); it != slices.end(); ++it) {
        for (size_t i = 0; i < len; ++i) {
            Slices &slv = *it;
            std::copy(slv[i].begin(), slv[i].end(), std::back_inserter(mrg[i]));
            slv[i] = {}; // clear and delete
        }
    }

    return mrg;
}

}} // namespace Slic3r::sla
