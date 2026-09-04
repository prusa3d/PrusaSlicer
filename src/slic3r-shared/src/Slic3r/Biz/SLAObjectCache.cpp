#include "Slic3r/Biz/SLAObjectCache.hpp"

#include "libslic3r/SLAResult.hpp"

#include "fmt/ostream.h"

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Slicing;
using Slic3r::Biz::Slicing::Sla::Object;

SLAObjectOptRef SLAObjectCache::get_instance(const SLAObjectCache::Key& key) const
{
    auto it = m_objects.find(key);
    if (it == m_objects.end()) {
        SPDLOG_INFO("{}, {}: no instance for key", fmt::streamed(key.first), key.second.id);
        return std::nullopt;
    }    
    return it->second;
}

std::vector<Slic3r::Domain::ObjectID> SLAObjectCache::get_object_ids(
    const Domain::SlicingId slicing_id
) const
{
    std::vector<Domain::ObjectID> result;
    for (const auto& pair : m_objects) {
        if (pair.first.first == slicing_id) {
            result.push_back(pair.first.second);
        }
    }
    return result;
}

void SLAObjectCache::on_sla_object_changed(const Domain::SlicingId& id, Object&& object)
{
    const std::size_t object_id{object.object_id.id};
    ASSERT(object_id != 0);

    Key key(id, object.object_id);
    if (object.instance_trafos.empty()) {
        SPDLOG_INFO("{}: sla object {} removed", fmt::streamed(id), object_id);
        m_objects.erase(key);
    } else {
        Object& object_in_cache{m_objects[key]};
        if (!object.mesh && !object.support_structure && !object.pad) {
            SPDLOG_INFO("{}: sla object {} trafos updated", fmt::streamed(id), object_id);
            object_in_cache.instance_trafos = std::move(object.instance_trafos);
        } else {
            SPDLOG_INFO("{}: sla object {} updated", fmt::streamed(id), object_id);
            m_objects[key] = std::move(object);
        }
    }
    invoke_listeners<ISLAObjectCacheChangedListener>([key](auto* listener) {
        listener->on_sla_object_cache_changed(key.first, key.second);
    });
}

void SLAObjectCache::on_remove_bed(const Domain::SlicingId& id) {
    std::erase_if(m_objects, [&id](const auto& pair) { return pair.first.first == id; });
}
