#pragma once

#include "Slic3r/Assert.hpp"

#include "IRenderRequestHandler.hpp"
#include "IMainThreadDispatcher.hpp"
#include "TimerQueue.hpp"
#include "Slic3r/Biz/Platform/ISecretStore.hpp"
#include "Slic3r/Biz/Platform/ISingleInstanceChecker.hpp"

namespace Slic3r::Biz::Platform {
    
class IAppConfigProvider;
class IAppInstanceMessageHandler;

namespace JobManager {
    class JobManager;
};

class PlatformServices
{
public:
    static PlatformServices& instance();
    ~PlatformServices();

    void set_render_request_handler(IRenderRequestHandler* render_request_handler);
    void set_main_thread_dispatcher(std::unique_ptr<IMainThreadDispatcher>&& main_thread_dispatcher);
    void set_secret_store(std::unique_ptr<ISecretStore>&& secret_store);
    void set_app_hash(size_t app_hash) { m_app_hash = app_hash; }
    void set_single_instance_checker(std::unique_ptr<ISingleInstanceChecker>&& single_instance_checker);
    void set_job_manager(std::unique_ptr<JobManager::JobManager>&& job_manager);
    void set_app_config_provider(std::unique_ptr<IAppConfigProvider>&& provider);
    void set_app_instance_message_handler(std::unique_ptr<IAppInstanceMessageHandler>&& message_handler);

    IRenderRequestHandler& render_request_handler()
    {
        ASSERT(m_render_request_handler != nullptr);
        return *m_render_request_handler;
    }

    IMainThreadDispatcher& main_thread_dispatcher()
    {
        ASSERT(m_main_thread_dispatcher != nullptr);
        return *m_main_thread_dispatcher;
    }

    TimerQueue& timer_queue()
    {
        ASSERT(m_timer_queue != nullptr);
        return *m_timer_queue;
    }

    ISecretStore& secret_store()
    {
        ASSERT(m_secret_store != nullptr);
        return *m_secret_store;
    }

    size_t app_hash() const 
    { 
        //ASSERT(m_app_hash != 0); 
        return m_app_hash; 
    }

    ISingleInstanceChecker& single_instance_checker()
    {
        ASSERT(m_single_instance_checker != nullptr);
        return *m_single_instance_checker;
    }

    IAppInstanceMessageHandler& app_instance_message_handler();

    JobManager::JobManager& job_manager();

    bool has_job_manager() const
    {
        return m_job_manager != nullptr;
    }

    IAppConfigProvider& app_config_provider();

private:
    IRenderRequestHandler* m_render_request_handler{nullptr};
    std::unique_ptr<IMainThreadDispatcher> m_main_thread_dispatcher{};
    std::unique_ptr<TimerQueue> m_timer_queue{};
    std::unique_ptr<ISecretStore> m_secret_store{};
    size_t m_app_hash {0};
    /**
     * ISingleInstanceChecker is stored here because:
     * 1. Windows (wxw) implemetation needs to be alive on whole run of app.
     * 2. Mac implementation of AppInstanceMessageHandler needs to call is_another_running to reaquire the lock.
     */ 
    std::unique_ptr<ISingleInstanceChecker> m_single_instance_checker{nullptr};  
    std::unique_ptr<JobManager::JobManager> m_job_manager;
    std::unique_ptr<IAppConfigProvider> m_app_config_provider;
    std::unique_ptr<IAppInstanceMessageHandler> m_app_instance_message_handler;
};

} // namespace Slic3r::Biz::Platform
