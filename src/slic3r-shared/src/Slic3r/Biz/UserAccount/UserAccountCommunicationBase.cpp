#include "Slic3r/Biz/UserAccount/UserAccountCommunicationBase.hpp"

#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Log.hpp"

#include <boost/nowide/cstdio.hpp>

namespace Slic3r::Biz::UserAccount {

UserAccountCommunicationBase::UserAccountCommunicationBase(
    Platform::IMainThreadDispatcher& dispatcher
) :
    m_session{dispatcher}
{}

UserAccountCommunicationBase::~UserAccountCommunicationBase()
{
    stop_session_thread();
}

void UserAccountCommunicationBase::stop_session_thread()
{
    m_session.stop();

    // Since there is m_thread_stop_condition in m_thread,
    // we need to manually request_stop and wake up the condition for the thread to stop.
    m_thread.request_stop();
    wakeup_session_thread();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UserAccountCommunicationBase::init_session_thread()
{
    m_thread = JThread::JThread(
        [&](JThread::StopToken stop_token)
        {
            for (;;) {
                {
                    std::unique_lock<std::mutex> lck(m_thread_stop_mutex);
                    m_thread_stop_condition.wait_for(
                        lck,
                        std::chrono::seconds(88'888),
                        [this, stop_token]
                        { return stop_token.stop_requested() || m_thread_wakeup; }
                    );
                    m_thread_wakeup = false;
                }
                if (stop_token.stop_requested())
                    // Stop the worker thread.
                    break;
                m_session.process_action_queue();
            }
        }
    );
}

void UserAccountCommunicationBase::wakeup_session_thread()
{
    {
        std::lock_guard<std::mutex> lck(m_thread_stop_mutex);
        m_thread_wakeup = true;
    }
    m_thread_stop_condition.notify_all();
}

boost::filesystem::path UserAccountCommunicationBase::avatar() const
{
    if (is_logged_in()) {
        const std::string filename = "slic3r3-avatar-"
            + std::to_string(Platform::PlatformServices::instance().app_hash())
            + m_avatar_extension;
        return Slic3r::temp_dir() / filename;
    } else {
        return boost::filesystem::path(resources_dir()) / "icons" / "user.svg";
    }
}

void UserAccountCommunicationBase::on_avatar_url(const std::string& data)
{
    const boost::filesystem::path server_file(data);
    enqueue_avatar_download(data, server_file.extension().string());
}

void UserAccountCommunicationBase::
    enqueue_avatar_download(const std::string& url, const std::string& extension)
{
    m_avatar_extension = extension;
    ASSERT(m_session.is_initialized());
    m_session.enqueue_action({UserAccountActionID::Avatar, nullptr, nullptr, url, {}});
    wakeup_session_thread();
}

void UserAccountCommunicationBase::on_avatar_success(std::string&& data) const
{
    ASSERT(is_logged_in());
    boost::filesystem::path path = avatar();
    FILE* file;
    file = boost::nowide::fopen(path.generic_string().c_str(), "wb");
    if (file == NULL) {
        SPDLOG_ERROR("Failed to create file to store avatar picture at: {}", path.string());
        return;
    }
    fwrite(data.c_str(), 1, data.size(), file);
    fclose(file);
}

} // namespace Slic3r::Biz::UserAccount
