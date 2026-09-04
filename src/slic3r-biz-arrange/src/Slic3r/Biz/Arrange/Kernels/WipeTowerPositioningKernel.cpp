#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Arrange/Bed.hpp"
#include "Slic3r/Biz/Arrange/Kernels/WipeTowerPositioningKernel.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Algorithms::BoundingBox::center;
using Algorithms::BoundingBox::translated;

WipeTowerPositioningKernel::WipeTowerPositioningKernel(std::unique_ptr<IKernel> child_kernel) :
    m_child_kernel{std::move(child_kernel)}
{}

double WipeTowerPositioningKernel::placement_fitness(
    const ArrangeItem& item,
    const Domain::Vec2crd& transl
) const
{
    double score{m_child_kernel->placement_fitness(item, transl)};
    if (!m_packed_wipe_tower_position) {
        return score;
    }

    const auto item_bounding_box{translated(item.movable_shape().bounding_box(), transl)};

    score -= (center(item_bounding_box) - *m_packed_wipe_tower_position).cast<double>().norm();
    return score;
}

bool WipeTowerPositioningKernel::on_start_packing(
    ArrangeItem& itm,
    const IBed& bed,
    const PackingContext& packing_context,
    std::span<const ArrangeItem> remaining_items
)
{
    for (auto& item : packing_context.packed_items()) {
        if (!m_packed_wipe_tower_position && item.is_wipe_tower) {
            m_packed_wipe_tower_position = center(item.fixed_shape().bounding_box());
        }
    }

    return m_child_kernel->on_start_packing(itm, bed, packing_context, remaining_items);
}

bool WipeTowerPositioningKernel::on_item_packed(ArrangeItem& itm)
{
    return m_child_kernel->on_item_packed(itm);
}
} // namespace Slic3r::Biz::Arrange::Kernels
