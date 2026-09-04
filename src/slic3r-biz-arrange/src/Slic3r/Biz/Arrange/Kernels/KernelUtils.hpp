#pragma once


#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"
#include "Slic3r/Biz/Arrange/PackingContext.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Biz/Arrange/Bed.hpp"

namespace Slic3r::Biz::Arrange::Kernels {

bool find_initial_position(
    ArrangeItem& itm,
    const Domain::Vec2crd& sink,
    const IBed& bed,
    const PackingContext& packing_context
);

} // namespace Slic3r::Biz::Arrange::Kernels
