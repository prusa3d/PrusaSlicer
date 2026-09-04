#pragma once

#include "Slic3r/Biz/Slicing/SlicingInteractor.hpp"
#include <map>

namespace Slic3r::Biz {


struct IStatusCacheChangedListener
{
    virtual ~IStatusCacheChangedListener() = default;
    virtual void on_status_cache_status_code_changed(const Domain::SlicingId id) {};
    virtual void on_status_cache_progress_changed(const Domain::SlicingId id) {};
    virtual void on_status_cache_warnings_changed(const Domain::SlicingId id) {};
    virtual void on_status_cache_errors_changed(const Domain::SlicingId id) {};
};

struct StatusCache :
    public Slicing::IStatusListener,
    public WithListeners<IStatusCacheChangedListener>
{
    void on_status_changed(const Slicing::StatusUpdate status, const Domain::SlicingId id);
    std::optional<Slicing::Status> get_status(const Domain::SlicingId id) const;
    std::vector<Biz::Slicing::Error> extract_latest_errors(const Domain::SlicingId id);
    std::vector<Biz::Slicing::Warning> extract_latest_warnings(const Domain::SlicingId id);

private:
    std::map<Domain::SlicingId, Slicing::Status> m_statuses;
    std::map<Domain::SlicingId, std::vector<Biz::Slicing::Warning>> m_latest_warnings;
    std::map<Domain::SlicingId, std::vector<Biz::Slicing::Error>> m_latest_errors;
};

} // namespace Slic3r::Biz
