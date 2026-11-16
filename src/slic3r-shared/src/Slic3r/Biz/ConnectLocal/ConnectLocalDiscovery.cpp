#include "Slic3r/Biz/ConnectLocal/ConnectLocalDiscovery.hpp"

#include "Slic3r/Biz/JsonForScript.hpp"
#include "Slic3r/Biz/Network/Bonjour.hpp"
#include "Slic3r/Biz/Platform/IMainThreadDispatcher.hpp"
#include "Slic3r/Log.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>

#include <utility>

namespace Slic3r::Biz::ConnectLocal {

namespace {
/** @brief The TXT value for a flag, as a boolean. Absent or unrecognised reads as false. */
bool txt_flag(const std::unordered_map<std::string, std::string>& txt, const char* key)
{
    const auto found = txt.find(key);
    if (found == txt.end()) {
        return false;
    }
    return found->second == "1" || found->second == "true" || found->second == "yes";
}

std::string txt_value(const std::unordered_map<std::string, std::string>& txt, const char* key)
{
    const auto found = txt.find(key);
    return found == txt.end() ? std::string{} : found->second;
}

/** @brief Trailing dots are how DNS spells a fully qualified name; a URL has no use for one. */
std::string without_trailing_dot(std::string name)
{
    while (!name.empty() && name.back() == '.') {
        name.pop_back();
    }
    return name;
}
} // namespace

std::string Discovered::base_url() const
{
    const std::string host = hostname.empty() ? address : hostname;
    if (host.empty() || port == 0) {
        return {};
    }
    const bool bare_ipv6 = host.find(':') != std::string::npos && !host.starts_with('[');
    return fmt::format(
        "{}://{}:{}",
        tls ? "https" : "http",
        bare_ipv6 ? "[" + host + "]" : host,
        port
    );
}

std::optional<Discovered> discovered_from_reply(
    const std::string& service_name,
    const std::string& hostname,
    const std::string& address,
    std::uint16_t port,
    const std::unordered_map<std::string, std::string>& txt
)
{
    if (port == 0) {
        return std::nullopt;
    }
    const std::string host = without_trailing_dot(hostname);
    if (host.empty() && address.empty()) {
        return std::nullopt;
    }

    Discovered found;
    found.hostname = host;
    found.address  = address;
    found.port     = port;
    found.tls      = txt_flag(txt, TXT_TLS);
    found.version  = txt_value(txt, TXT_VERSION);
    found.name     = txt_value(txt, TXT_NAME);
    if (found.name.empty()) {
        found.name = service_name.empty() ? host : service_name;
    }
    return found;
}

std::shared_ptr<void>
browse_for_instances(FoundFn on_found, DiscoveryDoneFn on_done, DiscoveryOptions options)
{
    SPDLOG_INFO("Connect Local: looking for instances on the network (_{}._tcp)", SERVICE_NAME);

    return PrintHost::Bonjour(SERVICE_NAME)
        .set_txt_keys({TXT_NAME, TXT_VERSION, TXT_TLS})
        .set_timeout(options.timeout)
        .set_retries(options.retries)
        .on_reply(
            [on_found = std::move(on_found)](PrintHost::BonjourReply&& reply)
            {
                std::optional<Discovered> found = discovered_from_reply(
                    reply.service_name,
                    reply.hostname,
                    reply.ip.to_string(),
                    reply.port,
                    reply.txt_data
                );
                if (found && on_found) {
                    on_found(std::move(*found));
                }
            }
        )
        .on_complete(std::move(on_done))
        .lookup();
}

bool DiscoveredList::add(Discovered found)
{
    std::string url = found.base_url();
    if (url.empty()) {
        return false;
    }
    m_answered.insert(url);
    auto known = std::ranges::find_if(
        m_instances,
        [&url](const Discovered& instance) { return instance.base_url() == url; }
    );
    if (known == m_instances.end()) {
        m_instances.push_back(std::move(found));
        return true;
    }
    if (known->name == found.name
        && known->address == found.address
        && known->version == found.version)
    {
        return false;
    }
    *known = std::move(found);
    return true;
}

bool DiscoveredList::end_round()
{
    const size_t erased = std::erase_if(
        m_instances,
        [this](const Discovered& instance)
        {
            const std::string url = instance.base_url();
            return !m_answered.contains(url) && !m_answered_before.contains(url);
        }
    );
    m_answered_before = std::exchange(m_answered, {});
    return erased > 0;
}

void DiscoveredList::restart()
{
    m_answered.clear();
    m_answered_before.clear();
    for (const Discovered& instance : m_instances) {
        m_answered_before.insert(instance.base_url());
    }
}

std::string discovered_list_json(const std::vector<Discovered>& list)
{
    nlohmann::json array = nlohmann::json::array();
    for (const Discovered& d : list) {
        array.push_back({
            {"name", d.name},
            {"hostname", d.hostname},
            {"address", d.address},
            {"port", d.port},
            {"tls", d.tls},
            {"version", d.version},
            {"url", d.base_url()},
        });
    }
    return json_for_script(array);
}

DiscoveryLoop::DiscoveryLoop(
    Platform::IMainThreadDispatcher& dispatcher,
    ListFn on_list_changed,
    RoundFn on_round_done
) :
    m_dispatcher(dispatcher),
    m_on_list_changed(std::move(on_list_changed)),
    m_on_round_done(std::move(on_round_done))
{}

DiscoveryLoop::~DiscoveryLoop()
{
    stop();
}

void DiscoveryLoop::start()
{
    if (m_running) {
        return;
    }
    m_running     = true;
    m_rounds_done = 0;
    m_found.restart();
    m_session = std::make_shared<char>(0);
    start_round();
}

void DiscoveryLoop::stop()
{
    m_running = false;
    m_session.reset();
    m_lookup.reset();
}

void DiscoveryLoop::start_round()
{
    std::weak_ptr<char> session = m_session;
    m_lookup = browse_for_instances(
        [this, session, &dispatcher = m_dispatcher](Discovered found)
        {
            dispatcher.dispatch_on_main_thread(
                [this, session, found = std::move(found)]() mutable
                {
                    if (session.expired()) {
                        return;
                    }
                    if (m_found.add(std::move(found)) && m_on_list_changed) {
                        m_on_list_changed(m_found.instances());
                    }
                }
            );
        },
        [this, session, &dispatcher = m_dispatcher]()
        {
            dispatcher.dispatch_on_main_thread(
                [this, session]()
                {
                    if (session.expired()) {
                        return;
                    }
                    ++m_rounds_done;
                    if (m_found.end_round()) {
                        SPDLOG_INFO(
                            "Connect Local: dropped servers that stopped announcing themselves"
                        );
                        if (m_on_list_changed) {
                            m_on_list_changed(m_found.instances());
                        }
                    }
                    if (m_on_round_done) {
                        m_on_round_done();
                    }
                    if (m_running) {
                        start_round();
                    }
                }
            );
        }
    );
}

} // namespace Slic3r::Biz::ConnectLocal
