#include "libslic3r/ShrinkageCompensation.hpp"

#include "Slic3r/Domain/Percentage.hpp"

#include "libslic3r/ConfigViews.hpp"

namespace Slic3r::Biz::Slicing {

static bool has_same_shrinkage_compensations(
    const std::vector<unsigned int> extruders,
    const PrintConfigView& config
)
{
    if (extruders.empty())
        return false;

    const auto compensation_xy{
        config.get<std::vector<Domain::Percentage>>("filament_shrinkage_compensation_xy")
    };

    const auto compensation_z{
        config.get<std::vector<Domain::Percentage>>("filament_shrinkage_compensation_z")
    };

    for (unsigned int extruder : extruders) {
        if (compensation_xy.front() != compensation_xy.at(extruder)
            || compensation_z.front() != compensation_z.at(extruder))
        {
            return false;
        }
    }

    return true;
}

std::optional<Domain::Vec3d> get_shrinkage_compensation(
    const std::vector<unsigned int>& extruders,
    const PrintConfigView& config
)

{
    if (extruders.empty()) {
        return std::nullopt;
    }
    if (!has_same_shrinkage_compensations(extruders, config)) {
        return std::nullopt;
    }

    const unsigned int first_extruder{extruders.front()};
    const double xy_compensation_percent{std::clamp(
        config.get<std::vector<Domain::Percentage>>("filament_shrinkage_compensation_xy")
            .at(first_extruder)
            .value,
        -99.,
        99.
    )};

    const double z_compensation_percent{std::clamp(
        config.get<std::vector<Domain::Percentage>>("filament_shrinkage_compensation_z")
            .at(first_extruder)
            .value,
        -99.,
        99.
    )};

    const double xy_compensation = 100. / (100. - xy_compensation_percent);
    const double z_compensation  = 100. / (100. - z_compensation_percent);

    return Domain::Vec3d{xy_compensation, xy_compensation, z_compensation};
}
} // namespace Slic3r::Biz::Slicing
