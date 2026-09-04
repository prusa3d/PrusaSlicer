#pragma once

#include <map>
#include <functional>
#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include "Slic3r/Biz/Platform/WithListeners.hpp"
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz {

class ISLAObjectCacheChangedListener
{
public:
    virtual ~ISLAObjectCacheChangedListener() = default;
    virtual void on_sla_object_cache_changed(const Domain::SlicingId& id, ::Slic3r::Domain::ObjectID instance_id) = 0;
};

using SLAObjectRef = std::reference_wrapper<const Slicing::Sla::Object>;
using SLAObjectOptRef = std::optional<SLAObjectRef>;

/// <summary>
/// Access to object data during slicing
/// Frontend is responsible for data lifetime (call remove())
/// NOTE: Multiple bed could be sliced at the same time
/// Data must survive for switch to another bed
/// </summary>
class SLAObjectCache :
    public Slicing::ISLAObjectListener,
    public WithListeners<ISLAObjectCacheChangedListener>
{
public:
    using Key = std::pair<Domain::SlicingId, ::Slic3r::Domain::ObjectID>;
    SLAObjectOptRef get_instance(const Key& key) const;
    std::vector<Domain::ObjectID> get_object_ids(const Domain::SlicingId slicing_id) const;
    // NOTE: instance with id == 0 means bed is removed
    void on_sla_object_changed(const Domain::SlicingId& id, Slicing::Sla::Object&& object) override;
    // remove object for bed which are not in object_ids
    void on_remove_bed(const Domain::SlicingId& id) override;
    using Cache = std::map<Key, Slicing::Sla::Object>;
private:
    Cache m_objects;
};
} // namespace Slic2r::Biz
