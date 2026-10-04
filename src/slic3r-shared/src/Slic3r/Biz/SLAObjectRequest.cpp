#include "Slic3r/Biz/SLAObjectRequest.hpp"

#include "libslic3r/SLAResult.hpp"

using Slic3r::Biz::Slicing::StatusCode;
using Slic3r::Domain::ObjectID;
using Slic3r::Domain::SlicingId;

namespace Slic3r::Biz {

SLAObjectRequest::SLAObjectRequest(
    Slicing::SlicingInteractor& slicing_interactor,
    StatusCache& status_cache,
    SLAObjectCache& object_cache
) :
    m_slicing_interactor(slicing_interactor),
    m_status_cache(status_cache),
    m_object_cache(object_cache)
{}

SLAObjectRequest::~SLAObjectRequest()
{
    this->cancel();
}

void SLAObjectRequest::start(
    const SlicingId slicing_id,
    const ObjectID model_object_id,
    const SLAPrintObjectStep step
)
{
    this->cancel();

    m_state           = State::WaitingForSlicing;
    m_slicing_id      = slicing_id;
    m_model_object_id = model_object_id;
    m_step            = step;
    m_object_cache.add_listener<ISLAObjectCacheChangedListener>(this);
    m_status_cache.add_listener<IStatusCacheChangedListener>(this);

    if (const SLAObjectOptRef object{this->cached_object()}; object && this->is_complete(*object)) {
        this->complete(object);
        return;
    }

    const StatusCode status = m_slicing_interactor.get_status(slicing_id);
    if (status == StatusCode::Modified) {
        this->request_slicing();
    } else if (status == StatusCode::Finished) {
        this->complete(this->cached_object());
    } else if (status == StatusCode::Empty || status == StatusCode::InvalidData) {
        this->complete(std::nullopt);
    }
    // Otherwise slicing is running or the process is being updated, wait for its status.
}

void SLAObjectRequest::cancel()
{
    if (!this->running()) {
        return;
    }

    m_object_cache.remove_listener<ISLAObjectCacheChangedListener>(this);
    m_status_cache.remove_listener<IStatusCacheChangedListener>(this);
    m_state = State::Idle;
}

bool SLAObjectRequest::running() const
{
    return m_state != State::Idle;
}

void SLAObjectRequest::on_sla_object_cache_changed(const SlicingId& id, const ObjectID object_id)
{
    if (!this->running() || id != m_slicing_id || object_id != m_model_object_id) {
        return;
    }

    if (const SLAObjectOptRef object{this->cached_object()}; object && this->is_complete(*object)) {
        this->complete(object);
    }
}

void SLAObjectRequest::on_status_cache_status_code_changed(const SlicingId id)
{
    if (!this->running() || id != m_slicing_id) {
        return;
    }

    const std::optional<Slicing::Status> status = m_status_cache.get_status(id);
    if (!status.has_value()) {
        this->complete(std::nullopt);
        return;
    }

    switch (status->code) {
    case StatusCode::Running:
    case StatusCode::Stopping:
        if (m_state == State::SlicingRequested) {
            m_state = State::SlicingActive;
        }
        break;
    case StatusCode::Modified:
        if (m_state == State::SlicingActive) {
            // The requested slicing ended (or was stopped), use what it got.
            this->complete(this->cached_object());
        } else if (m_state == State::WaitingForSlicing) {
            this->request_slicing();
        }
        break;
    case StatusCode::Finished:
        this->complete(this->cached_object());
        break;
    case StatusCode::Empty:
    case StatusCode::InvalidData:
    case StatusCode::Removed:
        this->complete(std::nullopt);
        break;
    case StatusCode::Updating:
        break;
    }
}

SLAObjectOptRef SLAObjectRequest::cached_object() const
{
    return m_object_cache.get_instance({m_slicing_id, m_model_object_id});
}

bool SLAObjectRequest::is_complete(const Slicing::Sla::Object& object) const
{
    if (!object.mesh) {
        return false;
    }
    return m_step < slaposSupportPoints || object.support_points != nullptr;
}

void SLAObjectRequest::complete(const SLAObjectOptRef object)
{
    this->cancel();
    m_callbacks.completed(object);
}

void SLAObjectRequest::request_slicing()
{
    m_state = State::SlicingRequested;
    m_slicing_interactor.slice_bed(
        m_slicing_id,
        Slicing::SliceUntilStep{m_step, m_model_object_id}
    );
}

} // namespace Slic3r::Biz
