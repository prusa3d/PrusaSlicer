#include "Slic3r/Biz/Algorithms/ClipperUtils.hpp"
#include "Slic3r/Biz/Algorithms/Geometry/ConvexHull.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Polygon.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Utils.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Arrange/Kernels/KernelUtils.hpp"
#include "Slic3r/Biz/Arrange/Kernels/CompactifyKernel.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Algorithms::BoundingBox::center;
using Algorithms::Polygon::to_expolygons;

double CompactifyKernel::placement_fitness(const ArrangeItem& itm, const Domain::Vec2crd& transl) const
{
    auto pile = merged_pile;

    Domain::ExPolygons itm_tr = to_expolygons(itm.movable_shape().transformed_outline());
    for (auto& p : itm_tr)
        p.translate(transl);

    append(pile, std::move(itm_tr));

    pile = Biz::Algorithms::ClipperUtils::union_ex(pile);

    Domain::Polygon chull = Biz::Algorithms::Geometry::convex_hull(pile);

    return -(chull.area());
}

bool CompactifyKernel::on_start_packing(
    ArrangeItem& itm,
    const IBed& bed,
    const PackingContext& packing_context,
    const std::span<const ArrangeItem> /*remaining_items*/
)
{
    bool ret = find_initial_position(itm, center(bed.bounding_box()), bed, packing_context);

    merged_pile.clear();
    for (const auto& gitm : packing_context.all_items()) {
        append(merged_pile, to_expolygons(gitm.fixed_shape().transformed_outline()));
    }
    merged_pile = Biz::Algorithms::ClipperUtils::union_ex(merged_pile);

    return ret;
}

bool CompactifyKernel::on_item_packed(ArrangeItem& itm)
{
    return true;
}

} // namespace Slic3r::Biz::Arrange::Kernels
