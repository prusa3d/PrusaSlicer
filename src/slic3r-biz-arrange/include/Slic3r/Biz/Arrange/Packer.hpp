#pragma once
#include <span>
#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"
#include "Slic3r/Biz/Arrange/Kernels/IKernel.hpp"
#include "Slic3r/Biz/Arrange/Bed.hpp"
#include "Slic3r/Biz/Arrange/PackingContext.hpp"
#include "Slic3r/Biz/Algorithms/Optimize/NLoptOptimizer.hpp"

namespace Slic3r::Biz::Arrange {

struct Packer
{
    std::unique_ptr<Kernels::IKernel> kernel;
    double accuracy{};
    Algorithms::Optimize::Optimizer<Algorithms::Optimize::AlgNLoptSubplex> solver;
    StopCondition stop_condition;

    bool pack(
        const IBed& bed,
        ArrangeItem& item,
        const PackingContext& packing_context,
        std::span<ArrangeItem> remaining_items
    );

private:
    double pick_best_spot_on_nfp(ArrangeItem& item, const Domain::ExPolygons& nfp, const IBed& bed);
};

} // namespace Slic3r::Biz::Arrange
