#pragma once

#include "Slic3r/Biz/SLAObjectCache.hpp"
#include "Slic3r/Biz/StatusCache.hpp"

#include "libslic3r/PrintSteps.hpp"

#include <functional>
#include <optional>

namespace Slic3r::Biz {

/**
 * @brief Provides an SLA object sliced at least until the given step, from the cache or by
 * requesting slicing of just that object until the step.
 *
 * The object is complete when it has the mesh and, for a support points step or later, the
 * support points. When slicing ends without them (e.g. supports are disabled, the slicing was
 * stopped or failed), the request completes with what is in the cache, or nothing.
 */
class SLAObjectRequest : public ISLAObjectCacheChangedListener, public IStatusCacheChangedListener
{
public:
    struct Callbacks
    {
        std::function<void(SLAObjectOptRef)> completed = [](SLAObjectOptRef) {};
    };

    SLAObjectRequest(
        Slicing::SlicingInteractor& slicing_interactor,
        StatusCache& status_cache,
        SLAObjectCache& object_cache
    );
    ~SLAObjectRequest() override;

    SLAObjectRequest(const SLAObjectRequest&)            = delete;
    SLAObjectRequest& operator=(const SLAObjectRequest&) = delete;

    Callbacks& callbacks()
    {
        return m_callbacks;
    }

    /**
     * @brief Starts the request. It may complete (and call the callback) before returning.
     */
    void start(Domain::SlicingId slicing_id, Domain::ObjectID model_object_id, SLAPrintObjectStep step);
    void cancel();
    [[nodiscard]] bool running() const;

    void on_sla_object_cache_changed(const Domain::SlicingId& id, Domain::ObjectID object_id) override;
    void on_status_cache_status_code_changed(Domain::SlicingId id) override;

private:
    enum class State
    {
        Idle,
        WaitingForSlicing,
        SlicingRequested,
        SlicingActive
    };

    [[nodiscard]] SLAObjectOptRef cached_object() const;
    [[nodiscard]] bool is_complete(const Slicing::Sla::Object& object) const;
    void complete(SLAObjectOptRef object);
    void request_slicing();

    Slicing::SlicingInteractor& m_slicing_interactor;
    StatusCache& m_status_cache;
    SLAObjectCache& m_object_cache;

    Callbacks m_callbacks;

    State m_state{State::Idle};
    Domain::SlicingId m_slicing_id;
    Domain::ObjectID m_model_object_id;
    SLAPrintObjectStep m_step{slaposAssembly};
};

} // namespace Slic3r::Biz
