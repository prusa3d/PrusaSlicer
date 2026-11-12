#include <memory>

#include "Slic3r/Biz/Algorithms/SVG.hpp"
#include "Slic3r/Biz/Algorithms/Scaling.hpp"
#include "Slic3r/Biz/Arrange/Utils.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/ExPolygon.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"
#include "Slic3r/Biz/Arrange/Kernels/SVGDebugOutputKernelWrapper.hpp"
#include "Slic3r/Biz/Algorithms/BoundingBox.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Slic3r::Biz::Algorithms::BoundingBox::inflated;
using Slic3r::Biz::Algorithms::Polygon::to_expolygons;
using Slic3r::Biz::Algorithms::Scaling::scaled;

SVGDebugOutputKernelWrapper::SVGDebugOutputKernelWrapper(
    const Domain::BoundingBox2crd& bounds,
    std::unique_ptr<IKernel> kern
) :
    k{std::move(kern)},
    drawbounds{bounds}
{}

bool SVGDebugOutputKernelWrapper::on_start_packing(
    ArrangeItem& itm,
    const IBed& bed,
    const PackingContext& packing_context,
    std::span<const ArrangeItem> rem
)
{
    using namespace Slic3r;

    bool ret = k->on_start_packing(itm, bed, packing_context, rem);

    svg.reset();
    auto fixed = packing_context.all_items();
    svg        = std::make_unique<Biz::Algorithms::SVG::SVG>(
        "arranged_step_" + std::to_string(fixed.size() + 1) + ".svg",
        inflated(drawbounds, scaled(10.0))
    );

    svg->draw(Domain::ExPolygon{to_rectangle(drawbounds)}, "blue", .2f);

    auto nfp = itm.calculate_nfp(packing_context, bed, []() { return false; });
    svg->draw_outline(nfp);
    svg->draw(nfp, "green", 0.2f);

    for (const auto& fixeditm : fixed) {
        Domain::ExPolygons fixeditm_outline = Slic3r::Biz::Algorithms::Polygon::to_expolygons(
            fixeditm.fixed_shape().transformed_outline()
        );
        svg->draw_outline(fixeditm_outline);
        svg->draw(fixeditm_outline, "yellow", 0.5f);
    }

    return ret;
}

double SVGDebugOutputKernelWrapper::placement_fitness(
    const ArrangeItem& item,
    const Domain::Vec2crd& transl
) const
{
    return k->placement_fitness(item, transl);
}

bool SVGDebugOutputKernelWrapper::on_item_packed(ArrangeItem& itm)
{
    bool ret = k->on_item_packed(itm);

    if (svg) {
        Domain::ExPolygons itm_outline = to_expolygons(itm.fixed_shape().transformed_outline());
        Domain::ExPolygons itm_movable_outline = to_expolygons(
            itm.movable_shape().transformed_outline()
        );

        svg->draw_outline(itm_outline);
        svg->draw_outline(itm_movable_outline, "red");
        svg->draw(itm_outline, "grey");

        svg->Close();
    }

    return ret;
}

} // namespace Slic3r::Biz::Arrange::Kernels
