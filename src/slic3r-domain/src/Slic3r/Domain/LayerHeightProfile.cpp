#include "Slic3r/Domain/LayerHeightProfile.hpp"

namespace Slic3r::Domain {

double LayerZRange::height() const
{
    return top_z - bottom_z;
}

double LayerZRange::middle_z() const
{
    return 0.5 * (bottom_z + top_z);
};

void LayerHeightProfile::assign(const LayerHeightProfile& rhs)
{
    if (!this->timestamp_matches(rhs)) {
        m_data = rhs.m_data;
        this->copy_timestamp(rhs);
    }
}

void LayerHeightProfile::assign(LayerHeightProfile&& rhs)
{
    if (!this->timestamp_matches(rhs)) {
        m_data = std::move(rhs.m_data);
        this->copy_timestamp(rhs);
    }
}

const ZHeightPairs& LayerHeightProfile::get() const noexcept
{
    return m_data;
}

bool LayerHeightProfile::empty() const noexcept
{
    return m_data.empty();
}

void LayerHeightProfile::set(const ZHeightPairs& data)
{
    if (m_data != data) {
        m_data = data;
        this->touch();
    }
}

void LayerHeightProfile::set(ZHeightPairs&& data)
{
    if (m_data != data) {
        m_data = std::move(data);
        this->touch();
    }
}

void LayerHeightProfile::clear()
{
    m_data.clear();
    this->touch();
}

} // namespace Slic3r::Domain
