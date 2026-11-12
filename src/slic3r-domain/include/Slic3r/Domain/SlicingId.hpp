#pragma once

#include <ostream>
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Domain {

struct SlicingId
{
    Domain::SelectionId project_id{};
    Domain::SelectionId bed_instance_id{};

    bool operator<(const SlicingId& other) const
    {
        if (project_id != other.project_id) {
            return project_id < other.project_id;
        }
        return bed_instance_id < other.bed_instance_id;
    }

    bool operator==(const SlicingId& b) const
    {
        return bed_instance_id == b.bed_instance_id && project_id == b.project_id;
    }
};

inline std::ostream& operator<<(std::ostream& output, const SlicingId& id) {
    return output
        << "{project_id: " << id.project_id
        << ", bed_id: " << id.bed_instance_id << "}";
}


} // namespace Slic3r::Domain
