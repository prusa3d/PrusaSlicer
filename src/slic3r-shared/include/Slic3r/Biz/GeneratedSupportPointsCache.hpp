#pragma once

#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"

#include <functional>
#include <map>
#include <optional>
#include <utility>

namespace Slic3r::Biz {

struct IGeneratedSupportPointsCacheChangedListener
{
    virtual ~IGeneratedSupportPointsCacheChangedListener()                       = default;
    virtual void on_generated_support_points_cache_changed(Domain::SlicingId id) = 0;
};

using ObjectSupportPointsRef = std::reference_wrapper<const Slicing::ObjectSupportPoints>;

class GeneratedSupportPointsCache :
    public Slicing::IGeneratedSupportPointsListener,
    public WithListeners<IGeneratedSupportPointsCacheChangedListener>
{
public:
    std::optional<ObjectSupportPointsRef>
    get_object_support_points(Domain::SlicingId id, Domain::ObjectID object_id) const;

    void on_generated_support_points_changed(
        Slicing::GeneratedSupportPointsSnapshot&& support_points,
        Domain::SlicingId id
    ) override;

private:
    // Latest generated support points indexed by (slicing ID, model object ID) pairs.
    std::map<std::pair<Domain::SlicingId, Domain::ObjectID>, Slicing::ObjectSupportPoints>
        m_results;
};

} // namespace Slic3r::Biz
