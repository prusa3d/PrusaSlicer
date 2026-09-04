#pragma once

#include "Slic3r/Biz/Arrange/ArrangeItem.hpp"

namespace Slic3r::Domain {
    class ModelInstance;
    class Model;
}

namespace Slic3r::Biz::Arrange {

struct ArrangeResult
{
    std::vector<ArrangeItem> packed;
    std::vector<ArrangeItem> not_packed;
};

std::vector<ArrangeItem> to_arrange_items(const std::vector<InputShape>& items, const Settings& settings);

std::optional<ArrangeResult> arrange(
    const Domain::Points& bed_contour,
    std::vector<ArrangeItem>& items,
    const std::vector<ArrangeItem>& fixed_items,
    const Settings& settings,
    StopCondition stop_condition
);

struct InstanceTransform2D {
    Domain::ElementRef instance_ref;
    Domain::Vec2d absolute_offset;
    double rotation_delta;
};

using InstanceTransforms = std::vector<InstanceTransform2D>;


// The following helper is easier to use if you just want
// to get the transforms for the instances,
InstanceTransforms arrange_instances(
    const std::vector<const Domain::ModelInstance*>& instances,
    const Domain::Points& bed_contour_scaled,
    const Settings& settings
);

// Following helper is the easiest one to use,
// but it does not call any listeners!
void arrange_model_in_place(
    Domain::Model& model,
    const Domain::Points& bed_contour_scaled,
    const Settings& settings
);


} // namespace Slic3r::Biz::Arrange
