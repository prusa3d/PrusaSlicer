#pragma once
#include <functional>
#include <list>
#include <ranges>
#include <map>
#include <string>
#include <optional>

namespace Slic3r {

/**
 * @brief Simple container of functions to be invoked when the container gets destroyed.
 *
 * This function container allows you to defer function call for later. This allows you
 * to invoke e.g. listeners at the end of operation, when whole state valid, while registering them
 * as you go (in moments when a state is not valid yet).
 *
 * @note All registered function are escaping, i.e. if you pass lambda as @a func to add() method,
 * the lambda will outlive the context it was created in. This means you have to capture all local
 * values by copy.
 *
 * @note All functions registered by add() are called (in order of registration). If you need to
 * deduplicate calls (so only actual data are passed), you need to use DeduplicatingInvokeLaterBag.
 */
struct InvokeLaterBag final
{
    using Func = std::function<void()>;
    using Tag = std::optional<std::string>;

    ~InvokeLaterBag();

    void add(Func&& func, const Tag& dedup_tag = std::nullopt);
    void invoke_now();

private:
    struct FuncRecord
    {
        Func func;
        Tag dedup_tag;
    };

    using FuncList = std::list<FuncRecord>;
    FuncList m_to_invoke;
};

/**
 * @brief Container of deduplicated functions to be invoked when the container gets destroyed.
 * @tparam ArgsT Types to construct deduplication key tuple.
 */
template <typename ... ArgsT>
struct DeduplicatingInvokeLaterBag final
{
    using Key = std::tuple<std::decay_t<ArgsT> ...>;
    using Func = std::function<void()>;

    ~DeduplicatingInvokeLaterBag()
    {
        for (const auto& k : m_keys_order) {
            m_to_invoke.at(k)();
        }
    }

    /**
     * @brief Adds function for later invocation
     * @param key A deduplication key, if there is already present a function with same key in the
     * bag, it gets replaced by this one.
     * @param func A function to invoke later.
     */
    void add(const Key& key, Func&& func)
    {
        auto it = m_to_invoke.find(key);
        if (it != m_to_invoke.end()) {
            auto key_it = std::find(m_keys_order.begin(), m_keys_order.end(), key);
            m_keys_order.erase(key_it);
        }
        m_to_invoke[key] = std::move(func);
        m_keys_order.push_back(key);
    }

private:
    using FuncMap = std::map<Key, Func>;
    using KeyList = std::list<Key>;
    FuncMap m_to_invoke;
    KeyList m_keys_order;
};


}
