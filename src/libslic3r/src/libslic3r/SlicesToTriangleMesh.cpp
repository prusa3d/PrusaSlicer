#include <Slic3r/Log.hpp>
#include <cassert>
#include <cstddef>

#include "SlicesToTriangleMesh.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Tesselate.hpp"
#include "libslic3r/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Algorithms/Execution/ExecutionTBB.hpp"

namespace Slic3r {

using Domain::its_merge;
namespace TriMesh = Biz::Algorithms::TriangleMesh;

// Same as walls() but with identical higher and lower polygons.
indexed_triangle_set inline straight_walls(const Polygon &plate,
                                     double         lo_z,
                                     double         hi_z)
{
    using Slic3r::Biz::Algorithms::Tesselate::wall_strip;
    return wall_strip(plate, lo_z, hi_z);
}

indexed_triangle_set inline straight_walls(const ExPolygon &plate,
                                     double           lo_z,
                                     double           hi_z)
{
    indexed_triangle_set ret = straight_walls(plate.contour, lo_z, hi_z);
    for (auto &h : plate.holes)
        its_merge(ret, straight_walls(h, lo_z, hi_z));

    return ret;
}

indexed_triangle_set inline straight_walls(const ExPolygons &slice,
                                     double            lo_z,
                                     double            hi_z)
{
    indexed_triangle_set ret;
    for (const ExPolygon &poly : slice)
        its_merge(ret, straight_walls(poly, lo_z, hi_z));

    return ret;
}

namespace execution = Slic3r::Biz::Algorithms::Execution;

indexed_triangle_set slices_to_mesh(
    const std::vector<ExPolygons> &slices,
    double                         zmin,
    const std::vector<float> &     grid)
{

    using Slic3r::Biz::Algorithms::Tesselate::triangulate_expolygons_3d;
    using Slic3r::Biz::Algorithms::Tesselate::NORMALS_UP;
    using Slic3r::Biz::Algorithms::Tesselate::NORMALS_DOWN;

    assert(slices.size() == grid.size());

    using Layers = std::vector<indexed_triangle_set>;
    Layers layers(slices.size());
    size_t len = slices.size() - 1;

    auto threads_cnt = execution::max_concurrency(execution::ex_tbb);
    execution::for_each(execution::ex_tbb, size_t(0), len, [&slices, &layers, &grid](size_t i) {
        const ExPolygons &upper = slices[i + 1];
        const ExPolygons &lower = slices[i];

        // Small 0 area artefacts can be created by diff_ex, and the
        // tesselation also can create 0 area triangles. These will be removed
        // by its_remove_degenerate_faces.
        ExPolygons free_top = diff_ex(lower, upper);
        ExPolygons overhang = diff_ex(upper, lower);
        its_merge(layers[i], triangulate_expolygons_3d(free_top, grid[i], NORMALS_UP));
        its_merge(layers[i], triangulate_expolygons_3d(overhang, grid[i], NORMALS_DOWN));
        its_merge(layers[i], straight_walls(upper, grid[i], grid[i + 1]));
        }, threads_cnt);

    auto merge_fn = []( const indexed_triangle_set &a, const indexed_triangle_set &b ) {
        indexed_triangle_set res{a}; its_merge(res, b); return res;
    };

    auto ret = execution::reduce(execution::ex_tbb, layers.begin(), layers.end(),
                                 indexed_triangle_set{}, merge_fn,
                                 threads_cnt);

    its_merge(ret, triangulate_expolygons_3d(slices.front(), zmin, NORMALS_DOWN));
    its_merge(ret, straight_walls(slices.front(), zmin, grid.front()));
    its_merge(ret, triangulate_expolygons_3d(slices.back(), grid.back(), NORMALS_UP));

    // FIXME: these repairs do not fix the mesh entirely. There will be cracks
    // in the output. It is very hard to do the meshing in a way that does not
    // leave errors.
    int num_mergedv = TriMesh::its_merge_vertices(ret);
    SPDLOG_DEBUG("Merged vertices count: {}", num_mergedv);

    int remcnt = TriMesh::its_remove_degenerate_faces(ret);
    SPDLOG_DEBUG("Removed degenerate faces count: {}", remcnt);

    int num_erasedv = TriMesh::its_compactify_vertices(ret);
    SPDLOG_DEBUG("Erased vertices count: {}", num_erasedv);

    return ret;
}

void slices_to_mesh(indexed_triangle_set &         mesh,
                    const std::vector<ExPolygons> &slices,
                    double                         zmin,
                    double                         lh,
                    double                         ilh)
{
    std::vector<float> grid(slices.size(), zmin + ilh);

    for (size_t i = 1; i < grid.size(); ++i) grid[i] = grid[i - 1] + lh;

    indexed_triangle_set cntr = slices_to_mesh(slices, zmin, grid);
    its_merge(mesh, cntr);
}

} // namespace Slic3r
