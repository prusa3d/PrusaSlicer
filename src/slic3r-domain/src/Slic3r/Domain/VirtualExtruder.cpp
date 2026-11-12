#include "Slic3r/Domain/VirtualExtruder.hpp"

namespace Slic3r::Domain {
bool VirtualExtruderComponent::operator==(const VirtualExtruderComponent& other) const
{
    return this->extruder_id == other.extruder_id && this->ratio == other.ratio;
}

bool VirtualExtruderComponent::operator!=(const VirtualExtruderComponent& other) const
{
    return !(*this == other);
}

bool VirtualExtruderGradientStop::operator==(const VirtualExtruderGradientStop& other) const
{
    return this->extruder_id == other.extruder_id && this->position == other.position;
}

bool VirtualExtruderGradientStop::operator!=(const VirtualExtruderGradientStop& other) const
{
    return !(*this == other);
}

bool VirtualExtruderGradient::operator==(const VirtualExtruderGradient& other) const
{
    return this->z_min == other.z_min && this->z_max == other.z_max && this->stops == other.stops;
}

bool VirtualExtruderGradient::operator!=(const VirtualExtruderGradient& other) const
{
    return !(*this == other);
}

VirtualExtruder::Type VirtualExtruder::type() const
{
    return gradient.has_value() ? Type::Gradient : Type::Blend;
}

bool VirtualExtruder::operator==(const VirtualExtruder& other) const
{
    return this->id == other.id
        && this->color == other.color
        && this->components == other.components
        && this->gradient == other.gradient;
}

bool VirtualExtruder::operator!=(const VirtualExtruder& other) const
{
    return !(*this == other);
}

} // namespace Slic3r::Domain
