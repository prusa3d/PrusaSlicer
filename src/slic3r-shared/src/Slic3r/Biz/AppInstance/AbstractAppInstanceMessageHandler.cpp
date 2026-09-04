#include "Slic3r/Biz/AppInstance/AbstractAppInstanceMessageHandler.hpp"

#include <Slic3r/Biz/Platform/PlatformServices.hpp>
#include "Slic3r/Biz/AppInstance/AppInstanceUtils.hpp"
#include "Slic3r/Biz/Platform/ISingleInstanceChecker.hpp"
#include "Slic3r/Log.hpp"
#include "Slic3r/Assert.hpp"
#include "Slic3r/Biz/Algorithms/StringUtils.hpp"

#include <nlohmann/json.hpp>
#include <boost/filesystem.hpp>
#include <vector>

namespace fs = boost::filesystem;

namespace Slic3r::Biz::AppInstance {

namespace {
fs::path get_path(const std::string& possible_path)
{
    if (possible_path.empty() || possible_path.size() < 3) {
        return {};
    }
    fs::path p(possible_path);
    if (fs::exists(p)) {
        return p;
    }
    if (possible_path.front() == '\"' && possible_path.back() == '\"' && possible_path.size() > 2) {
        fs::path quoted_p(possible_path.substr(1, possible_path.size() - 2));
        if (fs::exists(quoted_p)) {
            return quoted_p;
        }
    }
    return {};
}
} // namespace

AbstractAppInstanceMessageHandler::AbstractAppInstanceMessageHandler(
    Platform::IMainThreadDispatcher& dispatcher
) :
    m_dispatcher{dispatcher}
{
    m_message_handlers["CLI"] = [this](const std::string& data) //
    { handle_message_type_cli(data); };
    m_message_handlers["LOGIN"] = [this](const std::string& data)
    { handle_message_type_login(data); };
    m_message_handlers["STORE_READ"] = [this](const std::string& data)
    { handle_message_type_store_read(data); };
    m_message_handlers["OTHER_CLOSING"] = [this](const std::string& data)
    { handle_message_type_other_closed(data); };
    m_message_handlers["BACKUP_ID_REQUEST"] = [this](const std::string& data)
    { handle_message_type_request_backup_id(data); };
    m_message_handlers["BACKUP_ID_ANSWER"] = [this](const std::string& data)
    { handle_message_type_backup_id(data); };
}

AbstractAppInstanceMessageHandler::~AbstractAppInstanceMessageHandler()
{
    SPDLOG_DEBUG(__FUNCTION__);
    ASSERT(
        m_dispatcher.is_closed(),
        "There must be no queued events (not even in the future),"
        " because they may remember the address of this instance!"
    );
}

void AbstractAppInstanceMessageHandler::handle_message(const std::string& message)
{
    ASSERT(this != nullptr);
    SPDLOG_INFO("Message from another instance {}", redact_app_urls(message));
    // message in format { "type" : "TYPE", "data" : "data" }
    // types: CLI, STORE_READ
    std::string type;
    std::string data;

    try {
        nlohmann::json j = nlohmann::json::parse(message);
        if (j.contains("type")) {
            type = j["type"].get<std::string>();
        }
        if (j.contains("data")) {
            data = j["data"].get<std::string>();
        }
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("Could not parse other instance message: {}", redact_app_urls(e.what()));
        return;
    }

    DEBUG_ASSERT(!type.empty());
    DEBUG_ASSERT(
        m_message_handlers.find(type) != m_message_handlers.end(),
        "Message type that has no handler callback set."
    );
    if (m_message_handlers.find(type) != m_message_handlers.end()) {
        m_message_handlers[type](data);
    }
}

void AbstractAppInstanceMessageHandler::handle_message_type_cli(const std::string& data)
{
    SPDLOG_INFO(__FUNCTION__);
    const bool is_primary =
        Biz::Platform::PlatformServices::instance().single_instance_checker().is_primary_instance();
    // Note: login URLs (prusaslicer://login) are handled on EVERY instance, because the login code
    // must reach the instance that initiated the login (which may not be the primary). File paths
    // and prusaslicer://open are single-instance actions and are handled only on the primary.
    std::vector<std::string> args;
    bool parsed = Algorithms::unescape_strings_cstyle(data, args);
    assert(parsed);
    if (!parsed) {
        SPDLOG_ERROR(
            "message from other instance is incorrectly formatted: {}", redact_app_urls(data)
        );
        return;
    }

    std::vector<boost::filesystem::path> paths;
    std::vector<std::string> downloads;
    bool has_login_arg = false;
    for (auto it = args.begin(); it != args.end(); ++it) {
        if (it->rfind("prusaslicer://login", 0) == 0) {
            has_login_arg = true;
            dispatch_login(*it);
            continue;
        }
        // Everything below is a single-instance action - only the primary instance acts on it.
        if (!is_primary) {
            continue;
        }
        boost::filesystem::path p = get_path(*it);
        if (!p.string().empty()) {
            paths.emplace_back(p);
        } else if (it->rfind("prusaslicer://open", 0) == 0) {
            downloads.emplace_back(*it);
        }
    }

    if (is_primary && has_login_arg) {
        multicast_message("LOGIN", data);
    }

    if (!paths.empty()) {
        std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
        m_dispatcher.dispatch_on_main_thread(
            [this, paths = std::move(paths)]() mutable
            {
                invoke_listeners<Platform::IAppInstanceMessageContentListener>(
                    [paths = std::move(paths)](auto* listener) mutable
                    { listener->on_open_models(std::move(paths)); }
                );
            }
        );
    }
    if (!downloads.empty()) {
        std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
        m_dispatcher.dispatch_on_main_thread(
            [this, downloads = std::move(downloads)]() mutable
            {
                invoke_listeners<Platform::IAppInstanceMessageContentListener>(
                    [downloads = std::move(downloads)](auto* listener) mutable
                    { listener->on_download_models(std::move(downloads)); }
                );
            }
        );
    }
}

void AbstractAppInstanceMessageHandler::handle_message_type_login(const std::string& data)
{
    std::vector<std::string> args;
    bool parsed = Algorithms::unescape_strings_cstyle(data, args);
    assert(parsed);
    if (!parsed) {
        SPDLOG_ERROR(
            "login message from other instance is incorrectly formatted: {}",
            redact_app_urls(data)
        );
        return;
    }
    for (const std::string& arg : args) {
        if (arg.rfind("prusaslicer://login", 0) == 0) {
            dispatch_login(arg);
            break;
        }
    }
}

void AbstractAppInstanceMessageHandler::dispatch_login(const std::string& url)
{
    std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
    m_dispatcher.dispatch_on_main_thread(
        [this, url]()
        {
            invoke_listeners<Platform::IAppInstanceMessageContentListener>(
                [url](auto* listener) { listener->on_login_data(url); }
            );
        }
    );
}

void AbstractAppInstanceMessageHandler::handle_message_type_store_read(const std::string& data)
{
    std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
    m_dispatcher.dispatch_on_main_thread(
        [this]()
        {
            invoke_listeners<Platform::IAppInstanceMessageContentListener>(
                [](auto* listener) { listener->on_read_token_store_message(); }
            );
        }
    );
}

void AbstractAppInstanceMessageHandler::handle_message_type_other_closed(const std::string& data)
{
    SPDLOG_INFO(__FUNCTION__);
    // is_another_running does acquire lockfile if available
    if (!Biz::Platform::PlatformServices::instance().single_instance_checker().is_another_running())
    {
        on_becoming_primary_instance();
    }
}

void AbstractAppInstanceMessageHandler::handle_message_type_request_backup_id(
    const std::string& data
)
{
    std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
    m_dispatcher.dispatch_on_main_thread(
        [this]()
        {
            invoke_listeners<Platform::IAppInstanceMessageContentListener>(
                [](auto* listener) { listener->on_backup_id_requested(); }
            );
        }
    );
}

void AbstractAppInstanceMessageHandler::handle_message_type_backup_id(const std::string& data)
{
    std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
    m_dispatcher.dispatch_on_main_thread(
        [this, data]() // We should rather copy data to main thread
        {
            invoke_listeners<Platform::IAppInstanceMessageContentListener>(
                [&](auto* listener) { listener->on_backup_id_provided(data); }
            );
        }
    );
}

void AbstractAppInstanceMessageHandler::dispatch_go_to_front()
{
    std::lock_guard<std::mutex> lock(m_dispatcher_mutex);
    m_dispatcher.dispatch_on_main_thread(
        [this]()
        {
            invoke_listeners<Platform::IAppInstanceMessageContentListener>(
                [](auto* listener) { listener->on_app_go_front(); }
            );
        }
    );
}

} // namespace Slic3r::Biz::AppInstance
