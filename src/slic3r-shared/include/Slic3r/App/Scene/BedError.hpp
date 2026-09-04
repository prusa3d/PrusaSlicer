#pragma once

#include "Slic3r/Domain/SlicingId.hpp"

#include <vector>

#define ENABLE_DEBUG_BED_ERROR 0

namespace Slic3r::App::Scene {

class BedError
{
public:
    bool add_bed_instance(const Domain::SlicingId& id);
    bool remove_bed_instance(const Domain::SlicingId& id);
    bool contains(const Domain::SlicingId& id) const;
    const std::vector<Domain::SlicingId>& bed_instances() const { return m_bed_instances; }

private:
    // List of bed instances that have errors
    std::vector<Domain::SlicingId> m_bed_instances;
};

#if ENABLE_DEBUG_BED_ERROR
void render_imgui_debug_bed_error(const BedError& bed_error);
#endif // ENABLE_DEBUG_BED_ERROR

} // namespace Slic3r::App::Scene