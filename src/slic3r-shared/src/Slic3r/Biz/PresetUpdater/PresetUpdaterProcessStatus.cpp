#include "PresetUpdaterProcessStatus.hpp"

#include <chrono>
#include <utility>

#include <fmt/format.h>

using namespace std::chrono_literals;

namespace Slic3r::Biz::PresetUpdater {

namespace {
    constexpr Network::HttpRetryOpt get_retry_policy_opt(PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy policy) noexcept {
        switch (policy) {
            case PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_5_TRIES:
                return {500ms, 5s, 4};
            case PresetUpdaterProcessStatus::PresetUpdaterRetryPolicy::PURP_NO_RETRY:
            default:
                return {0ms};
        }
    }
}

PresetUpdaterProcessStatus::PresetUpdaterProcessStatus(JThread::StopToken stop_token, StatusChangedFn status_fn, PresetUpdaterRetryPolicy retry_policy, bool ignore_hash) :
    m_retry_policy(get_retry_policy_opt(retry_policy)),
    m_dispatch_status_fn(std::move(status_fn)),
    m_stop_token(std::move(stop_token)),
    m_ignore_file_hash(ignore_hash)
{
}

bool PresetUpdaterProcessStatus::on_attempt(int attempt, unsigned delay)
{
    if (m_dispatch_status_fn) {
        PresetUpdaterProgress reported = m_progress;
        reported.attempt               = attempt;
        reported.delay                 = delay;
        m_dispatch_status_fn(reported);
    }
    return get_canceled();
}

void PresetUpdaterProcessStatus::set_step(
    PresetUpdaterStep step, std::string vendor_id, std::string version
)
{
    m_progress.step      = step;
    m_progress.vendor_id = std::move(vendor_id);
    m_progress.version   = std::move(version);
    m_progress.attempt   = 0;
    m_progress.delay     = 0;

    if (m_dispatch_status_fn) {
        m_dispatch_status_fn(m_progress);
    }
}

} // namespace Slic3r::Biz::PresetUpdater