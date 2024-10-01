#include "ZCorrection.hpp"

#include <iterator>

#include "libslic3r/Execution/ExecutionTBB.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Exception.hpp"
#include "libslic3r/libslic3r.h"

namespace Slic3r { namespace sla {

std::vector<ExPolygons> apply_zcorrection(
    const std::vector<ExPolygons> &slices, size_t layers)
{
    return zcorr_detail::apply_zcorrection(ex_tbb, slices, layers);
}

std::vector<ExPolygons> apply_zcorrection(const std::vector<ExPolygons> &slices,
                                          const std::vector<float> &grid,
                                          float depth)
{
    return zcorr_detail::apply_zcorrection(ex_tbb, slices, grid, depth);
}

namespace zcorr_detail {

DepthMap create_depthmap(const std::vector<ExPolygons> &slices,
                         const std::vector<float> &grid,
                         size_t max_depth)
{
    struct DepthPoly {
        size_t depth = 0;
        ExPolygons contour;
    };

    DepthMap ret;

    if (slices.empty() || slices.size() != grid.size())
        return ret;

    size_t depth_limit = max_depth > 0 ? max_depth : slices.size();
    ret.resize(slices.size());

    ret.front() = DepthMapLayer{ {size_t{0}, slices.front()} };

    for (size_t i = 0; i < slices.size() - 1; ++i) {
        DepthMapLayer &depths_current = ret[i];
        DepthMapLayer &depths_nxt = ret[i + 1];

        for (const auto &[depth, cntrs] : depths_current) {
            DepthPoly common;

            common.contour = intersection_ex(slices[i + 1], cntrs);
            common.depth = std::min(depth_limit, depth + 1);

            DepthPoly overhangs;
            overhangs.contour = diff_ex(slices[i + 1], cntrs);

            if (!common.contour.empty()) {
                std::copy(common.contour.begin(), common.contour.end(),
                          std::back_inserter(depths_nxt[common.depth]));
            }

            if (!overhangs.contour.empty()) {
                std::copy(overhangs.contour.begin(), overhangs.contour.end(),
                          std::back_inserter(depths_nxt[overhangs.depth]));
            }
        }

        for(auto &[i, cntrs] : depths_nxt) {
            depths_nxt[i] = union_ex(cntrs);
        }
    }

    return ret;
}

void apply_zcorrection(DepthMap &dmap, size_t layers)
{
    for (size_t lyr = 0; lyr < dmap.size(); ++lyr) {
        size_t threshold = std::min(lyr, layers);

        auto &dlayer = dmap[lyr];

        for (auto it = dlayer.begin(); it != dlayer.end();)
            if (it->first < threshold)
                it = dlayer.erase(it);
            else
                ++it;
    }
}

ExPolygons merged_layer(const DepthMapLayer &dlayer)
{
    using namespace Slic3r;

    ExPolygons out;
    for (auto &[i, cntrs] : dlayer) {
        std::copy(cntrs.begin(), cntrs.end(), std::back_inserter(out));
    }

    out = union_ex(out);

    return out;
}

std::vector<ExPolygons> depthmap_to_slices(const DepthMap &dm)
{
    auto out = reserve_vector<ExPolygons>(dm.size());
    for (const auto &dlayer : dm) {
        out.emplace_back(merged_layer(dlayer));
    }

    return out;
}

ExPolygons intersect_layers(const std::vector<ExPolygons> &slices,
                            size_t layer_from, size_t layers_down)
{
    size_t drill_to = std::min(layer_from, layers_down);
    auto drill_to_layer = static_cast<int>(layer_from - drill_to);

    ExPolygons merged_lyr = slices[layer_from];
    for (int i = layer_from; i >= drill_to_layer; --i)
        merged_lyr = intersection_ex(merged_lyr, slices[i]);

    return merged_lyr;
}

} // namespace zcorr_detail

} // namespace sla
} // namespace Slic3r
