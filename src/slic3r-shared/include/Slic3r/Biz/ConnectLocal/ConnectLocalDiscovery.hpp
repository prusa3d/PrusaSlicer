#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Slic3r::Biz::Platform {
class IMainThreadDispatcher;
} // namespace Slic3r::Biz::Platform

namespace Slic3r::Biz::ConnectLocal {

/**
 * @brief The DNS-SD service a Connect Local instance answers to.
 *
 * Without the leading underscore and the protocol, which the lookup adds: this is the
 * service label, and DNS-SD caps that at 15 characters, which is why it is not spelled
 * out as "prusa-connect-local".
 */
constexpr const char* SERVICE_NAME = "prusa-connect";

/** @brief TXT key of the name an instance publishes beside its address. */
constexpr const char* TXT_NAME = "name";
/** @brief TXT key of the version an instance publishes. */
constexpr const char* TXT_VERSION = "version";
/** @brief TXT key telling whether an instance serves https. */
constexpr const char* TXT_TLS = "tls";

/**
 * @brief One Connect Local instance found on the local network.
 */
struct Discovered
{
    /** @brief What it calls itself, from TXT; the service name when it published none. */
    std::string name;
    /** @brief Its mDNS host name, e.g. "workshop.local". */
    std::string hostname;
    /** @brief The address it answered from, as text. */
    std::string address;
    std::uint16_t port{0};
    /** @brief Whether it serves https, from TXT. */
    bool tls{false};
    /** @brief The version it published, empty when it published none. */
    std::string version;

    /**
     * @brief The address to configure it under.
     *
     * The host name is preferred over the address it answered from: a lease changes and
     * a name does not, and an instance saved under a stale IP is one DHCP renewal away
     * from being unreachable with nothing on screen saying why.
     */
    std::string base_url() const;
};

/**
 * @brief Reads one mDNS answer as an instance, or nothing when it cannot be one.
 *
 * Split out from the lookup so the shape of an answer can be tested without a network:
 * everything below this is boost::asio and a multicast socket.
 */
std::optional<Discovered> discovered_from_reply(
    const std::string& service_name,
    const std::string& hostname,
    const std::string& address,
    std::uint16_t port,
    const std::unordered_map<std::string, std::string>& txt
);

/**
 * @brief How long to listen, and how many times to ask.
 */
struct DiscoveryOptions
{
    /** @brief Seconds per attempt. */
    unsigned timeout{2};
    /**
     * @brief How many times the question is asked.
     *
     * More than one because a single multicast packet is allowed to go missing and an
     * instance that answers the second one is not a slower instance, it is the normal
     * case on a busy network. The same instance may then answer twice, which the caller
     * is expected to fold by address.
     */
    unsigned retries{2};
};

using FoundFn         = std::function<void(Discovered)>;
using DiscoveryDoneFn = std::function<void()>;

/**
 * @brief Starts looking for instances on the local network.
 *
 * The callbacks arrive on the lookup's own thread, so whatever they touch has to be
 * safe there - a UI has to marshal them itself. Dropping the returned token stops the
 * lookup, which is what makes it safe to start one from a window that can close.
 */
std::shared_ptr<void>
browse_for_instances(FoundFn on_found, DiscoveryDoneFn on_done, DiscoveryOptions options = {});

/**
 * @brief The instances found by a search in rounds.
 *
 * An instance stays listed while it answered in the current or the previous round: one missed
 * round may be a lost packet, two mean it stopped announcing itself (e.g. a server that went
 * down or came back under another host name). The order of the instances is kept.
 */
class DiscoveredList
{
public:
    /**
     * @brief Adds an instance, or refreshes the one with the same base_url() (the same instance
     * answering again), and marks it answered in this round.
     * @return True when what the list shows changed (a new instance, or a changed name, address
     * or version); a mere "answered again" is not a change, so the list does not flicker.
     */
    bool add(Discovered found);

    /**
     * @brief Ends a round, drops the instances that answered in neither this nor the previous one.
     * @return True when something was dropped.
     */
    bool end_round();

    /**
     * @brief Starts a new search: the instances listed so far have to answer in its first two
     * rounds to stay.
     */
    void restart();

    const std::vector<Discovered>& instances() const
    {
        return m_instances;
    }

private:
    std::vector<Discovered> m_instances;
    /** @brief base_url() of the instances that answered in the current round. */
    std::set<std::string> m_answered;
    /** @brief base_url() of the instances that answered in the previous round. */
    std::set<std::string> m_answered_before;
};

/**
 * @brief The list as a JSON array for the setup page.
 *
 * Objects with name, hostname, address, port, tls, version and url. The values come from the
 * network, so the result is plain escaped JSON (ASCII only, invalid UTF-8 replaced) that can be
 * passed to a script as a literal.
 */
std::string discovered_list_json(const std::vector<Discovered>& list);

/**
 * @brief Keeps browsing in rounds until stopped and collects what answers.
 *
 * Main thread only. The callbacks are delivered on the main thread through the dispatcher,
 * and never after stop() or destruction: results of a lookup still running in the
 * background are dropped.
 */
class DiscoveryLoop
{
public:
    using ListFn  = std::function<void(const std::vector<Discovered>&)>;
    using RoundFn = std::function<void()>;

    DiscoveryLoop(
        Platform::IMainThreadDispatcher& dispatcher,
        ListFn on_list_changed,
        RoundFn on_round_done = {}
    );
    ~DiscoveryLoop();

    DiscoveryLoop(const DiscoveryLoop&)            = delete;
    DiscoveryLoop& operator=(const DiscoveryLoop&) = delete;

    /**
     * @brief Starts browsing, keeps the instances found so far (they are dropped if they do not
     * answer in the first two rounds). No-op when already running.
     */
    void start();
    /**
     * @brief Stops browsing; a lookup in flight finishes in the background and is ignored.
     */
    void stop();

    bool running() const
    {
        return m_running;
    }

    /**
     * @brief Number of finished rounds since the last start().
     */
    unsigned rounds_done() const
    {
        return m_rounds_done;
    }

    const std::vector<Discovered>& found() const
    {
        return m_found.instances();
    }

    bool empty() const
    {
        return found().empty();
    }

private:
    void start_round();

    Platform::IMainThreadDispatcher& m_dispatcher;
    ListFn m_on_list_changed;
    RoundFn m_on_round_done;
    DiscoveredList m_found;
    /**
     * @brief Replaced on every start and dropped on stop: callbacks holding an expired one are
     * stale.
     */
    std::shared_ptr<char> m_session;
    std::shared_ptr<void> m_lookup;
    bool m_running{false};
    unsigned m_rounds_done{0};
};

} // namespace Slic3r::Biz::ConnectLocal
