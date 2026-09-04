#pragma once

#include "Slic3r/Domain/SelectionId.hpp"
#include "Slic3r/Domain/BedRef.hpp"

namespace Slic3r::App::Scene {
class BedError;
} // namespace Slic3r::App::Scene

namespace Slic3r::App::Plater {

class IBedVisuallyChangedListener
{
public:
    virtual ~IBedVisuallyChangedListener() = default;

    virtual void on_bed_changed(Domain::SelectionId project_id, const Domain::BedRefs& bed_refs,
        const Scene::BedError& bed_error) = 0;
};

} // namespace Slic3r::App::Plater
