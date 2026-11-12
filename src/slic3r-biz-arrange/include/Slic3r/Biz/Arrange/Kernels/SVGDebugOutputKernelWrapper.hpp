#pragma once

#include <memory>

#include "Slic3r/Biz/Algorithms/SVG.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Algorithms/Polygon.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

using Slic3r::Biz::Algorithms::Polygon::to_expolygons;

struct SVGDebugOutputKernelWrapper : public IKernel
{
    std::unique_ptr<IKernel> k;
    std::unique_ptr<Biz::Algorithms::SVG::SVG> svg;
    Domain::BoundingBox2crd drawbounds;

    SVGDebugOutputKernelWrapper(const Domain::BoundingBox2crd& bounds, std::unique_ptr<IKernel> kern);

    bool on_start_packing(
        ArrangeItem& itm,
        const IBed& bed,
        const PackingContext& packing_context,
        std::span<const ArrangeItem> rem
    ) final;

    double placement_fitness(const ArrangeItem& item, const Domain::Vec2crd& transl) const final;

    bool on_item_packed(ArrangeItem& itm) final;
};

} // namespace Slic3r::Biz::Arrange::Kernels
