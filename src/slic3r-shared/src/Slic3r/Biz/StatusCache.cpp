#include "Slic3r/Biz/StatusCache.hpp"

namespace Slic3r::Biz {

void StatusCache::on_status_changed(const Slicing::StatusUpdate status_update, const Domain::SlicingId id)
{
    const std::optional<Slicing::StatusCode> previous_status_code{
        m_statuses.contains(id) ? std::optional{m_statuses.at(id).code} : std::nullopt
    };

    if (status_update.code == Slicing::StatusCode::Removed) {
        m_statuses.erase(id);
        m_latest_warnings.erase(id);
    } else {
        Slicing::Status& status{m_statuses[id]};
        if (status_update.code) {
            status.code = *status_update.code;
        }

        if (status_update.clear_errors) {
            status.errors.clear();
        }
        for (const Biz::Slicing::Error& error : status_update.errors_to_append) {
            m_latest_errors[id].push_back(error);
            status.errors.push_back(error);
        }
        if (status_update.clear_errors || !status_update.errors_to_append.empty()) {
            invoke_listeners<IStatusCacheChangedListener>([&](auto* listener){
                listener->on_status_cache_errors_changed(id);
            });
        }

        if (status_update.clear_warnings) {
            m_latest_warnings.clear();
            status.warrnings.clear();
        }
        for (const Biz::Slicing::Warning& warning : status_update.warnings_to_append) {
            m_latest_warnings[id].push_back(warning);
            status.warrnings.push_back(warning);
        }
        if (status_update.clear_warnings || !status_update.warnings_to_append.empty()) {
            invoke_listeners<IStatusCacheChangedListener>([&](auto* listener){
                listener->on_status_cache_warnings_changed(id);
            });
        }

        if (status_update.clear_progress) {
            status.progress = std::nullopt;
        }
        if (status_update.progress) {
            status.progress = *status_update.progress;
        }
        if (status_update.clear_progress || status_update.progress) {
            invoke_listeners<IStatusCacheChangedListener>([&](auto* listener){
                listener->on_status_cache_progress_changed(id);
            });
        }
    }

    if (status_update.code && previous_status_code != status_update.code) {
        invoke_listeners<IStatusCacheChangedListener>([&](auto* listener){
            listener->on_status_cache_status_code_changed(id);
        });
    }
}

std::optional<Slicing::Status> StatusCache::get_status(const Domain::SlicingId id) const
{
    if(!m_statuses.contains(id)) {
        return std::nullopt;
    }
    return m_statuses.at(id);
}

std::vector<Biz::Slicing::Error> StatusCache::extract_latest_errors(const Domain::SlicingId id) {
    if (!m_latest_errors.contains(id)) {
        return {};
    }
    const std::vector<Biz::Slicing::Error> result{std::move(m_latest_errors.at(id))};
    m_latest_errors.erase(id);
    return result;
}

std::vector<Biz::Slicing::Warning> StatusCache::extract_latest_warnings(const Domain::SlicingId id) {
    if (!m_latest_warnings.contains(id)) {
        return {};
    }
    const std::vector<Biz::Slicing::Warning> result{std::move(m_latest_warnings.at(id))};
    m_latest_warnings.erase(id);
    return result;
}

} // namespace Slic3r::Biz
