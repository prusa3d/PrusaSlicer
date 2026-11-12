#include <algorithm>
#include <numeric>
#include <ranges>
#include <vector>
#include <cmath>
#include <cstddef>

#include "Slic3r/Exception.hpp"
#include "Slic3r/Biz/Algorithms/VirtualExtruder.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Flow.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/HwConfigUtils.hpp"
#include "libslic3r/Feature/VirtualExtruder/VirtualExtruder.hpp"

using Slic3r::Biz::Algorithms::VirtualExtruder::expand_virtual_extruders_1based;
using Slic3r::Biz::Slicing::get_nozzle_diameter;
using Slic3r::Domain::VirtualExtruders;

namespace Slic3r {

// 1-based extruder identifier for this region and role.
unsigned int PrintRegion::extruder(FlowRole role) const
{
    size_t extruder = 0;
    if (role == frPerimeter || role == frExternalPerimeter)
        extruder = m_config.get<int>("perimeter_extruder");
    else if (role == frInfill)
        extruder = m_config.get<int>("infill_extruder");
    else if (role == frSolidInfill || role == frTopSolidInfill)
        extruder = m_config.get<int>("solid_infill_extruder");
    else
        throw Slic3r::InvalidArgument("Unknown role");

    if (extruder == 0) {
        return 1;
    }

    // Virtual extruders resolve to the physical extruder of their first component.
    if (extruder > m_config.hw_config().material_slot_count()) {
        for (const Domain::VirtualExtruder& virtual_extruder : m_config.virtual_extruders()) {
            if (virtual_extruder.id == extruder && !virtual_extruder.components.empty()) {
                return virtual_extruder.components.front().extruder_id;
            }
        }

        return 1;
    }

    return extruder;
}

Flow PrintRegion::flow(const PrintObject &object, FlowRole role, double layer_height, bool first_layer) const
{
    const unsigned extruder_id{extruder(role) - 1};

    const PrintConfigView &print_config = object.print()->config();
    Domain::FloatOrPercentage config_width;
    // Get extrusion width from configuration.
    // (might be an absolute value, or a percent value, or zero for auto)
    if (first_layer && !print_config.get<std::vector<Domain::FloatOrPercentage>>("first_layer_extrusion_width").at(extruder_id).is_zero()) {
        config_width = print_config.get<std::vector<Domain::FloatOrPercentage>>("first_layer_extrusion_width").at(extruder_id);
    } else if (role == frExternalPerimeter) {
        config_width = m_config.get<std::vector<Domain::FloatOrPercentage>>("external_perimeter_extrusion_width").at(extruder_id);
    } else if (role == frPerimeter) {
        config_width = m_config.get<std::vector<Domain::FloatOrPercentage>>("perimeter_extrusion_width").at(extruder_id);
    } else if (role == frInfill) {
        config_width = m_config.get<std::vector<Domain::FloatOrPercentage>>("infill_extrusion_width").at(extruder_id);
    } else if (role == frSolidInfill) {
        config_width = m_config.get<std::vector<Domain::FloatOrPercentage>>("solid_infill_extrusion_width").at(extruder_id);
    } else if (role == frTopSolidInfill) {
        config_width = m_config.get<std::vector<Domain::FloatOrPercentage>>("top_infill_extrusion_width").at(extruder_id);
    } else {
        throw Slic3r::InvalidArgument("Unknown role");
    }

    if (config_width.is_zero())
        config_width = object.config().get<std::vector<Domain::FloatOrPercentage>>("extrusion_width").at(extruder_id);

    // Get the configured nozzle_diameter for the extruder associated to the flow role requested.
    // Here this->extruder(role) is > 0.
    auto nozzle_diameter = float(Biz::Slicing::get_nozzle_diameter(print_config.hw_config(), extruder_id));
    return Flow::new_from_config_width(role, config_width, nozzle_diameter, float(layer_height));
}

double PrintRegion::nozzle_dmr_avg(const PrintConfigView& print_config) const
{
    const std::size_t num_extruders{print_config.hw_config().material_slot_count()};
    const auto nozzle_diameter = [&](const unsigned int extruder_1based)
    {
        return get_nozzle_diameter(
            print_config.hw_config(),
            extruder_1based > 0 && extruder_1based <= num_extruders ? extruder_1based - 1 : 0
        );
    };

    const std::vector<unsigned int> physical_extruders{expand_virtual_extruders_1based(
        {static_cast<unsigned int>(m_config.get<int>("perimeter_extruder")),
         static_cast<unsigned int>(m_config.get<int>("infill_extruder")),
         static_cast<unsigned int>(m_config.get<int>("solid_infill_extruder"))},
        m_config.virtual_extruders()
    )};

    if (physical_extruders.empty()) {
        return nozzle_diameter(1);
    }

    const auto diameters{physical_extruders | std::views::transform(nozzle_diameter)};
    return std::accumulate(diameters.begin(), diameters.end(), 0.)
        / static_cast<double>(physical_extruders.size());
}

double PrintRegion::bridging_height_avg(const PrintConfigView &print_config) const
{
    const double bridge_flow_avg{(
        extruder_config_value<double>("bridge_flow_ratio", FlowRole::frPerimeter) +
        extruder_config_value<double>("bridge_flow_ratio", FlowRole::frSolidInfill)
    ) / 2.0};
    return this->nozzle_dmr_avg(print_config) * bridge_flow_avg;
}

void PrintRegion::collect_object_printing_extruders(
    const PrintRegionConfigView& config,
    const bool has_brim,
    std::vector<unsigned int>& object_extruders,
    const VirtualExtruders& virtual_extruders
)
{
    // These checks reflect the same logic used in the GUI for enabling/disabling extruder selection fields.
    const int num_extruders = static_cast<int>(config.hw_config().material_slot_count());
    auto emplace_extruder =
        [num_extruders, &object_extruders, &virtual_extruders](
            int extruder_id,
            const auto& feature_enabled
        )
    {
        const unsigned int extruder_id_u = static_cast<unsigned int>(extruder_id);

        if (extruder_id > num_extruders
            && Biz::Algorithms::VirtualExtruder::is_virtual_extruder(
                extruder_id_u,
                virtual_extruders
            ))
        {
            for (const unsigned int physical_1based :
                 Biz::Algorithms::VirtualExtruder::expand_virtual_extruders_1based(
                     {extruder_id_u},
                     virtual_extruders
                 ))
            {
                if (feature_enabled(physical_1based - 1)) {
                    object_extruders.emplace_back(physical_1based - 1);
                }
            }

            return;
        }

        const int i                        = std::max(0, extruder_id - 1);
        const unsigned int physical_0based = static_cast<unsigned int>(i >= num_extruders ? 0 : i);
        if (feature_enabled(physical_0based)) {
            object_extruders.emplace_back(physical_0based);
        }
    };

    const int perimeter_extruder{config.get<int>("perimeter_extruder")};
    ASSERT(perimeter_extruder > 0);
    emplace_extruder(perimeter_extruder, [&](std::size_t slot) {
        return config.get<std::vector<int>>("perimeters").at(slot) > 0 || has_brim;
    });

    const int infill_extruder{config.get<int>("infill_extruder")};
    ASSERT(infill_extruder > 0);
    emplace_extruder(infill_extruder, [&](std::size_t slot) {
        return config.get<std::vector<Domain::Percentage>>("fill_density").at(slot)
            > Domain::Percentage{0};
    });

    const int solid_infill_extruder{config.get<int>("solid_infill_extruder")};
    emplace_extruder(solid_infill_extruder, [&](std::size_t slot) {
        return config.get<std::vector<int>>("top_solid_layers").at(slot) > 0
            || config.get<std::vector<int>>("bottom_solid_layers").at(slot) > 0;
    });
}

double PrintRegion::nozzle_diameter(FlowRole role) const
{
    return Biz::Slicing::get_nozzle_diameter(m_config.hw_config(), extruder(role) - 1);
}

void PrintRegion::collect_object_printing_extruders(const Print &print, std::vector<unsigned int> &object_extruders) const
{
    // PrintRegion, if used by some PrintObject, shall have all the extruders set to an existing printer extruder.
    // If not, then there must be something wrong with the Print::apply() function.
    auto num_extruders = int(print.config().hw_config().material_slot_count());
    auto valid_extruder = [&](int id) {
        return id <= num_extruders
            || Biz::Algorithms::VirtualExtruder::is_virtual_extruder(static_cast<unsigned int>(id), print.virtual_extruders());
    };
    ASSERT(valid_extruder(this->config().get<int>("perimeter_extruder")));
    ASSERT(valid_extruder(this->config().get<int>("infill_extruder")));
    ASSERT(valid_extruder(this->config().get<int>("solid_infill_extruder")));
    collect_object_printing_extruders(this->config(), print.has_brim(), object_extruders, print.virtual_extruders());
}

}
