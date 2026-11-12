#pragma once

#include <functional>
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelInstance.hpp"
#include "Slic3r/Domain/BedInstance.hpp"

namespace Slic3r::Biz::Slicing {

void with_limited_instances(
    Domain::Model &model,
    const Domain::ModelInstanceList& bed_instances,
    const std::function<void()> &callable
);

}
