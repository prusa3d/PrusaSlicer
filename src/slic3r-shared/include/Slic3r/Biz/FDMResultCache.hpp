#pragma once

#include <map>
#include <functional>
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/libpgcode/ProcessorResult.hpp"

namespace Slic3r::Biz {

struct IFDMResultCacheChangedListener
{
    virtual ~IFDMResultCacheChangedListener() = default;
    virtual void on_fdm_result_cache_changed(const Domain::SlicingId id) = 0;
};

using FDMResultRef = std::reference_wrapper<const Slicing::FDMResult>;
class FDMResultCache :
    public Slicing::IFDMResultListener,
    public WithListeners<IFDMResultCacheChangedListener>
{
public:
    std::optional<FDMResultRef> get_result(const Domain::SlicingId id) const;

    void on_fdm_result_changed(
        Slicing::FDMResult&& result,
        const Domain::SlicingId id
    ) override;

private:
    std::map<Domain::SlicingId, Slicing::FDMResult> m_results;

    std::size_t memsize() const;
};
} // namespace Slic2r::Biz::FDMResultCache
