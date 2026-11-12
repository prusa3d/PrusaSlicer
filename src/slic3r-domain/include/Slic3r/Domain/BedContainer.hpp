#pragma once

#include <vector>
#include <string>
#include <functional>

#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/SelectionId.hpp"

namespace Slic3r::Domain {

class ConfigContainer;

class BedContainer
{
public:
    BedContainer copy() const;

    [[nodiscard]] Bed& get_or_create_bed(
        BedType bed_type,
        const Domain::Vec2ds& bed_shape,
        const indexed_triangle_set& contour_mesh,
        const ConfigContainer& config_container,
        const std::string& resources_dir_path,
        SelectionId project_id                                                         = INVALID_ID,
        SelectionId config_container_id                                                = INVALID_ID,
        std::function<Vec2ds(SelectionId, SelectionId)> system_preset_bed_shape_getter = nullptr
    );

    void remove(const Bed* bed);

    size_t beds_count() const
    {
        return m_beds.size();
    }

    std::vector<size_t> beds_indices() const;

    [[nodiscard]] Bed* bed(size_t idx);
    [[nodiscard]] const Bed* bed(size_t idx) const;

    using BedList = std::vector<BedPtr>;

    [[nodiscard]] const BedList& beds() const
    {
        return m_beds;
    }

    [[nodiscard]] BedList& beds()
    {
        return m_beds;
    }

    void reset()
    {
        m_beds.clear();
    }

private:
    BedList m_beds;
};

} // namespace Slic3r::Domain
