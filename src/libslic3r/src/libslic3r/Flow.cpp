#include "libslic3r/Flow.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <cmath>

#include "libslic3r/I18N.hpp"
#include "libslic3r/Print.hpp"
#include "Slic3r/Exception.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/HwConfigUtils.hpp"

namespace Slic3r {

FlowErrorNegativeSpacing::FlowErrorNegativeSpacing() : 
	FlowError("Flow::spacing() produced negative spacing. Did you set some extrusion width too small?") {}

FlowErrorNegativeFlow::FlowErrorNegativeFlow() :
    FlowError("Flow::mm3_per_mm() produced negative flow. Did you set some extrusion width too small?") {}

// This static method returns a sane extrusion width default.
float Flow::auto_extrusion_width(FlowRole role, float nozzle_diameter)
{
    switch (role) {
    case frSupportMaterial:
    case frSupportMaterialInterface:
    case frTopSolidInfill:
        return nozzle_diameter;
    default:
    case frExternalPerimeter:
    case frPerimeter:
    case frSolidInfill:
    case frInfill:
        return 1.125f * nozzle_diameter;
    }
}

// This constructor builds a Flow object from an extrusion width config setting
// and other context properties.
Flow Flow::new_from_config_width(FlowRole role, const Domain::FloatOrPercentage &width, float nozzle_diameter, float height)
{
    if (height <= 0)
        throw Slic3r::InvalidArgument("Invalid flow height supplied to new_from_config_width()");

    float w;
    if (! width.is_percentage() && width.is_zero()) {
        // If user left option to 0, calculate a sane default width.
        w = auto_extrusion_width(role, nozzle_diameter);
    } else {
        // If user set a manual value, use it.
        w = float(width.get_abs_value(nozzle_diameter));
    }
    
    return Flow(w, height, rounded_rectangle_extrusion_spacing(w, height), nozzle_diameter, false);
}

// Adjust extrusion flow for new extrusion line spacing, maintaining the old spacing between extrusions.
Flow Flow::with_spacing(float new_spacing) const
{
    Flow out = *this;
    if (m_bridge) {
        // Diameter of the rounded extrusion.
        assert(m_width == m_height);
        float gap          = m_spacing - m_width;
        auto  new_diameter = new_spacing - gap;
        out.m_width        = out.m_height = new_diameter;
    } else {
        assert(m_width >= m_height);
        out.m_width += new_spacing - m_spacing;
        if (out.m_width < out.m_height)
            throw Slic3r::InvalidArgument("Invalid spacing supplied to Flow::with_spacing()");
    }
    out.m_spacing = new_spacing;
    return out;
}

// Adjust the width / height of a rounded extrusion model to reach the prescribed cross section area while maintaining extrusion spacing.
Flow Flow::with_cross_section(float area_new) const
{
    assert(! m_bridge);
    assert(m_width >= m_height);

    // Adjust for bridge_flow_ratio, maintain the extrusion spacing.
    float area = this->mm3_per_mm();
    if (area_new > area + EPSILON) {
        // Increasing the flow rate.
        float new_full_spacing = area_new / m_height;
        if (new_full_spacing > m_spacing) {
            // Filling up the spacing without an air gap. Grow the extrusion in height.
            float height = area_new / m_spacing;
            return Flow(rounded_rectangle_extrusion_width_from_spacing(m_spacing, height), height, m_spacing, m_nozzle_diameter, false);
        } else {
            return this->with_width(rounded_rectangle_extrusion_width_from_spacing(area / m_height, m_height));
        }
    } else if (area_new < area - EPSILON) {
        // Decreasing the flow rate.
        float width_new = m_width - (area - area_new) / m_height;
        assert(width_new > 0);
        if (width_new > m_height) {
            // Shrink the extrusion width.
            return this->with_width(width_new);
        } else {
            // Create a rounded extrusion.
            auto dmr = 2.0 * float(sqrt(area_new / M_PI));
            return Flow(dmr, dmr, m_spacing, m_nozzle_diameter, false);
        }
    } else
        return *this;
}

float Flow::rounded_rectangle_extrusion_spacing(float width, float height)
{
    auto out = width - height * float(1. - 0.25 * PI);
    if (out <= 0.f)
        throw FlowErrorNegativeSpacing();
    return out;
}

float Flow::rounded_rectangle_extrusion_width_from_spacing(float spacing, float height)
{
    return float(spacing + height * (1. - 0.25 * PI));
}

float Flow::bridge_extrusion_spacing(float dmr)
{
    return dmr + BRIDGE_EXTRA_SPACING;
}

// This method returns extrusion volume per head move unit.
double Flow::mm3_per_mm() const
{
    float res = m_bridge ?
        // Area of a circle with dmr of this->width.
        float((m_width * m_width) * 0.25 * PI) :
        // Rectangle with semicircles at the ends. ~ h (w - 0.215 h)
        float(m_height * (m_width - m_height * (1. - 0.25 * PI)));
    //assert(res > 0.);
	if (res <= 0.)
		throw FlowErrorNegativeFlow();
    return res;
}

static unsigned get_support_extruder_id(const PrintObject& object, bool is_interface = false) {
    const std::string key{is_interface ? "support_material_interface_extruder" : "support_material_extruder"};
    const int extruder{object.config().get<int>(key) - 1};
    if (extruder >= 0) {
        return static_cast<unsigned>(extruder);
    }

    // If object->config().support_material_extruder == 0 (which means to not trigger tool change,
    // but use the current extruder instead), use the smallest nozzle diameter.
    const std::vector<double> nozzle_diameters{
        object.config().get<std::vector<double>>("nozzle_diameter")
    };
    double min_nozzle_diameter{std::numeric_limits<double>::max()};
    int min_nozzle_extruder_id{-1};

    const std::vector<unsigned>& extruders{object.print()->get_extruder_candidates()};

    for (unsigned extruder_id : extruders) {
        const double nozzle_diameter{nozzle_diameters[extruder_id]};
        min_nozzle_diameter = std::min(min_nozzle_diameter, nozzle_diameter);
        min_nozzle_extruder_id = extruder_id;
    }

    ASSERT(min_nozzle_extruder_id >= 0);
    return min_nozzle_extruder_id;
}

Flow support_material_flow(const PrintObject *object, float layer_height)
{
    const PrintObjectConfigView &config{object->config()};
    const unsigned extruder{get_support_extruder_id(*object)};

    const float nozzle_diameter{
        static_cast<float>(Biz::Slicing::get_nozzle_diameter(config.hw_config(), extruder))
    };
    const float default_width{
        static_cast<float>(config.get<std::vector<Domain::FloatOrPercentage>>("extrusion_width")
                               .at(extruder)
                               .get_abs_value(nozzle_diameter))
    };

    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        !object->config()
                .get<Domain::FloatOrPercentage>("support_material_extrusion_width")
                .is_zero() ?
            object->config().get<Domain::FloatOrPercentage>("support_material_extrusion_width") :
            default_width,
        nozzle_diameter,
        (layer_height > 0.f) ? layer_height : float(object->config().get<double>("layer_height"))
    );
}

