#include "Slic3r/Biz/RemovableDrive/RemovableDriveService.hpp"
#include "RemovableDriveMonitorLinux.hpp"

#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include <fmt/format.h>
#define BOOST_PROCESS_VERSION 1
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
#include <boost/process/v1/pipe.hpp>
#include <boost/process/v1/search_path.hpp>

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
    m_eject_thread = JThread::JThread(
        [this, path, previous = std::move(m_eject_thread)](JThread::StopToken stop_token) mutable
        {
            if (previous.joinable()) {
                previous.request_stop();
                previous.join();
            }
            if (stop_token.stop_requested()) {
                return;
            }
            bool res = eject_inner(path);
            if (!res && !stop_token.stop_requested()) {
                dispatch_status_on_main_thread(path, RemovableDriveStatus::Failed);
            }
            // TODO: Dispatch Removed?
        }
    );
}

} // namespace Slic3r::Biz::RemovableDrive
