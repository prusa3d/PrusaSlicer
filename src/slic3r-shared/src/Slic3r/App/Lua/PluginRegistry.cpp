#include "Slic3r/App/Lua/PluginRegistry.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Log.hpp"

#include <ranges>
#include <set>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/directory.hpp>
#include <fmt/ranges.h>

namespace Slic3r::App::Lua {

namespace fs = boost::filesystem;

PluginRegistry::PluginRegistry() :
    m_author_registry(fs::path{data_dir()} / "authorized_authors")
{}

void PluginRegistry::clear()
{
    m_bundles.clear();
    m_plugins.clear();
}

void PluginRegistry::scan(const std::string& path)
{
    struct PluginPath
    {
        std::string bundle_id;
        Plugin* plugin;
    };

    std::map<std::string, std::vector<PluginPath>> plugin_menu_items;

    fs::path p(path);
    if (!fs::exists(p)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator{p}) {
        if (!entry.is_directory()) {
            if (entry.path().extension() == ".lua") {
                std::string file_path = entry.path().string();
                SPDLOG_WARN(
                    "File '{}' will be ignored as it is not located in plugin bundle directory",
                    file_path
                );
            }
            continue;
        }

        auto bundle_path = entry.path().string();
        if (!PluginBundle::is_plugin_bundle_dir(bundle_path)) {
            SPDLOG_WARN("Directory '{}' is not a plugin bundle", bundle_path);
            continue;
        }

        PluginBundle bundle{Biz::Crypto::create_directory_source(bundle_path)};
        auto result = bundle.load_meta();
        if (!result.has_value()) {
            SPDLOG_ERROR("Error loading plugin bundle '{}': {}", bundle_path, result.error());
            continue;;
        }

        auto plugins = bundle.load_plugins();
        for (auto& plugin : plugins) {
            // make copy of metadata before moving plugin
            const auto plugin_id = plugin.meta().id;
            const auto plugin_path = plugin.path();
            auto [it, inserted] = m_plugins.emplace(plugin_id, std::move(plugin));
            if (!inserted) {
                SPDLOG_ERROR(
                    "Plugin ID: {} of {} already registered by {}",
                    plugin_id,
                    plugin_path,
                    it->second.path()
                );
            } else {
                auto menu_item = fmt::to_string(fmt::join(it->second.meta().menu, "/"));
                plugin_menu_items[menu_item].emplace_back(bundle.meta().id, &it->second);
            }
        }

        m_bundles.emplace_back(std::move(bundle));
    }

    for (auto& [menu_item, conflicting_plugins] : plugin_menu_items) {
        if (conflicting_plugins.size() <= 1) {
            continue;
        }

        SPDLOG_WARN(
            "Found conflicting menu item: {} registered in plugins: {}",
            menu_item,
            fmt::join(
                conflicting_plugins
                    | std::views::
                        transform([](const auto& p) { return p.plugin->path(); }),
                ", "
            )
        );

        std::set<std::string> bundle_ids;
        std::ranges::copy(
            conflicting_plugins
                | std::views::transform([](const auto& p) { return p.bundle_id; }),
            std::inserter(bundle_ids, bundle_ids.begin())
        );

        const bool use_bundle_id_suffix = bundle_ids.size() == conflicting_plugins.size();

        for (size_t i = 0; i < conflicting_plugins.size(); i++) {
            auto& plugin_path = conflicting_plugins.at(i);
            auto suffix       = use_bundle_id_suffix ?
                      fmt::format(" ({})", plugin_path.bundle_id) :
                      fmt::format(" ({})", i + 1);
            plugin_path.plugin->meta().menu.back() += suffix;
        }
    }
}

PluginInstallResult PluginRegistry::install(PluginBundle& bundle)
{
    try {
        auto author_key = m_author_registry.load_author_key(bundle.meta().author);
        if (!bundle.verify(author_key)) {
            return tl::unexpected(
                Biz::_u8L("Plugin installation failed: Integrity verification failed for bundle ")
                + bundle.meta().id
            );
        }
    } catch (Biz::Crypto::CryptoException& e) {
        return tl::unexpected(
            fmt::format(
                fmt::runtime(
                    // TRN {} is an author username
                    Biz::_u8L("Plugin installation failed: Cannot load public key for author {}")
                ),
                bundle.meta().author
            )
        );
    }

    auto dest_dir = fs::path{data_dir()} / "lua" / bundle.meta().id;
    if (fs::exists(dest_dir)) {
        fs::remove_all(dest_dir);
    }
    if (auto result = bundle.install(dest_dir.string()); !result.has_value()) {
        return tl::unexpected{Biz::_u8L("Plugin installation failed: ") + result.error()};
    }

    return {};
}


} // namespace Slic3r::App::Lua