Flow support_material_1st_layer_flow(const PrintObject *object, float layer_height)
{
    const PrintObjectConfigView &config{object->config()};
    const unsigned extruder{get_support_extruder_id(*object)};

    const float nozzle_diameter{
        static_cast<float>(Biz::Slicing::get_nozzle_diameter(config.hw_config(), extruder))
    };
    const Domain::FloatOrPercentage first_layer_extrusion_width{
        config.get<std::vector<Domain::FloatOrPercentage>>("first_layer_extrusion_width")
            .at(extruder)
    };
    const auto width =
        !first_layer_extrusion_width.is_zero() ?
        first_layer_extrusion_width :
        object->config().get<Domain::FloatOrPercentage>("support_material_extrusion_width");

    const float default_width{
        static_cast<float>(config.get<std::vector<Domain::FloatOrPercentage>>("extrusion_width")
                               .at(extruder)
                               .get_abs_value(nozzle_diameter))
    };

    return Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        !width.is_zero() ? width : default_width,
        nozzle_diameter,
        (layer_height > 0.f) ? layer_height : float(config.get<Domain::FloatOrPercentage>("first_layer_height").get_abs_value(object->config().get<double>("layer_height"))));
}

Flow support_material_interface_flow(const PrintObject *object, float layer_height)
{
    const PrintObjectConfigView& config{object->config()};
    const unsigned extruder{get_support_extruder_id(*object)};

    // If object->config().support_material_interface_extruder == 0 (which means to not trigger tool change, but use the current extruder instead), use the smallest nozzle diameter.
    const float nozzle_diameter{
        static_cast<float>(Biz::Slicing::get_nozzle_diameter(config.hw_config(), extruder))
    };
    const float default_width{
        static_cast<float>(config.get<std::vector<Domain::FloatOrPercentage>>("extrusion_width")
                               .at(extruder)
                               .get_abs_value(nozzle_diameter))
    };

    return Flow::new_from_config_width(
        frSupportMaterialInterface,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        !object->config()
                .get<Domain::FloatOrPercentage>("support_material_extrusion_width")
                .is_zero() ?
            object->config().get<Domain::FloatOrPercentage>("support_material_extrusion_width") :
            default_width,
        nozzle_diameter,
        (layer_height > 0.f) ? layer_height : float(object->config().get<double>("layer_height"))
    );
}

}
