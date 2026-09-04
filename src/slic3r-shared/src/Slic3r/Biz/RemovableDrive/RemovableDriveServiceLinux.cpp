#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "RemovableDriveMonitorLinux.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include <fmt/format.h>
#include <boost/process.hpp>

namespace Slic3r::Biz::RemovableDrive {

RemovableDriveService::RemovableDriveService(Platform::IMainThreadDispatcher& dispatcher) :
    m_monitor(std::make_unique<RemovableDriveMonitorLinux>(dispatcher)),
    m_dispatcher(dispatcher)
{}

namespace {
bool eject_inner(const boost::filesystem::path& path)
{
    // there is no usable command in c++ so terminal command is used instead
    // but neither triggers "succesful safe removal messege"
    boost::process::ipstream istd_err;
    boost::process::child child(boost::process::search_path("umount"), path.string().c_str(), (boost::process::std_out & boost::process::std_err) > istd_err);
    std::string line;
    while (child.running() && std::getline(istd_err, line)) {
    }
    // wait for command to finish
    std::error_code ec;
    child.wait(ec);
    if (ec) {
        // The wait call can fail, as it did in https://github.com/prusa3d/PrusaSlicer/issues/5507
        // It can happen even in cases where the eject is sucessful, but better report it as failed.
        // We did not find a way to reliably retrieve the exit code of the process.
        SPDLOG_ERROR("boost::process::child::wait() failed during Ejection. State of Ejection is unknown. Error code: {}", ec.value());
        return false;
    } else if (int err = child.exit_code(); err) {
        SPDLOG_ERROR("Ejecting failed. Exit code: {}", std::to_string(err));
        return false;
    }
    return true;
}
} // namespace

void RemovableDriveService::eject_in_thread(const boost::filesystem::path& path)
{
    if (m_eject_thread.joinable()) {
        m_eject_thread.request_stop();
        m_eject_thread.join();
    }

    m_eject_thread = JThread::JThread(
        [this, path](JThread::StopToken stop_token)
        {
            bool res = eject_inner(path);
            if (!res) {
                dispatch_status_on_main_thread(path, RemovableDriveStatus::Failed);
            }
            // TODO: Dispatch Removed?
        }
    );
}

} // namespace Slic3r::Biz::RemovableDrive
