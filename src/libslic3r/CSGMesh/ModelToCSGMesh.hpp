///|/ Copyright (c) Prusa Research 2022 - 2023 Tomáš Mészáros @tamasmeszaros
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef MODELTOCSGMESH_HPP
#define MODELTOCSGMESH_HPP

#include "CSGMesh.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/SLA/Hollowing.hpp"
#include "libslic3r/MeshSplitImpl.hpp"

namespace Slic3r { namespace csg {

// Flags to select which parts to export from Model into a csg part collection.
// These flags can be chained with the | operator
enum ModelParts {
    mpartsPositive = 1,   // Include positive parts
    mpartsNegative = 2,   // Include negative parts
    mpartsDrillHoles = 4, // Include drill holes
    mpartsDoSplits = 8,   // Split each splitable mesh and export as a union of csg parts
};

template<class OutIt>
void model_to_csgmesh(const ModelObject &mo,
                      const Transform3d &trafo, // Applies to all exported parts
                      OutIt              out,   // Output iterator
                      // values of ModelParts OR-ed
                      int                parts_to_include = mpartsPositive
                      )
{
    bool do_positives  = parts_to_include & mpartsPositive;
    bool do_negatives  = parts_to_include & mpartsNegative;
    bool do_drillholes = parts_to_include & mpartsDrillHoles;
    bool do_splits     = parts_to_include & mpartsDoSplits;

    for (const ModelVolume *vol : mo.volumes) {
        if (vol && vol->mesh_ptr() &&
            ((do_positives && vol->is_model_part()) ||
             (do_negatives && vol->is_negative_volume()))) {

            bool attempt_split = do_splits && its_is_splittable(vol->mesh().its);
            bool split_failed = false;

            if (attempt_split) {
                // Collect partial meshes and keep outward and inward facing meshes separately.
                std::vector<indexed_triangle_set> meshes_union;
                std::vector<indexed_triangle_set> meshes_difference;
                its_split(vol->mesh().its, SplitOutputFn{[&meshes_union, &meshes_difference, &split_failed](indexed_triangle_set &&its) {
                    if (its.empty())
                        return;
                    double volume = its_volume(its);
                    if (std::abs(volume) > 1.) {
                        if (volume > 0.)
                            meshes_union.emplace_back(std::move(its));
                        else if (volume < 0.) {
                            its_flip_triangles(its);
                            meshes_difference.emplace_back(std::move(its));
                        }
                    } else {
                        // Little mesh like that may be some degenerate artifact.
                        // Things like that might throw further processing off track (SPE-2661).
                        // Better do not split the mesh and work with it as we got it.
                        split_failed = true;
                    }
                }});

                if (! split_failed) {
                    CSGPart part_begin{{}, vol->is_model_part() ? CSGType::Union : CSGType::Difference};
                    part_begin.stack_operation = CSGStackOp::Push;
                    *out = std::move(part_begin);
                    ++out;

                    // Add the operation for each of the partial mesh (outward-facing normals go first).                
                    for (auto* meshes : { &meshes_union, &meshes_difference}) {
                        for (indexed_triangle_set& its : *meshes) {
                            CSGPart part{ std::make_unique<indexed_triangle_set>(std::move(its)),
                                meshes == &meshes_union ? CSGType::Union : CSGType::Difference,
                                (trafo * vol->get_matrix()).cast<float>() };
                            *out = std::move(part);
                            ++out;
                        }
                    }

                    CSGPart part_end{{}};
                    part_end.stack_operation = CSGStackOp::Pop;
                    *out = std::move(part_end);
                    ++out;
                }
            }
            
            if (! attempt_split || split_failed) {
                CSGPart part{&(vol->mesh().its),
                             vol->is_model_part() ? CSGType::Union : CSGType::Difference,
                             (trafo * vol->get_matrix()).cast<float>()};

                *out = std::move(part);
                ++out;
            }
        }
    }

    if (do_drillholes) {
        sla::DrainHoles drainholes = sla::transformed_drainhole_points(mo, trafo);

        for (const sla::DrainHole &dhole : drainholes) {
            CSGPart part{std::make_unique<const indexed_triangle_set>(
                             dhole.to_mesh()),
                         CSGType::Difference};

            *out = std::move(part);
            ++out;
        }
    }
}

}} // namespace Slic3r::csg

#endif // MODELTOCSGMESH_HPP
