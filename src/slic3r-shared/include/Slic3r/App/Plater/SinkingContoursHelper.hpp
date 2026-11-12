#pragma once

#include "Slic3r/App/Scene/SceneNodeTag.hpp"

#include <boost/functional/hash.hpp>

namespace Slic3r::App::Plater {

struct SinkingSceneNodeTag : public Scene::SceneNodeTag
{
};

struct SinkingAuxiliaryElementId
{
    size_t instance_id;
    size_t volume_id;

    bool operator==(const SinkingAuxiliaryElementId& rhs) const { return instance_id == rhs.instance_id && volume_id == rhs.volume_id; }
    bool operator<(const SinkingAuxiliaryElementId& rhs) const
    {
        return (instance_id < rhs.instance_id) || (instance_id == rhs.instance_id && volume_id < rhs.volume_id);
    }
};

} // namespace Slic3r::App::Plater

namespace std {
template<>
struct hash<Slic3r::App::Plater::SinkingAuxiliaryElementId>
{
    using value_type = Slic3r::App::Plater::SinkingAuxiliaryElementId;
    std::uint64_t operator()(const value_type& val) const
    {
        size_t ret = boost::hash_value(val.instance_id);
        boost::hash_combine(ret, val.volume_id);
        return ret;
    }
};
} // namespace std
