#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Assert.hpp"

#include "libslic3r/SLAResult.hpp"

#include "fmt/ostream.h"

using namespace Slic3r::Biz;
using namespace Slic3r::Biz::Slicing;
using Slic3r::Domain::SlicingId;

SLAResultOptRef SLAResultCache::get_result(const SlicingId& id) const
{
    auto it = m_results.find(id);
    if (it == m_results.end()) {
        SPDLOG_WARN("{}: no data", fmt::streamed(id));
        return std::nullopt;
    }
    return it->second;
}

void SLAResultCache::on_sla_result_changed(const SlicingId& id, SLAResult&& result)
{
    switch (result.type) {
    // initialize, rewrite or remove cache entry
    case Sla::ResultType::None: m_results.erase(id); break;
    case Sla::ResultType::Slices: m_results[id] = std::move(result); break;
    case Sla::ResultType::Files: { // extend cache entry
        auto cache_it = m_results.find(id);
        ASSERT(cache_it != m_results.end(), "cache must be already filled");
        cache_it->second.export_data->files = std::move(result.export_data->files);
        break;
    }
    default:
        PANIC("Unsupported SLAResult type in cache");
    }

    invoke_listeners<ISLAResultCacheChangedListener>([&id](auto* listener) {
        listener->on_sla_result_cache_changed(id); });
    SPDLOG_INFO("{}: update sla ", fmt::streamed(id));
}
