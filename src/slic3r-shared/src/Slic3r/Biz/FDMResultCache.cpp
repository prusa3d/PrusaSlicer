#include "Slic3r/Biz/FDMResultCache.hpp"
#include <ranges>
#include "fmt/ostream.h"

namespace Slic3r::Biz {

std::optional<FDMResultRef> FDMResultCache::get_result(const Domain::SlicingId id) const
{
    if (!m_results.contains(id)) {
        return std::nullopt;
    }
    return m_results.at(id);
}

void FDMResultCache::on_fdm_result_changed(
    Slicing::FDMResult&& result,
    const Domain::SlicingId id
)
{
    m_results[id] = std::move(result);

    if (m_results[id].const_moves()->empty()) {
        m_results.erase(id);
        SPDLOG_TRACE("{}: cleared", fmt::streamed(id));
    } else {
        SPDLOG_TRACE("{}: updated", fmt::streamed(id));
    }

    const std::size_t mb{1024 * 1024};
    SPDLOG_TRACE("FDMResultCache size: {} MB", memsize() / static_cast<double>(mb));

    invoke_listeners<IFDMResultCacheChangedListener>([&](auto* listener) {
        listener->on_fdm_result_cache_changed(id);
    });
}

std::size_t FDMResultCache::memsize() const
{
    std::size_t bytes{};
    for (const Slicing::FDMResult& result : m_results | std::views::values) {
        bytes += result.const_gcode()->size_in_bytes();
        bytes += result.const_moves()->capacity() * sizeof(libpgcode::MoveVertex);
    }
    return bytes;
}

} // namespace Slic2r::Biz::FDMResultCache
