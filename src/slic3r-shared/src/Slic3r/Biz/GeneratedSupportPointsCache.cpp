#include "Slic3r/Biz/GeneratedSupportPointsCache.hpp"

#include <fmt/ostream.h>

using Slic3r::Biz::Slicing::GeneratedSupportPointsSnapshot;
using Slic3r::Biz::Slicing::ObjectSupportPoints;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::SlicingId;

namespace Slic3r::Biz {

std::optional<ObjectSupportPointsRef> GeneratedSupportPointsCache::get_object_support_points(
    const SlicingId id,
    const ObjectID object_id
) const
{
    const auto results_it = m_results.find(std::pair{id, object_id});
    if (results_it == m_results.end()) {
        return std::nullopt;
    }

    return results_it->second;
}

void GeneratedSupportPointsCache::on_generated_support_points_changed(
    GeneratedSupportPointsSnapshot&& support_points,
    const SlicingId id
)
{
    std::erase_if(m_results, [&](const auto& entry) { return entry.first.first == id; });

    if (support_points.empty()) {
        SPDLOG_TRACE("{}: cleared", fmt::streamed(id));
    } else {
        for (ObjectSupportPoints& object_support_points : support_points) {
            const ObjectID object_id = object_support_points.model_object_id;
            m_results.emplace(std::pair{id, object_id}, std::move(object_support_points));
        }

        SPDLOG_TRACE("{}: updated", fmt::streamed(id));
    }

    invoke_listeners<IGeneratedSupportPointsCacheChangedListener>(
        [&](auto* listener) { listener->on_generated_support_points_cache_changed(id); }
    );
}

} // namespace Slic3r::Biz
