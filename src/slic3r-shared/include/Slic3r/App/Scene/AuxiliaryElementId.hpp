#pragma once

#include <boost/functional/hash.hpp>

namespace Slic3r::App::Scene {

struct AuxiliaryElementId
{
    enum class Type : uint8_t
    {
        Volume = 0,
        BedLabel,
        BedPlate,
        BedGrid,
        BedContour,
        BedPrintVolume,
        BedAxis,
        BedModel,
        WipeTower,
        SlaMesh,
        SlaSupports,
        SlaPad,
        SlaTopClip,
        SlaBottomClip,
        AABB,
    };

    Type type;
    size_t id;

    /**
     *
     * @param rhs
     * @return
     */
    bool operator==(const AuxiliaryElementId& rhs) const { return type == rhs.type && id == rhs.id; }

    bool operator<(const AuxiliaryElementId& rhs) const
    {
        return type < rhs.type || (type == rhs.type && id < rhs.id);
    }
};

} // namespace Slic3r::App::Scene


namespace std {
template<>
struct hash<Slic3r::App::Scene::AuxiliaryElementId>
{
    using value_type = Slic3r::App::Scene::AuxiliaryElementId;
    std::uint64_t operator()(const value_type& val) const
    {
        size_t ret = boost::hash_value(val.type);
        boost::hash_combine(ret, val.id);
        return ret;
    }
};

} // namespace std
