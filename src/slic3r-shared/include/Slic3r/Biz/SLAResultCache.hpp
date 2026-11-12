#pragma once

#include <map>
#include <functional>
#include <optional>
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"

namespace Slic3r::Biz {

class ISLAResultCacheChangedListener
{
public:
    virtual ~ISLAResultCacheChangedListener() = default;
    virtual void on_sla_result_cache_changed(const Domain::SlicingId& id) = 0;
};

using SLAResultRef = std::reference_wrapper<const Slicing::SLAResult>;
using SLAResultOptRef = std::optional<SLAResultRef>;
/// <summary>
/// Cache data from SLA slicing backend for the frontend access
/// Frontend is responsible for data lifetime (call remove())
/// NOTE: Multiple bed could be sliced at the same time
/// Data must survive for switch to another bed
/// </summary>
class SLAResultCache :
    public Slicing::ISLAResultListener,
    public WithListeners<ISLAResultCacheChangedListener>
{
public:
    SLAResultOptRef get_result(const Domain::SlicingId& id) const;
    void on_sla_result_changed(const Domain::SlicingId& id, Slicing::SLAResult&& result) override;
private:
    using Cache = std::map<Domain::SlicingId, Slicing::SLAResult>;
    Cache m_results;
};
} // namespace Slic2r::Biz
