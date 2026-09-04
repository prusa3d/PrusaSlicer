#include <Slic3r/Biz/Platform/PlatformServices.hpp>

#include <Slic3r/Biz/Platform/JobManager/JobManager.hpp>
#include <Slic3r/Biz/Platform/IAppConfigProvider.hpp>
#include <Slic3r/Biz/Platform/IAppInstanceMessageHandler.hpp>

namespace Slic3r::Biz::Platform {

PlatformServices& PlatformServices::instance()
{
    static PlatformServices instance;
    return instance;
}

PlatformServices::~PlatformServices() = default;

void PlatformServices::set_render_request_handler(
    IRenderRequestHandler* render_request_handler
)
{
    m_render_request_handler = render_request_handler;
}

void PlatformServices::set_main_thread_dispatcher(
    std::unique_ptr<IMainThreadDispatcher>&& main_thread_dispatcher
)
{
    ASSERT(main_thread_dispatcher, "The new main_thread_dispatcher pointer must not be nullptr!");
    ASSERT(
        m_main_thread_dispatcher == nullptr || m_main_thread_dispatcher->is_closed(),
        "The main thread dispatcher may only be replaced after the previous one was closed! "
        "Multiple places take a reference to it!"
    );

    m_timer_queue.reset();
    m_main_thread_dispatcher = std::move(main_thread_dispatcher);
    m_timer_queue            = std::make_unique<TimerQueue>(*m_main_thread_dispatcher);
}

void PlatformServices::set_secret_store(std::unique_ptr<ISecretStore>&& secret_store)
{
    m_secret_store = std::move(secret_store);
}

void PlatformServices::set_single_instance_checker(std::unique_ptr<ISingleInstanceChecker>&& single_instance_checker)
{
    m_single_instance_checker = std::move(single_instance_checker);
}

void PlatformServices::set_job_manager(std::unique_ptr<JobManager::JobManager>&& job_manager) {
    m_job_manager = std::move(job_manager);
}

JobManager::JobManager& PlatformServices::job_manager()
{
    ASSERT(m_job_manager);
    return *m_job_manager;
}

void PlatformServices::set_app_config_provider(std::unique_ptr<IAppConfigProvider>&& provider)
{
    m_app_config_provider = std::move(provider);
}

IAppConfigProvider& PlatformServices::app_config_provider()
{
    ASSERT(m_app_config_provider);
    return *m_app_config_provider;
}

void PlatformServices::set_app_instance_message_handler(
    std::unique_ptr<IAppInstanceMessageHandler>&& message_handler
)
{
    m_app_instance_message_handler = std::move(message_handler);
}

IAppInstanceMessageHandler& PlatformServices::app_instance_message_handler()
{
    ASSERT(m_app_instance_message_handler);
    return *m_app_instance_message_handler;
}

} // namespace Slic3r::Biz::Platform
