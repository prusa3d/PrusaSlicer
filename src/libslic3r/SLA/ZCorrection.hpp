#ifndef ZCORRECTION_HPP
#define ZCORRECTION_HPP

#include <stddef.h>
#include <algorithm>
#include <map>
#include <vector>
#include <cstddef>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Execution/Execution.hpp"

namespace Slic3r {
namespace sla {

std::vector<ExPolygons> apply_zcorrection(const std::vector<ExPolygons> &slices,
                                          size_t layers);

std::vector<ExPolygons> apply_zcorrection(const std::vector<ExPolygons> &slices,
                                          const std::vector<float> &grid,
                                          float depth);

namespace zcorr_detail {

ExPolygons intersect_layers(const std::vector<ExPolygons> &slices,
                            size_t layer_from,
                            size_t layers_down);

template<class Ex>
std::vector<ExPolygons> apply_zcorrection(Ex ep,
                                          const std::vector<ExPolygons> &slices,
                                          size_t layers)
{
    std::vector<ExPolygons> output(slices.size());

    execution::for_each(ep, size_t{0}, slices.size(),
        [&output, &slices, layers] (size_t lyr) {
            output[lyr] = intersect_layers(slices, lyr, layers);
        }, execution::max_concurrency(ep));

    return output;
}

inline size_t depth_to_layers(const std::vector<float> &grid,
                              size_t from_layer,
                              float depth)
{
    size_t depth_layers = 0;
    while (from_layer > depth_layers &&
           grid[from_layer - depth_layers] > grid[from_layer] - depth)
        depth_layers++;

    return depth_layers;
}

template<class Ex>
std::vector<ExPolygons> apply_zcorrection(Ex ep,
                                          const std::vector<ExPolygons> &slices,
                                          const std::vector<float> &grid,
                                          float depth)
{
    std::vector<ExPolygons> output(slices.size());

    execution::for_each(ep, size_t{0}, slices.size(),
        [&output, &slices, &grid, depth] (size_t lyr) {
            output[lyr] = intersect_layers(slices, lyr,
                                           depth_to_layers(grid, lyr, depth));
        }, execution::max_concurrency(ep));

    return output;
}

using DepthMapLayer = std::map<size_t, ExPolygons>;
using DepthMap = std::vector<DepthMapLayer>;

DepthMap create_depthmap(const std::vector<ExPolygons> &slices,
                         const std::vector<float> &grid, size_t max_depth = 0);

void apply_zcorrection(DepthMap &dmap, size_t layers);

ExPolygons merged_layer(const DepthMapLayer &dlayer);

std::vector<ExPolygons> depthmap_to_slices(const DepthMap &dm);

} // namespace zcorr_detail

} // namespace sla
} // namespace Slic3r

#endif // ZCORRECTION_HPP
