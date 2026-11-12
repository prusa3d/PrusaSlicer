#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Kernels/KernelUtils.hpp"
#include "Slic3r/Biz/Arrange/Kernels/GravityKernel.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

GravityKernel::GravityKernel(Domain::Vec2crd gravity_center) :
    sink{gravity_center},
    active_sink{Biz::Algorithms::Scaling::unscaled<double>(gravity_center)}
{}

double GravityKernel::placement_fitness(const ArrangeItem& itm, const Domain::Vec2crd& transl) const
{
    Domain::Vec2d center = Biz::Algorithms::Scaling::unscaled<double>(itm.movable_shape().centroid());

    center += Biz::Algorithms::Scaling::unscaled<double>(transl);

    return -(center - active_sink).squaredNorm();
}

bool GravityKernel::on_start_packing(
    ArrangeItem& itm,
    const IBed& bed,
    const PackingContext& packing_context,
    std::span<const ArrangeItem> /*remaining_items*/
)
{
    bool ret = false;

    const std::optional<Domain::Vec2d> gravity_sink{itm.gravity_sink};
    if (gravity_sink) {
        const Domain::Vec2d dimensions{
            Algorithms::BoundingBox::sizes(bed.bounding_box()).cast<double>()
        };
        const Domain::Vec2d min{bed.bounding_box().min.cast<double>()};
        item_sink = (min + gravity_sink->cwiseProduct(dimensions)).cast<int>();
    } else {
        item_sink = std::nullopt;
    }

    if (!sink) {
        sink = Biz::Algorithms::BoundingBox::center(bed.bounding_box());
    }

    if (item_sink)
        active_sink = Biz::Algorithms::Scaling::unscaled<double>(*item_sink);
    else
        active_sink = Biz::Algorithms::Scaling::unscaled<double>(*sink);

    ret = find_initial_position(itm, Biz::Algorithms::Scaling::scaled(active_sink), bed, packing_context);

    return ret;
}

bool GravityKernel::on_item_packed(ArrangeItem& itm)
{
    return true;
}

} // namespace Slic3r::Biz::Arrange::Kernels
